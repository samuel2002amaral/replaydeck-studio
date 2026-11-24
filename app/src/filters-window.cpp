#include "filters-window.hpp"
#include "properties-view.hpp"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

void FiltersWindow::open(obs_source_t *source, const QString &title, QWidget *parent)
{
	static QMap<obs_source_t *, QPointer<FiltersWindow>> wins;
	QPointer<FiltersWindow> &w = wins[source];
	if (!w) {
		w = new FiltersWindow(source, title, parent);
		w->setAttribute(Qt::WA_DeleteOnClose);
	}
	w->show();
	w->raise();
	w->activateWindow();
}

FiltersWindow::FiltersWindow(obs_source_t *source, const QString &title, QWidget *parent)
	: QWidget(parent, Qt::Window), weak(obs_source_get_weak_source(source))
{
	setWindowTitle("Filtros · " + title);
	resize(860, 560);
	auto *root = new QHBoxLayout(this);
	auto *split = new QSplitter;
	root->addWidget(split);

	auto *left = new QWidget;
	auto *lv = new QVBoxLayout(left);
	lv->setContentsMargins(0, 0, 0, 0);
	list = new QListWidget;
	list->setToolTip("Marque/desmarque para ligar/desligar. A ordem conta: o de cima age primeiro.");
	lv->addWidget(list, 1);
	auto *row = new QHBoxLayout;
	auto *add = new QPushButton("＋ Filtro");
	add->setObjectName("primaryButton");
	auto *del = new QPushButton("Remover");
	auto *up = new QToolButton;
	up->setText("↑");
	auto *down = new QToolButton;
	down->setText("↓");
	auto *ren = new QPushButton("Renomear");
	row->addWidget(add);
	row->addWidget(del);
	row->addWidget(ren);
	row->addStretch(1);
	row->addWidget(up);
	row->addWidget(down);
	lv->addLayout(row);
	split->addWidget(left);

	auto *right = new QWidget;
	propsBox = new QVBoxLayout(right);
	propsBox->setContentsMargins(0, 0, 0, 0);
	split->addWidget(right);
	split->setStretchFactor(1, 1);
	split->setSizes({260, 600});

	connect(add, &QPushButton::clicked, this, &FiltersWindow::addMenu);
	connect(del, &QPushButton::clicked, this, [this] {
		OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
		obs_source_t *f = current();
		if (src && f &&
		    QMessageBox::question(this, "Filtros", QString("Remover o filtro \"%1\"?").arg(obs_source_get_name(f))) ==
			    QMessageBox::Yes) {
			obs_source_filter_remove(src, f);
			refresh();
		}
	});
	connect(ren, &QPushButton::clicked, this, [this] {
		obs_source_t *f = current();
		if (!f)
			return;
		bool ok = false;
		const QString n = QInputDialog::getText(this, "Renomear filtro", "Nome:", QLineEdit::Normal,
							QString::fromUtf8(obs_source_get_name(f)), &ok);
		if (ok && !n.trimmed().isEmpty()) {
			obs_source_set_name(f, n.trimmed().toUtf8().constData());
			refresh(f);
		}
	});
	connect(up, &QToolButton::clicked, this, [this] { move(OBS_ORDER_MOVE_UP); });
	connect(down, &QToolButton::clicked, this, [this] { move(OBS_ORDER_MOVE_DOWN); });
	connect(list, &QListWidget::currentRowChanged, this, [this] { showFilter(current()); });
	connect(list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
		auto *f = static_cast<obs_source_t *>(it->data(Qt::UserRole).value<void *>());
		if (f)
			obs_source_set_enabled(f, it->checkState() == Qt::Checked);
	});
	refresh();
}

obs_source_t *FiltersWindow::current() const
{
	QListWidgetItem *it = list->currentItem();
	return it ? static_cast<obs_source_t *>(it->data(Qt::UserRole).value<void *>()) : nullptr;
}

void FiltersWindow::refresh(obs_source_t *select)
{
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	if (!select)
		select = current();
	const QSignalBlocker b(list);
	list->clear();
	if (!src)
		return;
	struct Ctx {
		QListWidget *list;
		obs_source_t *select;
	} ctx{list, select};
	obs_source_enum_filters(
		src,
		[](obs_source_t *, obs_source_t *f, void *p) {
			auto *c = static_cast<Ctx *>(p);
			const bool audio = !(obs_source_get_output_flags(f) & OBS_SOURCE_VIDEO);
			auto *it = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(f)) +
							       (audio ? QString::fromUtf8("  ♪") : QString()));
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(obs_source_enabled(f) ? Qt::Checked : Qt::Unchecked);
			it->setData(Qt::UserRole, QVariant::fromValue((void *)f));
			it->setToolTip(QString::fromUtf8(obs_source_get_display_name(obs_source_get_id(f))));
			c->list->addItem(it); /* mesma ordem do OBS */
			if (f == c->select)
				c->list->setCurrentItem(it);
		},
		&ctx);
	if (!list->currentItem() && list->count())
		list->setCurrentRow(0);
	showFilter(current());
}

void FiltersWindow::showFilter(obs_source_t *f)
{
	if (props) {
		props->deleteLater();
		props = nullptr;
	}
	while (QLayoutItem *it = propsBox->takeAt(0)) {
		delete it->widget();
		delete it;
	}
	if (!f) {
		auto *empty = new QLabel("Sem filtros. ＋ Filtro para adicionar:\n"
					 "imagem: chroma key, correção de cor, LUT, recorte, nitidez...\n"
					 "som: supressão de ruído, compressor, ganho, gate...");
		empty->setObjectName("muted");
		empty->setAlignment(Qt::AlignCenter);
		propsBox->addWidget(empty, 1);
		return;
	}
	props = new PropertiesView(f, obs_source_get_id(f), this);
	OBSWeakSourceAutoRelease wf = obs_source_get_weak_source(f);
	/* ajuste ao vivo: cada mudança já vale na entrada */
	connect(props, &PropertiesView::changed, this, [this, w = OBSWeakSource(wf.Get())] {
		OBSSourceAutoRelease ff = obs_weak_source_get_source(w);
		if (ff && props)
			props->applyToSource(ff);
	});
	propsBox->addWidget(props, 1);
}

void FiltersWindow::addMenu()
{
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	if (!src)
		return;
	const uint32_t caps = obs_source_get_output_flags(src);
	QMenu menu(this);
	QMenu *video = menu.addMenu("Imagem");
	QMenu *audio = menu.addMenu("Som");
	const char *id = nullptr;
	QMap<QString, QPair<QString, bool>> items; /* rótulo -> (id, áudio) */
	for (size_t i = 0; obs_enum_filter_types(i, &id); i++) {
		const uint32_t fl = obs_get_source_output_flags(id);
		if (fl & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_CAP_OBSOLETE))
			continue;
		const bool isAudio = (fl & OBS_SOURCE_AUDIO) && !(fl & OBS_SOURCE_VIDEO);
		const bool isVideo = fl & OBS_SOURCE_VIDEO;
		if ((isAudio && !(caps & OBS_SOURCE_AUDIO)) || (isVideo && !(caps & OBS_SOURCE_VIDEO)))
			continue; /* filtro de som numa imagem (e o contrário) */
		const char *dn = obs_source_get_display_name(id);
		if (!dn)
			continue;
		items.insert(QString::fromUtf8(dn), qMakePair(QString::fromUtf8(id), isAudio));
	}
	for (auto it = items.begin(); it != items.end(); ++it) {
		QMenu *m = it.value().second ? audio : video;
		const QString type = it.value().first, label = it.key();
		m->addAction(label, this, [this, type, label] {
			OBSSourceAutoRelease s = obs_weak_source_get_source(weak);
			if (!s)
				return;
			/* nome único entre os filtros desta entrada */
			QString name = label;
			for (int n = 2; OBSSourceAutoRelease(obs_source_get_filter_by_name(s, name.toUtf8().constData())); n++)
				name = QString("%1 %2").arg(label).arg(n);
			OBSSourceAutoRelease f = obs_source_create(type.toUtf8().constData(), name.toUtf8().constData(),
								    nullptr, nullptr);
			if (!f) {
				QMessageBox::warning(this, "Filtros", "Não foi possível criar o filtro.");
				return;
			}
			obs_source_filter_add(s, f);
			refresh(f);
		});
	}
	video->setEnabled(!video->isEmpty());
	audio->setEnabled(!audio->isEmpty());
	menu.exec(QCursor::pos());
}

void FiltersWindow::move(obs_order_movement m)
{
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	obs_source_t *f = current();
	if (!src || !f)
		return;
	obs_source_filter_set_order(src, f, m);
	refresh(f);
}
