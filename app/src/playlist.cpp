#include "playlist.hpp"
#include "engine.hpp"

#include <util/base.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

Playlist &Playlist::get()
{
	static Playlist p;
	return p;
}

static QString file()
{
	return QDir(Engine::get().configDir()).filePath("playlist.json");
}

void Playlist::load()
{
	items.clear();
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(file().toUtf8().constData(), "bak");
	if (!d)
		return;
	loop = obs_data_get_bool(d, "repetir");
	transitionId = obs_data_has_user_value(d, "transicao") ? (int)obs_data_get_int(d, "transicao") : -1;
	OBSDataArrayAutoRelease arr = obs_data_get_array(d, "itens");
	for (size_t i = 0, n = obs_data_array_count(arr); i < n; i++) {
		OBSDataAutoRelease o = obs_data_array_item(arr, i);
		items << PlaylistItem{(int)obs_data_get_int(o, "entrada"), (int)obs_data_get_int(o, "segundos")};
	}
}

void Playlist::save()
{
	OBSDataAutoRelease d = obs_data_create();
	obs_data_set_bool(d, "repetir", loop);
	obs_data_set_int(d, "transicao", transitionId);
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const PlaylistItem &it : items) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_int(o, "entrada", it.inputId);
		obs_data_set_int(o, "segundos", it.seconds);
		obs_data_array_push_back(arr, o);
	}
	obs_data_set_array(d, "itens", arr);
	obs_data_save_json_safe(d, file().toUtf8().constData(), "tmp", "bak");
}

static bool isMedia(obs_source_t *s)
{
	return s && (obs_source_get_output_flags(s) & OBS_SOURCE_CONTROLLABLE_MEDIA);
}

bool Playlist::start()
{
	stop();
	/* só os itens cuja entrada ainda existe */
	bool any = false;
	for (const PlaylistItem &it : items)
		any = any || Engine::get().input(it.inputId);
	if (!any)
		return false;
	running = true;
	timer.setSingleShot(true);
	disconnect(&timer, nullptr, this, nullptr);
	connect(&timer, &QTimer::timeout, this, &Playlist::next);
	blog(LOG_INFO, "playlist começou (%d itens)", (int)items.size());
	play(0);
	return true;
}

void Playlist::stop()
{
	timer.stop();
	watch(nullptr);
	if (running)
		blog(LOG_INFO, "playlist parou");
	running = false;
	idx = -1;
	emit stateChanged();
}

void Playlist::next()
{
	if (running)
		play(idx + 1);
}

void Playlist::play(int i)
{
	Engine &e = Engine::get();
	/* pula itens cuja entrada foi removida */
	for (int tries = 0; tries <= items.size(); tries++) {
		if (i >= items.size()) {
			if (!loop) {
				stop();
				return;
			}
			i = 0;
		}
		if (e.input(items[i].inputId))
			break;
		i++;
	}
	Input *in = e.input(items.value(i).inputId);
	if (!in) {
		stop();
		return;
	}
	idx = i;
	const PlaylistItem it = items[i];
	obs_source_t *tr = e.fadeTransition();
	int ms = e.transitionMs();
	if (transitionId == 0) {
		tr = e.cutTransition();
	} else if (transitionId > 0) {
		if (StudioTransition *t = e.userTransition(transitionId)) {
			tr = t->source;
			ms = t->durationMs;
		}
	}
	const bool media = isMedia(in->source);
	if (media)
		obs_source_media_restart(in->source); /* começa do início */
	e.takeToProgram(in->id, tr, ms);
	timer.stop();
	watch(nullptr);
	if (media && it.seconds <= 0) {
		watch(in->source); /* vídeo: vai até o fim */
	} else {
		timer.start(std::max(1, it.seconds > 0 ? it.seconds : 10) * 1000);
	}
	emit stateChanged();
}

void Playlist::watch(obs_source_t *src)
{
	if (OBSSourceAutoRelease old = obs_weak_source_get_source(watched))
		signal_handler_disconnect(obs_source_get_signal_handler(old), "media_ended", mediaEnded, this);
	watched = src ? obs_source_get_weak_source(src) : nullptr;
	if (src)
		signal_handler_connect(obs_source_get_signal_handler(src), "media_ended", mediaEnded, this);
}

void Playlist::mediaEnded(void *data, calldata_t *)
{
	/* vem da thread do vídeo: o próximo item é na thread da interface */
	auto *self = static_cast<Playlist *>(data);
	QMetaObject::invokeMethod(self, [self] { self->next(); }, Qt::QueuedConnection);
}

/* ---------- aba Lista ---------- */

PlaylistPanel::PlaylistPanel(QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(8, 8, 8, 8);
	auto *top = new QHBoxLayout;
	go = new QPushButton("▶ Iniciar");
	go->setObjectName("greenButton");
	auto *nextBtn = new QPushButton("⏭ Próximo");
	auto *add = new QPushButton("＋ Entrada");
	auto *del = new QPushButton("Remover");
	auto *up = new QToolButton;
	up->setText("↑");
	auto *down = new QToolButton;
	down->setText("↓");
	top->addWidget(go);
	top->addWidget(nextBtn);
	top->addStretch(1);
	top->addWidget(add);
	top->addWidget(del);
	top->addWidget(up);
	top->addWidget(down);
	v->addLayout(top);

	table = new QTableWidget(0, 2);
	table->setHorizontalHeaderLabels({"Entrada", "Tempo"});
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
	table->setColumnWidth(1, 150);
	table->verticalHeader()->setDefaultSectionSize(30);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	v->addWidget(table, 1);

	auto *bottom = new QHBoxLayout;
	loop = new QCheckBox("Repetir a lista");
	bottom->addWidget(loop);
	bottom->addWidget(new QLabel("Transição:"));
	trans = new QComboBox;
	bottom->addWidget(trans);
	state = new QLabel;
	state->setObjectName("muted");
	bottom->addWidget(state, 1);
	v->addLayout(bottom);

	Playlist &p = Playlist::get();
	connect(go, &QPushButton::clicked, this, [this] {
		Playlist &pl = Playlist::get();
		if (pl.active())
			pl.stop();
		else {
			pull();
			pl.start();
		}
	});
	connect(nextBtn, &QPushButton::clicked, this, [] { Playlist::get().next(); });
	connect(add, &QPushButton::clicked, this, [this] {
		pull();
		Engine &e = Engine::get();
		const int pv = e.previewId() ? e.previewId() : (e.inputs().isEmpty() ? 0 : e.inputs().first()->id);
		Playlist::get().items << PlaylistItem{pv, 0};
		Playlist::get().save();
		rebuild();
	});
	connect(del, &QPushButton::clicked, this, [this] {
		pull();
		const int r = table->currentRow();
		if (r >= 0 && r < Playlist::get().items.size()) {
			Playlist::get().items.removeAt(r);
			Playlist::get().save();
			rebuild();
		}
	});
	auto moveRow = [this](int d) {
		pull();
		auto &items = Playlist::get().items;
		const int r = table->currentRow(), t = r + d;
		if (r < 0 || t < 0 || t >= items.size())
			return;
		items.swapItemsAt(r, t);
		Playlist::get().save();
		rebuild();
		table->selectRow(t);
	};
	connect(up, &QToolButton::clicked, this, [moveRow] { moveRow(-1); });
	connect(down, &QToolButton::clicked, this, [moveRow] { moveRow(+1); });
	connect(loop, &QCheckBox::toggled, this, [](bool on) {
		Playlist::get().loop = on;
		Playlist::get().save();
	});
	connect(trans, &QComboBox::currentIndexChanged, this, [this] {
		if (filling)
			return;
		Playlist::get().transitionId = trans->currentData().toInt();
		Playlist::get().save();
	});
	connect(&p, &Playlist::stateChanged, this, [this] {
		const Playlist &pl = Playlist::get();
		go->setText(pl.active() ? "■ Parar" : "▶ Iniciar");
		state->setText(pl.active() ? QString("tocando o item %1 de %2").arg(pl.currentIndex() + 1).arg(pl.items.size())
					   : QString());
		for (int r = 0; r < table->rowCount(); r++)
			if (QWidget *w = table->cellWidget(r, 0))
				w->setStyleSheet(r == pl.currentIndex() ? "font-weight:700;color:#e05a4f;" : "");
	});
	connect(&Engine::get(), &Engine::inputsChanged, this, &PlaylistPanel::rebuild, Qt::QueuedConnection);
	connect(&Engine::get(), &Engine::transitionsChanged, this, &PlaylistPanel::rebuild, Qt::QueuedConnection);
	rebuild();
}

void PlaylistPanel::pull()
{
	auto &items = Playlist::get().items;
	for (int r = 0; r < table->rowCount() && r < items.size(); r++) {
		if (auto *cb = qobject_cast<QComboBox *>(table->cellWidget(r, 0)))
			items[r].inputId = cb->currentData().toInt();
		if (auto *sb = qobject_cast<QSpinBox *>(table->cellWidget(r, 1)))
			items[r].seconds = sb->value();
	}
}

void PlaylistPanel::rebuild()
{
	filling = true;
	Engine &e = Engine::get();
	Playlist &p = Playlist::get();
	table->setRowCount(0);
	for (int r = 0; r < p.items.size(); r++) {
		table->insertRow(r);
		auto *cb = new QComboBox;
		for (Input *in : e.inputs())
			cb->addItem(QString("%1  %2").arg(e.inputs().indexOf(in) + 1).arg(in->name), in->id);
		const int at = cb->findData(p.items[r].inputId);
		if (at >= 0)
			cb->setCurrentIndex(at);
		else
			cb->insertItem(0, "(entrada removida)", p.items[r].inputId), cb->setCurrentIndex(0);
		connect(cb, &QComboBox::currentIndexChanged, this, [this] {
			if (!filling) {
				pull();
				Playlist::get().save();
			}
		});
		table->setCellWidget(r, 0, cb);
		auto *sb = new QSpinBox;
		sb->setRange(0, 3600);
		sb->setSuffix(" s");
		sb->setSpecialValueText("até o fim");
		sb->setToolTip("0 = vídeo até o fim (imagem/câmera: 10 s)");
		sb->setValue(p.items[r].seconds);
		connect(sb, &QSpinBox::valueChanged, this, [this] {
			if (!filling) {
				pull();
				Playlist::get().save();
			}
		});
		table->setCellWidget(r, 1, sb);
	}
	trans->clear();
	trans->addItem("Fade", -1);
	trans->addItem("Corte", 0);
	for (StudioTransition *t : e.userTransitions())
		trans->addItem(t->name, t->id);
	const int ti = trans->findData(p.transitionId);
	trans->setCurrentIndex(ti < 0 ? 0 : ti);
	loop->setChecked(p.loop);
	filling = false;
	emit p.stateChanged();
}
