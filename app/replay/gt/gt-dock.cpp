#include "gt-dock.hpp"
#include "i18n.hpp"
#include <algorithm>
#include "gt-source.hpp"
#include "../overlay-channels.hpp"
#include "../common.hpp"
#include "../ui-theme.hpp"
#include "gt-render.hpp"
#include "gt-doc.hpp"
#include <QPainter>
#include <QPixmap>
#include <obs-frontend-api.h>

#include <cstring>
#include <QComboBox>
#include <QGridLayout>
#include <QResizeEvent>
#include <QSettings>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

bool gtConvertToScene(const QString &file, QString &message);

GtDock::GtDock(QWidget *parent) : QWidget(parent)
{
	i18n::track(this);
	ui::apply(this);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);
	auto *top = new QHBoxLayout();
	top->setSpacing(6);
	auto *add = ui::button(tl("＋ Importar"), tl("Importa títulos do vMix GT (.gtzip): cada um vira uma entrada"),
			       this, "primary");
	connect(add, &QPushButton::clicked, this, &GtDock::addTitle);
	top->addWidget(add);
	search = new QLineEdit(this);
	search->setPlaceholderText(tl("Buscar título…"));
	search->setClearButtonEnabled(true);
	connect(search, &QLineEdit::textChanged, this, [this] {
		shownNames.clear();
		refresh();
	});
	top->addWidget(search, 1);
	listBtn = ui::toggle(QString::fromUtf8("☰"), tl("Exibir em lista"), this);
	gridBtn = ui::toggle(QString::fromUtf8("▦"), tl("Exibir em grade, com miniatura"), this);
	for (auto *b : {listBtn, gridBtn}) {
		b->setFixedWidth(34);
		top->addWidget(b);
	}
	top->setSpacing(6);
	ui::segment({listBtn, gridBtn});
	connect(listBtn, &QPushButton::clicked, this, [this] { setMode(false); });
	connect(gridBtn, &QPushButton::clicked, this, [this] { setMode(true); });
	root->addLayout(top);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *inner = new QWidget(scroll);
	inner->setObjectName("irBody");
	auto *innerV = new QVBoxLayout(inner);
	innerV->setContentsMargins(0, 0, 0, 0);
	listLayout = new QGridLayout();
	listLayout->setSpacing(6);
	innerV->addLayout(listLayout);
	innerV->addStretch(1);
	scroll->setWidget(inner);
	root->addWidget(scroll, 1);

	gridMode = QSettings(ui::settingsFile(), QSettings::IniFormat).value("gtGrid", true).toBool();
	listBtn->setChecked(!gridMode);
	gridBtn->setChecked(gridMode);

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &GtDock::refresh);
	timer->start(200);
}

GtDock::~GtDock()
{
	clearRows();
}

void GtDock::clearRows()
{
	for (auto &r : rows) {
		obs_weak_source_release(r.src);
		delete r.w;
	}
	rows.clear();
}

void GtDock::setMode(bool grid)
{
	gridMode = grid;
	listBtn->setChecked(!grid);
	gridBtn->setChecked(grid);
	QSettings(ui::settingsFile(), QSettings::IniFormat).setValue("gtGrid", grid);
	shownNames.clear();
	refresh();
}

int GtDock::columns() const
{
	if (!gridMode)
		return 1;
	return std::max(1, (width() - 16) / 260);
}

void GtDock::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	if (gridMode && columns() != shownColumns) {
		shownNames.clear();
		refresh();
	}
}

void GtDock::removeTitle(obs_weak_source_t *weak)
{
	obs_source_t *x = obs_weak_source_get_source(weak);
	if (!x)
		return;
	const QString name = gtDisplayName(x);
	const bool inLib = obs_obj_is_private(x);
	const QString q = inLib ? tl("Remover o título \"%1\"? (a entrada também sai)").arg(name)
				: tl("O título \"%1\" é uma fonte de cena.\nRemover a fonte de todas as cenas?").arg(name);
	if (QMessageBox::question(this, "ReplayDeck GT", q) == QMessageBox::Yes) {
		if (overlaysAlive()) {
			const int ch = overlays().channelOf(obs_source_get_name(x));
			if (ch >= 0)
				overlays().setOn(ch, false);
		}
		if (inLib)
			gtLibraryRemove(x);
		else
			obs_source_remove(x);
	}
	obs_source_release(x);
	shownNames.clear();
	refresh();
}

/* o canal de overlay guarda o nome: acompanha a troca */
static void renameInOverlays(const std::string &from, const std::string &to)
{
	if (!overlaysAlive())
		return;
	for (int i = 0; i < OVERLAY_COUNT; i++) {
		OverlaySlot sl = overlays().slot(i);
		if (sl.source == from) {
			sl.source = to;
			overlays().setSlot(i, sl);
		}
	}
}

void GtDock::renameTitle(obs_weak_source_t *weak)
{
	obs_source_t *x = obs_weak_source_get_source(weak);
	if (!x)
		return;
	const QString old = gtDisplayName(x);
	bool ok = false;
	const QString name = QInputDialog::getText(this, "ReplayDeck GT", tl("Novo nome do título:"),
						   QLineEdit::Normal, old, &ok)
				     .trimmed();
	if (ok && !name.isEmpty() && name != old) {
		if (gtRename(x, name))
			renameInOverlays(old.toStdString(), name.toStdString());
		else
			QMessageBox::warning(this, "ReplayDeck GT",
					     tl("Já existe uma fonte ou título chamado \"%1\".").arg(name));
	}
	obs_source_release(x);
	shownNames.clear();
	refresh();
}

void GtDock::duplicateTitle(obs_weak_source_t *weak)
{
	obs_source_t *x = obs_weak_source_get_source(weak);
	if (!x)
		return;
	const QString old = gtDisplayName(x);
	bool ok = false;
	const QString name = QInputDialog::getText(
		this, "ReplayDeck GT",
		tl("Nome da cópia (o mesmo GT com outros dados).\nEx.: ESCALAÇÃO CASA / ESCALAÇÃO FORA"),
		QLineEdit::Normal, old + tl(" FORA"), &ok);
	if (ok) {
		obs_source_t *c = gtLibraryDuplicate(x, name);
		obs_source_release(c);
	}
	obs_source_release(x);
	shownNames.clear();
	refresh();
}

static int overlayOf(obs_source_t *s)
{
	obs_data_t *st = obs_source_get_settings(s);
	obs_data_set_default_int(st, "overlay", 1);
	const int v = (int)obs_data_get_int(st, "overlay");
	obs_data_release(st);
	return v;
}

void GtDock::rebuild(const std::vector<obs_source_t *> &sources)
{
	clearRows();
	const int cols = columns();
	shownColumns = cols;
	for (int c = 0; c < 8; c++)
		listLayout->setColumnStretch(c, c < cols ? 1 : 0);
	if (sources.empty()) {
		Row r = {};
		r.w = new QLabel(search && !search->text().isEmpty()
					 ? tl("Nenhum título com \"%1\".").arg(search->text())
					 : tl("Nenhum título ainda.\nClique em ＋ Importar e escolha arquivos .gtzip do vMix."));
		r.w->setProperty("role", "muted");
		static_cast<QLabel *>(r.w)->setAlignment(Qt::AlignCenter);
		r.w->setMinimumHeight(120);
		listLayout->addWidget(r.w, 0, 0, 1, std::max(1, cols));
		rows.push_back(r);
		return;
	}
	for (obs_source_t *s : sources) {
		Row r;
		r.src = obs_source_get_weak_source(s);
		auto *v = new QVBoxLayout();
		auto *card = ui::card(nullptr, v, gridMode ? 6 : 4, 4);
		r.w = card;

		/* miniatura (desenhada aos poucos no refresh) */
		r.thumb = new QLabel(card);
		r.thumb->setAlignment(Qt::AlignCenter);
		r.thumb->setStyleSheet("background: #0b0d10; border-radius: 6px;");
		if (gridMode)
			r.thumb->setFixedSize(84, 48);
		else
			r.thumb->setFixedSize(48, 27);

		r.name = new QLabel(gtDisplayName(s), card);
		r.name->setProperty("role", "title");
		r.name->setToolTip(r.name->text());
		r.name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

		auto *more = new QToolButton(card);
		more->setText(QString::fromUtf8("⋮"));
		more->setProperty("kind", "ghost");
		more->setPopupMode(QToolButton::InstantPopup);
		auto *menu = new QMenu(more);
		obs_weak_source_t *weak = r.src;
		auto withSrc = [weak](auto fn) {
			obs_source_t *x = obs_weak_source_get_source(weak);
			if (x)
				fn(x);
			obs_source_release(x);
		};
		menu->addAction(tl("✎  Editar título (texto, imagens, cores, layout)"), this,
				[this, withSrc] { withSrc([this](obs_source_t *x) { gtOpenEditor(x, this); }); });
		menu->addAction(tl("Aa  Renomear…"), this, [this, weak] { renameTitle(weak); });
		menu->addAction(tl("⧉  Duplicar (mesmo GT, outros dados — ex.: casa e fora)…"), this,
				[this, weak] { duplicateTitle(weak); });
		menu->addAction(tl("⚙  Propriedades da fonte"), this,
				[withSrc] { withSrc([](obs_source_t *x) { obs_frontend_open_source_properties(x); }); });
		const QStringList pages = gtPages(s);
		if (!pages.isEmpty()) {
			menu->addSeparator();
			for (const QString &pg : pages)
				menu->addAction(QString::fromUtf8("▷  ") + pg, this,
						[withSrc, pg] { withSrc([pg](obs_source_t *x) { gtPlayPage(x, pg); }); });
		}
		menu->addSeparator();
		menu->addAction(tl("✕  Remover título"), this, [this, weak] { removeTitle(weak); });
		more->setMenu(menu);

		r.go = ui::button(QString(), QString(), card, "go");
		r.go->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		r.ov = new QComboBox(card);
		r.ov->addItem(tl("Ent."), 0);
		for (int i = 1; i <= OVERLAY_COUNT; i++)
			r.ov->addItem(QString("O%1").arg(i), i);
		r.ov->setToolTip(tl("Onde o título entra: num overlay (O1–O4, por cima do programa)\n"
						   "ou \"Ent.\" (só anima: use a entrada na Prévia/Programa)"));
		r.ov->setCurrentIndex(r.ov->findData(overlayOf(s)));
		r.ov->setFixedWidth(58);
		connect(r.ov, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [withSrc, r](int) {
			const int val = r.ov->currentData().toInt();
			withSrc([val](obs_source_t *x) {
				obs_data_t *st = obs_source_get_settings(x);
				obs_data_set_int(st, "overlay", val);
				obs_data_release(st);
			});
		});
		connect(r.go, &QPushButton::clicked, this, [withSrc] {
			withSrc([](obs_source_t *x) {
				const std::string name = obs_source_get_name(x);
				const int ov = overlayOf(x);
				const int st = gtState(x);
				const bool onAir = st == 1 || st == 2;
				if (ov >= 1 && overlaysAlive()) {
					const int ch = overlays().channelOf(name);
					if (onAir && ch >= 0)
						overlays().setOn(ch, false);
					else if (onAir)
						gtPlayOut(x);
					else
						overlays().assignOn(ov - 1, name);
				} else {
					if (onAir)
						gtPlayOut(x);
					else
						gtPlayIn(x);
				}
			});
		});

		if (gridMode) {
			/* cartão: miniatura à esquerda; nome e botões à direita */
			auto *h = new QHBoxLayout();
			h->setSpacing(8);
			h->addWidget(r.thumb, 0, Qt::AlignTop);
			auto *rc = new QVBoxLayout();
			rc->setSpacing(4);
			auto *h1 = new QHBoxLayout();
			h1->setSpacing(2);
			h1->addWidget(r.name, 1);
			h1->addWidget(more);
			rc->addLayout(h1);
			auto *h2 = new QHBoxLayout();
			h2->setSpacing(4);
			h2->addWidget(r.go, 1);
			h2->addWidget(r.ov);
			rc->addLayout(h2);
			h->addLayout(rc, 1);
			v->addLayout(h);
			r.name->setMinimumWidth(40);
		} else {
			auto *line = new QHBoxLayout();
			line->setSpacing(8);
			line->addWidget(r.thumb);
			line->addWidget(r.name, 1);
			line->addWidget(r.go);
			line->addWidget(r.ov);
			line->addWidget(more);
			r.go->setMinimumWidth(96);
			r.go->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			v->addLayout(line);
		}
		const int idx = (int)rows.size();
		listLayout->addWidget(r.w, idx / cols, idx % cols);
		rows.push_back(r);
	}
}

static bool collectGt(void *param, obs_source_t *src)
{
	/* fontes removidas continuam na lista do OBS por alguns instantes */
	if (strcmp(obs_source_get_id(src), GT_SOURCE_ID) == 0 && !obs_source_removed(src))
		static_cast<std::vector<obs_source_t *> *>(param)->push_back(src);
	return true;
}

void GtDock::refresh()
{
	std::vector<obs_source_t *> libRefs = gtLibraryList();
	std::vector<obs_source_t *> all = libRefs;
	obs_enum_sources(collectGt, &all);
	const QString f = search ? search->text().trimmed() : QString();
	std::vector<obs_source_t *> sources;
	QStringList names;
	for (auto *s : all) {
		const QString n = gtDisplayName(s);
		if (!f.isEmpty() && !n.contains(f, Qt::CaseInsensitive))
			continue;
		sources.push_back(s);
		names << n;
	}
	names << (gridMode ? "#grid" : "#list");
	if (names != shownNames) {
		shownNames = names;
		rebuild(sources);
	}
	for (auto *s : libRefs)
		obs_source_release(s);

	bool thumbDone = false;
	for (auto &r : rows) {
		if (!r.src)
			continue;
		obs_source_t *s = obs_weak_source_get_source(r.src);
		if (!s)
			continue;
		const int st = gtState(s);
		const bool onAir = st == 1 || st == 2;
		const QString txt = (onAir ? tl("■  SAÍDA") : tl("▶  ENTRADA"));
		if (r.go->text() != txt)
			r.go->setText(txt);
		ui::setKind(r.go, onAir ? "out" : "go");
		ui::setProp(r.w, "live", onAir);

		/* miniatura: no máximo uma por vez, só quando o conteúdo muda */
		if (!thumbDone) {
			auto title = gtTitleOf(s);
			const auto vals = gtValues(s);
			QString key = QString::number((quintptr)title.get());
			for (auto it = vals.begin(); it != vals.end(); ++it)
				key += it.key() + "=" + it.value() + ";";
			const QSize box = r.thumb->size();
			key += QString("@%1x%2").arg(box.width()).arg(box.height());
			if (title && key != r.thumbKey && box.width() > 8 && box.height() > 8) {
				thumbDone = true;
				r.thumbKey = key;
				QImage still = gtRenderStill(*title, vals);
				if (!still.isNull()) {
					/* corta o espaço vazio: a miniatura mostra só a arte */
					const QImage a = still.convertToFormat(QImage::Format_ARGB32).scaled(
						still.width() / 8, still.height() / 8, Qt::IgnoreAspectRatio, Qt::FastTransformation);
					int x0 = a.width(), y0 = a.height(), x1 = -1, y1 = -1;
					for (int y = 0; y < a.height(); y++) {
						const QRgb *line = reinterpret_cast<const QRgb *>(a.constScanLine(y));
						for (int x = 0; x < a.width(); x++)
							if (qAlpha(line[x]) > 8) {
								x0 = std::min(x0, x);
								x1 = std::max(x1, x);
								y0 = std::min(y0, y);
								y1 = std::max(y1, y);
							}
					}
					if (x1 >= x0 && y1 >= y0) {
						const QRect crop = QRect(QPoint(x0 * 8, y0 * 8), QPoint(x1 * 8 + 7, y1 * 8 + 7))
									   .adjusted(-16, -16, 16, 16)
									   .intersected(still.rect());
						still = still.copy(crop);
					}
					QPixmap pm(box * r.thumb->devicePixelRatioF());
					pm.setDevicePixelRatio(r.thumb->devicePixelRatioF());
					pm.fill(QColor("#0b0d10"));
					QPainter p(&pm);
					p.setRenderHint(QPainter::SmoothPixmapTransform);
					const QSizeF sz = QSizeF(still.size()).scaled(QSizeF(box), Qt::KeepAspectRatio);
					p.drawImage(QRectF((box.width() - sz.width()) / 2, (box.height() - sz.height()) / 2, sz.width(),
							   sz.height()),
						    still);
					p.end();
					r.thumb->setPixmap(pm);
				}
			}
		}
		obs_source_release(s);
	}
}

void GtDock::addTitle()
{
	const QStringList files = QFileDialog::getOpenFileNames(this, tl("Importar títulos do vMix GT"), QString(),
								tl("Títulos GT (*.gtzip)"));
	QStringList failed;
	for (const QString &file : files) {
		obs_source_t *src = gtLibraryAdd(file);
		if (!src)
			continue;
		if (gtTitleName(src).isEmpty())
			failed << QFileInfo(file).fileName();
		obs_source_release(src);
	}
	refresh();
	if (!failed.isEmpty())
		QMessageBox::warning(this, "ReplayDeck GT",
				     tl("Não foi possível ler: %1\nVeja o motivo em Ajuda → Arquivos de log (linhas \"GT:\").")
					     .arg(failed.join(", ")));
}

void GtDock::convertTitle()
{
	const QString file = QFileDialog::getOpenFileName(this, tl("Converter título GT em cena"), QString(),
							  tl("Títulos GT (*.gtzip)"));
	if (file.isEmpty())
		return;
	QString msg;
	const bool ok = gtConvertToScene(file, msg);
	if (ok)
		QMessageBox::information(this, "ReplayDeck GT", msg);
	else
		QMessageBox::warning(this, "ReplayDeck GT", msg);
}
