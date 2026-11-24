#include "main-window.hpp"
#include "replay-host.hpp"
#include "flow-layout.hpp"
#include "audio-mixer.hpp"
#include "external-output.hpp"
#include "outputs.hpp"
#include "settings-dialog.hpp"
#include <QMenu>
#include <QStyle>
#include "dialogs.hpp"
#include "engine.hpp"
#include "input-tile.hpp"
#include "video-display.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

static void repolish(QWidget *w)
{
	w->style()->unpolish(w);
	w->style()->polish(w);
}

MainWindow::MainWindow()
{
	setWindowTitle(QString("ReplayDeck Studio %1").arg(RDS_VERSION));
	resize(1600, 900);

	auto *central = new QWidget;
	central->setObjectName("central");
	auto *v = new QVBoxLayout(central);
	v->setContentsMargins(6, 6, 6, 0);
	v->setSpacing(6);

	auto *split = new QSplitter(Qt::Vertical);
	split->setChildrenCollapsible(false);
	split->addWidget(buildVideoArea());

	auto *bottom = new QSplitter(Qt::Horizontal);
	bottom->setChildrenCollapsible(false);
	bottom->addWidget(buildInputsArea());
	bottom->addWidget(buildRightPanel());
	bottom->setStretchFactor(0, 1);
	bottom->setStretchFactor(1, 0);
	bottom->setSizes({900, 860});
	split->addWidget(bottom);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 1);
	split->setSizes({470, 470});
	v->addWidget(split, 1);
	v->addWidget(buildBottomBar());
	setCentralWidget(central);

	auto *sb = statusBar();
	for (auto *&l : status) {
		l = new QLabel;
		l->setObjectName("statusText");
		sb->addWidget(l);
	}
	outStatus = new QLabel;
	outStatus->setObjectName("statusText");
	sb->addPermanentWidget(outStatus);
	connect(&Outputs::get(), &Outputs::stateChanged, this, &MainWindow::updateOutputs);
	connect(&Outputs::get(), &Outputs::failed, this, [this](const QString &what, const QString &msg) {
		QMessageBox::warning(this, what, what + " parou.\n\n" + msg);
	});

	buildMenus();

	Engine &e = Engine::get();
	connect(&e, &Engine::inputsChanged, this, &MainWindow::rebuildTiles);
	connect(&e, &Engine::busChanged, this, &MainWindow::updateBus);
	connect(&e, &Engine::inputChanged, this, [this](int) { updateBus(); });

	program->showProgram();
	rebuildTiles();
	updateBus();

	cpu = os_cpu_usage_info_start();
	statusTimer = new QTimer(this);
	connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
	statusTimer->start(1000);
}

MainWindow::~MainWindow()
{
	os_cpu_usage_info_destroy(cpu);
}

/* ---------- prévia | transições | programa ---------- */

static QWidget *busPanel(const QString &busName, VideoDisplay *&display, QLabel *&title, QLabel *&footer)
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	auto *head = new QWidget;
	head->setObjectName(busName == "preview" ? "pvwHeader" : "pgmHeader");
	head->setFixedHeight(24);
	auto *h = new QHBoxLayout(head);
	h->setContentsMargins(8, 0, 8, 0);
	title = new QLabel;
	title->setObjectName("busTitle");
	h->addWidget(title, 1);
	v->addWidget(head);
	display = new VideoDisplay;
	display->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	display->setMinimumSize(320, 180);
	v->addWidget(display, 1);
	footer = new QLabel;
	footer->setObjectName("busFooter");
	footer->setFixedHeight(22);
	v->addWidget(footer);
	return w;
}

QWidget *MainWindow::buildVideoArea()
{
	auto *w = new QWidget;
	auto *h = new QHBoxLayout(w);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(8);
	QLabel *pvwFooter = nullptr;
	h->addWidget(busPanel("preview", preview, pvwTitle, pvwFooter), 1);
	pvwFooter->setText("  Prévia");
	connect(preview, &VideoDisplay::doubleClicked, this, [this] {
		if (Engine::get().previewId())
			openInput(Engine::get().previewId());
	});
	h->addWidget(buildTransitions());
	h->addWidget(busPanel("program", program, pgmTitle, pgmFooter), 1);
	return w;
}

static QPushButton *transButton(const QString &text, const QString &tip)
{
	auto *b = new QPushButton(text);
	b->setObjectName("transButton");
	b->setFixedHeight(30);
	b->setToolTip(tip);
	return b;
}

QWidget *MainWindow::buildTransitions()
{
	auto *w = new QWidget;
	w->setFixedWidth(140);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 24, 0, 0);
	v->setSpacing(4);
	Engine &e = Engine::get();

	auto *quick = transButton("Quick Play", "Prévia para o programa com fade");
	connect(quick, &QPushButton::clicked, this, [&e] { e.transition(TransitionKind::Fade); });
	auto *cut = transButton("Cortar", "Prévia para o programa com corte seco");
	connect(cut, &QPushButton::clicked, this, [&e] { e.transition(TransitionKind::Cut); });
	auto *fade = transButton("Fade", "Prévia para o programa com fade");
	connect(fade, &QPushButton::clicked, this, [&e] { e.transition(TransitionKind::Fade); });
	auto *stinger = transButton("Stinger 1", "Escolha o vídeo do stinger em Configurações (próxima versão)");
	stinger->setEnabled(false);
	auto *merge = transButton("Merge", "Chega numa próxima versão");
	merge->setEnabled(false);
	ftbBtn = transButton("FTB", "Fade para o preto (e volta)");
	connect(ftbBtn, &QPushButton::clicked, this, [&e] { e.transition(TransitionKind::FadeToBlack); });
	for (QPushButton *b : {quick, cut, fade, stinger, merge, ftbBtn})
		v->addWidget(b);

	auto *ovRow = new QHBoxLayout;
	ovRow->setSpacing(3);
	for (int ch = 1; ch <= 4; ch++) {
		ovBtn[ch] = new QPushButton(QString::number(ch));
		ovBtn[ch]->setObjectName("ovButton");
		ovBtn[ch]->setFixedHeight(22);
		ovBtn[ch]->setToolTip(QString("Overlay %1: liga/desliga a entrada da prévia por cima do programa").arg(ch));
		connect(ovBtn[ch], &QPushButton::clicked, this, [&e, ch] {
			const int cur = e.overlay(ch);
			e.toggleOverlay(ch, cur ? cur : e.previewId());
		});
		ovRow->addWidget(ovBtn[ch]);
	}
	v->addSpacing(6);
	v->addLayout(ovRow);
	auto *ovLbl = new QLabel("overlays");
	ovLbl->setObjectName("muted");
	ovLbl->setAlignment(Qt::AlignCenter);
	v->addWidget(ovLbl);

	tbar = new QSlider(Qt::Vertical);
	tbar->setObjectName("tbar");
	tbar->setRange(0, 1000);
	tbar->setMinimumHeight(40);
	connect(tbar, &QSlider::sliderMoved, this, [&e](int val) { e.tbarMove(val / 1000.0f); });
	connect(tbar, &QSlider::sliderReleased, this, [this, &e] {
		e.tbarRelease(tbar->value() / 1000.0f);
		QSignalBlocker b(tbar);
		tbar->setValue(0);
	});
	auto *tbRow = new QHBoxLayout;
	tbRow->addStretch(1);
	tbRow->addWidget(tbar);
	tbRow->addStretch(1);
	v->addSpacing(6);
	v->addLayout(tbRow, 1);

	duration = new QSpinBox;
	duration->setRange(50, 10000);
	duration->setSingleStep(50);
	duration->setSuffix(" ms");
	duration->setValue(e.transitionMs());
	duration->setToolTip("Duração do fade");
	connect(duration, &QSpinBox::valueChanged, this, [&e](int ms) { e.setTransitionMs(ms); });
	v->addWidget(duration);
	return w;
}

/* ---------- entradas ---------- */

QWidget *MainWindow::buildInputsArea()
{
	auto *w = new QWidget;
	w->setObjectName("panel");
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	auto *bar = new QWidget;
	bar->setObjectName("panelBar");
	bar->setFixedHeight(24);
	auto *h = new QHBoxLayout(bar);
	h->setContentsMargins(8, 0, 8, 0);
	auto *title = new QLabel("Entradas");
	title->setObjectName("panelTitle");
	h->addWidget(title);
	h->addStretch(1);
	auto *hint = new QLabel("clique = prévia · ⚙ ou duplo clique = abrir · botão direito = mais opções");
	hint->setObjectName("muted");
	h->addWidget(hint);
	v->addWidget(bar);

	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	/* as imagens das entradas são janelas nativas (desenhadas pela placa de vídeo): só uma área
	 * de rolagem também nativa consegue cortar o que passa da borda do painel */
	scroll->viewport()->setAttribute(Qt::WA_NativeWindow);
	tilesHost = new QWidget;
	tilesHost->setObjectName("tilesHost");
	tilesGrid = new FlowLayout(tilesHost, 6, 6); /* quadros e barras lado a lado, quebrando a linha */
	emptyHint = new QLabel("Nenhuma entrada ainda.\nClique em \"Adicionar entrada\" lá embaixo para colocar uma câmera, vídeo ou imagem.");
	emptyHint->setObjectName("muted");
	emptyHint->setAlignment(Qt::AlignCenter);
	scroll->setWidget(tilesHost);
	v->addWidget(scroll, 1);
	return w;
}

void MainWindow::rebuildTiles()
{
	for (InputTile *t : tiles) {
		tilesGrid->removeWidget(t);
		t->hide();
		t->deleteLater();
	}
	tiles.clear();
	tilesGrid->removeWidget(emptyHint);
	emptyHint->hide();
	for (Input *in : Engine::get().inputs()) {
		auto *t = new InputTile(in->id, tilesHost);
		connect(t, &InputTile::openRequested, this, &MainWindow::openInput);
		tiles.append(t);
	}
	if (tiles.isEmpty()) {
		tilesGrid->addWidget(emptyHint);
		emptyHint->show();
	}
	for (InputTile *t : tiles) {
		tilesGrid->addWidget(t);
		t->show();
	}
	updateBus();
}

void MainWindow::layoutTiles()
{
	tilesGrid->invalidate(); /* o layout em fila se ajusta sozinho à largura */
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
	QMainWindow::resizeEvent(e);
	QTimer::singleShot(0, this, &MainWindow::layoutTiles);
}

/* ---------- painel da direita (replay, áudio, títulos...) ---------- */

static QWidget *comingSoon(const QString &title, const QString &text)
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->addStretch(1);
	auto *t = new QLabel(title);
	t->setObjectName("panelTitle");
	t->setAlignment(Qt::AlignCenter);
	auto *d = new QLabel(text);
	d->setObjectName("muted");
	d->setAlignment(Qt::AlignCenter);
	d->setWordWrap(true);
	v->addWidget(t);
	v->addWidget(d);
	v->addStretch(1);
	return w;
}

QWidget *MainWindow::buildRightPanel()
{
	rightTabs = new QTabWidget;
	rightTabs->setObjectName("rightTabs");
	rightTabs->setMinimumWidth(560);
	rightTabs->addTab(replayhost::start(this), "Replay instantâneo");
	rightTabs->addTab(new AudioMixer, "Mixer de áudio");
	rightTabs->addTab(comingSoon("Títulos GT", "Chega na fase 5."), "Títulos GT");
	rightTabs->addTab(comingSoon("PTZ", "Chega na fase 6."), "PTZ");
	rightTabs->addTab(comingSoon("Lista", "Lista de vídeos: chega na fase 6."), "Lista");
	return rightTabs;
}

/* ---------- barra de baixo ---------- */

QWidget *MainWindow::buildBottomBar()
{
	auto *w = new QWidget;
	w->setObjectName("bottomBar");
	w->setFixedHeight(32);
	auto *h = new QHBoxLayout(w);
	h->setContentsMargins(0, 3, 0, 3);
	h->setSpacing(4);
	auto *add = new QPushButton("Adicionar entrada");
	connect(add, &QPushButton::clicked, this, &MainWindow::addInput);
	h->addWidget(add);
	h->addSpacing(10);
	recBtn = new QPushButton("Gravar");
	recBtn->setObjectName("outButton");
	recBtn->setToolTip("Grava o Programa num arquivo (Configurações → Gravação)");
	connect(recBtn, &QPushButton::clicked, this, &MainWindow::toggleRecording);
	h->addWidget(recBtn);
	auto *ext = new QPushButton("Externo ▾");
	ext->setToolTip("Programa, Prévia ou Replay em tela cheia noutro monitor ou TV (telão, VAR)");
	auto *extMenu = new QMenu(ext);
	connect(extMenu, &QMenu::aboutToShow, this, [this, extMenu] { ExternalOutput::fillMenu(extMenu, this); });
	ext->setMenu(extMenu);
	h->addWidget(ext);
	streamBtn = new QPushButton("Transmitir");
	streamBtn->setObjectName("outButton");
	streamBtn->setToolTip("Transmite o Programa ao vivo (Configurações → Transmissão)");
	connect(streamBtn, &QPushButton::clicked, this, &MainWindow::toggleStreaming);
	h->addWidget(streamBtn);
	const QList<QPair<QString, QString>> later = {{"MultiCorder", "fase 6"}, {"Playlist", "fase 6"}};
	for (const auto &p : later) {
		auto *b = new QPushButton(p.first);
		b->setToolTip("Chega na " + p.second);
		connect(b, &QPushButton::clicked, this, [this, p] { notYet(p.first, p.second); });
		h->addWidget(b);
	}
	h->addStretch(1);
	auto *cfg = new QPushButton("⚙ Configurações");
	connect(cfg, &QPushButton::clicked, this, [this] { SettingsDialog(this).exec(); });
	h->addWidget(cfg);
	return w;
}

void MainWindow::notYet(const QString &what, const QString &phase)
{
	QMessageBox::information(this, what, QString("\"%1\" chega na %2.").arg(what, phase));
}

/* ---------- menus ---------- */

void MainWindow::buildMenus()
{
	Engine &e = Engine::get();
	QMenu *file = menuBar()->addMenu("Arquivo");
	file->addAction("Novo projeto", this, [this, &e] {
		if (QMessageBox::question(this, "Novo projeto", "Começar um projeto vazio? O atual é salvo antes.") != QMessageBox::Yes)
			return;
		e.saveProject(e.projectPath());
		const QString p = QFileDialog::getSaveFileName(this, "Novo projeto", e.configDir(), "Projeto ReplayDeck (*.rdsproj)");
		if (p.isEmpty())
			return;
		while (!e.inputs().isEmpty())
			e.removeInput(e.inputs().first()->id);
		e.saveProject(p);
	});
	file->addAction("Abrir projeto…", this, [this, &e] {
		const QString p = QFileDialog::getOpenFileName(this, "Abrir projeto", e.configDir(), "Projeto ReplayDeck (*.rdsproj)");
		if (!p.isEmpty() && !e.loadProject(p))
			QMessageBox::warning(this, "Abrir projeto", "Não foi possível abrir este arquivo.");
	});
	file->addAction("Salvar", QKeySequence::Save, this, [this, &e] {
		if (!e.saveProject(e.projectPath()))
			QMessageBox::warning(this, "Salvar", "Não foi possível salvar o projeto.");
	});
	file->addAction("Salvar como…", this, [this, &e] {
		const QString p = QFileDialog::getSaveFileName(this, "Salvar projeto", e.projectPath(), "Projeto ReplayDeck (*.rdsproj)");
		if (!p.isEmpty())
			e.saveProject(p);
	});
	file->addSeparator();
	file->addAction("Sair", this, &QWidget::close);

	QMenu *view = menuBar()->addMenu("Visualizar");
	view->addAction("Tela cheia", QKeySequence(Qt::Key_F11), this, [this] {
		isFullScreen() ? showNormal() : showFullScreen();
	});

	QMenu *inputs = menuBar()->addMenu("Entradas");
	inputs->addAction("Adicionar entrada…", QKeySequence("Ctrl+N"), this, &MainWindow::addInput);
	inputs->addAction("Abrir entrada da prévia", this, [this, &e] {
		if (e.previewId())
			openInput(e.previewId());
	});

	QMenu *replay = menuBar()->addMenu("Replay");
	replay->addAction("Gravar / parar gravação do replay", this, [] { replayhost::toggleRecording(); });
	replay->addSeparator();
	replay->addAction("Configuração rápida…", this, [] { replayhost::runWizard(); });
	replay->addAction("Configurações do replay…", this, [] { replayhost::openSettings(); });
	QMenu *titles = menuBar()->addMenu("Títulos");
	titles->addAction("Títulos GT…", this, [this] { notYet("Títulos GT", "fase 5"); });
	QMenu *cfg = menuBar()->addMenu("Configurações");
	cfg->addAction("Configurações…", this, [this] { SettingsDialog(this).exec(); });
	cfg->addAction("Vídeo (resolução e fps)…", this, [this] { SettingsDialog(this, SettingsDialog::Video).exec(); });
	cfg->addAction("Gravação…", this, [this] { SettingsDialog(this, SettingsDialog::Recording).exec(); });
	cfg->addAction("Transmissão…", this, [this] { SettingsDialog(this, SettingsDialog::Streaming).exec(); });

	QMenu *help = menuBar()->addMenu("Ajuda");
	help->addAction("Abrir pasta de logs", this, [&e] { QDesktopServices::openUrl(QUrl::fromLocalFile(e.configDir() + "/logs")); });
	help->addAction("Sobre", this, [this] {
		QMessageBox::about(this, "ReplayDeck Studio",
				   QString("<b>ReplayDeck Studio %1</b><br>Motor de vídeo: libobs %2 (OBS Studio, GPL)<br>"
					   "Software livre, GPL versão 2 ou posterior.")
					   .arg(RDS_VERSION, obs_get_version_string()));
	});
}

/* ---------- estado ---------- */

void MainWindow::updateBus()
{
	Engine &e = Engine::get();
	Input *pv = e.input(e.previewId());
	Input *pg = e.input(e.programId());
	pvwTitle->setText(pv ? QString("%1   %2").arg(e.indexOf(pv->id) + 1).arg(pv->name) : "Prévia vazia");
	pgmTitle->setText(e.fadedToBlack() ? "PRETO (FTB)" : pg ? QString("%1   %2").arg(e.indexOf(pg->id) + 1).arg(pg->name) : "Programa vazio");
	preview->showSource(pv ? pv->scene.Get() : nullptr);

	QStringList ovs;
	for (int ch = 1; ch <= 4; ch++) {
		const int id = e.overlay(ch);
		ovBtn[ch]->setProperty("on", id != 0);
		repolish(ovBtn[ch]);
		if (Input *in = e.input(id))
			ovs << QString("%1: %2").arg(ch).arg(in->name);
	}
	pgmFooter->setText("  " + (ovs.isEmpty() ? QString("sem overlays") : "Overlays  " + ovs.join("   ")));
	ftbBtn->setProperty("on", e.fadedToBlack());
	repolish(ftbBtn);
}

void MainWindow::updateStatus()
{
	obs_video_info ovi;
	obs_get_video_info(&ovi);
	const uint32_t total = obs_get_total_frames();
	const uint32_t skipped = obs_get_lagged_frames();
	status[0]->setText(QString("%1x%2  %3 fps").arg(ovi.base_width).arg(ovi.base_height).arg(obs_get_active_fps(), 0, 'f', 2));
	status[1]->setText(QString("Render %1 ms").arg(obs_get_average_frame_time_ns() / 1000000.0, 0, 'f', 1));
	status[2]->setText(QString("CPU %1%").arg(os_cpu_usage_info_query(cpu), 0, 'f', 0));
	status[3]->setText(QString("Atrasados %1 de %2 quadros").arg(skipped).arg(total));
	status[4]->setText(QString("%1 entradas").arg(Engine::get().inputs().size()));
	updateOutputs();
}

static QString hms(qint64 ms)
{
	const qint64 s = ms / 1000;
	return QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

void MainWindow::updateOutputs()
{
	Outputs &o = Outputs::get();
	auto paint = [](QPushButton *b, bool on, const QString &text) {
		b->setText(text);
		if (b->property("on").toBool() != on) {
			b->setProperty("on", on);
			b->style()->unpolish(b);
			b->style()->polish(b);
		}
	};
	paint(recBtn, o.recording(), o.recording() ? "● Gravando " + hms(o.recordingMs()) : "Gravar");
	QString st = "Transmitir";
	if (o.streaming())
		st = (o.streamReconnecting() ? "↻ Reconectando " : "● Ao vivo ") + hms(o.streamingMs());
	paint(streamBtn, o.streaming(), st);
	if (o.streaming()) {
		const int total = o.streamTotalFrames(), lost = o.streamDroppedFrames();
		outStatus->setText(QString("Transmissão: %1 quadros perdidos (%2%)")
					   .arg(lost)
					   .arg(total ? 100.0 * lost / total : 0.0, 0, 'f', 1));
	} else {
		outStatus->setText(o.recording() ? "Gravando em " + o.recordingFile() : QString());
	}
}

void MainWindow::toggleRecording()
{
	Outputs &o = Outputs::get();
	if (o.recording()) {
		o.stopRecording();
	} else {
		QString err;
		if (!o.startRecording(err))
			QMessageBox::warning(this, "Gravar", "A gravação não começou.\n\n" + err);
	}
	updateOutputs();
}

void MainWindow::toggleStreaming()
{
	Outputs &o = Outputs::get();
	if (o.streaming()) {
		if (QMessageBox::question(this, "Transmitir", "Encerrar a transmissão ao vivo?") != QMessageBox::Yes)
			return;
		o.stopStreaming();
	} else {
		if (o.settings().streamKey.trimmed().isEmpty()) {
			SettingsDialog d(this, SettingsDialog::Streaming);
			if (d.exec() != QDialog::Accepted || o.settings().streamKey.trimmed().isEmpty())
				return;
		}
		QString err;
		if (!o.startStreaming(err))
			QMessageBox::warning(this, "Transmitir", "A transmissão não começou.\n\n" + err);
	}
	updateOutputs();
}

/* ---------- janelas ---------- */

void MainWindow::openInput(int id)
{
	auto *w = new InputWindow(id, this);
	w->show();
}

void MainWindow::addInput()
{
	AddInputDialog d(this);
	d.exec();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	Outputs &o = Outputs::get();
	if ((o.recording() || o.streaming()) &&
	    QMessageBox::question(this, "Fechar", QString("%1 ainda está ligada. Parar e fechar o programa?")
							  .arg(o.streaming() ? "A transmissão" : "A gravação")) != QMessageBox::Yes) {
		e->ignore();
		return;
	}
	statusTimer->stop();
	ExternalOutput::closeAll();
	/* fecha as telas de vídeo antes de desligar o motor */
	for (InputTile *t : tiles)
		delete t;
	tiles.clear();
	preview->clear();
	program->clear();
	e->accept();
}
