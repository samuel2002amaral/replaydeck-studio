#include "external-output.hpp"
#include "engine.hpp"
#include "outputs.hpp"
#include "vertical-canvas.hpp"
#include "video-display.hpp"

#include <QGuiApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMenu>
#include <QScreen>
#include <QVBoxLayout>

static QList<ExternalOutput *> g_outputs;

ExternalOutput::ExternalOutput(What w, int inputId, QScreen *screen) : QWidget(nullptr), what(w), id(inputId)
{
	setAttribute(Qt::WA_DeleteOnClose);
	/* bordas definidas ANTES da tela de vídeo existir (no Windows mudar depois recria a janela) */
	setWindowFlags(screen ? Qt::Window | Qt::FramelessWindowHint : Qt::Window);
	setStyleSheet("background: #000000;");
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	video = new VideoDisplay;
	video->setToolTip(QString());
	v->addWidget(video);
	connect(video, &VideoDisplay::doubleClicked, this, &QWidget::close);
	setWindowTitle("ReplayDeck Studio — " + description());

	if (screen) {
		/* tela cheia: sem bordas, ocupando o monitor escolhido */
		setGeometry(screen->geometry());
		setScreen(screen);
		showFullScreen();
	} else {
		/* janela normal (com título): dá para mover, mudar o tamanho e fechar */
		setMinimumSize(320, 180);
		resize(960, 540);
		show();
	}
	g_outputs << this;
	connect(&Engine::get(), &Engine::busChanged, this, &ExternalOutput::refresh);
	connect(&Engine::get(), &Engine::inputsChanged, this, [this] {
		if (what == What::Input && !Engine::get().input(id))
			close();
	});
	refresh();
}

ExternalOutput::~ExternalOutput()
{
	g_outputs.removeAll(this);
}

QString ExternalOutput::description() const
{
	switch (what) {
	case What::Program:
		return "Programa";
	case What::Preview:
		return "Prévia";
	case What::Vertical:
		return "Vertical 9:16";
	default: {
		Input *in = Engine::get().input(id);
		return in ? in->name : QString("Entrada");
	}
	}
}

void ExternalOutput::refresh()
{
	Engine &e = Engine::get();
	if (what == What::Program) {
		video->showProgram();
	} else if (what == What::Vertical) {
		const StudioSettings s = Outputs::get().settings();
		VerticalCanvas &vc = VerticalCanvas::get();
		if (vc.ensureScene(s.verticalWidth, s.verticalHeight, s.verticalFit, s.verticalOffset)) {
			video->setCanvasSize(QSize(vc.width(), vc.height()));
			video->showSource(vc.scene());
		}
	} else {
		Input *in = e.input(what == What::Preview ? e.previewId() : id);
		if (in)
			video->showSource(in->scene);
		else
			video->clear();
	}
}

void ExternalOutput::contextMenuEvent(QContextMenuEvent *e)
{
	QMenu menu(this);
	if (isFullScreen())
		menu.addAction("Em janela", this, [this] {
			setWindowFlags(Qt::Window);
			showNormal();
			resize(960, 540);
		});
	else
		menu.addAction("Tela cheia aqui", this, [this] {
			setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
			showFullScreen();
		});
	menu.addSeparator();
	menu.addAction("Fechar (Esc)", this, &QWidget::close);
	menu.exec(e->globalPos());
}

void ExternalOutput::keyPressEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Escape)
		close();
	else
		QWidget::keyPressEvent(e);
}

void ExternalOutput::mouseDoubleClickEvent(QMouseEvent *)
{
	close();
}

void ExternalOutput::closeAll()
{
	const auto copy = g_outputs;
	for (ExternalOutput *o : copy)
		o->close();
}

int ExternalOutput::openCount()
{
	return g_outputs.size();
}

static QString screenLabel(QScreen *s, int index)
{
	const QRect g = s->geometry();
	QString name = s->model().trimmed();
	if (name.isEmpty())
		name = s->name();
	return QString("Tela %1 — %2 (%3×%4)%5")
		.arg(index + 1)
		.arg(name)
		.arg(g.width())
		.arg(g.height())
		.arg(s == QGuiApplication::primaryScreen() ? "  · principal" : "");
}

void ExternalOutput::fillMenu(QMenu *menu, QWidget *parent)
{
	menu->clear();
	const QList<QScreen *> screens = QGuiApplication::screens();
	auto addTarget = [&](QMenu *m, What w, int inputId) {
		for (int i = 0; i < screens.size(); i++) {
			QScreen *s = screens[i];
			m->addAction(screenLabel(s, i), parent, [w, inputId, s, parent] {
				/* a mesma tela do programa fica coberta: avisa como sair */
				if (parent && parent->screen() == s &&
				    QMessageBox::question(parent, "Externo",
							  "Essa é a tela onde o programa está: ela vai ficar coberta.\n\n"
							  "Para sair: Esc, duplo clique ou botão direito → Fechar.\n\nContinuar?") !=
					    QMessageBox::Yes)
					return;
				new ExternalOutput(w, inputId, s);
			});
		}
		m->addSeparator();
		m->addAction("Numa janela (para testar)", parent, [w, inputId] { new ExternalOutput(w, inputId, nullptr); });
	};
	menu->addSection("Mostrar em outra tela / TV");
	addTarget(menu->addMenu("Programa"), What::Program, 0);
	addTarget(menu->addMenu("Prévia"), What::Preview, 0);
	addTarget(menu->addMenu("Vertical 9:16 (como sai no Reels/TikTok)"), What::Vertical, 0);
	/* replay primeiro (monitor do VAR), depois as outras entradas */
	QMenu *others = nullptr;
	for (Input *in : Engine::get().inputs()) {
		const bool replay = strcmp(obs_source_get_unversioned_id(in->source), "instant_replay_output") == 0;
		if (!(obs_source_get_output_flags(in->source) & OBS_SOURCE_VIDEO))
			continue;
		QMenu *target;
		if (replay) {
			target = menu->addMenu(in->name + "  (monitor do VAR)");
		} else {
			if (!others)
				others = new QMenu("Outra entrada", menu);
			target = others->addMenu(QString("%1  %2").arg(Engine::get().indexOf(in->id) + 1).arg(in->name));
		}
		addTarget(target, What::Input, in->id);
	}
	if (others)
		menu->addMenu(others);
	if (!g_outputs.isEmpty()) {
		menu->addSection("Abertas");
		for (ExternalOutput *o : g_outputs)
			menu->addAction("Fechar: " + o->description(), o, &QWidget::close);
		menu->addAction("Fechar todas", parent, [] { closeAll(); });
	}
}
