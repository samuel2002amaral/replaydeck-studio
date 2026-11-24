#include "external-output.hpp"
#include "engine.hpp"
#include "video-display.hpp"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QScreen>
#include <QVBoxLayout>

static QList<ExternalOutput *> g_outputs;

ExternalOutput::ExternalOutput(What w, int inputId, QScreen *screen) : QWidget(nullptr), what(w), id(inputId)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
	setStyleSheet("background: #000000;");
	setCursor(Qt::BlankCursor);
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	video = new VideoDisplay;
	video->setToolTip(QString());
	v->addWidget(video);
	connect(video, &VideoDisplay::doubleClicked, this, &QWidget::close);
	setWindowTitle("ReplayDeck Studio — " + description());

	if (screen) {
		setGeometry(screen->geometry());
		setScreen(screen);
		showFullScreen();
	} else {
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
	} else {
		Input *in = e.input(what == What::Preview ? e.previewId() : id);
		if (in)
			video->showSource(in->scene);
		else
			video->clear();
	}
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
			m->addAction(screenLabel(s, i), parent, [w, inputId, s] { new ExternalOutput(w, inputId, s); });
		}
		m->addSeparator();
		m->addAction("Numa janela (para testar)", parent, [w, inputId] { new ExternalOutput(w, inputId, nullptr); });
	};
	menu->addSection("Mostrar em outra tela / TV");
	addTarget(menu->addMenu("Programa"), What::Program, 0);
	addTarget(menu->addMenu("Prévia"), What::Preview, 0);
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
