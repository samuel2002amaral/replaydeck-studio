#include "main-window.hpp"
#include "mac-permissions.hpp"
#include "pack.hpp"
#include "multicorder.hpp"
#include "playlist.hpp"
#include "ptz.hpp"
#include "vmix-keys.hpp"
#include "scripts.hpp"
#include "plugins.hpp"
#include <QPointer>
#include <media-io/video-io.h>
#include "gt/gt-dock.hpp"
#include "transitions-panel.hpp"
#include "replay-host.hpp"
#include "perf.hpp"
#include "flow-layout.hpp"
#include "audio-mixer.hpp"
#include "external-output.hpp"
#include "outputs.hpp"
#include "settings-dialog.hpp"
#include "virtualcam.hpp"
#include <QMenu>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QDropEvent>
#include <QMimeData>
#include <climits>
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
	/* botão direito no Programa / na Prévia: tela cheia noutro monitor */
	auto screenMenu = [this](VideoDisplay *d, ExternalOutput::What what, const QString &label) {
		d->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(d, &QWidget::customContextMenuRequested, this, [this, d, what, label](const QPoint &pos) {
			QMenu menu(this);
			menu.addAction(label + " em tela cheia:")->setEnabled(false);
			ExternalOutput::addScreenActions(&menu, what, 0, this);
			menu.exec(d->mapToGlobal(pos));
		});
	};
	screenMenu(program, ExternalOutput::What::Program, "Programa");
	screenMenu(preview, ExternalOutput::What::Preview, "Prévia");
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

void MainWindow::rebuildUserTransitions()
{
	while (QLayoutItem *it = userTrBox->takeAt(0)) {
		delete it->widget();
		delete it;
	}
	const auto &list = Engine::get().userTransitions();
	if (list.isEmpty()) {
		auto *add = transButton("＋ Stinger", "Adicionar um stinger ou outra transição (aba Transições)");
		add->setObjectName("transAddButton");
		connect(add, &QPushButton::clicked, this, [this] {
			rightTabs->setCurrentIndex(3);
			if (auto *p = qobject_cast<TransitionsPanel *>(rightTabs->widget(3)))
				p->addStinger();
		});
		userTrBox->addWidget(add);
		return;
	}
	for (StudioTransition *t : list) {
		const int id = t->id;
		auto *b = transButton(t->name, QString("Prévia para o Programa com \"%1\" (%2)")
							   .arg(t->name, Engine::transitionTypeName(obs_source_get_unversioned_id(t->source))));
		if (t->isStinger())
			b->setProperty("stinger", true);
		connect(b, &QPushButton::clicked, this, [id] { Engine::get().transitionWith(id); });
		userTrBox->addWidget(b);
	}
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
	auto *merge = transButton("Merge", "Prévia para o programa com Merge: o que está nas duas entradas se move\n"
					   "para o lugar novo (ex.: a câmera em tela cheia encolhe até a camada do canto)");
	connect(merge, &QPushButton::clicked, this, [&e] { e.transition(TransitionKind::Merge); });
	ftbBtn = transButton("FTB", "Fade para o preto (e volta)");
	connect(ftbBtn, &QPushButton::clicked, this, [&e] { e.transition(TransitionKind::FadeToBlack); });
	for (QPushButton *b : {quick, cut, fade, merge})
		v->addWidget(b);
	/* stingers e outras transições criadas na aba Transições */
	userTrBox = new QVBoxLayout;
	userTrBox->setContentsMargins(0, 0, 0, 0);
	userTrBox->setSpacing(4);
	v->addLayout(userTrBox);
	v->addWidget(ftbBtn);
	connect(&e, &Engine::transitionsChanged, this, &MainWindow::rebuildUserTransitions, Qt::QueuedConnection);
	rebuildUserTransitions();

	auto *ovRow = new QHBoxLayout;
	ovRow->setSpacing(3);
	for (int ch = 1; ch <= 4; ch++) {
		ovBtn[ch] = new QPushButton(QString::number(ch));
		ovBtn[ch]->setObjectName("ovButton");
		ovBtn[ch]->setFixedHeight(22);
		ovBtn[ch]->setToolTip(QString("Overlay %1: liga/desliga a entrada da prévia por cima do programa\n"
					      "Botão direito: como entra e sai (Corte, Fade ou Merge)")
					      .arg(ch));
		ovBtn[ch]->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(ovBtn[ch], &QWidget::customContextMenuRequested, this, [this, &e, ch](const QPoint &pos) {
			QMenu menu(this);
			menu.addAction(QString("Overlay %1 entra e sai com:").arg(ch))->setEnabled(false);
			const struct {
				OverlayFx fx;
				const char *label;
			} opts[] = {{OverlayFx::Cut, "Corte (na hora)"},
				    {OverlayFx::Fade, "Fade"},
				    {OverlayFx::Merge, "Merge — o que está no programa e no overlay se move (ex.: jogo encolhendo para o L)"}};
			for (const auto &o : opts) {
				QAction *a = menu.addAction(QString::fromUtf8(o.label), this, [&e, ch, fx = o.fx] { e.setOverlayFx(ch, fx); });
				a->setCheckable(true);
				a->setChecked(e.overlayFx(ch) == o.fx);
			}
			menu.addSeparator();
			menu.addAction(QString("Duração: a mesma das transições (%1 ms)").arg(e.transitionMs()))->setEnabled(false);
			menu.exec(ovBtn[ch]->mapToGlobal(pos));
		});
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
	auto *hint = new QLabel("clique = prévia · duplo clique = abrir · botão direito = minimizar · ⚙ = opções · arraste para mudar a ordem");
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
	/* arrastar uma entrada pelo cabeçalho muda a ordem; a linha azul mostra onde ela entra */
	tilesHost->setAcceptDrops(true);
	tilesHost->installEventFilter(this);
	dropMark = new QWidget(tilesHost);
	dropMark->setObjectName("dropMark");
	dropMark->setAttribute(Qt::WA_StyledBackground);
	dropMark->hide();
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

/* posição de soltar: antes de qual entrada (índice na lista) e onde desenhar a linha */
int MainWindow::dropIndex(const QPoint &pos, QRect *mark) const
{
	int best = -1;
	int bestDist = INT_MAX;
	for (int i = 0; i < tiles.size(); i++) {
		const QRect g = tiles[i]->geometry();
		const int dy = pos.y() < g.top() ? g.top() - pos.y() : pos.y() > g.bottom() ? pos.y() - g.bottom() : 0;
		const int dx = pos.x() < g.left() ? g.left() - pos.x() : pos.x() > g.right() ? pos.x() - g.right() : 0;
		const int d = dy * 4 + dx; /* a linha (altura) pesa mais */
		if (d < bestDist) {
			bestDist = d;
			best = i;
		}
	}
	if (best < 0)
		return 0;
	const QRect g = tiles[best]->geometry();
	const bool after = pos.x() > g.center().x();
	if (mark)
		*mark = QRect(after ? g.right() + 2 : g.left() - 5, g.top(), 3, g.height());
	return after ? best + 1 : best;
}

bool MainWindow::eventFilter(QObject *o, QEvent *ev)
{
	if (o == tilesHost) {
		switch (ev->type()) {
		case QEvent::DragEnter:
		case QEvent::DragMove: {
			auto *de = static_cast<QDropEvent *>(ev);
			if (!de->mimeData()->hasFormat(InputTile::INPUT_MIME))
				return false;
			QRect r;
			dropIndex(de->position().toPoint(), &r);
			dropMark->setGeometry(r);
			dropMark->show();
			dropMark->raise();
			de->acceptProposedAction();
			return true;
		}
		case QEvent::DragLeave:
			dropMark->hide();
			return true;
		case QEvent::Drop: {
			auto *de = static_cast<QDropEvent *>(ev);
			dropMark->hide();
			if (!de->mimeData()->hasFormat(InputTile::INPUT_MIME))
				return false;
			const int id = de->mimeData()->data(InputTile::INPUT_MIME).toInt();
			int to = dropIndex(de->position().toPoint(), nullptr);
			const int from = Engine::get().indexOf(id);
			if (from < 0)
				return true;
			if (to > from)
				to--; /* ela sai do lugar antigo antes de entrar no novo */
			to = qBound(0, to, (int)Engine::get().inputs().size() - 1);
			if (to != from)
				Engine::get().moveInput(id, to);
			de->acceptProposedAction();
			return true;
		}
		default:
			break;
		}
	}
	return QMainWindow::eventFilter(o, ev);
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
	rightTabs->addTab(new GtDock, "Títulos GT");
	rightTabs->addTab(new TransitionsPanel, "Transições");
	rightTabs->addTab(new PtzPanel, "PTZ");
	rightTabs->addTab(new PlaylistPanel, "Lista");
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
	vcamBtn = new QPushButton("Câmera virtual ▾");
	vcamBtn->setObjectName("outButton");
	vcamBtn->setToolTip("Compartilha o Programa ou uma entrada como webcam (Zoom, navegador, WhatsApp)");
	auto *vcamMenu = new QMenu(vcamBtn);
	connect(vcamMenu, &QMenu::aboutToShow, this, [this, vcamMenu] { VirtualCam::get().fillMenu(vcamMenu, this); });
	vcamBtn->setMenu(vcamMenu);
	connect(&VirtualCam::get(), &VirtualCam::stateChanged, this, &MainWindow::updateOutputs);
	streamBtn = new QPushButton("Transmitir");
	streamBtn->setObjectName("outButton");
	streamBtn->setToolTip("Transmite o Programa ao vivo (Configurações → Transmissão)");
	connect(streamBtn, &QPushButton::clicked, this, &MainWindow::toggleStreaming);
	h->addWidget(streamBtn);
	h->addWidget(vcamBtn);
	mcBtn = new QPushButton("MultiCorder");
	mcBtn->setObjectName("outButton");
	mcBtn->setToolTip("Grava cada entrada escolhida num arquivo separado (câmeras limpas para editar depois)");
	connect(mcBtn, &QPushButton::clicked, this, &MainWindow::toggleMultiCorder);
	connect(&MultiCorder::get(), &MultiCorder::stateChanged, this, &MainWindow::updateOutputs);
	h->addWidget(mcBtn);
	plBtn = new QPushButton("Playlist");
	plBtn->setObjectName("outButton");
	plBtn->setToolTip("Toca a lista da aba Lista: entradas ao ar uma depois da outra");
	connect(plBtn, &QPushButton::clicked, this, [this] {
		Playlist &pl = Playlist::get();
		if (pl.active())
			pl.stop();
		else if (!pl.start()) {
			rightTabs->setCurrentIndex(rightTabs->count() - 1);
			QMessageBox::information(this, "Playlist", "A lista está vazia: monte na aba Lista (＋ Entrada).");
		}
	});
	connect(&Playlist::get(), &Playlist::stateChanged, this, &MainWindow::updateOutputs);
	h->addWidget(plBtn);
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
	file->addAction("Exportar tudo para outro computador (pacote)…", this, [this] { pack::exportDialog(this); });
	file->addAction("Importar pacote de outro computador…", this, [this] { pack::importDialog(this); });
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
	inputs->addSeparator();
	inputs->addAction("Importar chaves do vMix (.vmix, api.xml ou config do APPVMIX)…", this,
			  [this] { importVmixKeys(this); });
	inputs->addAction("O que o app externo pediu e não achou…", this, [this] { showMissing(this); });

	QMenu *replay = menuBar()->addMenu("Replay");
	replay->addAction("Gravar / parar gravação do replay", this, [] { replayhost::toggleRecording(); });
	replay->addSeparator();
	replay->addAction("Configuração rápida…", this, [] { replayhost::runWizard(); });
	replay->addAction("Configurações do replay…", this, [] { replayhost::openSettings(); });
	QMenu *titles = menuBar()->addMenu("Títulos");
	titles->addAction("Painel de títulos GT", this, [this] { rightTabs->setCurrentIndex(2); });
	titles->addAction("Importar título GT (.gtzip)…", this, [this] {
		rightTabs->setCurrentIndex(2);
		if (auto *d = qobject_cast<GtDock *>(rightTabs->widget(2)))
			QMetaObject::invokeMethod(d, "addTitle");
	});
	titles->addAction("Novo título em branco…", this, [this] {
		rightTabs->setCurrentIndex(2);
		if (auto *d = qobject_cast<GtDock *>(rightTabs->widget(2)))
			d->newTitle();
	});
	QMenu *cfg = menuBar()->addMenu("Configurações");
	cfg->addAction("Configurações…", this, [this] { SettingsDialog(this).exec(); });
	cfg->addAction("Vídeo (resolução e fps)…", this, [this] { SettingsDialog(this, SettingsDialog::Video).exec(); });
	cfg->addAction("Gravação…", this, [this] { SettingsDialog(this, SettingsDialog::Recording).exec(); });
	cfg->addAction("Transmissão…", this, [this] { SettingsDialog(this, SettingsDialog::Streaming).exec(); });
	cfg->addAction("Atalhos de teclado…", this, [this] { SettingsDialog(this, SettingsDialog::Shortcuts).exec(); });
	cfg->addAction("Controle remoto (API do vMix)…", this, [this] { SettingsDialog(this, SettingsDialog::Remote).exec(); });
	cfg->addSeparator();
	cfg->addAction("Plugins do OBS…", this, [this] { PluginsDialog(this).exec(); });
	cfg->addAction("Scripts…", this, [this] {
		static QPointer<ScriptsDialog> dlg;
		if (!dlg) {
			dlg = new ScriptsDialog(this);
			dlg->setAttribute(Qt::WA_DeleteOnClose);
		}
		dlg->show();
		dlg->raise();
		dlg->activateWindow();
	});

	QMenu *help = menuBar()->addMenu("Ajuda");
	/* abre com o Explorer e o Bloco de Notas direto: não depende da associação de pastas do
	 * Windows (quebrada em alguns PCs: "não tem um aplicativo associado") */
	help->addAction("Abrir pasta de logs", this, [this] { openLogs(LogAction::Folder); });
	help->addAction("Abrir o log atual (Bloco de Notas)", this, [this] { openLogs(LogAction::Notepad); });
	help->addAction("Copiar o log atual para a Área de Trabalho", this, [this] { openLogs(LogAction::Desktop); });
#ifdef __APPLE__
	help->addAction("Permissões do Mac (câmera, microfone, tela)…", this, [this] { macperm::showDialog(this); });
#endif
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
		{
			const OverlayFx fx = Engine::get().overlayFx(ch);
			ovBtn[ch]->setText(QString::number(ch) + (fx == OverlayFx::Merge ? " M" : fx == OverlayFx::Fade ? " F" : ""));
		}
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
	/* atrasados = a placa de vídeo não deu conta de desenhar; codificação = o codificador não deu conta */
	const uint32_t encSkipped = video_output_get_skipped_frames(obs_get_video());
	QString lag = QString("Atrasados %1 de %2 quadros").arg(skipped).arg(total);
	if (encSkipped)
		lag += QString("  ·  codificação perdeu %1").arg(encSkipped);
	status[3]->setText(lag);
	status[3]->setToolTip("Placa de vídeo: " + Engine::get().adapterName() +
			      "\nAtrasados: quadros que a placa não desenhou a tempo"
			      "\nCodificação: quadros que o codificador (gravação/transmissão) não deu conta");
	{
		/* replay do VAR: sempre à vista se está gravando inteiro; vermelho se perdeu quadro */
		bool alarm = false, rec = false;
		const QString r = replayhost::statusText(alarm, rec);
		perf::setReplayRecording(rec);
		QString text = QString("%1 entradas").arg(Engine::get().inputs().size());
		if (rec)
			text = r + "    " + text;
		status[4]->setText(text);
		status[4]->setStyleSheet(alarm ? "color: #ff5a5a; font-weight: bold;" : QString());
		status[4]->setToolTip(rec ? "Quadros do replay que não foram gravados (o VAR precisa de 0).\n"
					    "Disco atrasado: a gravação está esperando o disco (antivírus, disco cheio ou lento)."
					  : QString());
	}
	/* uma linha por minuto no log: com o log dá para ver como o programa rodou no PC de quem usa */
	static int perfTick = 0;
	static const int perfEvery = qEnvironmentVariableIsSet("RDS_PERF_LOG") ? 2 : 60;
	if (++perfTick >= perfEvery) {
		perfTick = 0;
		blog(LOG_INFO, "[desempenho] %.2f fps, desenho %.1f ms, CPU %.0f%%, atrasados %u/%u, codificação perdeu %u",
		     obs_get_active_fps(), obs_get_average_frame_time_ns() / 1000000.0, os_cpu_usage_info_query(cpu),
		     skipped, total, encSkipped);
	}
	updateOutputs();
}

static QString hms(qint64 ms)
{
	const qint64 s = ms / 1000;
	return QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

void MainWindow::toggleMultiCorder()
{
	MultiCorder &mc = MultiCorder::get();
	if (mc.active()) {
		if (QMessageBox::question(this, "MultiCorder", "Parar o MultiCorder?") == QMessageBox::Yes)
			mc.stop();
		return;
	}
	MultiCorderDialog dlg(this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	const QList<int> ids = dlg.selected();
	mc.lastSelection = QSet<int>(ids.begin(), ids.end());
	QString err;
	if (!mc.start(ids, err))
		QMessageBox::warning(this, "MultiCorder", err);
	else if (!err.isEmpty())
		QMessageBox::information(this, "MultiCorder", err);
	updateOutputs();
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
		st = (o.streamReconnecting() ? "↻ Reconectando " : "● Ao vivo ") +
		     (o.liveCount() > 1 ? QString("(%1) ").arg(o.liveCount()) : QString()) + hms(o.streamingMs());
	paint(streamBtn, o.streaming(), st);
	const Playlist &pl = Playlist::get();
	paint(plBtn, pl.active(),
	      pl.active() ? QString("● Playlist %1/%2").arg(pl.currentIndex() + 1).arg(pl.items.size()) : QString("Playlist"));
	MultiCorder &mc = MultiCorder::get();
	paint(mcBtn, mc.active(),
	      mc.active() ? QString("● MultiCorder (%1) %2").arg(mc.count()).arg(hms(mc.elapsedMs())) : QString("MultiCorder"));
	VirtualCam &vc = VirtualCam::get();
	paint(vcamBtn, vc.active(), vc.active() ? "● Câmera virtual: " + vc.description() + " ▾" : "Câmera virtual ▾");
	if (o.streaming()) {
		const int total = o.streamTotalFrames(), lost = o.streamDroppedFrames();
		outStatus->setText(QString("Transmissão (%3): %1 quadros perdidos (%2%)")
					   .arg(lost)
					   .arg(total ? 100.0 * lost / total : 0.0, 0, 'f', 1)
					   .arg(o.streamEncoderName()));
	} else {
		outStatus->setText(o.recording() ? "Gravando em " + o.recordingFile() + " (" + o.recordingEncoderName() + ")"
						 : QString());
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
		if (QMessageBox::question(this, "Transmitir",
					  o.liveCount() > 1 ? QString("Encerrar a transmissão nos %1 destinos?").arg(o.liveCount())
							    : QString("Encerrar a transmissão ao vivo?")) != QMessageBox::Yes)
			return;
		o.stopStreaming();
	} else {
		if (!o.settings().hasStreamKey()) {
			SettingsDialog d(this, SettingsDialog::Streaming);
			if (d.exec() != QDialog::Accepted || !o.settings().hasStreamKey())
				return;
		}
		QString err;
		if (!o.startStreaming(err))
			QMessageBox::warning(this, "Transmitir", "A transmissão não começou.\n\n" + err);
	}
	updateOutputs();
}

/* ---------- log ---------- */

void MainWindow::openLogs(LogAction what)
{
	const QString dir = QDir::cleanPath(Engine::get().configDir() + "/logs");
	const QFileInfoList files = QDir(dir).entryInfoList({"*.txt"}, QDir::Files, QDir::Time);
	const QString newest = files.isEmpty() ? QString() : files.first().absoluteFilePath();
	if (what == LogAction::Desktop) {
		if (newest.isEmpty()) {
			QMessageBox::information(this, "Log", "Nenhum log encontrado em:\n" + QDir::toNativeSeparators(dir));
			return;
		}
		const QString desk = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
		const QString dest = desk + "/ReplayDeck Studio log " + files.first().baseName() + ".txt";
		QFile::remove(dest);
		if (QFile::copy(newest, dest))
			QMessageBox::information(this, "Log", "O log foi copiado para a Área de Trabalho:\n" +
								 QDir::toNativeSeparators(dest) + "\n\nÉ só arrastar esse arquivo para a conversa.");
		else
			QMessageBox::warning(this, "Log", "Não consegui copiar para a Área de Trabalho.\nO log está em:\n" +
								QDir::toNativeSeparators(newest));
		return;
	}
#ifdef _WIN32
	bool ok;
	if (what == LogAction::Notepad && !newest.isEmpty())
		ok = QProcess::startDetached("notepad.exe", {QDir::toNativeSeparators(newest)});
	else if (!newest.isEmpty())
		ok = QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(newest)});
	else
		ok = QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(dir)});
	if (!ok)
		QMessageBox::information(this, "Log", "O log está em:\n" + QDir::toNativeSeparators(newest.isEmpty() ? dir : newest));
#else
	QDesktopServices::openUrl(QUrl::fromLocalFile(what == LogAction::Notepad && !newest.isEmpty() ? newest : dir));
#endif
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
