#include "replay-dock.hpp"
#include "studio-host.hpp"
#include "i18n.hpp"
#include "engine.hpp"
#include "air-control.hpp"
#include "ui-theme.hpp"
#include "setup-wizard.hpp"
#include <obs-frontend-api.h>

#include <QButtonGroup>
#include <QEventLoop>
#include <QListWidget>
#include <QRadioButton>
#include <QActionGroup>
#include <cmath>
#include <QComboBox>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDialog>
#include <QDockWidget>
#include <QFrame>
#include <QDir>
#include <QDirIterator>
#include <QStorageInfo>
#include <QUrl>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QDateTime>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QSettings>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QTabWidget>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPolygonF>
#include <QTimer>
#include <QVBoxLayout>
#include <cstring>
#include <climits>
#include <functional>

static QString tc(double frame, double fps)
{
	if (frame < 0)
		frame = 0;
	const int ifps = (int)(fps + 0.5) > 0 ? (int)(fps + 0.5) : 30;
	const int64_t f = (int64_t)frame;
	const int64_t totalSec = f / ifps;
	return QString("%1:%2:%3:%4")
		.arg(totalSec / 3600, 2, 10, QChar('0'))
		.arg((totalSec / 60) % 60, 2, 10, QChar('0'))
		.arg(totalSec % 60, 2, 10, QChar('0'))
		.arg(f % ifps, 2, 10, QChar('0'));
}

/* ---------------- diálogo de configuração ---------------- */

static bool enumSourceCb(void *param, obs_source_t *src)
{
	auto *names = static_cast<QStringList *>(param);
	const uint32_t flags = obs_source_get_output_flags(src);
	if ((flags & OBS_SOURCE_VIDEO) && strcmp(obs_source_get_id(src), "instant_replay_output") != 0)
		names->append(QString::fromUtf8(obs_source_get_name(src)));
	return true;
}

static QString hms(qint64 sec)
{
	return QString("%1:%2:%3")
		.arg(sec / 3600, 2, 10, QChar('0'))
		.arg((sec / 60) % 60, 2, 10, QChar('0'))
		.arg(sec % 60, 2, 10, QChar('0'));
}

static qint64 dirSize(const QString &path)
{
	qint64 total = 0;
	QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		total += it.fileInfo().size();
	}
	return total;
}

/* ---------------- funções visíveis no painel ---------------- */

struct DockPart {
	const char *id;
	const char *label;
	bool defOn;
};

static const DockPart kParts[] = {
	{"clock", N_("Relógio e estado"), true},
	{"sel", N_("Seleção A+B / A / B"), true},
	{"export", N_("Exportar"), true},
	{"scrub", N_("Linha do tempo"), true},
	{"nav", N_("Evento anterior / próximo"), true},
	{"reverse", N_("Reverso"), true},
	{"ff", N_("Avanço rápido"), false},
	{"live", N_("Ao vivo"), true},
	{"mark", N_("IN / OUT"), false},
	{"cancel", N_("Cancelar IN"), false},
	{"quick", N_("−5 / −10 / −20 s"), true},
	{"speed", N_("Velocidades"), true},
	{"loop", N_("Loop"), true},
	{"audio", N_("Áudio"), true},
	{"zoom", N_("Zoom (aproximar e mover)"), true},
	{"camA", N_("Câmeras da saída A"), true},
	{"camB", N_("Câmeras da saída B"), false},
	{"preview", N_("Botão P (pré-visualizar saída)"), false},
	{"last", N_("Último evento no ar"), true},
	{"events", N_("Play Events"), true},
	{"playsel", N_("Evento selecionado no ar"), false},
	{"delete", N_("Apagar evento selecionado"), false},
	{"status", N_("Avisos (quadros perdidos, exportação…)"), true},
};

static bool partOn(const char *id)
{
	for (const DockPart &p : kParts)
		if (strcmp(p.id, id) == 0)
			return QSettings(ui::settingsFile(), QSettings::IniFormat).value(QString("parts/") + id, p.defOn).toBool();
	return true;
}

/* ---------------- gravações no disco ---------------- */

static QString sizeText(uint64_t b)
{
	const double gb = (double)b / (1024.0 * 1024.0 * 1024.0);
	if (gb >= 1.0)
		return QString::number(gb, 'f', gb >= 100 ? 0 : 1) + " GB";
	return QString::number((double)b / (1024.0 * 1024.0), 'f', 0) + " MB";
}

static QString sessionTitle(const std::string &name)
{
	/* Replay_2026-11-19_02-58-10 → 19/11/2026  02:58:10 */
	const QString n = QString::fromUtf8(name.c_str());
	QRegularExpressionMatch m =
		QRegularExpression("(\\d{4})-(\\d{2})-(\\d{2})_(\\d{2})-(\\d{2})-(\\d{2})").match(n);
	if (!m.hasMatch())
		return n;
	return QString("%1/%2/%3  %4:%5:%6")
		.arg(m.captured(3), m.captured(2), m.captured(1), m.captured(4), m.captured(5), m.captured(6));
}

/* pergunta e limpa todo o cache do plugin no disco */
bool clearCacheAsk(QWidget *parent)
{
	uint64_t total = 0;
	const auto ss = engine().listSessions();
	for (const SessionInfo &s : ss)
		total += s.bytes;
	QString msg = tl("Apagar TODO o cache do ReplayDeck?\n\n"
					"• %1 gravação(ões) — %2\n"
					"• os clipes exportados dentro delas\n"
					"• toda a lista de eventos\n"
					"• os arquivos temporários do plugin\n\n")
			      .arg(ss.size())
			      .arg(sizeText(total));
	if (engine().recording())
		msg += tl("A gravação será PARADA antes.\n\n");
	msg += tl("Configurações, títulos GT e overlays não são apagados. Não dá para desfazer.");
	if (QMessageBox::warning(parent, tl("Limpar cache"), msg, QMessageBox::Yes | QMessageBox::Cancel,
				 QMessageBox::Cancel) != QMessageBox::Yes)
		return false;
	std::string err;
	const uint64_t freed = engine().clearAllCache(err);
	if (!err.empty())
		QMessageBox::warning(parent, "ReplayDeck",
				     tl("Liberado: %1\nNão foi possível apagar:\n%2")
					     .arg(sizeText(freed))
					     .arg(QString::fromUtf8(err.c_str())));
	else
		QMessageBox::information(parent, "ReplayDeck", tl("Cache limpo: %1 liberados.").arg(sizeText(freed)));
	return true;
}

void showSessionsDialog(QWidget *parent)
{
	QDialog dlg(parent);
	dlg.setWindowTitle(tl("Gravações do replay"));
	dlg.setMinimumSize(560, 380);
	ui::applyDialog(&dlg);
	auto *v = new QVBoxLayout(&dlg);
	v->setContentsMargins(12, 12, 12, 12);
	auto *info = new QLabel(&dlg);
	info->setWordWrap(true);
	v->addWidget(info);
	auto *table = new QTableWidget(0, 4, &dlg);
	table->setHorizontalHeaderLabels({tl("Gravação"), tl("Tamanho"),
					  tl("Eventos"), QString()});
	table->verticalHeader()->setVisible(false);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setAlternatingRowColors(true);
	table->setShowGrid(false);
	table->horizontalHeader()->setStretchLastSection(true);
	table->setColumnWidth(0, 200);
	table->setColumnWidth(1, 90);
	table->setColumnWidth(2, 70);
	v->addWidget(table, 1);
	std::vector<SessionInfo> ss;
	auto reload = [&] {
		ss = engine().listSessions();
		table->setRowCount(0);
		uint64_t total = 0;
		for (const SessionInfo &s : ss) {
			total += s.bytes;
			const int r = table->rowCount();
			table->insertRow(r);
			auto *it = new QTableWidgetItem(sessionTitle(s.name));
			it->setFlags(s.recording ? Qt::ItemIsEnabled : (Qt::ItemIsEnabled | Qt::ItemIsUserCheckable));
			if (!s.recording)
				it->setCheckState(Qt::Unchecked);
			it->setToolTip(QString::fromUtf8(s.dir.c_str()));
			table->setItem(r, 0, it);
			table->setItem(r, 1, new QTableWidgetItem(sizeText(s.bytes)));
			table->setItem(r, 2, new QTableWidgetItem(QString::number(s.events)));
			table->setItem(r, 3, new QTableWidgetItem(s.recording ? tl("● gravando agora") : QString()));
		}
		const ReplayConfig c = engine().config();
		QStorageInfo st(QString::fromUtf8(c.outputDir.c_str()));
		info->setText(tl("<b>%1</b> em %2 gravação(ões) • livre no disco: <b>%3</b>%4")
				      .arg(sizeText(total))
				      .arg(ss.size())
				      .arg(st.isValid() ? sizeText((uint64_t)st.bytesAvailable()) : QString("?"))
				      .arg(c.cacheLimitGB > 0 ? tl(" • limite: %1 GB").arg(c.cacheLimitGB) : QString()));
	};
	reload();

	auto *row = new QHBoxLayout();
	auto *selOld = new QPushButton(tl("Marcar antigas"), &dlg);
	selOld->setToolTip(tl("Marca todas, menos a mais recente e a que está gravando"));
	QObject::connect(selOld, &QPushButton::clicked, &dlg, [&] {
		for (int r = 0; r < table->rowCount(); r++)
			if (table->item(r, 0)->flags() & Qt::ItemIsUserCheckable)
				table->item(r, 0)->setCheckState(r + 1 < table->rowCount() ? Qt::Checked : Qt::Unchecked);
	});
	auto *open = new QPushButton(tl("Abrir pasta"), &dlg);
	QObject::connect(open, &QPushButton::clicked, &dlg, [] {
		QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromUtf8(engine().config().outputDir.c_str())));
	});
	auto *del = new QPushButton(tl("Apagar marcadas"), &dlg);
	ui::setKind(del, "danger");
	QObject::connect(del, &QPushButton::clicked, &dlg, [&] {
		std::vector<std::string> dirs;
		uint64_t bytes = 0;
		int evs = 0;
		for (int r = 0; r < table->rowCount(); r++)
			if (table->item(r, 0)->checkState() == Qt::Checked) {
				dirs.push_back(ss[(size_t)r].dir);
				bytes += ss[(size_t)r].bytes;
				evs += ss[(size_t)r].events;
			}
		if (dirs.empty())
			return;
		if (QMessageBox::question(&dlg, tl("Apagar gravações"),
					  tl("Apagar %1 gravação(ões) (%2)?\n%3 evento(s) delas saem da lista.\n\nNão dá para desfazer.")
						  .arg(dirs.size())
						  .arg(sizeText(bytes))
						  .arg(evs)) != QMessageBox::Yes)
			return;
		QStringList fails;
		for (const std::string &d : dirs) {
			std::string err;
			if (!engine().deleteSession(d, err))
				fails << QString::fromUtf8((d + ": " + err).c_str());
		}
		if (!fails.isEmpty())
			QMessageBox::warning(&dlg, "ReplayDeck", tl("Não foi possível apagar:\n") + fails.join("\n"));
		reload();
	});
	auto *all = new QPushButton(tl("Limpar todo o cache"), &dlg);
	ui::setKind(all, "danger");
	all->setToolTip(tl("Apaga TODAS as gravações do replay (inclusive clipes exportados nelas),\n"
					  "a lista de eventos e os temporários do plugin. Para a gravação antes."));
	QObject::connect(all, &QPushButton::clicked, &dlg, [&] { clearCacheAsk(&dlg); reload(); });
	row->addWidget(selOld);
	row->addWidget(open);
	row->addStretch(1);
	row->addWidget(all);
	row->addWidget(del);
	auto *close = new QPushButton(tl("Fechar"), &dlg);
	QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
	row->addWidget(close);
	v->addLayout(row);
	dlg.exec();
}

/* mini-mapa do zoom: o retângulo é a parte da imagem que está no ar.
 * Clique/arraste = move; rodinha = zoom; duplo clique = imagem inteira. */
class ZoomMap : public QWidget {
public:
	explicit ZoomMap(QWidget *parent, bool live_ = true) : QWidget(parent), live(live_)
	{
		setFixedSize(64, 36);
		setCursor(Qt::OpenHandCursor);
		setToolTip(tl("Zoom: clique ou arraste para mover, rodinha para aproximar,\n"
					     "duplo clique volta para a imagem inteira"));
	}
	PlaybackChannel::Zoom z{1, 0.5f, 0.5f};
	bool live;

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		p.setPen(QPen(QColor("#3a4352"), 1));
		p.setBrush(QColor("#1a2230"));
		p.drawRoundedRect(r, 4, 4);
		p.setPen(QPen(QColor(255, 255, 255, 40), 1));
		p.drawLine(QPointF(r.center().x(), r.top() + 3), QPointF(r.center().x(), r.bottom() - 3));
		p.drawEllipse(r.center(), 5, 5);
		const float w = (float)r.width() / z.z, h = (float)r.height() / z.z;
		const QRectF v(r.left() + (z.x - 0.5f / z.z) * r.width(), r.top() + (z.y - 0.5f / z.z) * r.height(), w, h);
		const bool zoomed = z.z > 1.01f;
		p.setPen(QPen(zoomed ? QColor("#fbbf24") : QColor("#5b6475"), zoomed ? 2 : 1));
		p.setBrush(zoomed ? QColor(251, 191, 36, 35) : Qt::NoBrush);
		p.drawRoundedRect(v.adjusted(1, 1, -1, -1), 2, 2);
	}
	void moveTo(const QPointF &pt)
	{
		if (!live)
			return;
		const float x = (float)(pt.x() / width()), y = (float)(pt.y() / height());
		engine().zoomSet(z.z < 1.01f ? 2.0f : z.z, x, y);
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		setCursor(Qt::ClosedHandCursor);
		moveTo(e->position());
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (e->buttons() & Qt::LeftButton)
			moveTo(e->position());
	}
	void mouseReleaseEvent(QMouseEvent *) override { setCursor(Qt::OpenHandCursor); }
	void mouseDoubleClickEvent(QMouseEvent *) override
	{
		if (live)
			engine().zoomReset();
	}
	void wheelEvent(QWheelEvent *e) override
	{
		if (!live)
			return;
		const int d = e->angleDelta().y();
		if (d)
			engine().zoomStep(d > 0 ? 1.15f : 1.0f / 1.15f);
		e->accept();
	}
};

/* ---------- zoom do painel: peças combináveis (Configurações → Zoom) ---------- */
enum ZoomPart { ZP_MAP = 1, ZP_SLIDER = 2, ZP_ARROWS = 4, ZP_PLUSMINUS = 8, ZP_FIT = 16 };
struct ZoomCfg {
	int parts = ZP_SLIDER | ZP_ARROWS | ZP_FIT;
	bool cross = true; /* setas em cruz (senão, em linha) */
	int place = 0;      /* 0 automático, 1 coluna à direita, 2 junto com os botões */
	/* coluna à direita: ocupa a altura das linhas sem esticar nenhuma */
	bool side() const { return place == 1 || (place == 0 && cross && (parts & ZP_ARROWS)); }
};

static ZoomCfg zoomCfgSetting()
{
	QSettings st(ui::settingsFile(), QSettings::IniFormat);
	ZoomCfg c;
	c.parts = st.value("zoomParts", c.parts).toInt() & 31;
	c.cross = st.value("zoomCross", true).toBool();
	c.place = qBound(0, st.value("zoomPlace", 0).toInt(), 2);
	return c;
}

/* monta os controles do zoom dentro de box. live = false: só para mostrar (prévia) */
static void makeZoomControls(QWidget *box, const ZoomCfg &cfg, bool live, ZoomMap **mapOut, QSlider **sliderOut)
{
	if (mapOut)
		*mapOut = nullptr;
	if (sliderOut)
		*sliderOut = nullptr;
	const bool cross = cfg.cross && (cfg.parts & ZP_ARROWS);
	const int bw = cross ? 30 : 28;
	auto hold = [&](const char *ic, const QString &tip, int axis, int dir) {
		auto *b = ui::button(QString(), tl("Zoom: ") + tip, box);
		b->setIcon(ui::icon(ic));
		b->setIconSize(QSize(12, 12));
		b->setFixedWidth(bw);
		if (live) {
			QObject::connect(b, &QPushButton::pressed, box, [axis, dir] {
				if (axis != 2) {
					const PlaybackChannel::Zoom zm = engine().channel((engine().getSelection() & SEL_A) ? 0 : 1).zoom();
					if (zm.z < 1.01f)
						engine().zoomSet(1.5f, zm.x, zm.y); /* mover sem zoom não faz nada: entra em 1.5x */
				}
				engine().zoomMove(axis, dir);
			});
			QObject::connect(b, &QPushButton::released, box, [axis] { engine().zoomMove(axis, 0); });
		}
		return b;
	};
	auto fit = [&]() {
		auto *f = ui::button(QString::fromUtf8("1x"), tl("Zoom: volta para a imagem inteira"), box);
		f->setFixedWidth(30);
		if (live)
			QObject::connect(f, &QPushButton::clicked, box, [] { engine().zoomReset(); });
		return f;
	};
	auto L = [&]() { return hold("left", tl("esquerda (segure)"), 0, -1); };
	auto U = [&]() { return hold("up", tl("cima (segure)"), 1, -1); };
	auto D = [&]() { return hold("down", tl("baixo (segure)"), 1, 1); };
	auto R = [&]() { return hold("right", tl("direita (segure)"), 0, 1); };
	auto ZO = [&]() { return hold("zoomout", tl("afastar (segure)"), 2, -1); };
	auto ZI = [&]() { return hold("zoomin", tl("aproximar (segure)"), 2, 1); };

	auto *h = new QHBoxLayout(box);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(3);
	if (cfg.parts & ZP_MAP) {
		auto *m = new ZoomMap(box, live);
		if (!live)
			m->z = {1.8f, 0.4f, 0.45f}; /* exemplo na prévia */
		if (mapOut)
			*mapOut = m;
		h->addWidget(m);
	}
	if (cfg.parts & ZP_SLIDER) {
		const bool vert = cfg.side() && cross;
		auto *sl = new QSlider(vert ? Qt::Vertical : Qt::Horizontal, box);
		sl->setRange(100, 500);
		sl->setSingleStep(10);
		sl->setPageStep(50);
		if (vert)
			sl->setFixedSize(20, 84);
		else
			sl->setFixedWidth(90);
		sl->setToolTip(tl("Zoom: 1x a 5x"));
		if (!live)
			sl->setValue(180);
		if (sliderOut)
			*sliderOut = sl;
		h->addWidget(sl);
	}
	if (cfg.parts & (ZP_MAP | ZP_SLIDER))
		h->addSpacing(4);
	bool fitDone = false;
	if (cfg.parts & ZP_ARROWS) {
		if (cross) {
			auto *g = new QGridLayout();
			g->setContentsMargins(0, 0, 0, 0);
			g->setSpacing(2);
			g->addWidget(U(), 0, 1);
			g->addWidget(L(), 1, 0);
			g->addWidget(R(), 1, 2);
			g->addWidget(D(), 2, 1);
			if (cfg.parts & ZP_FIT) {
				g->addWidget(fit(), 1, 1); /* 1x no meio da cruz */
				fitDone = true;
			}
			h->addLayout(g);
		} else {
			h->addWidget(L());
			h->addWidget(U());
			h->addWidget(D());
			h->addWidget(R());
		}
	}
	if (cfg.parts & ZP_PLUSMINUS) {
		h->addSpacing(4);
		if (cross) {
			auto *v = new QVBoxLayout();
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(2);
			v->addWidget(ZI());
			v->addStretch(1);
			v->addWidget(ZO());
			h->addLayout(v);
		} else {
			h->addWidget(ZO());
			h->addWidget(ZI());
		}
	}
	if ((cfg.parts & ZP_FIT) && !fitDone) {
		h->addSpacing(2);
		h->addWidget(fit());
	}
}

/* blocos da barra de controles que podem mudar de ordem (Configurações → Painel) */
struct DockBlock {
	const char *id;
	const char *label;
};
static const DockBlock kBlocks[] = {
	{"transport", N_("Transporte (◀ ▶ ▶▶)")}, {"live", N_("Ao vivo")},          {"mark", N_("IN / OUT e −5 / −10 / −20")},
	{"speed", N_("Velocidades")},             {"loopaudio", N_("Loop e áudio")}, {"zoom", N_("Zoom")},
	{"cams", N_("Câmeras A / B")},            {"air", N_("Último no ar / Play Events")},
};

/* ordem de fábrica: Reprodução · Saída · Marcar / No ar · Câmeras / Velocidade · Repetir (zoom na coluna) */
static QStringList defaultBlockOrder()
{
	return {"transport", "live", "mark", "br1", "air", "cams", "br2", "speed", "loopaudio", "zoom"};
}

static QStringList blockOrderSetting()
{
	QStringList def;
	for (const DockBlock &b : kBlocks)
		def << QString::fromLatin1(b.id);
	QStringList saved = QSettings(ui::settingsFile(), QSettings::IniFormat).value("blockOrder", defaultBlockOrder()).toStringList();
	QStringList out;
	for (const QString &id : saved)
		if ((def.contains(id) || id.startsWith("br")) && !out.contains(id))
			out << id;
	for (const QString &id : def)
		if (!out.contains(id))
			out << id;
	return out;
}

static const char *kDockLayouts[] = {N_("Padrão — lista em cima, controles embaixo"), N_("Controles em cima, lista embaixo"),
				     N_("Lado a lado — lista à esquerda, controles à direita"), N_("Só controles (sem a lista de eventos)")};

/* nome dos grupos (Configurações → Painel). Estilos:
 * 0 sem nome · 1 caixa com nome em cima · 2 nome na borda · 3 nome ao lado (vertical)
 * 4 só o nome em cima (sem caixa) · 5 só a caixa (sem nome) */
static int groupTitleStyle()
{
	return qBound(0, QSettings(ui::settingsFile(), QSettings::IniFormat).value("groupTitleStyle", 2).toInt(), 5);
}

class TitleFrame : public QWidget {
public:
	TitleFrame(QWidget *parent, const QString &t, int st) : QWidget(parent), title(t), style(st)
	{
		setProperty("irCard", true);
		auto *v = new QVBoxLayout(this);
		v->setSpacing(0);
		switch (style) {
		case 1: v->setContentsMargins(4, 14, 4, 4); break;
		case 2: v->setContentsMargins(4, 9, 4, 4); break;
		case 3: v->setContentsMargins(15, 3, 4, 3); break;
		case 4: v->setContentsMargins(0, 12, 0, 0); break;
		default: v->setContentsMargins(3, 3, 3, 3); break;
		}
		if (style == 1 || style == 2 || style == 4) { /* o nome nunca fica cortado */
			QFont f = font();
			f.setPixelSize(9);
			f.setBold(true);
			f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
			setMinimumWidth(QFontMetrics(f).horizontalAdvance(title) + (style == 4 ? 4 : 22));
		}
	}
	void setInner(QWidget *w)
	{
		w->setParent(this);
		static_cast<QVBoxLayout *>(layout())->addWidget(w);
	}
	QString title;
	int style;

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		QFont f = font();
		f.setPixelSize(9);
		f.setBold(true);
		f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
		p.setFont(f);
		const QFontMetrics fm(f);
		const QColor txt("#7d8696"), border("#2a313d"), fill("#141821");
		const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		if (style == 1 || style == 5 || style == 3) {
			p.setPen(QPen(border, 1));
			p.setBrush(fill);
			p.drawRoundedRect(r, 7, 7);
		}
		if (style == 1) {
			p.setPen(txt);
			p.drawText(QPointF(7, 11), title);
		} else if (style == 2) {
			const int tw = fm.horizontalAdvance(title);
			const QRectF b = r.adjusted(0, 5, 0, 0);
			QPainterPath path;
			path.addRoundedRect(b, 7, 7);
			p.setBrush(fill);
			p.setPen(Qt::NoPen);
			p.drawPath(path);
			/* borda com uma abertura onde vai o nome */
			p.save();
			p.setClipRegion(QRegion(rect()) - QRegion(QRect(7, 0, tw + 8, 10)));
			p.setPen(QPen(border, 1));
			p.setBrush(Qt::NoBrush);
			p.drawPath(path);
			p.restore();
			p.setPen(txt);
			p.drawText(QPointF(11, 8), title);
		} else if (style == 3) {
			p.save();
			p.translate(11, height() / 2.0 + fm.horizontalAdvance(title) / 2.0);
			p.rotate(-90);
			p.setPen(txt);
			p.drawText(QPointF(0, 0), title);
			p.restore();
		} else if (style == 4) {
			p.setPen(txt);
			p.drawText(QPointF(1, 9), title);
		}
	}
};

static QWidget *titled(QWidget *parent, QWidget *inner, const char *title)
{
	const int st = groupTitleStyle();
	if (st == 0)
		return inner;
	auto *f = new TitleFrame(parent, tl(title), st);
	f->setInner(inner);
	return f;
}

/* moldura arrastável sobre cada grupo no modo "organizar" */
class BlockHandle : public QWidget {
public:
	BlockHandle(QWidget *parent, const QString &id_, bool isBreak) : QWidget(parent), id(id_), brk(isBreak)
	{
		setCursor(Qt::OpenHandCursor);
		setToolTip(isBreak ? tl("Quebra de linha — arraste; duplo clique remove")
				   : tl("Arraste para mudar o lugar deste grupo"));
	}
	QString id;
	bool brk;
	bool dragging = false;
	bool bad = false; /* posição livre: em cima de outro grupo */
	QPoint grab;
	std::function<void(BlockHandle *, QPoint)> onMove;
	std::function<void(BlockHandle *, QPoint)> onDrop;
	std::function<void(BlockHandle *)> onRemove;

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QColor c = bad ? QColor("#ef4444") : brk ? QColor("#f59e0b") : QColor("#3b82f6");
		QColor fill = c;
		fill.setAlpha(dragging ? 90 : 45);
		p.setBrush(fill);
		p.setPen(QPen(c, 1.5, Qt::DashLine));
		p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 5, 5);
		if (brk) {
			p.setPen(Qt::white);
			QFont f = font();
			f.setBold(true);
			p.setFont(f);
			p.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("↵"));
			return;
		}
		/* alça no canto: 6 pontinhos */
		const QRectF g(3, 3, 12, 14);
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawRoundedRect(g, 3, 3);
		p.setBrush(Qt::white);
		for (int r = 0; r < 3; r++)
			for (int k = 0; k < 2; k++)
				p.drawEllipse(QPointF(g.left() + 4 + k * 4, g.top() + 3.5 + r * 3.5), 1.1, 1.1);
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		dragging = true;
		grab = e->position().toPoint();
		setCursor(Qt::ClosedHandCursor);
		raise();
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!dragging)
			return;
		move(mapToParent(e->position().toPoint() - grab));
		if (onMove)
			onMove(this, mapToParent(e->position().toPoint()));
	}
	void mouseReleaseEvent(QMouseEvent *e) override
	{
		if (!dragging)
			return;
		dragging = false;
		bad = false;
		setCursor(Qt::OpenHandCursor);
		update();
		if (onDrop)
			onDrop(this, mapToParent(e->position().toPoint()));
	}
	void mouseDoubleClickEvent(QMouseEvent *) override
	{
		if (brk && onRemove)
			onRemove(this);
	}
};

static bool g_wantWizard = false;
static bool g_wantArrange = false;
static bool g_obsLoaded = false;
static void dockFrontendEventCb(enum obs_frontend_event event, void *param);

static bool g_configOpen = false;
static QString g_pendingLang;   /* idioma escolhido na ⚙: aplica depois de salvar */
static int g_configReopenTab = -1; /* reabre a ⚙ nesta aba (já no idioma novo) */
static int g_configStartTab = -1;

static bool showConfigDialog(QWidget *parent)
{
	if (g_configOpen)
		return false;
	g_configOpen = true;
	struct ConfigOpenReset {
		~ConfigOpenReset() { g_configOpen = false; }
	} configOpenReset;
	ReplayConfig cfg = engine().config();

	QDialog dlg(parent);
	dlg.setWindowTitle(tl("Configurar ReplayDeck"));
	/* abre já no tamanho bom (todas as abas visíveis) e lembra o último tamanho */
	dlg.setMinimumSize(760, 480);
	{
		const QSize last = QSettings(ui::settingsFile(), QSettings::IniFormat).value("configSize", QSize(920, 580)).toSize();
		dlg.resize(last.expandedTo(QSize(920, 580)));
	}
	ui::applyDialog(&dlg);
	auto *dlgV = new QVBoxLayout(&dlg);
	dlgV->setContentsMargins(12, 12, 12, 12);
	auto *tabs = new QTabWidget(&dlg);
	dlgV->addWidget(tabs, 1);
	auto newTab = [&](const QString &title, const QString &hint) {
		auto *page = new QWidget();
		auto *v = new QVBoxLayout(page);
		v->setContentsMargins(14, 12, 14, 12);
		if (!hint.isEmpty()) {
			auto *h = new QLabel(hint, page);
			h->setProperty("role", "muted");
			h->setWordWrap(true);
			v->addWidget(h);
		}
		auto *f = new QFormLayout();
		f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
		f->setVerticalSpacing(10);
		f->setHorizontalSpacing(12);
		v->addLayout(f);
		v->addStretch(1);
		tabs->addTab(page, title);
		return f;
	};
	QFormLayout *fRec = newTab(tl("Gravação"),
				   tl("Onde e como as câmeras são gravadas. Qualidade constante mantém a imagem igual em "
						     "qualquer lance e só usa disco quando precisa."));
	QFormLayout *fCam = newTab(tl("Câmeras e áudio"), QString());
	QFormLayout *fAir = newTab(tl("No ar"),
				   tl("O que acontece quando o replay entra e sai do programa."));
	QFormLayout *fTr = newTab(tl("Transições"), QString());
	/* aba Zoom: monte o zoom com as peças que quiser, com prévia */
	QMap<int, QCheckBox *> zoomBoxes;
	QRadioButton *zoomCrossRb;
	QComboBox *zoomPlaceBox;
	{
		const ZoomCfg zc = zoomCfgSetting();
		auto *page = new QWidget();
		auto *v = new QVBoxLayout(page);
		v->setContentsMargins(14, 12, 14, 12);
		v->setSpacing(10);
		auto *h = new QLabel(tl("Marque as peças que quer no painel. As setas e o − / + funcionam segurando; "
						       "nada sai da imagem."),
				     page);
		h->setProperty("role", "muted");
		h->setWordWrap(true);
		v->addWidget(h);
		auto *grid = new QGridLayout();
		grid->setHorizontalSpacing(24);
		grid->setVerticalSpacing(8);
		const std::pair<int, const char *> items[] = {{ZP_MAP, N_("Mini-mapa (arrastar e rodinha)")},
							      {ZP_SLIDER, N_("Barra deslizante")},
							      {ZP_ARROWS, N_("Setas (cima, baixo, esquerda, direita)")},
							      {ZP_PLUSMINUS, N_("Botões − / + (aproximar e afastar)")},
							      {ZP_FIT, N_("Botão 1x (imagem inteira)")}};
		int r = 0;
		for (const auto &it : items) {
			auto *cb = new QCheckBox(tl(it.second), page);
			cb->setChecked(zc.parts & it.first);
			zoomBoxes.insert(it.first, cb);
			grid->addWidget(cb, r % 3, r / 3);
			r++;
		}
		v->addLayout(grid);
		auto *ar = new QHBoxLayout();
		ar->addWidget(new QLabel(tl("Setas:"), page));
		auto *lineRb = new QRadioButton(tl("em linha"), page);
		zoomCrossRb = new QRadioButton(tl("em cruz"), page);
		(zc.cross ? zoomCrossRb : lineRb)->setChecked(true);
		ar->addWidget(lineRb);
		ar->addWidget(zoomCrossRb);
		ar->addSpacing(30);
		ar->addWidget(new QLabel(tl("Lugar no painel:"), page));
		zoomPlaceBox = new QComboBox(page);
		zoomPlaceBox->addItem(tl("Automático (cruz vai para a coluna à direita)"), 0);
		zoomPlaceBox->addItem(tl("Coluna à direita"), 1);
		zoomPlaceBox->addItem(tl("Junto com os botões"), 2);
		zoomPlaceBox->setCurrentIndex(zc.place);
		ar->addWidget(zoomPlaceBox);
		ar->addStretch(1);
		v->addLayout(ar);

		auto *pt = new QLabel(tl("<b>Como vai ficar no painel</b>"), page);
		pt->setContentsMargins(0, 8, 0, 0);
		v->addWidget(pt);
		auto *frame = new QFrame(page);
		frame->setStyleSheet("QFrame#zprev { background: #0e1116; border: 1px solid #2a303c; border-radius: 8px; }");
		frame->setObjectName("zprev");
		frame->setMinimumHeight(110);
		auto *fl = new QHBoxLayout(frame);
		fl->setContentsMargins(14, 12, 14, 12);
		auto *holder = new QWidget(frame);
		fl->addWidget(holder);
		fl->addStretch(1);
		v->addWidget(frame);
		auto *emptyLbl = new QLabel(tl("Nada marcado: o zoom fica só nos atalhos."), frame);
		emptyLbl->setProperty("role", "muted");
		fl->insertWidget(0, emptyLbl);
		v->addStretch(1);

		auto rebuild = [holder, emptyLbl, zoomBoxes, zoomCrossRb, zoomPlaceBox]() mutable {
			ZoomCfg c;
			c.place = zoomPlaceBox->currentData().toInt();
			c.parts = 0;
			for (auto it = zoomBoxes.begin(); it != zoomBoxes.end(); ++it)
				if (it.value()->isChecked())
					c.parts |= it.key();
			c.cross = zoomCrossRb->isChecked();
			delete holder->layout();
			for (QWidget *w : holder->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
				delete w;
			makeZoomControls(holder, c, false, nullptr, nullptr);
			for (QWidget *w : holder->findChildren<QWidget *>())
				w->setAttribute(Qt::WA_TransparentForMouseEvents);
			holder->setVisible(c.parts != 0);
			emptyLbl->setVisible(c.parts == 0);
			holder->adjustSize();
		};
		rebuild();
		for (QCheckBox *cb : zoomBoxes)
			QObject::connect(cb, &QCheckBox::toggled, page, rebuild);
		QObject::connect(zoomCrossRb, &QRadioButton::toggled, page, rebuild);
		QObject::connect(zoomPlaceBox, &QComboBox::currentIndexChanged, page, rebuild);
		tabs->addTab(page, tl("Zoom"));
	}
	/* aba Painel: layout, ordem dos grupos e o que aparece */
	QMap<QString, QCheckBox *> partBoxes;
	QComboBox *dockLayoutBox;
	QComboBox *titleStyleBox;
	QListWidget *orderList;
	{
		auto *page = new QWidget();
		auto *v = new QVBoxLayout(page);
		v->setContentsMargins(14, 12, 14, 12);
		v->setSpacing(8);
		auto *lr = new QHBoxLayout();
		lr->addWidget(new QLabel(tl("Layout do painel"), page));
		dockLayoutBox = new QComboBox(page);
		for (int i = 0; i < 4; i++)
			dockLayoutBox->addItem(tl(kDockLayouts[i]), i);
		dockLayoutBox->setCurrentIndex(qBound(0, QSettings(ui::settingsFile(), QSettings::IniFormat).value("dockLayout", 0).toInt(), 3));
		lr->addWidget(dockLayoutBox, 1);
		lr->addSpacing(16);
		lr->addWidget(new QLabel(tl("Nome dos grupos"), page));
		titleStyleBox = new QComboBox(page);
		for (const char *t : {N_("Sem nome"), N_("Caixa com nome em cima"), N_("Nome na borda"), N_("Nome ao lado (vertical)"),
				      N_("Só o nome em cima (sem caixa)"), N_("Só a caixa (sem nome)")})
			titleStyleBox->addItem(tl(t));
		titleStyleBox->setCurrentIndex(groupTitleStyle());
		lr->addWidget(titleStyleBox);
		v->addLayout(lr);

		auto *cols = new QHBoxLayout();
		cols->setSpacing(20);
		/* ordem: arraste ou use as setas */
		auto *oc = new QVBoxLayout();
		auto *ot = new QLabel(tl("<b>Ordem dos botões</b> — arraste para mudar"), page);
		oc->addWidget(ot);
		orderList = new QListWidget(page);
		orderList->setDragDropMode(QAbstractItemView::InternalMove);
		orderList->setDefaultDropAction(Qt::MoveAction);
		orderList->setStyleSheet("QListWidget::item { padding: 4px 4px; }");
		orderList->setMinimumWidth(230);
		auto fillOrder = [orderList](const QStringList &order) {
			orderList->clear();
			for (const QString &id : order) {
				if (id.startsWith("br")) {
					auto *it = new QListWidgetItem(tl("↵  quebra de linha"), orderList);
					it->setData(Qt::UserRole, id);
					it->setForeground(QColor("#f59e0b"));
					continue;
				}
				for (const DockBlock &b : kBlocks)
					if (id == QLatin1String(b.id)) {
						auto *it = new QListWidgetItem(tl(b.label), orderList);
						it->setData(Qt::UserRole, id);
					}
			}
		};
		fillOrder(blockOrderSetting());
		oc->addWidget(orderList, 1);
		auto *ob = new QHBoxLayout();
		auto *upB = new QPushButton(tl("▲ Subir"), page);
		auto *dnB = new QPushButton(tl("▼ Descer"), page);
		auto moveSel = [orderList](int d) {
			const int r = orderList->currentRow();
			if (r < 0 || r + d < 0 || r + d >= orderList->count())
				return;
			QListWidgetItem *it = orderList->takeItem(r);
			orderList->insertItem(r + d, it);
			orderList->setCurrentRow(r + d);
		};
		QObject::connect(upB, &QPushButton::clicked, page, [moveSel] { moveSel(-1); });
		QObject::connect(dnB, &QPushButton::clicked, page, [moveSel] { moveSel(1); });
		auto *brB = new QPushButton(tl("+ Quebra"), page);
		brB->setToolTip(tl("Quebra de linha: o que vem depois começa numa linha nova"));
		QObject::connect(brB, &QPushButton::clicked, page, [orderList] {
			QStringList ids;
			for (int r = 0; r < orderList->count(); r++)
				ids << orderList->item(r)->data(Qt::UserRole).toString();
			int n = 1;
			while (ids.contains(QString("br%1").arg(n)))
				n++;
			auto *it = new QListWidgetItem(tl("↵  quebra de linha"));
			it->setData(Qt::UserRole, QString("br%1").arg(n));
			it->setForeground(QColor("#f59e0b"));
			const int at = orderList->currentRow() < 0 ? orderList->count() : orderList->currentRow() + 1;
			orderList->insertItem(at, it);
			orderList->setCurrentRow(at);
		});
		auto *rmB = new QPushButton(tl("Tirar quebra"), page);
		QObject::connect(rmB, &QPushButton::clicked, page, [orderList] {
			QListWidgetItem *it = orderList->currentItem();
			if (it && it->data(Qt::UserRole).toString().startsWith("br"))
				delete orderList->takeItem(orderList->row(it));
		});
		ob->addWidget(upB);
		ob->addWidget(dnB);
		ob->addWidget(brB);
		ob->addWidget(rmB);
		ob->addStretch(1);
		oc->addLayout(ob);
		auto *arr = new QPushButton(tl("✥ Organizar arrastando no painel"), page);
		arr->setToolTip(tl("Salva e fecha esta janela; no painel, arraste cada grupo para onde quiser"));
		ui::setKind(arr, "primary");
		QObject::connect(arr, &QPushButton::clicked, &dlg, [&dlg] {
			g_wantArrange = true;
			dlg.accept();
		});
		oc->addWidget(arr);
		cols->addLayout(oc, 1);

		/* o que aparece */
		auto *pc = new QVBoxLayout();
		pc->addWidget(new QLabel(tl("<b>O que aparece</b> — tudo continua no atalho"), page));
		auto *g = new QGridLayout();
		g->setHorizontalSpacing(18);
		g->setVerticalSpacing(6);
		const int n = (int)(sizeof(kParts) / sizeof(kParts[0]));
		for (int i = 0; i < n; i++) {
			auto *cb = new QCheckBox(tl(kParts[i].label), page);
			cb->setChecked(partOn(kParts[i].id));
			partBoxes.insert(kParts[i].id, cb);
			g->addWidget(cb, i % ((n + 1) / 2), i / ((n + 1) / 2));
		}
		pc->addLayout(g);
		pc->addStretch(1);
		cols->addLayout(pc, 2);
		v->addLayout(cols, 1);

		auto *reset = new QPushButton(tl("Voltar ao padrão"), page);
		QObject::connect(reset, &QPushButton::clicked, page, [&partBoxes, fillOrder, dockLayoutBox, titleStyleBox] {
			for (const DockPart &p : kParts)
				partBoxes.value(p.id)->setChecked(p.defOn);
			fillOrder(defaultBlockOrder());
			dockLayoutBox->setCurrentIndex(0);
			titleStyleBox->setCurrentIndex(2);
		});
		auto *rr = new QHBoxLayout();
		rr->addWidget(reset);
		rr->addStretch(1);
		v->addLayout(rr);
		tabs->addTab(page, tl("Painel"));
	}
	/* aba Disco: limite e limpeza das gravações */
	QSpinBox *limitGb, *minFree;
	{
		QFormLayout *fDisk = newTab(tl("Disco"),
					    tl("Para o replay não encher o PC. As gravações mais antigas são apagadas primeiro "
							      "(junto com os eventos delas); a que está gravando nunca é apagada."));
		limitGb = new QSpinBox(&dlg);
		limitGb->setRange(0, 100000);
		limitGb->setSuffix(" GB");
		limitGb->setSpecialValueText(tl("sem limite"));
		limitGb->setValue(cfg.cacheLimitGB);
		limitGb->setToolTip(tl("Tamanho máximo da pasta de gravações. Passou disso, apaga as mais antigas."));
		fDisk->addRow(tl("Limite da pasta"), limitGb);
		minFree = new QSpinBox(&dlg);
		minFree->setRange(0, 10000);
		minFree->setSuffix(tl(" GB livres"));
		minFree->setSpecialValueText(tl("desligado"));
		minFree->setValue(cfg.minFreeGB);
		minFree->setToolTip(tl("Se o disco ficar com menos que isso, apaga as gravações antigas;\n"
						      "sem mais nada para apagar, para a gravação (o PC nunca enche)."));
		fDisk->addRow(tl("Espaço livre mínimo"), minFree);
		auto *manage = new QPushButton(tl("Ver e apagar gravações…"), &dlg);
		QObject::connect(manage, &QPushButton::clicked, &dlg, [&dlg] { showSessionsDialog(&dlg); });
		auto *clr = new QPushButton(tl("Limpar todo o cache…"), &dlg);
		ui::setKind(clr, "danger");
		QObject::connect(clr, &QPushButton::clicked, &dlg, [&dlg] { clearCacheAsk(&dlg); });
		auto *mr = new QHBoxLayout();
		mr->addWidget(manage);
		mr->addWidget(clr);
		mr->addStretch(1);
		fDisk->addRow(QString(), mr);
	}

	const bool recording = engine().recording();
	if (recording) {
		/* gravando: o que muda a gravação fica travado; o resto pode mudar */
		for (int t = 0; t < 2; t++) {
			tabs->widget(t)->setEnabled(false);
			tabs->setTabText(t, tabs->tabText(t) + QString::fromUtf8(" 🔒"));
			tabs->setTabToolTip(t, tl("Pare a gravação (●) para mudar"));
		}
		tabs->setCurrentIndex(2);
	}

	/* ---- pasta da sessão ---- */
	auto *dirEdit = new QLineEdit(QString::fromUtf8(cfg.outputDir.c_str()), &dlg);
	auto *browse = new QPushButton(tl("Procurar"), &dlg);
	auto *dirRow = new QHBoxLayout();
	dirRow->addWidget(dirEdit, 1);
	dirRow->addWidget(browse);
	fRec->addRow(tl("Pasta"), dirRow);

	/* ---- formato de gravação ---- */
	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	auto *resLbl = new QLineEdit(QString("%1x%2").arg(ovi.base_width).arg(ovi.base_height), &dlg);
	auto *fpsLbl = new QLineEdit(
		QString("%1 fps").arg(ovi.fps_den ? (double)ovi.fps_num / ovi.fps_den : 0.0, 0, 'f', 2), &dlg);
	for (QLineEdit *e : {resLbl, fpsLbl}) {
		e->setReadOnly(true);
		e->setEnabled(false);
		e->setToolTip(tl("Vem do vídeo do programa (cada câmera grava no tamanho da própria entrada)"));
	}
	/* dado: negativo = qualidade constante (−QP); positivo = taxa fixa em Mbps */
	auto *quality = new QComboBox(&dlg);
	quality->addItem(tl("Máxima — qualidade constante"), -14);
	quality->addItem(tl("Alta — qualidade constante  ★ recomendada"), -18);
	quality->addItem(tl("Equilibrada — qualidade constante"), -22);
	quality->addItem(tl("Taxa fixa 40 Mbps"), 40);
	quality->addItem(tl("Taxa fixa 80 Mbps"), 80);
	quality->addItem(tl("Taxa fixa 144 Mbps"), 144);
	quality->addItem(tl("Taxa fixa 220 Mbps"), 220);
	quality->setToolTip(tl("Qualidade constante: todos os quadros com a mesma nitidez (inclusive lances rápidos);\n"
		"o disco usado varia com a imagem. Taxa fixa: uso de disco previsível."));
	{
		const int curQ = cfg.rec.cq > 0 ? -cfg.rec.cq : cfg.rec.bitrateMbps;
		if (quality->findData(curQ) < 0)
			quality->addItem(cfg.rec.cq > 0 ? tl("Qualidade constante QP %1").arg(cfg.rec.cq)
							: tl("Taxa fixa %1 Mbps").arg(cfg.rec.bitrateMbps),
					 curQ);
		quality->setCurrentIndex(quality->findData(curQ));
	}
	auto *fmtRow = new QHBoxLayout();
	fmtRow->addWidget(resLbl);
	fmtRow->addWidget(fpsLbl);
	fRec->addRow(tl("Formato"), fmtRow);
	fRec->addRow(tl("Qualidade"), quality);

	auto *enc = new QComboBox(&dlg);
#ifdef __APPLE__
	/* Mac: o codificador do próprio chip (Apple Silicon) */
	enc->addItem(tl("Automático (Apple VideoToolbox → MJPEG)"), "auto");
	enc->addItem("Apple VideoToolbox H.264 intra", "h264_videotoolbox");
	enc->addItem("Apple VideoToolbox HEVC intra", "hevc_videotoolbox");
#else
	enc->addItem(tl("Automático (NVENC → QSV → AMF → MJPEG)"), "auto");
	enc->addItem("NVIDIA NVENC H.264 intra", "h264_nvenc");
	enc->addItem("NVIDIA NVENC HEVC intra", "hevc_nvenc");
	enc->addItem("Intel QSV H.264 intra", "h264_qsv");
	enc->addItem("Intel QSV HEVC intra", "hevc_qsv");
	enc->addItem("AMD AMF H.264 intra", "h264_amf");
	enc->addItem("AMD AMF HEVC intra", "hevc_amf");
#endif
	enc->addItem("MJPEG 4:2:2 (CPU)", "mjpeg");
	enc->setCurrentIndex(qMax(0, enc->findData(QString::fromUtf8(cfg.rec.encoder.c_str()))));
	fRec->addRow(tl("Encoder"), enc);

	/* ---- tempo disponível / espaço usado ---- */
	auto *timeLbl = new QLabel(&dlg);
	timeLbl->setAlignment(Qt::AlignCenter);
	timeLbl->setMinimumHeight(30);
	auto *usedLbl = new QLabel(&dlg);
	usedLbl->setAlignment(Qt::AlignCenter);
	usedLbl->setMinimumHeight(30);
	usedLbl->setStyleSheet("background-color: #1f2430; color: #cbd2de; font-weight: bold; border-radius: 6px;");
	auto *openDir = new QPushButton(tl("Abrir pasta"), &dlg);
	auto *usedRow = new QHBoxLayout();
	usedRow->addWidget(usedLbl, 1);
	usedRow->addWidget(openDir);
	auto *bars = new QVBoxLayout();
	bars->addWidget(timeLbl);
	bars->addLayout(usedRow);
	fRec->addRow(QString(), bars);

	/* ---- câmeras (2 colunas) ---- */
	QStringList names; /* Studio: as entradas com imagem (sem as saídas do replay) */
	for (const std::string &n : rdsCameraNames())
		names << QString::fromUtf8(n.c_str());

	QComboBox *cams[MAX_CAMERAS];
	auto *camGrid = new QGridLayout();
	camGrid->setHorizontalSpacing(6);
	for (int i = 0; i < MAX_CAMERAS; i++) {
		cams[i] = new QComboBox(&dlg);
		cams[i]->addItem(tl("Nenhuma"), QString());
		for (const QString &n : names)
			cams[i]->addItem(n, n);
		const QString cur = QString::fromUtf8(cfg.cameras[i].c_str());
		int idx = cams[i]->findData(cur);
		if (idx < 0 && !cur.isEmpty()) {
			cams[i]->addItem(cur + tl(" (não encontrada)"), cur);
			idx = cams[i]->count() - 1;
		}
		cams[i]->setCurrentIndex(idx < 0 ? 0 : idx);
		auto *num = new QLabel(QString("<b>%1</b>").arg(i + 1), &dlg);
		num->setAlignment(Qt::AlignCenter);
		num->setFixedWidth(22);
		const int rowI = i % 4, colI = (i / 4) * 2;
		camGrid->addWidget(num, rowI, colI);
		camGrid->addWidget(cams[i], rowI, colI + 1);
	}
	camGrid->setColumnStretch(1, 1);
	camGrid->setColumnStretch(3, 1);
	fCam->addRow(tl("Câmeras"), camGrid);
	{
		auto *wiz = new QPushButton(tl("Configuração rápida…"), &dlg);
		wiz->setToolTip(tl("Assistente: marca as câmeras, escolhe a pasta e cria a cena Replay"));
		QObject::connect(wiz, &QPushButton::clicked, &dlg, [&dlg] {
			g_wantWizard = true;
			dlg.reject();
		});
		auto *addC = new QPushButton(tl("+ Adicionar câmera…"), &dlg);
		addC->setToolTip(tl("Cria uma entrada nova com a câmera"));
		QObject::connect(addC, &QPushButton::clicked, &dlg, [&dlg, &cams] {
			const QString n = setup::addCameraDialog(&dlg);
			if (n.isEmpty())
				return;
			bool placed = false;
			for (int i = 0; i < MAX_CAMERAS; i++) {
				if (cams[i]->findData(n) < 0)
					cams[i]->addItem(n, n);
				if (!placed && cams[i]->currentData().toString().isEmpty()) {
					cams[i]->setCurrentIndex(cams[i]->findData(n));
					placed = true;
				}
			}
		});
		auto *wr = new QHBoxLayout();
		wr->addWidget(addC);
		wr->addWidget(wiz);
		wr->addStretch(1);
		fCam->addRow(QString(), wr);
	}

	/* ---- áudio ---- */
	auto *trA = new QComboBox(&dlg);
	auto *trB = new QComboBox(&dlg);
	trB->addItem(tl("Nenhuma"), 0);
	for (int t = 1; t <= MAX_AUDIO_MIXES; t++) {
		trA->addItem(tl("Trilha %1").arg(t), t);
		trB->addItem(tl("Trilha %1").arg(t), t);
	}
	trA->setCurrentIndex(qMax(0, trA->findData(cfg.trackA)));
	trB->setCurrentIndex(qMax(0, trB->findData(cfg.trackB)));
	auto *recAudio = new QHBoxLayout();
	recAudio->addWidget(new QLabel(tl("Canais 1-2"), &dlg));
	recAudio->addWidget(trA, 1);
	recAudio->addWidget(new QLabel(tl("Canais 3-4"), &dlg));
	recAudio->addWidget(trB, 1);
	fCam->addRow(tl("Áudio gravado"), recAudio);

	auto *pbAudio = new QComboBox(&dlg);
	pbAudio->addItem(tl("Canais 1-2"), 0);
	pbAudio->addItem(tl("Canais 3-4"), 1);
	pbAudio->setCurrentIndex(cfg.playbackPair == 1 ? 1 : 0);
	fCam->addRow(tl("Áudio do replay"), pbAudio);

	/* ---- transições ---- */

	QStringList sceneNames;
	{
		struct obs_frontend_source_list sl = {};
		obs_frontend_get_scenes(&sl);
		for (size_t i = 0; i < sl.sources.num; i++)
			sceneNames << QString::fromUtf8(obs_source_get_name(sl.sources.array[i]));
		obs_frontend_source_list_free(&sl);
	}
	auto *sceneBox = new QComboBox(&dlg);
	sceneBox->addItem(tl("Automático (entrada Replay A)"), QString());
	for (const QString &n : sceneNames)
		sceneBox->addItem(n, n);
	sceneBox->setCurrentIndex(qMax(0, sceneBox->findData(QString::fromUtf8(cfg.replayScene.c_str()))));
	sceneBox->setToolTip(tl("Entrada que vai ao ar quando o replay toca"));
	fAir->addRow(tl("Entrada do replay"), sceneBox);

	QStringList transNames;
	{
		struct obs_frontend_source_list trl = {};
		obs_frontend_get_transitions(&trl);
		for (size_t i = 0; i < trl.sources.num; i++)
			transNames << QString::fromUtf8(obs_source_get_name(trl.sources.array[i]));
		obs_frontend_source_list_free(&trl);
	}
	auto makeTrans = [&](const std::string &cur, int curMs, QComboBox *&box, QSpinBox *&ms) {
		box = new QComboBox(&dlg);
		box->addItem(tl("Transição atual (Fade)"), QString());
		for (const QString &n : transNames)
			box->addItem(n, n);
		box->setCurrentIndex(qMax(0, box->findData(QString::fromUtf8(cur.c_str()))));
		ms = new QSpinBox(&dlg);
		ms->setRange(0, 10000);
		ms->setSingleStep(100);
		ms->setSuffix(" ms");
		ms->setSpecialValueText(tl("duração do programa"));
		ms->setValue(curMs);
		ms->setToolTip(tl("Stinger usa a duração do próprio vídeo"));
		auto *row = new QHBoxLayout();
		row->addWidget(box, 1);
		row->addWidget(ms);
		return row;
	};
	QComboBox *inBox, *outBox;
	QSpinBox *inMs, *outMs;
	fAir->addRow(tl("Entrada"), makeTrans(cfg.inTransition, cfg.inDurationMs, inBox, inMs));
	fAir->addRow(tl("Saída"), makeTrans(cfg.outTransition, cfg.outDurationMs, outBox, outMs));

	auto *evBox = new QComboBox(&dlg);
	evBox->addItem(tl("Corte"), 0);
	evBox->addItem(tl("Fade (mix)"), 1);
	evBox->addItem(tl("Fade para preto"), 2);
	evBox->addItem(tl("Transição do programa (Corte / Fade)"), 3);
	evBox->setCurrentIndex(qMax(0, evBox->findData(cfg.eventTransition)));
	auto *evMs = new QSpinBox(&dlg);
	evMs->setRange(0, 5000);
	evMs->setSingleStep(100);
	evMs->setSuffix(" ms");
	evMs->setValue(cfg.eventDurationMs);
	auto *evMatch = new QCheckBox(tl("Acompanhar a velocidade do replay"), &dlg);
	evMatch->setChecked(cfg.eventMatchSpeed);
	evMatch->setToolTip(tl("Em câmera lenta a transição também fica mais lenta"));
	auto *evRow = new QVBoxLayout();
	auto *evTop = new QHBoxLayout();
	evTop->addWidget(evBox, 1);
	evTop->addWidget(evMs);
	evRow->addLayout(evTop);
	evRow->addWidget(evMatch);
	fTr->addRow(tl("Entre eventos"), evRow);

	auto *stBox = new QComboBox(&dlg);
	stBox->addItem(tl("Nenhuma"), QString());
	for (const QString &n : transNames)
		stBox->addItem(n, n);
	{
		const QString cur = QString::fromUtf8(cfg.stingerSource.c_str());
		int idx = stBox->findData(cur);
		if (idx < 0 && !cur.isEmpty()) {
			stBox->addItem(cur + tl(" (não encontrada)"), cur);
			idx = stBox->count() - 1;
		}
		stBox->setCurrentIndex(idx < 0 ? 0 : idx);
	}
	stBox->setToolTip(tl("Transição usada na virada entre eventos"));
	auto *stCut = new QSpinBox(&dlg);
	stCut->setRange(0, 20000);
	stCut->setSingleStep(100);
	stCut->setSuffix(" ms");
	stCut->setPrefix(tl("começa "));
	stCut->setValue(cfg.stingerCutMs);
	stCut->setToolTip(tl("Quanto tempo antes da troca de evento a transição começa — use o tempo que o gráfico leva para cobrir a tela"));
	auto *stDur = new QSpinBox(&dlg);
	stDur->setRange(100, 20000);
	stDur->setSingleStep(100);
	stDur->setSuffix(" ms");
	stDur->setPrefix(tl("duração "));
	stDur->setValue(cfg.stingerDurationMs);
	stDur->setToolTip(tl("Só vale para transições sem duração própria; stingers usam a duração do gráfico"));
	auto *stRow = new QHBoxLayout();
	stRow->addWidget(stBox, 1);
	stRow->addWidget(stCut);
	stRow->addWidget(stDur);
	fTr->addRow(tl("Transição"), stRow);

	auto *stCover = new QCheckBox(tl("Stinger da troca de cena passa por cima dos overlays e títulos"), &dlg);
	stCover->setChecked(cfg.stingerCoverOverlays);
	stCover->setToolTip(tl("Ligado: o gráfico do stinger cobre os overlays (O1–O8) e os títulos GT durante a transição.\n"
		"Desligado: os overlays e títulos ficam sempre por cima, inclusive do stinger."));
	fTr->addRow(tl("Stinger"), stCover);

	auto *zMark = new QCheckBox(tl("Zoom volta ao normal a cada marcação nova (Mark In / Mark Out / evento)"), &dlg);
	zMark->setChecked(cfg.zoomResetOnMark);
	auto *zEv = new QCheckBox(tl("Zoom volta ao normal ao tocar eventos (Play Events / último evento)"), &dlg);
	zEv->setChecked(cfg.zoomResetOnEvents);
	fTr->addRow(tl("Zoom"), zMark);
	fTr->addRow(QString(), zEv);

	auto *peMode = new QComboBox(&dlg);
	peMode->addItem(tl("Lista inteira"), 0);
	peMode->addItem(tl("A partir do evento selecionado"), 1);
	peMode->addItem(tl("Só o evento selecionado"), 2);
	peMode->setCurrentIndex(qMax(0, peMode->findData(cfg.playEventsMode)));
	fAir->addRow(tl("Play Events toca"), peMode);
	auto *smooth = new QCheckBox(tl("Câmera lenta suave — mistura os quadros vizinhos (menos \"tremido\" em 25–50%)"), &dlg);
	smooth->setChecked(cfg.smoothSlow);
	smooth->setToolTip(tl("Desligado: cada quadro gravado é repetido (imagem mais nítida, movimento em degraus).\n"
					     "Ligado: entre dois quadros mostra a mistura dos dois (movimento contínuo)."));
	fAir->addRow(tl("Câmera lenta"), smooth);

	auto *autoIn = new QCheckBox(tl("⟲ e Play Events trocam para a cena do replay"), &dlg);
	autoIn->setChecked(cfg.autoIn);
	auto *autoOut = new QCheckBox(tl("Voltar para a cena anterior quando o clipe terminar, depois de"), &dlg);
	autoOut->setChecked(cfg.autoOut);
	auto *hold = new QSpinBox(&dlg);
	hold->setRange(0, 10000);
	hold->setSingleStep(250);
	hold->setSuffix(" ms");
	hold->setValue(cfg.outHoldMs);
	auto *autoRow = new QGridLayout();
	autoRow->addWidget(autoIn, 0, 0, 1, 2);
	autoRow->addWidget(autoOut, 1, 0);
	autoRow->addWidget(hold, 1, 1);
	autoRow->setColumnStretch(2, 1);
	fAir->addRow(tl("Automático"), autoRow);

	/* Studio: o controle pelo app (API do vMix) chega na fase 6, no programa todo */

	/* ---- cálculo ao vivo do tempo disponível ---- */
	auto update = [&] {
		int n = 0;
		for (auto *c : cams)
			n += c->currentData().toString().isEmpty() ? 0 : 1;
		const int qv = quality->currentData().toInt();
		RecConfig est;
		est.cq = qv < 0 ? -qv : 0;
		est.bitrateMbps = qv > 0 ? qv : 80;
		const int mbps = (int)(recEstimateMbps(est) + 0.5);
		const QString dir = dirEdit->text();
		QStorageInfo st(dir);
		if (dir.isEmpty() || !QDir(dir).exists() || !st.isValid()) {
			timeLbl->setText(tl("Escolha uma pasta de gravação"));
			timeLbl->setStyleSheet("background-color: #2a2f3a; color: #cbd2de; font-weight: bold; border-radius: 6px;");
			usedLbl->setText("-");
			return;
		}
		if (n == 0) {
			timeLbl->setText(tl("Escolha pelo menos uma câmera"));
			timeLbl->setStyleSheet("background-color: #2a2f3a; color: #cbd2de; font-weight: bold; border-radius: 6px;");
		} else {
			/* vídeo + áudio 4 canais float (768 kB/s) */
			const double bytesPerSec = n * mbps * 1e6 / 8.0 + 768000.0;
			const qint64 sec = (qint64)(st.bytesAvailable() / bytesPerSec);
			timeLbl->setText(tl("%1 de gravação disponível para %2 câmera(s) · ~%3 Mbps cada")
						 .arg(hms(sec))
						 .arg(n)
						 .arg(mbps));
			/* menos de 30 min: vermelho */
			timeLbl->setStyleSheet(sec < 1800 ? "background-color: #7f1d1d; color: #fee2e2; font-weight: bold; border-radius: 6px;"
							  : "background-color: #14532d; color: #dcfce7; font-weight: bold; border-radius: 6px;");
		}
		usedLbl->setText(tl("%1 GB usados nesta pasta de replays")
					 .arg(dirSize(dir) / 1073741824.0, 0, 'f', 2));
	};
	update();
	QObject::connect(dirEdit, &QLineEdit::editingFinished, &dlg, update);
	QObject::connect(quality, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, update);
	for (auto *c : cams)
		QObject::connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, update);
	QObject::connect(browse, &QPushButton::clicked, &dlg, [&] {
		const QString d = QFileDialog::getExistingDirectory(&dlg, tl("Pasta da sessão"), dirEdit->text());
		if (!d.isEmpty()) {
			dirEdit->setText(d);
			update();
		}
	});
	QObject::connect(openDir, &QPushButton::clicked, &dlg, [&] {
		if (QDir(dirEdit->text()).exists())
			QDesktopServices::openUrl(QUrl::fromLocalFile(dirEdit->text()));
	});

	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	if (auto *okB = bb->button(QDialogButtonBox::Ok)) {
		okB->setText(tl("Salvar"));
		ui::setKind(okB, "primary");
	}
	if (auto *cB = bb->button(QDialogButtonBox::Cancel))
		cB->setText(tl("Cancelar"));
	/* idioma: troca na hora (salva o que foi mexido e reabre a janela no idioma novo) */
	auto *bottom = new QHBoxLayout();
	auto *langLbl = new QLabel(QString::fromUtf8("🌐"), &dlg);
	langLbl->setToolTip(tl("Idioma do plugin"));
	auto *langBox = new QComboBox(&dlg);
	langBox->setToolTip(tl("Idioma do plugin"));
	langBox->addItem(tl("Automático (português)"), "auto");
	for (const i18n::Lang &lg : i18n::languages())
		langBox->addItem(QString::fromUtf8(lg.name), QString::fromLatin1(lg.code));
	{
		const int li = langBox->findData(i18n::setting());
		langBox->setCurrentIndex(li < 0 ? 0 : li);
	}
	bottom->addWidget(langLbl);
	bottom->addWidget(langBox);
	bottom->addStretch(1);
	bottom->addWidget(bb);
	dlgV->addLayout(bottom);
	QObject::connect(langBox, &QComboBox::currentIndexChanged, &dlg, [&, langBox, tabs](int) {
		g_pendingLang = langBox->currentData().toString();
		g_configReopenTab = tabs->currentIndex();
		dlg.accept();
	});
	if (g_configStartTab >= 0 && g_configStartTab < tabs->count())
		tabs->setCurrentIndex(g_configStartTab);
	g_configStartTab = -1;
	QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	/* não modal: deixa usar a janela de propriedades do OBS aberta por "Adicionar câmera" */
	dlg.setWindowModality(Qt::NonModal);
	dlg.show();
	{
		QEventLoop loop;
		QObject::connect(&dlg, &QDialog::finished, &loop, &QEventLoop::quit);
		loop.exec();
	}
	const int execResult = dlg.result();
	QSettings(ui::settingsFile(), QSettings::IniFormat).setValue("configSize", dlg.size());
	if (execResult != QDialog::Accepted) {
		g_pendingLang.clear();
		g_configReopenTab = -1;
		return false;
	}

	for (int i = 0; i < MAX_CAMERAS; i++)
		cfg.cameras[i] = cams[i]->currentData().toString().toUtf8().constData();
	cfg.outputDir = dirEdit->text().toUtf8().constData();
	cfg.rec.encoder = enc->currentData().toString().toUtf8().constData();
	{
		const int qv = quality->currentData().toInt();
		if (qv < 0) {
			cfg.rec.cq = -qv;
			/* taxa usada só se o driver não aceitar qualidade constante */
			cfg.rec.bitrateMbps = cfg.rec.cq <= 14 ? 220 : cfg.rec.cq <= 18 ? 144 : 80;
		} else {
			cfg.rec.cq = 0;
			cfg.rec.bitrateMbps = qv;
		}
		cfg.rec.mjpegQuality = cfg.rec.cq > 0 ? (cfg.rec.cq <= 14 ? 2 : cfg.rec.cq <= 18 ? 3 : 4)
						      : cfg.rec.bitrateMbps >= 140 ? 2 : (cfg.rec.bitrateMbps >= 80 ? 3 : 5);
	}
	cfg.smoothSlow = smooth->isChecked();
	cfg.trackA = trA->currentData().toInt();
	cfg.trackB = trB->currentData().toInt();
	cfg.playbackPair = pbAudio->currentData().toInt();
	cfg.replayScene = sceneBox->currentData().toString().toUtf8().constData();
	cfg.inTransition = inBox->currentData().toString().toUtf8().constData();
	cfg.inDurationMs = inMs->value();
	cfg.outTransition = outBox->currentData().toString().toUtf8().constData();
	cfg.outDurationMs = outMs->value();
	cfg.eventTransition = evBox->currentData().toInt();
	cfg.eventDurationMs = evMs->value();
	cfg.eventMatchSpeed = evMatch->isChecked();
	cfg.stingerSource = stBox->currentData().toString().toUtf8().constData();
	cfg.stingerDurationMs = stDur->value();
	cfg.stingerCutMs = stCut->value();
	cfg.stingerCoverOverlays = stCover->isChecked();
	cfg.zoomResetOnMark = zMark->isChecked();
	cfg.zoomResetOnEvents = zEv->isChecked();
	cfg.playEventsMode = peMode->currentData().toInt();
	cfg.autoIn = autoIn->isChecked();
	cfg.autoOut = autoOut->isChecked();
	cfg.outHoldMs = hold->value();
	cfg.cacheLimitGB = limitGb->value();
	cfg.minFreeGB = minFree->value();
	if (recording) {
		const ReplayConfig live = engine().config();
		for (int i = 0; i < MAX_CAMERAS; i++)
			cfg.cameras[i] = live.cameras[i];
		cfg.outputDir = live.outputDir;
		cfg.rec = live.rec;
		cfg.trackA = live.trackA;
		cfg.trackB = live.trackB;
		cfg.playbackPair = live.playbackPair;
	}
	{
		QSettings st(ui::settingsFile(), QSettings::IniFormat);
		for (auto it = partBoxes.begin(); it != partBoxes.end(); ++it)
			st.setValue("parts/" + it.key(), it.value()->isChecked());
		{
			int zp = 0;
			for (auto it = zoomBoxes.begin(); it != zoomBoxes.end(); ++it)
				if (it.value()->isChecked())
					zp |= it.key();
			st.setValue("zoomParts", zp);
			st.setValue("zoomCross", zoomCrossRb->isChecked());
			st.setValue("zoomPlace", zoomPlaceBox->currentData().toInt());
		}
		st.setValue("dockLayout", dockLayoutBox->currentData().toInt());
		st.setValue("groupTitleStyle", titleStyleBox->currentIndex());
		QStringList order;
		for (int r = 0; r < orderList->count(); r++)
			order << orderList->item(r)->data(Qt::UserRole).toString();
		st.setValue("blockOrder", order);
	}
	engine().setConfig(cfg);
	if (!g_pendingLang.isEmpty()) {
		const QString code = g_pendingLang;
		g_pendingLang.clear();
		i18n::setLanguage(code);
	}
	return true;
}

/* ---------------- dock ---------------- */

#define COL_EVENT 0
#define COL_DUR 1
#define COL_SPEED 2
#define COL_CAM0 3
#define COL_COUNT (COL_CAM0 + MAX_CAMERAS)

static const char *kAccent = "QPushButton:checked, QToolButton:checked { background-color: #1f8a70; color: white; "
			     "font-weight: bold; }";

static QPushButton *btn(const QString &text, const QString &tip, QWidget *parent, std::function<void()> fn)
{
	auto *b = new QPushButton(text, parent);
	b->setToolTip(tip);
	b->setMinimumSize(44, 34);
	b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	QObject::connect(b, &QPushButton::clicked, parent, [fn] { fn(); });
	return b;
}

static QPushButton *toggleBtn(const QString &text, const QString &tip, QWidget *parent)
{
	auto *b = new QPushButton(text, parent);
	b->setToolTip(tip);
	b->setCheckable(true);
	b->setMinimumSize(44, 34);
	b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	b->setStyleSheet(kAccent);
	return b;
}

/* hora do relógio no formato HH:MM:SS.cc */
static QString wallTc(const ChannelState &s)
{
	const double ms = (s.pos < 0 ? 0 : s.pos) * 1000.0 / (s.fps > 0 ? s.fps : 30.0);
	const QDateTime t = QDateTime::fromMSecsSinceEpoch(s.wallStartMs + (qint64)ms);
	return t.toString("HH:mm:ss") + QString(".%1").arg((t.time().msec() / 10), 2, 10, QChar('0'));
}

static QString durTc(int64_t frames, double fps)
{
	const double sec = (double)frames / (fps > 0 ? fps : 30.0);
	const int whole = (int)sec;
	return QString("%1:%2:%3.%4")
		.arg(whole / 3600, 2, 10, QChar('0'))
		.arg((whole / 60) % 60, 2, 10, QChar('0'))
		.arg(whole % 60, 2, 10, QChar('0'))
		.arg((int)((sec - whole) * 100 + 0.5) % 100, 2, 10, QChar('0'));
}

/* célula de câmera na lista de eventos: caixinha própria (azul = ângulo no evento) + comentário */
class CamCellDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	static QRect boxRect(const QRect &cell) { return QRect(cell.left() + 8, cell.center().y() - 8, 16, 16); }
	void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
	{
		QStyleOptionViewItem o = opt;
		initStyleOption(&o, idx);
		const bool checked = idx.data(Qt::CheckStateRole).toInt() == Qt::Checked;
		const QString text = idx.data(Qt::DisplayRole).toString();
		p->save();
		if (o.state & QStyle::State_Selected)
			p->fillRect(o.rect, QColor("#1e3a8a"));
		else if (o.features & QStyleOptionViewItem::Alternate)
			p->fillRect(o.rect, QColor("#14171d"));
		p->setRenderHint(QPainter::Antialiasing);
		const QRect b = boxRect(o.rect);
		if (checked) {
			p->setPen(Qt::NoPen);
			p->setBrush(QColor("#3b82f6"));
			p->drawRoundedRect(b, 4, 4);
			p->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			p->drawPolyline(QPolygonF({QPointF(b.left() + 4, b.top() + 8.5), QPointF(b.left() + 7, b.top() + 11.5),
						   QPointF(b.left() + 12, b.top() + 5)}));
		} else {
			p->setPen(QPen(QColor("#3b4250"), 1));
			p->setBrush(QColor("#0f1115"));
			p->drawRoundedRect(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
		}
		if (!text.isEmpty()) {
			p->setPen(QColor(o.state & QStyle::State_Selected ? "#ffffff" : "#e8ebf1"));
			p->drawText(o.rect.adjusted(32, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
				    o.fontMetrics.elidedText(text, Qt::ElideRight, o.rect.width() - 36));
		}
		p->restore();
	}
	bool editorEvent(QEvent *ev, QAbstractItemModel *model, const QStyleOptionViewItem &opt, const QModelIndex &idx) override
	{
		if (ev->type() == QEvent::MouseButtonRelease) {
			auto *me = static_cast<QMouseEvent *>(ev);
			if (boxRect(opt.rect).adjusted(-4, -4, 4, 4).contains(me->position().toPoint())) {
				const bool checked = idx.data(Qt::CheckStateRole).toInt() == Qt::Checked;
				model->setData(idx, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
				return true;
			}
		}
		return QStyledItemDelegate::editorEvent(ev, model, opt, idx);
	}
};

static ReplayDock *g_dock = nullptr;

int64_t replayDockSelectedEvent()
{
	return g_dock ? g_dock->selectedEventId() : 0;
}

void replayDockSelect(int how)
{
	if (!g_dock)
		return;
	const int n = (int)engine().events(engine().activeList()).size();
	if (how == -2)
		g_dock->selectRelative(-n - 1);
	else if (how == 2)
		g_dock->selectRelative(n + 1);
	else
		g_dock->selectRelative(how);
}

ReplayDock::~ReplayDock()
{
	obs_frontend_remove_event_callback(dockFrontendEventCb, this);
	if (g_dock == this)
		g_dock = nullptr;
}

/* grupo de botões que não se separa quando o painel quebra linha */
static QWidget *group(QWidget *parent, std::initializer_list<QWidget *> ws, int spacing = 4)
{
	auto *g = new QWidget(parent);
	g->setProperty("irGroup", true);
	auto *h = new QHBoxLayout(g);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(spacing);
	for (QWidget *w : ws) {
		w->setParent(g);
		h->addWidget(w);
	}
	return g;
}

ReplayDock::ReplayDock(QWidget *parent) : QWidget(parent)
{
	g_dock = this;
	obs_frontend_add_event_callback(dockFrontendEventCb, this);
	ui::apply(this);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(6);

	/* ---- topo: gravar · relógio · estado · A/B · exportar · configurar ---- */
	auto reg = [this](const char *id, QWidget *w) { parts[QString::fromLatin1(id)].push_back(w); };
	auto *top = new QHBoxLayout();
	top->setSpacing(6);
	recBtn = ui::toggle(tl("● GRAVAR"), tl("Inicia/para a gravação contínua das câmeras (sem ela não há replay)"),
			    this, "rec");
	recBtn->setMinimumWidth(92);
	connect(recBtn, &QPushButton::clicked, this, &ReplayDock::toggleRecording);
	top->addWidget(recBtn);
	tcLbl = new QLabel("--:--:--.--", this);
	tcLbl->setProperty("role", "tc");
	modeChip = ui::chip(QString(), this);
	zoomChip = ui::chip(QString(), this);
	zoomChip->setToolTip(tl("Zoom ativo na saída — clique em 1x para voltar à imagem inteira"));
	auto *clockW = group(this, {tcLbl, modeChip, zoomChip}, 6);
	reg("clock", clockW);
	top->addWidget(clockW);
	remainLbl = ui::chip(QString(), this);
	remainLbl->setToolTip(tl("Tempo de gravação que ainda cabe no disco, pelo ritmo atual"));
	remainLbl->hide();
	top->addWidget(remainLbl);
	top->addStretch(1);
	selGroup = new QButtonGroup(this);
	const char *selNames[3] = {"A+B", "A", "B"};
	const char *selTips[3] = {N_("Comanda as saídas A e B ao mesmo tempo"), N_("Comanda só a saída A"),
				  N_("Comanda só a saída B")};
	const int selVals[3] = {SEL_AB, SEL_A, SEL_B};
	QPushButton *selB[3];
	for (int i = 0; i < 3; i++) {
		selB[i] = ui::toggle(selNames[i], tl(selTips[i]), this);
		selB[i]->setMinimumWidth(i == 0 ? 38 : 28);
		selGroup->addButton(selB[i], selVals[i]);
	}
	ui::segment({selB[0], selB[1], selB[2]});
	auto *selW = group(this, {selB[0], selB[1], selB[2]}, 0);
	reg("sel", selW);
	top->addWidget(selW);
	connect(selGroup, &QButtonGroup::idClicked, this, [](int id) {
		engine().setSelection(id);
		if (id == SEL_AB)
			engine().syncBToA();
	});
	auto *exportBtn = new QToolButton(this);
	exportBtn->setIcon(ui::icon("export", ui::MUTED));
	exportBtn->setIconSize(QSize(15, 15));
	exportBtn->setProperty("kind", "ghost");
	exportBtn->setToolTip(tl("Exportar: salva eventos como vídeo (MP4), sem recompressão"));
	exportBtn->setPopupMode(QToolButton::InstantPopup);
	auto *exportMenu = new QMenu(exportBtn);
	exportMenu->addAction(tl("Exportar evento selecionado"), this, [this] {
		if (int64_t id = selectedEventId())
			engine().exportEvents(engine().activeList(), id);
	});
	exportMenu->addAction(tl("Exportar todos os eventos"), this,
			      [] { engine().exportEvents(engine().activeList(), 0); });
	exportMenu->addSeparator();
	exportMenu->addAction(tl("Ver e apagar gravações…"), this, [this] {
		showSessionsDialog(this);
		shownEventsVersion = UINT64_MAX;
	});
	exportMenu->addAction(tl("Limpar todo o cache…"), this, [this] {
		if (clearCacheAsk(this))
			shownEventsVersion = UINT64_MAX;
	});
	exportBtn->setMenu(exportMenu);
	reg("export", exportBtn);
	top->addWidget(exportBtn);
	auto *cfgBtn = ui::button(QString(), tl("Configurações: câmeras, gravação, zoom, painel, transições e app"),
				  this, "ghost", [this] { openConfig(); });
	cfgBtn->setIcon(ui::icon("gear", ui::MUTED));
	cfgBtn->setIconSize(QSize(15, 15));
	cfgBtn->setFixedWidth(30);
	top->addWidget(cfgBtn);
	root->addLayout(top);

	/* barra do modo "organizar" */
	arrangeBar = new QFrame(this);
	arrangeBar->setObjectName("irArrange");
	arrangeBar->setStyleSheet("QFrame#irArrange { background: #172554; border: 1px solid #2563eb; border-radius: 6px; }");
	{
		auto *ah = new QHBoxLayout(arrangeBar);
		ah->setContentsMargins(10, 6, 6, 6);
		auto *al = new QLabel(tl("<b>Organizar:</b> arraste os grupos. <b>Posição livre</b> = solte em qualquer "
							"ponto (não deixa cobrir outro grupo); senão, um ao lado do outro com ↵ quebras de linha."),
				      arrangeBar);
		al->setWordWrap(true);
		ah->addWidget(al, 1);
		freeBtn = ui::toggle(tl("Posição livre"), tl("Ligado: cada grupo fica exatamente onde você soltar"),
				     arrangeBar);
		connect(freeBtn, &QPushButton::toggled, this, [this](bool on) {
			if (on != freeLayoutOn()) {
				setFreeLayout(on);
				brBtn->setVisible(!on);
			}
		});
		ah->addWidget(freeBtn);
		auto *brB = ui::button(tl("+ Quebra de linha"), tl("Começa uma linha nova a partir daqui"),
				       arrangeBar, "", [this] {
					       QStringList order = blockOrderSetting();
					       int n = 1;
					       while (order.contains(QString("br%1").arg(n)))
						       n++;
					       order << QString("br%1").arg(n);
					       QSettings(ui::settingsFile(), QSettings::IniFormat).setValue("blockOrder", order);
					       applyBlockOrder();
					       QTimer::singleShot(30, this, [this] { syncHandles(); });
				       });
		auto *defB = ui::button(tl("Padrão"), tl("Volta à ordem original"), arrangeBar, "", [this] {
			QSettings st(ui::settingsFile(), QSettings::IniFormat);
			st.remove("blockOrder");
			st.remove("freePos");
			st.setValue("freeLayout", false);
			freeBtn->setChecked(false);
			brBtn->setVisible(true);
			for (auto it = blocks.begin(); it != blocks.end(); ++it)
				if (it.key().startsWith("br"))
					it.value()->setVisible(arranging);
			applyBlockOrder();
			QTimer::singleShot(30, this, [this] { syncHandles(); });
		});
		auto *okB = ui::button(tl("Concluir"), QString(), arrangeBar, "primary", [this] { setArranging(false); });
		brBtn = brB;
		ah->addWidget(brB);
		ah->addWidget(defB);
		ah->addWidget(okB);
	}
	arrangeBar->hide();
	root->addWidget(arrangeBar);

	/* ---- lista de eventos ---- */
	table = new QTableWidget(0, COL_COUNT, this);
	table->verticalHeader()->setVisible(false);
	table->verticalHeader()->setDefaultSectionSize(22);
	table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	table->horizontalHeader()->setHighlightSections(false);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setAlternatingRowColors(true);
	table->setShowGrid(false);
	table->setFocusPolicy(Qt::NoFocus);
	table->setMinimumHeight(70);
	for (int c = 0; c < MAX_CAMERAS; c++)
		table->setItemDelegateForColumn(COL_CAM0 + c, new CamCellDelegate(table));
	connect(table, &QTableWidget::itemChanged, this, &ReplayDock::eventItemChanged);
	/* um clique: o replay vai direto para o evento (tocando ou pausado) */
	connect(table, &QTableWidget::cellClicked, this, [this](int r, int) {
		if (!table->item(r, 0))
			return;
		const int64_t id = table->item(r, 0)->data(Qt::UserRole).toLongLong();
		ReplayEngine &e = engine();
		const ChannelState st = e.channel((e.getSelection() & SEL_A) ? 0 : 1).state();
		if (st.playing && !st.liveMode)
			e.playEvent(e.activeList(), id);
		else
			e.cueEvent(e.activeList(), id);
	});
	/* dois cliques em Evento/Duração: põe no ar, toca só ele e volta */
	connect(table, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
		if (c > COL_DUR)
			return; /* velocidade e comentários são editáveis */
		engine().playEventAllAngles(engine().activeList(), table->item(r, 0)->data(Qt::UserRole).toLongLong());
		if (AirControl::instance())
			AirControl::instance()->takeIn();
	});

	/* ---- linha do tempo ---- */
	scrub = new QSlider(Qt::Horizontal, this);
	scrub->setRange(0, 0);
	scrub->setToolTip(tl("Arraste para achar qualquer momento da gravação"));
	connect(scrub, &QSlider::sliderMoved, this, [](int v) { engine().seekTo(v); });
	reg("scrub", scrub);

	/* ---- tudo o mais numa barra que quebra linha sozinha ---- */
	ctrlW = new QWidget(this);
	auto *flow = new ui::FlowLayout(ctrlW, 6, 5);
	flowL = flow;
	auto iconBtn = [this](const char *ic, const QString &tip, bool checkable, const char *kind) {
		auto *b = checkable ? ui::toggle(QString(), tip, this, kind) : ui::button(QString(), tip, this, kind);
		b->setIcon(ui::icon(ic));
		b->setIconSize(QSize(13, 13));
		b->setFixedWidth(32);
		return b;
	};

	auto *prevB = iconBtn("prev", tl("Evento anterior"), false, "");
	connect(prevB, &QPushButton::clicked, this, [this] { selectRelative(-1); });
	auto *nextB = iconBtn("next", tl("Próximo evento"), false, "");
	connect(nextB, &QPushButton::clicked, this, [this] { selectRelative(1); });
	reverseBtn = iconBtn("reverse", tl("Reprodução reversa (liga/desliga)"), true, "");
	connect(reverseBtn, &QPushButton::clicked, this, [](bool on) { engine().setReverse(on); });
	playBtn = iconBtn("play", tl("Play / pausa"), false, "primary");
	playBtn->setFixedWidth(42);
	connect(playBtn, &QPushButton::clicked, this, [] { engine().togglePlay(); });
	auto *ffB = iconBtn("ff", tl("Avanço rápido (2x, 4x…)"), false, "");
	connect(ffB, &QPushButton::clicked, this, [] {
		engine().setReverse(false);
		engine().fastForward();
	});
	reg("nav", prevB);
	reg("nav", nextB);
	reg("reverse", reverseBtn);
	reg("ff", ffB);
	{
		QWidget *blk = wrapBlock("transport", group(ctrlW, {prevB, reverseBtn, playBtn, ffB, nextB}, 3), N_("REPRODUÇÃO"));
		flow->addWidget(blk);
	}

	liveBtn = ui::button(tl("AO VIVO"), tl("Volta as saídas selecionadas para o ponto ao vivo"), this, "",
			     [] { engine().goLive(); });
	reg("live", liveBtn);
	{
		QWidget *blk = wrapBlock("live", group(ctrlW, {liveBtn}), N_("SAÍDA"));
		flow->addWidget(blk);
	}

	inBtn = ui::toggle(tl("IN"), tl("Mark In: marca o início de um evento. No ao vivo marca o ao vivo; com o replay parado ou tocando, marca onde o replay está"), this, "warn");
	connect(inBtn, &QPushButton::clicked, this, [this] {
		engine().markIn();
		refresh();
	});
	auto *outB = ui::button(tl("OUT"), tl("Mark Out: fecha o evento (no ao vivo ou onde o replay está)"), this, "", [this] {
		engine().markOut();
		refresh();
	});
	auto *cancelB = ui::button(tl("✕ IN"), tl("Cancela o IN marcado"), this, "ghost", [this] {
		engine().markCancel();
		refresh();
	});
	reg("mark", inBtn);
	reg("mark", outB);
	reg("cancel", cancelB);
	QPushButton *qb[3];
	int qi = 0;
	for (int sec : {5, 10, 20}) {
		qb[qi] = ui::button(QString::fromUtf8("−%1").arg(sec),
				    tl("Cria um evento com os últimos %1 s e para no início dele").arg(sec), this, "",
				    [sec] { engine().autoEvent(sec); });
		i18n::setTip(qb[qi], N_("Cria um evento com os últimos %1 s e para no início dele"), {QString::number(sec)});
		reg("quick", qb[qi]);
		qi++;
	}
	{
		QWidget *blk = wrapBlock("mark", group(ctrlW, {inBtn, outB, cancelB, qb[0], qb[1], qb[2]}, 3), N_("MARCAR"));
		flow->addWidget(blk);
	}

	std::vector<QPushButton *> spd;
	for (int pct : {25, 33, 50, 75, 100}) {
		auto *b = ui::toggle(QString("%1").arg(pct), tl("Velocidade de reprodução %1%").arg(pct), this);
		i18n::setTip(b, N_("Velocidade de reprodução %1%"), {QString::number(pct)});
		b->setMinimumWidth(34);
		connect(b, &QPushButton::clicked, this, [pct] { engine().setSpeedPercent(pct); });
		speedBtns.push_back({pct, b});
		spd.push_back(b);
	}
	ui::segment({spd[0], spd[1], spd[2], spd[3], spd[4]});
	auto *spdW = group(ctrlW, {spd[0], spd[1], spd[2], spd[3], spd[4]}, 0);
	spdW->setToolTip(tl("Velocidade (%)"));
	reg("speed", spdW);
	{
		QWidget *blk = wrapBlock("speed", spdW, N_("VELOCIDADE"));
		flow->addWidget(blk);
	}

	loopBtn = ui::toggle(tl("Loop"), tl("Repete o clipe (ou a sequência do Play Events) sem parar"), this);
	connect(loopBtn, &QPushButton::clicked, this, [](bool on) { engine().setLoop(on); });
	audioBtn = ui::toggle(QString::fromUtf8("♫"), tl("Liga/desliga o áudio original durante o replay"), this);
	audioBtn->setChecked(true);
	audioBtn->setFixedWidth(32);
	connect(audioBtn, &QPushButton::clicked, this, [](bool on) { engine().setAudio(on); });
	reg("loop", loopBtn);
	reg("audio", audioBtn);
	{
		QWidget *blk = wrapBlock("loopaudio", group(ctrlW, {loopBtn, audioBtn}, 3), N_("REPETIR · ÁUDIO"));
		flow->addWidget(blk);
	}

	/* zoom digital: o layout é escolhido em Configurações → Zoom */
	zoomBox = new QWidget(ctrlW);
	reg("zoom", zoomBox);
	{
		QWidget *blk = wrapBlock("zoom", zoomBox, N_("ZOOM"));
		flow->addWidget(blk);
	}
	buildZoom();

	/* câmeras por saída: "A 1 2 3 4   B 1 2 3 4" juntos */
	auto *camsW = new QWidget(ctrlW);
	auto *camsH = new QHBoxLayout(camsW);
	camsH->setContentsMargins(0, 0, 0, 0);
	camsH->setSpacing(10);
	for (int ch = 0; ch < 2; ch++) {
		auto *lbl = new QLabel(QString("<b>%1</b>").arg(ch == 0 ? "A" : "B"), this);
		lbl->setProperty("role", "muted");
		lbl->setFixedWidth(10);
		auto *pb = ui::button("P", tl("Leva a saída %1 para a Prévia").arg(ch == 0 ? "A" : "B"),
				      this, "ghost", [this, ch] { previewChannel(ch); });
		i18n::setTip(pb, N_("Leva a saída %1 para a Prévia"), {ch == 0 ? "A" : "B"});
		pb->setFixedWidth(22);
		reg("preview", pb);
		auto *gw = new QWidget(camsW);
		auto *gh = new QHBoxLayout(gw);
		gh->setContentsMargins(0, 0, 0, 0);
		gh->setSpacing(3);
		gh->addWidget(lbl);
		gh->addWidget(pb);
		camGroup[ch] = new QButtonGroup(this);
		for (int i = 0; i < MAX_CAMERAS; i++) {
			auto *b = ui::toggle(QString::number(i + 1), QString(), gw);
			b->setFixedWidth(28);
			camBtns[ch][i] = b;
			camGroup[ch]->addButton(b, i);
			gh->addWidget(b);
		}
		connect(camGroup[ch], &QButtonGroup::idClicked, this, [ch](int id) { engine().channel(ch).setAngle(id); });
		reg(ch == 0 ? "camA" : "camB", gw);
		camsH->addWidget(gw);
	}
	camsW->setProperty("irGroup", true);
	{
		QWidget *blk = wrapBlock("cams", camsW, N_("CÂMERAS"));
		flow->addWidget(blk);
	}

	lastBtn = ui::toggle(tl("⟲ Último no ar"),
			     tl("Troca para a cena do replay, toca o último evento e volta.\n"
					       "Clique de novo para encerrar antes do fim."),
			     this, "go");
	eventsBtn = ui::toggle(tl("▶▶ Play Events"),
			       tl("Põe no ar e toca os eventos em sequência (o que tocar é definido na ⚙).\n"
						 "Clique de novo para encerrar antes do fim."),
			       this, "go");
	auto *playSelB = ui::button(tl("▶ Selecionado no ar"),
				    tl("Põe no ar e toca o evento selecionado (todos os ângulos marcados) e volta"), this,
				    "go", [this] {
					    if (int64_t id = selectedEventId()) {
						    engine().playEventAllAngles(engine().activeList(), id);
						    if (AirControl::instance())
							    AirControl::instance()->takeIn();
					    }
				    });
	auto *delB = ui::button(QString::fromUtf8("🗑"), tl("Apaga o evento selecionado"), this, "danger", [this] {
		if (int64_t id = selectedEventId()) {
			if (QMessageBox::question(this, "ReplayDeck", tl("Apagar o evento %1?").arg(id)) ==
			    QMessageBox::Yes)
				engine().deleteEvent(engine().activeList(), id);
		}
	});
	delB->setFixedWidth(32);
	connect(lastBtn, &QPushButton::clicked, this, [] {
		if (AirControl::instance())
			AirControl::instance()->toggleLast();
	});
	connect(eventsBtn, &QPushButton::clicked, this, [this] {
		if (AirControl::instance())
			AirControl::instance()->toggleEvents((qlonglong)selectedEventId());
	});
	reg("last", lastBtn);
	reg("events", eventsBtn);
	reg("playsel", playSelB);
	reg("delete", delB);
	{
		QWidget *blk = wrapBlock("air", group(ctrlW, {lastBtn, eventsBtn, playSelB, delB}, 4), N_("NO AR"));
		flow->addWidget(blk);
	}
	body = new QWidget(this);
	root->addWidget(body, 1);
	ctrlW->installEventFilter(this);
	dropMark = new QFrame(ctrlW);
	dropMark->setStyleSheet("background: #60a5fa; border-radius: 1px;");
	dropMark->hide();

	statusLbl = new QLabel(this);
	statusLbl->setProperty("role", "muted");
	statusLbl->hide();
	root->addWidget(statusLbl);

	/* botão com texto nunca encolhe abaixo do texto (o painel cresce em vez de cortar) */
	for (QPushButton *b : findChildren<QPushButton *>())
		if (!b->text().isEmpty() && b->minimumWidth() < b->maximumWidth()) {
			b->setProperty("irMin0", b->minimumWidth());
			b->setMinimumWidth(b->sizeHint().width());
		}
	i18n::track(this);
	i18n::onChanged(this, [](QWidget *w) { static_cast<ReplayDock *>(w)->retranslated(); });

	applyBlockOrder();
	applyDockLayout();
	applyParts();
	applyCameraLayout();

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &ReplayDock::refresh);
	timer->start(100);
}

/* idioma trocado: os textos já foram trocados; refaz nomes dos grupos e larguras */
void ReplayDock::retranslated()
{
	rewrapBlocks();
	for (QPushButton *b : findChildren<QPushButton *>()) {
		const QVariant m0 = b->property("irMin0");
		if (m0.isValid())
			b->setMinimumWidth(qMax(m0.toInt(), b->sizeHint().width()));
	}
	buildZoom();
	applyBlockOrder();
	applyDockLayout();
	applyParts();
	applyCameraLayout();
	updateCtrlMin();
	shownEventsVersion = UINT64_MAX;
	refresh();
}

QWidget *ReplayDock::wrapBlock(const char *id, QWidget *inner, const char *title)
{
	const QString key = QString::fromLatin1(id);
	blockInner.insert(key, inner);
	blockTitle.insert(key, QString::fromUtf8(title));
	QWidget *blk = titled(ctrlW, inner, title);
	blk->setProperty("irBlock", key);
	blocks.insert(key, blk);
	if (key == "zoom")
		zoomBlk = blk;
	return blk;
}

/* troca o estilo do nome dos grupos sem reiniciar */
void ReplayDock::rewrapBlocks()
{
	for (auto it = blockInner.begin(); it != blockInner.end(); ++it) {
		QWidget *old = blocks.value(it.key());
		QWidget *inner = it.value();
		if (!old)
			continue;
		flowL->removeWidget(old);
		inner->setParent(ctrlW);
		QWidget *blk = titled(ctrlW, inner, blockTitle.value(it.key()).toUtf8().constData());
		blk->setProperty("irBlock", it.key());
		blocks.insert(it.key(), blk);
		if (it.key() == "zoom")
			zoomBlk = blk;
		if (old != inner) {
			old->hide();
			old->deleteLater();
		}
		blk->show();
		inner->show();
	}
	qDeleteAll(handles);
	handles.clear();
}

/* largura em que cada linha (entre as quebras ↵) cabe inteira, sem descer grupos */
int ReplayDock::idealWidth()
{
	int best = 0, cur = 0;
	bool first = true;
	const int hs = 6;
	if (static_cast<ui::FlowLayout *>(flowL)->freeMode()) {
		best = flowL->minimumSize().width();
	} else {
		for (const QString &id : blockOrderSetting()) {
			if (id.startsWith("br")) {
				best = std::max(best, cur);
				cur = 0;
				first = true;
				continue;
			}
			QWidget *w = blocks.value(id);
			if (!w || w->isHidden() || (w == zoomBlk && zoomSide))
				continue;
			cur += (first ? 0 : hs) + w->sizeHint().width();
			first = false;
		}
		best = std::max(best, cur);
		const QMargins fm = flowL->contentsMargins();
		best += fm.left() + fm.right();
	}
	if (zoomSide && zoomBlk && !zoomBlk->isHidden())
		best += 10 + zoomBlk->sizeHint().width();
	if (layout()) {
		const QMargins m = layout()->contentsMargins();
		best += m.left() + m.right();
	}
	return std::max(best, minimumSizeHint().width());
}

void ReplayDock::fitFloating(QDockWidget *dw, bool center)
{
	if (!dw || !dw->isFloating())
		return;
	const int extraW = dw->width() - width();
	const int extraH = dw->height() - height();
	const int w = idealWidth() + std::max(0, extraW);
	updateCtrlMin();
	const int h = std::max(406, minimumSizeHint().height() + std::max(0, extraH) + 60);
	dw->resize(w, h);
	if (center)
		if (QWidget *mw = dw->parentWidget())
			dw->move(mw->geometry().center() - QPoint(w / 2, h / 2));
	dw->raise();
}

/* a altura dos controles acompanha a largura: o painel nunca fica menor do que cabe */
void ReplayDock::updateCtrlMin()
{
	if (!ctrlW || !flowL)
		return;
	const int w = ctrlW->width() > 0 ? ctrlW->width() : flowL->minimumSize().width();
	const int h = flowL->heightForWidth(w);
	if (ctrlW->minimumHeight() != h)
		ctrlW->setMinimumHeight(h);
}

bool ReplayDock::eventFilter(QObject *o, QEvent *e)
{
	if (o == ctrlW && (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest))
		QTimer::singleShot(0, this, [this] { updateCtrlMin(); });
	return QWidget::eventFilter(o, e);
}

void ReplayDock::buildZoom()
{
	/* refaz os controles do zoom no layout escolhido */
	if (QLayout *old = zoomBox->layout()) {
		while (QLayoutItem *it = old->takeAt(0)) {
			if (QWidget *w = it->widget()) {
				w->hide();
				w->deleteLater();
			}
			delete it;
		}
		delete old;
	}
	for (QWidget *w : zoomBox->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
		w->hide();
		w->deleteLater();
	}
	const ZoomCfg zc = zoomCfgSetting();
	zoomSide = zc.side() && zc.parts != 0;
	makeZoomControls(zoomBox, zc, true, &zoomMap, &zoomSlider);
	if (zoomSlider)
		connect(zoomSlider, &QSlider::valueChanged, this, [this](int v) {
			if (zoomSliderSync)
				return;
			const PlaybackChannel::Zoom zm = engine().channel((engine().getSelection() & SEL_A) ? 0 : 1).zoom();
			engine().zoomSet(v / 100.0f, zm.x, zm.y);
		});
	zoomBox->updateGeometry();
}

void ReplayDock::applyBlockOrder()
{
	const QStringList order = blockOrderSetting();
	/* quebras de linha: cria as que faltam, apaga as que saíram */
	for (const QString &id : order)
		if (id.startsWith("br") && !blocks.contains(id)) {
			auto *b = new QLabel(QString(), ctrlW);
			b->setProperty("irBreak", true);
			b->setFixedSize(26, 28);
			b->setVisible(arranging && !QSettings(ui::settingsFile(), QSettings::IniFormat).value("freeLayout", false).toBool());
			blocks.insert(id, b);
		}
	for (const QString &id : blocks.keys())
		if (id.startsWith("br") && !order.contains(id)) {
			QWidget *w = blocks.take(id);
			flowL->removeWidget(w);
			delete w;
			delete handles.take(id);
		}
	for (const QString &id : order)
		if (QWidget *w = blocks.value(id))
			flowL->removeWidget(w);
	for (const QString &id : order)
		if (QWidget *w = blocks.value(id)) {
			if (w == zoomBlk && zoomSide)
				continue; /* fica na coluna à direita (applyDockLayout) */
			if (w->parentWidget() != ctrlW)
				w->setParent(ctrlW);
			flowL->addWidget(w);
		}
	{
		auto *fl = static_cast<ui::FlowLayout *>(flowL);
		QSettings st(ui::settingsFile(), QSettings::IniFormat);
		fl->clearFreePos();
		fl->setFreeMode(st.value("freeLayout", false).toBool());
		for (auto it = blocks.begin(); it != blocks.end(); ++it) {
			const QVariant v = st.value("freePos/" + it.key());
			if (v.isValid())
				fl->setFreePos(it.value(), v.toPoint());
		}
	}
	flowL->invalidate();
}

void ReplayDock::applyDockLayout()
{
	int mode = QSettings(ui::settingsFile(), QSettings::IniFormat).value("dockLayout", 0).toInt();
	if (mode < 0 || mode > 3)
		mode = 0;
	delete body->layout(); /* os widgets continuam (são filhos de body) */
	table->setParent(body);
	scrub->setParent(body);
	ctrlW->setParent(body);
	if (zoomSide && zoomBlk->parentWidget() != body)
		zoomBlk->setParent(body);
	auto ctrlCol = [this]() {
		auto *v = new QVBoxLayout();
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(6);
		v->addWidget(scrub);
		if (zoomSide) {
			auto *h = new QHBoxLayout();
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(10);
			h->addWidget(ctrlW, 1);
			h->addWidget(zoomBlk, 0, Qt::AlignVCenter);
			v->addLayout(h);
		} else {
			v->addWidget(ctrlW);
		}
		return v;
	};
	if (mode == 2) {
		auto *h = new QHBoxLayout(body);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(8);
		h->addWidget(table, 3);
		auto *v = ctrlCol();
		v->addStretch(1);
		h->addLayout(v, 2);
	} else {
		auto *v = new QVBoxLayout(body);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(6);
		if (mode == 1) {
			v->addLayout(ctrlCol());
			v->addWidget(table, 1);
		} else if (mode == 3) {
			v->addLayout(ctrlCol());
			v->addStretch(1);
		} else {
			v->addWidget(table, 1);
			v->addLayout(ctrlCol());
		}
	}
	table->setVisible(mode != 3);
	ctrlW->show();
	scrub->setVisible(partOn("scrub"));
}

void ReplayDock::setArranging(bool on)
{
	arranging = on;
	arrangeBar->setVisible(on);
	for (auto it = blocks.begin(); it != blocks.end(); ++it)
		if (it.key().startsWith("br"))
			it.value()->setVisible(on && !freeLayoutOn());
	freeBtn->setChecked(freeLayoutOn());
	brBtn->setVisible(!freeLayoutOn());
	if (!on) {
		qDeleteAll(handles);
		handles.clear();
		dropMark->hide();
	}
	flowL->invalidate();
	syncHandles();
}

void ReplayDock::syncHandles()
{
	if (!arranging)
		return;
	for (auto it = blocks.begin(); it != blocks.end(); ++it) {
		QWidget *w = it.value();
		BlockHandle *h = handles.value(it.key());
		if (!h) {
			h = new BlockHandle(ctrlW, it.key(), it.key().startsWith("br"));
			h->onMove = [this](BlockHandle *hd, QPoint p) { dropAt(hd, p, false); };
			h->onDrop = [this](BlockHandle *hd, QPoint p) { dropAt(hd, p, true); };
			h->onRemove = [this](BlockHandle *hd) {
				QStringList order = blockOrderSetting();
				order.removeAll(hd->id);
				QSettings(ui::settingsFile(), QSettings::IniFormat).setValue("blockOrder", order);
				QTimer::singleShot(0, this, [this] {
					applyBlockOrder();
					syncHandles();
				});
			};
			handles.insert(it.key(), h);
		}
		if (h->dragging)
			continue;
		const bool vis = !w->isHidden() && w->width() > 0 && w->parentWidget() == ctrlW;
		h->setVisible(vis);
		if (vis) {
			h->setGeometry(w->geometry().adjusted(-2, -2, 2, 2));
			h->raise();
		}
	}
}

bool ReplayDock::freeLayoutOn() const
{
	return static_cast<ui::FlowLayout *>(flowL)->freeMode();
}

void ReplayDock::setFreeLayout(bool on)
{
	QSettings st(ui::settingsFile(), QSettings::IniFormat);
	if (on) {
		/* começa de onde cada grupo está agora: nada pula de lugar */
		const QPoint o = flowL->contentsRect().topLeft();
		for (auto it = blocks.begin(); it != blocks.end(); ++it) {
			QWidget *w = it.value();
			if (it.key().startsWith("br") || w->isHidden() || w->parentWidget() != ctrlW)
				continue;
			if (!st.contains("freePos/" + it.key()))
				st.setValue("freePos/" + it.key(), w->geometry().topLeft() - o);
		}
	}
	st.setValue("freeLayout", on);
	for (auto it = blocks.begin(); it != blocks.end(); ++it)
		if (it.key().startsWith("br"))
			it.value()->setVisible(arranging && !on);
	applyBlockOrder();
	QTimer::singleShot(30, this, [this] { syncHandles(); });
}

/* posição livre: arrasta para qualquer ponto, sem cobrir outro grupo */
void ReplayDock::dropFree(BlockHandle *hd, bool drop)
{
	QWidget *me = blocks.value(hd->id);
	if (!me)
		return;
	const QPoint o = flowL->contentsRect().topLeft();
	const int areaW = flowL->contentsRect().width();
	QRect r(hd->pos() + QPoint(2, 2) - o, me->size());
	std::vector<QRect> others;
	for (auto it = blocks.begin(); it != blocks.end(); ++it) {
		QWidget *w = it.value();
		if (w == me || it.key().startsWith("br") || w->isHidden() || w->parentWidget() != ctrlW)
			continue;
		others.push_back(w->geometry().translated(-o));
	}
	auto clampR = [&](QRect &q) {
		q.moveLeft(std::max(0, std::min(q.left(), areaW - q.width())));
		q.moveTop(std::max(0, q.top()));
	};
	clampR(r);
	/* ímã: encosta e alinha com as bordas dos vizinhos */
	const int gap = 5, mag = 7;
	int bestX = mag, bestY = mag, nx = r.left(), ny = r.top();
	for (const QRect &q : others) {
		const int xs[] = {q.left(), q.right() + 1 + gap, q.left() - gap - r.width(), q.right() + 1 - r.width()};
		for (int cx : xs)
			if (std::abs(cx - r.left()) < bestX) {
				bestX = std::abs(cx - r.left());
				nx = cx;
			}
		const int ys[] = {q.top(), q.bottom() + 1 + gap, q.top() - gap - r.height(), q.bottom() + 1 - r.height()};
		for (int cy : ys)
			if (std::abs(cy - r.top()) < bestY) {
				bestY = std::abs(cy - r.top());
				ny = cy;
			}
	}
	r.moveTo(nx, ny);
	clampR(r);
	auto hits = [&](const QRect &q) {
		for (const QRect &x : others)
			if (x.adjusted(-2, -2, 2, 2).intersects(q))
				return true;
		return false;
	};
	bool bad = hits(r);
	if (!drop) {
		hd->bad = bad;
		hd->update();
		dropMark->setStyleSheet(bad ? "background: transparent; border: 2px dashed #ef4444; border-radius: 5px;"
					    : "background: rgba(96,165,250,0.18); border: 2px solid #60a5fa; border-radius: 5px;");
		dropMark->setGeometry(r.translated(o));
		dropMark->show();
		dropMark->stackUnder(hd);
		return;
	}
	dropMark->hide();
	if (bad) {
		/* caiu em cima de outro: procura o lugar livre mais perto */
		bool found = false;
		for (int rad = 4; rad <= 400 && !found; rad += 4)
			for (int dy = -rad; dy <= rad && !found; dy += 4)
				for (int dx = -rad; dx <= rad && !found; dx += 4) {
					if (std::max(std::abs(dx), std::abs(dy)) != rad)
						continue;
					QRect q = r.translated(dx, dy);
					clampR(q);
					if (!hits(q)) {
						r = q;
						found = true;
					}
				}
		if (!found) {
			syncHandles();
			return;
		}
	}
	QSettings(ui::settingsFile(), QSettings::IniFormat).setValue("freePos/" + hd->id, r.topLeft());
	static_cast<ui::FlowLayout *>(flowL)->setFreePos(me, r.topLeft());
	QTimer::singleShot(30, this, [this] { syncHandles(); });
}

void ReplayDock::dropAt(BlockHandle *hd, QPoint p, bool drop)
{
	if (freeLayoutOn() && !hd->brk) {
		dropFree(hd, drop);
		return;
	}
	dropMark->setStyleSheet("background: #60a5fa; border-radius: 1px;");
	const QStringList order = blockOrderSetting();
	QString target;
	QRect tr;
	int best = INT_MAX;
	for (const QString &id : order) {
		QWidget *w = blocks.value(id);
		if (id == hd->id || !w || w->isHidden() || w->parentWidget() != ctrlW)
			continue;
		const QRect r = w->geometry();
		const int d = r.contains(p) ? 0 : (QPoint(r.center().x(), r.center().y()) - p).manhattanLength();
		if (d < best) {
			best = d;
			target = id;
			tr = r;
		}
	}
	if (target.isEmpty()) {
		dropMark->hide();
		if (drop)
			syncHandles();
		return;
	}
	const bool after = p.x() > tr.center().x();
	if (!drop) {
		dropMark->setGeometry(after ? tr.right() + 2 : tr.left() - 4, tr.top() - 2, 3, tr.height() + 4);
		dropMark->show();
		dropMark->raise();
		return;
	}
	dropMark->hide();
	QStringList n = order;
	n.removeAll(hd->id);
	int idx = n.indexOf(target);
	if (after)
		idx++;
	n.insert(idx, hd->id);
	QSettings(ui::settingsFile(), QSettings::IniFormat).setValue("blockOrder", n);
	applyBlockOrder();
	QTimer::singleShot(30, this, [this] { syncHandles(); });
}

void ReplayDock::applyParts()
{
	QSet<QWidget *> off; /* escondidos pela aba Botões: o grupo não pode reaparecer */
	for (auto it = parts.begin(); it != parts.end(); ++it) {
		const bool on = partOn(it.key().toLatin1().constData());
		for (QWidget *w : it.value()) {
			w->setVisible(on);
			if (!on)
				off.insert(w);
		}
	}
	/* grupo vazio (todos os botões escondidos) não ocupa lugar */
	for (QWidget *g : findChildren<QWidget *>(QString(), Qt::FindChildrenRecursively)) {
		if (!g->property("irGroup").toBool() || off.contains(g))
			continue;
		bool any = false;
		for (QWidget *c : g->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
			any = any || !c->isHidden();
		g->setVisible(any);
	}
	/* caixinhas com nome: somem junto com o grupo de dentro */
	for (QWidget *c : findChildren<QWidget *>(QString(), Qt::FindChildrenRecursively)) {
		if (!c->property("irCard").toBool())
			continue;
		bool any = false;
		for (QWidget *k : c->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
			if (k->property("role").toString() != "cap")
				any = any || !k->isHidden();
		c->setVisible(any);
	}
	if (layout())
		layout()->invalidate();
}

/* Mostra só as câmeras configuradas (ou 4, se ainda não há configuração). */
void ReplayDock::applyCameraLayout()
{
	const ReplayConfig cfg = engine().config();
	bool any = false;
	for (auto &c : cfg.cameras)
		any = any || !c.empty();

	QStringList headers = {tl("Evento"), tl("Duração"), tl("Veloc.")};
	for (int i = 0; i < MAX_CAMERAS; i++) {
		const bool show = any ? !cfg.cameras[i].empty() : i < 4;
		const QString name = QString::fromUtf8(cfg.cameras[i].c_str());
		headers << (name.isEmpty() ? QString::number(i + 1) : QString("%1 %2").arg(i + 1).arg(name));
		table->setColumnHidden(COL_CAM0 + i, !show);
		for (int ch = 0; ch < 2; ch++) {
			camBtns[ch][i]->setVisible(show);
			camBtns[ch][i]->setToolTip(tl("Saída %1: câmera %2%3 (mesmo instante, outro ângulo)")
							   .arg(ch == 0 ? "A" : "B")
							   .arg(i + 1)
							   .arg(name.isEmpty() ? QString() : QString(" — ") + name));
		}
	}
	table->setHorizontalHeaderLabels(headers);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(COL_EVENT, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(COL_DUR, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(COL_SPEED, QHeaderView::ResizeToContents);
}

int64_t ReplayDock::selectedEventId() const
{
	const int r = table->currentRow();
	if (r < 0 || !table->item(r, 0))
		return 0;
	return table->item(r, 0)->data(Qt::UserRole).toLongLong();
}

void ReplayDock::selectRelative(int delta)
{
	const int n = table->rowCount();
	if (!n)
		return;
	int r = table->currentRow() + delta;
	if (r < 0)
		r = 0;
	if (r >= n)
		r = n - 1;
	table->setCurrentCell(r, 0);
	if (int64_t id = selectedEventId())
		engine().cueEvent(engine().activeList(), id);
}

/* P: leva para o Preview do OBS (Modo Estúdio) a cena que contém a saída. */
struct FindOutputCtx {
	int channel;
	bool hit;
};

static bool findOutputItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<FindOutputCtx *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (src && strcmp(obs_source_get_id(src), "instant_replay_output") == 0) {
		obs_data_t *st = obs_source_get_settings(src);
		if ((int)obs_data_get_int(st, "channel") == ctx->channel)
			ctx->hit = true;
		obs_data_release(st);
	}
	return !ctx->hit;
}

void ReplayDock::previewChannel(int ch)
{
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	obs_source_t *found = nullptr;
	for (size_t i = 0; i < scenes.sources.num && !found; i++) {
		FindOutputCtx ctx{ch, false};
		obs_scene_enum_items(obs_scene_from_source(scenes.sources.array[i]), findOutputItem, &ctx);
		if (ctx.hit)
			found = scenes.sources.array[i];
	}
	if (!found)
		tempMsg = tl("Não há entrada Replay %1 (Adicionar entrada → Replay)").arg(ch ? "B" : "A");
	else if (!obs_frontend_preview_program_mode_active())
		tempMsg = tl("Sem Prévia disponível");
	else
		obs_frontend_set_current_preview_scene(found);
	tempMsgTicks = found && tempMsg.isEmpty() ? 0 : 50;
	obs_frontend_source_list_free(&scenes);
}

void ReplayDock::toggleRecording()
{
	ReplayEngine &e = engine();
	if (e.recording()) {
		e.stopRecording();
	} else {
		bool anyCam = false;
		{
			const ReplayConfig c = e.config();
			for (int i = 0; i < MAX_CAMERAS; i++)
				anyCam |= !c.cameras[i].empty();
		}
		if (!anyCam) { /* nada configurado: abre o assistente em vez de dar erro */
			runWizard();
			return;
		}
		const std::string note = e.enforceDiskLimits();
		if (!note.empty())
			showMessage(QString::fromUtf8(note.c_str()));
		std::string err;
		if (!e.startRecording(err))
			QMessageBox::warning(this, "ReplayDeck", tl(err.c_str()));
	}
	refresh();
}

void ReplayDock::openConfig()
{
	const ReplayConfig before = engine().config();
	const int styleBefore = groupTitleStyle();
	g_wantWizard = false;
	g_wantArrange = false;
	if (showConfigDialog(this)) {
		if (groupTitleStyle() != styleBefore)
			rewrapBlocks();
		buildZoom();
		applyBlockOrder();
		if (g_wantArrange) {
			g_wantArrange = false;
			QTimer::singleShot(100, this, [this] { setArranging(true); });
		}
		applyDockLayout();
		applyCameraLayout();
		applyParts();
		shownEventsVersion = UINT64_MAX;
		/* câmera nova e nenhuma cena com o replay: cria a cena Replay sozinho */
		const ReplayConfig now = engine().config();
		bool added = false;
		for (int i = 0; i < MAX_CAMERAS; i++)
			if (!now.cameras[i].empty() && now.cameras[i] != before.cameras[i])
				added = true;
		if (added) {
			const QString sc = setup::ensureReplayScene();
			if (!sc.isEmpty())
				showMessage(tl("Cena \"%1\" criada com o replay (canal A)").arg(sc));
		}
	} else if (g_wantWizard) {
		g_wantWizard = false;
		runWizard();
	}
	if (g_configReopenTab >= 0) {
		g_configStartTab = g_configReopenTab;
		g_configReopenTab = -1;
		QTimer::singleShot(0, this, [this] { openConfig(); });
	}
}

void ReplayDock::runWizard()
{
	const setup::Result r = setup::showWizard(this);
	if (r == setup::Cancelled)
		return;
	applyCameraLayout();
	shownEventsVersion = UINT64_MAX;
	if (r == setup::DoneStartRecording && !engine().recording())
		toggleRecording();
	showMessage(tl("Pronto! Marque IN/OUT ou use ◀◀ para ver o lance"));
	refresh();
}

static void dockFrontendEventCb(enum obs_frontend_event event, void *param)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;
	auto *dock = static_cast<ReplayDock *>(param);
	g_obsLoaded = true;
	/* primeira vez: o OBS deixa painéis novos escondidos; abre o ReplayDeck já montado */
	{
		QSettings st(ui::settingsFile(), QSettings::IniFormat);
		if (!st.value("dockShownOnce", false).toBool()) {
			st.setValue("dockShownOnce", true);
			if (auto *dw = qobject_cast<QDockWidget *>(dock->parentWidget())) {
				/* abre solto, no meio da janela do OBS, na largura em que as linhas cabem.
				 * Repete depois: o OBS reposiciona os painéis logo após carregar. */
				dw->setFloating(true);
				dw->show();
				for (int ms : {0, 400, 1200, 2500})
					QTimer::singleShot(ms, dock, [dock, dw] { dock->fitFloating(dw, true); });
			}
		}
	}
	/* o painel já estava aberto quando o OBS abriu: oferece agora */
	if (dock->isVisible())
		dock->maybeFirstRun();
}

/* primeira vez que o painel ReplayDeck é aberto (menu Painéis): configuração rápida.
 * Só uma vez; depois ela fica em ⚙ → Câmeras e no GRAVAR sem câmera. */
void ReplayDock::maybeFirstRun()
{
	if (firstRunAsked || !g_obsLoaded || !setup::needsSetup())
		return;
	firstRunAsked = true;
	QTimer::singleShot(500, this, [this] {
		if (isVisible() && setup::needsSetup())
			runWizard();
	});
}

void ReplayDock::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	maybeFirstRun();
}

void ReplayDock::refresh()
{
	if (!engineAlive())
		return;
	if (arranging)
		syncHandles();
	updateCtrlMin();
	ReplayEngine &e = engine();
	const bool rec = e.recording();
	recBtn->setChecked(rec);

	const ChannelState st[2] = {e.channel(0).state(), e.channel(1).state()};
	const int sel = e.getSelection();
	const ChannelState &ref = st[(sel & SEL_A) ? 0 : 1];
	const int pct = e.speedPercent();

	if (ref.hasSession) {
		tcLbl->setText(wallTc(ref));
		const int spdPct = (int)(ref.speed * 100 + 0.5);
		if (ref.liveMode)
			ui::setChip(modeChip, tl("AO VIVO"), "live");
		else if (ref.playing && ref.dir < 0)
			ui::setChip(modeChip, QString::fromUtf8("◀ %1%").arg(spdPct), "info");
		else if (ref.playing && ref.mult > 1)
			ui::setChip(modeChip, QString::fromUtf8("▶▶ %1x").arg((int)ref.mult), "info");
		else if (ref.playing)
			ui::setChip(modeChip, QString::fromUtf8("▶ %1%").arg(spdPct), "ok");
		else
			ui::setChip(modeChip, tl("❚❚ PAUSA"), "");

		if (!scrub->isSliderDown()) {
			const int live = ref.live < 0 ? 0 : (int)ref.live;
			scrub->blockSignals(true);
			scrub->setRange(0, live);
			scrub->setValue((int)ref.pos);
			scrub->blockSignals(false);
		}
	} else {
		tcLbl->setText("--:--:--.--");
		ui::setChip(modeChip, QString(), "");
	}
	recBtn->setText(rec ? QString::fromUtf8("● %1").arg(durTc(ref.recLive, ref.recFps).left(8))
			    : tl("● GRAVAR"));
	liveBtn->setEnabled(ref.hasSession);
	{
		const PlaybackChannel::Zoom zm = e.channel((sel & SEL_A) ? 0 : 1).zoom();
		ui::setChip(zoomChip, zm.z > 1.01f ? tl("ZOOM %1x").arg(zm.z, 0, 'f', 1) : QString(), "info");
		if (zoomMap && (zoomMap->z.z != zm.z || zoomMap->z.x != zm.x || zoomMap->z.y != zm.y)) {
			zoomMap->z = zm;
			zoomMap->update();
		}
		if (zoomSlider && !zoomSlider->isSliderDown()) {
			zoomSliderSync = true;
			zoomSlider->setValue((int)std::lround(zm.z * 100));
			zoomSliderSync = false;
		}
	}
	inBtn->setChecked(e.hasPendingIn());

	/* contador regressivo (atualiza 1x por segundo) */
	/* limite de disco: confere a cada 30 s enquanto grava */
	if (rec && ++diskTick >= 300) {
		diskTick = 0;
		const std::string note = e.enforceDiskLimits();
		if (!note.empty()) {
			showMessage(QString::fromUtf8(note.c_str()));
			if (!e.recording())
				QMessageBox::warning(this, "ReplayDeck", tl(note.c_str()));
		}
	}
	if (++remainTick >= 10 || (!rec && remainLbl->isVisible())) {
		remainTick = 0;
		uint64_t bytes = 0;
		double secs = 0;
		if (rec && e.recordingStats(bytes, secs)) {
			const ReplayConfig cfg = e.config();
			int n = 0;
			for (auto &c : cfg.cameras)
				n += c.empty() ? 0 : 1;
			/* ritmo real depois de 5 s; antes disso, a estimativa da qualidade */
			const double rate = secs >= 5 && bytes > 0 ? (double)bytes / secs
								   : n * recEstimateMbps(cfg.rec) * 1e6 / 8.0 + 768000.0;
			QStorageInfo st(QString::fromUtf8(cfg.outputDir.c_str()));
			if (st.isValid() && rate > 0) {
				const qint64 left = (qint64)(st.bytesAvailable() / rate);
				ui::setChip(remainLbl, tl("RESTAM %1").arg(hms(left)),
					    left < 600 ? "live" : left < 1800 ? "warn" : "");
			}
		} else {
			ui::setChip(remainLbl, QString(), "");
		}
	}

	QString status;
	if (tempMsgTicks > 0) {
		tempMsgTicks--;
		status = tempMsg;
	} else {
		tempMsg.clear();
		QStringList bits;
		const std::string ex = e.exportStatus();
		if (!ex.empty() && (e.exporting() || !rec))
			bits << QString::fromUtf8(ex.c_str());
		if (const uint64_t dropped = e.droppedFrames())
			bits << tl("%1 quadros perdidos").arg(dropped);
		if (e.hasPendingIn())
			bits << tl("IN marcado: aperte OUT para fechar o evento");
		if (!ref.hasSession && !rec)
			bits << tl("⚙ para configurar, ● para gravar");
		status = bits.join(QString::fromUtf8("  •  "));
	}
	statusLbl->setVisible(!status.isEmpty() && partOn("status"));
	statusLbl->setText(status);

	{
		const bool showPause = ref.playing && !ref.liveMode;
		if (playBtn->property("isPause").toBool() != showPause) {
			playBtn->setProperty("isPause", showPause);
			playBtn->setIcon(ui::icon(showPause ? "pause" : "play", "#ffffff"));
		}
	}


	/* reflete mudanças feitas por atalho */
	if (auto *sb = selGroup->button(sel))
		sb->setChecked(true);
	for (int ch = 0; ch < 2; ch++)
		if (auto *cb = camGroup[ch]->button(st[ch].angle))
			cb->setChecked(true);
	for (auto &sbn : speedBtns)
		sbn.second->setChecked(sbn.first == pct);
	loopBtn->setChecked(e.loopOn());
	reverseBtn->setChecked(e.reverseOn());
	const int runKind = AirControl::instance() ? AirControl::instance()->runKind() : 0;
	lastBtn->setChecked(runKind == 1);
	eventsBtn->setChecked(runKind == 2);
	audioBtn->setChecked(e.audioOn());
	if (e.eventsVersion() != shownEventsVersion && !table->viewport()->findChild<QLineEdit *>())
		rebuildEvents();
}

void ReplayDock::rebuildEvents()
{
	ReplayEngine &e = engine();
	shownEventsVersion = e.eventsVersion();
	const auto evs = e.events(e.activeList());
	const double fps = e.channel(0).state().fps;
	const int64_t keepId = selectedEventId();
	bool kept = false;

	rebuilding = true;
	table->setRowCount((int)evs.size());
	for (int r = 0; r < (int)evs.size(); r++) {
		const ReplayEvent &ev = evs[(size_t)r];
		auto put = [&](int col, const QString &text, bool editable) {
			auto *it = new QTableWidgetItem(text);
			if (!editable)
				it->setFlags(it->flags() & ~Qt::ItemIsEditable);
			it->setData(Qt::UserRole, (qlonglong)ev.id);
			table->setItem(r, col, it);
			return it;
		};
		put(COL_EVENT, QString("%1").arg(ev.id, 4, 10, QChar('0')), false);
		put(COL_DUR, durTc(ev.out - ev.in, fps), false)
			->setToolTip(tl("In %1 — Out %2").arg(tc((double)ev.in, fps)).arg(tc((double)ev.out, fps)));
		put(COL_SPEED, ev.speedPct > 0 ? QString("%1%").arg(ev.speedPct) : QString("-"), true)
			->setToolTip(tl("Velocidade própria deste evento (ex.: 50). \"-\" usa a velocidade atual"));
		for (int c = 0; c < MAX_CAMERAS; c++) {
			auto *it = put(COL_CAM0 + c, QString::fromUtf8(ev.comments[c].c_str()), true);
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState((ev.angleMask & (1u << c)) ? Qt::Checked : Qt::Unchecked);
			it->setToolTip(tl("Marque os ângulos deste evento; dê dois cliques para escrever um comentário"));
		}
		if (ev.id == keepId) {
			table->setCurrentCell(r, 0);
			kept = true;
		}
	}
	if (!kept && !evs.empty()) {
		table->setCurrentCell((int)evs.size() - 1, 0);
		table->scrollToBottom();
	}
	rebuilding = false;
}

void ReplayDock::eventItemChanged(QTableWidgetItem *item)
{
	if (rebuilding)
		return;
	ReplayEngine &e = engine();
	const int64_t id = item->data(Qt::UserRole).toLongLong();
	const int col = item->column();
	if (col >= COL_CAM0) {
		const int cam = col - COL_CAM0;
		QString text = item->text().trimmed();
		if (text == "-")
			text.clear();
		e.setEventAngleChecked(e.activeList(), id, cam, item->checkState() == Qt::Checked);
		e.setEventAngleComment(e.activeList(), id, cam, text.toUtf8().constData());
	} else if (col == COL_SPEED) {
		QString text = item->text();
		text.remove('%');
		e.setEventSpeed(e.activeList(), id, text.trimmed().toInt());
	}
}
