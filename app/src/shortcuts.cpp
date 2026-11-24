#include "shortcuts.hpp"
#include "engine.hpp"
#include "functions.hpp"

#include <obs.hpp>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

Shortcuts &Shortcuts::get()
{
	static Shortcuts s;
	return s;
}

static QString path()
{
	return Engine::get().configDir() + "/atalhos.json";
}

void Shortcuts::load()
{
	items.clear();
	OBSDataAutoRelease root = obs_data_create_from_json_file_safe(QFile::encodeName(path()).constData(), "bak");
	if (!root)
		return;
	OBSDataArrayAutoRelease arr = obs_data_get_array(root, "atalhos");
	for (size_t i = 0; i < obs_data_array_count(arr); i++) {
		OBSDataAutoRelease o = obs_data_array_item(arr, i);
		Shortcut s;
		s.key = QKeySequence::fromString(QString::fromUtf8(obs_data_get_string(o, "tecla")), QKeySequence::PortableText);
		s.function = QString::fromUtf8(obs_data_get_string(o, "funcao"));
		s.input = QString::fromUtf8(obs_data_get_string(o, "entrada"));
		s.value = QString::fromUtf8(obs_data_get_string(o, "valor"));
		if (!s.key.isEmpty() && findFunction(s.function))
			items << s;
	}
}

void Shortcuts::save()
{
	OBSDataAutoRelease root = obs_data_create();
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const Shortcut &s : items) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_string(o, "tecla", s.key.toString(QKeySequence::PortableText).toUtf8().constData());
		obs_data_set_string(o, "funcao", s.function.toUtf8().constData());
		obs_data_set_string(o, "entrada", s.input.toUtf8().constData());
		obs_data_set_string(o, "valor", s.value.toUtf8().constData());
		obs_data_array_push_back(arr, o);
	}
	obs_data_set_array(root, "atalhos", arr);
	obs_data_save_json_safe(root, QFile::encodeName(path()).constData(), "tmp", "bak");
}

void Shortcuts::setList(const QList<Shortcut> &l)
{
	items = l;
	save();
}

QList<Shortcut> Shortcuts::suggested()
{
	auto k = [](const char *s) { return QKeySequence::fromString(QString::fromLatin1(s), QKeySequence::PortableText); };
	QList<Shortcut> l = {
		{k("Space"), "Cut", "", ""},
		{k("Return"), "Fade", "", ""},
		{k("Ctrl+B"), "FadeToBlack", "", ""},
		{k("F1"), "OverlayInput1", "", ""},
		{k("F2"), "OverlayInput2", "", ""},
		{k("F3"), "OverlayInput3", "", ""},
		{k("F4"), "OverlayInput4", "", ""},
		{k("Right"), "PreviewInputNext", "", ""},
		{k("Left"), "PreviewInputPrevious", "", ""},
		{k("I"), "ReplayMarkIn", "", ""},
		{k("O"), "ReplayMarkOut", "", ""},
		{k("P"), "ReplayPlayLastEvent", "", ""},
		{k("L"), "ReplayLive", "", ""},
		{k("K"), "ReplayPlayPause", "", ""},
		{k("J"), "ReplayJumpSeconds", "", "-5"},
		{k("Ctrl+R"), "StartStopRecording", "", ""},
	};
	for (int i = 1; i <= 9; i++)
		l << Shortcut{k(QString::number(i).toLatin1().constData()), "PreviewInput", QString::number(i), ""};
	for (int i = 1; i <= 4; i++)
		l << Shortcut{k(QString("Ctrl+%1").arg(i).toLatin1().constData()), "ReplayCamera", "", QString::number(i)};
	return l;
}

void Shortcuts::install()
{
	qApp->installEventFilter(this);
}

static bool typingInto(QObject *o)
{
	QWidget *w = QApplication::focusWidget();
	for (QObject *x : {o, static_cast<QObject *>(w)}) {
		if (qobject_cast<QLineEdit *>(x) || qobject_cast<QTextEdit *>(x) || qobject_cast<QPlainTextEdit *>(x) ||
		    qobject_cast<QAbstractSpinBox *>(x) || qobject_cast<QKeySequenceEdit *>(x))
			return true;
		if (auto *cb = qobject_cast<QComboBox *>(x); cb && cb->isEditable())
			return true;
	}
	return false;
}

bool Shortcuts::eventFilter(QObject *o, QEvent *e)
{
	if (e->type() != QEvent::KeyPress || items.isEmpty())
		return false;
	auto *ke = static_cast<QKeyEvent *>(e);
	/* o mesmo aperto chega em vários objetos (janela, widget): só uma vez, e sem repetição */
	if (ke->isAutoRepeat() || !o->isWidgetType() || (static_cast<QWidget *>(o) != QApplication::focusWidget() &&
								    QApplication::focusWidget() != nullptr))
		return false;
	if (typingInto(o))
		return false;
	const int key = ke->key();
	if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta)
		return false;
	const QKeySequence pressed(ke->keyCombination());
	bool handled = false;
	for (const Shortcut &s : items) {
		if (s.key != pressed)
			continue;
		QString err;
		if (!runFunction(s.function, s.input, s.value, &err))
			blog(LOG_WARNING, "atalho %s: %s", s.key.toString().toUtf8().constData(), err.toUtf8().constData());
		handled = true;
	}
	return handled;
}
