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
#include <QSet>
#include <QStyle>
#include <QTextEdit>

#ifdef _WIN32
#include <Windows.h>
#endif

static const int MOD_MASK = Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier;

Shortcuts &Shortcuts::get()
{
	static Shortcuts s;
	return s;
}

static QString path()
{
	return Engine::get().configDir() + "/atalhos.json";
}

QString Shortcut::display() const
{
	if (!label.isEmpty())
		return label;
	return key.toString(QKeySequence::NativeText);
}

/* nomes do teclado numérico pelo código físico (Windows: scan code; o mesmo com Num Lock ligado ou não) */
static QString keypadName(quint32 scan)
{
#ifdef _WIN32
	switch (scan) {
	case 0x47: return "Num 7";
	case 0x48: return "Num 8";
	case 0x49: return "Num 9";
	case 0x4A: return "Num -";
	case 0x4B: return "Num 4";
	case 0x4C: return "Num 5";
	case 0x4D: return "Num 6";
	case 0x4E: return "Num +";
	case 0x4F: return "Num 1";
	case 0x50: return "Num 2";
	case 0x51: return "Num 3";
	case 0x52: return "Num 0";
	case 0x53: return "Num ,";
	case 0x37: return "Num *";
	case 0x135: return "Num /";
	case 0x11C: return "Num Enter";
	default: return QString();
	}
#else
	(void)scan;
	return QString();
#endif
}

QString Shortcuts::keyName(quint32 scan, int mods, int qtKey, bool keypad)
{
	QString name = keypadName(scan);
	if (name.isEmpty()) {
		name = QKeySequence(qtKey).toString(QKeySequence::NativeText);
		if (qtKey == Qt::Key_Space)
			name = "Espaço";
		if (keypad)
			name = "Num " + name;
	}
	QString prefix;
	if (mods & Qt::ControlModifier)
		prefix += "Ctrl+";
	if (mods & Qt::AltModifier)
		prefix += "Alt+";
	if (mods & Qt::ShiftModifier)
		prefix += "Shift+";
	if (mods & Qt::MetaModifier)
		prefix += "Win+";
	return prefix + name;
}

void Shortcuts::load()
{
	items.clear();
	OBSDataAutoRelease root = obs_data_create_from_json_file_safe(QFile::encodeName(path()).constData(), "bak");
	if (!root)
		return;
	globalOn = obs_data_get_bool(root, "global");
	OBSDataArrayAutoRelease arr = obs_data_get_array(root, "atalhos");
	for (size_t i = 0; i < obs_data_array_count(arr); i++) {
		OBSDataAutoRelease o = obs_data_array_item(arr, i);
		Shortcut s;
		s.scan = (quint32)obs_data_get_int(o, "codigo");
		s.mods = (int)obs_data_get_int(o, "modificadores");
		s.label = QString::fromUtf8(obs_data_get_string(o, "nome"));
		s.key = QKeySequence::fromString(QString::fromUtf8(obs_data_get_string(o, "tecla")), QKeySequence::PortableText);
		s.function = QString::fromUtf8(obs_data_get_string(o, "funcao"));
		s.input = QString::fromUtf8(obs_data_get_string(o, "entrada"));
		s.value = QString::fromUtf8(obs_data_get_string(o, "valor"));
		if ((s.scan || !s.key.isEmpty()) && findFunction(s.function))
			items << s;
	}
}

void Shortcuts::save()
{
	OBSDataAutoRelease root = obs_data_create();
	obs_data_set_bool(root, "global", globalOn);
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const Shortcut &s : items) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_int(o, "codigo", s.scan);
		obs_data_set_int(o, "modificadores", s.mods);
		obs_data_set_string(o, "nome", s.label.toUtf8().constData());
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

void Shortcuts::setGlobal(bool on)
{
	globalOn = on;
	save();
	applyGlobalHook();
}

QList<Shortcut> Shortcuts::suggested()
{
	auto mk = [](const char *key, const char *fn, const QString &input = QString(), const QString &value = QString()) {
		Shortcut sc;
		sc.key = QKeySequence::fromString(QString::fromLatin1(key), QKeySequence::PortableText);
		sc.function = QString::fromLatin1(fn);
		sc.input = input;
		sc.value = value;
		return sc;
	};
	QList<Shortcut> l = {
		mk("Space", "Cut"),
		mk("Return", "Fade"),
		mk("Ctrl+B", "FadeToBlack"),
		mk("F1", "OverlayInput1"),
		mk("F2", "OverlayInput2"),
		mk("F3", "OverlayInput3"),
		mk("F4", "OverlayInput4"),
		mk("Right", "PreviewInputNext"),
		mk("Left", "PreviewInputPrevious"),
		mk("I", "ReplayMarkIn"),
		mk("O", "ReplayMarkOut"),
		mk("P", "ReplayPlayLastEvent"),
		mk("L", "ReplayLive"),
		mk("K", "ReplayPlayPause"),
		mk("J", "ReplayJumpSeconds", QString(), "-5"),
		mk("Ctrl+R", "StartStopRecording"),
	};
	for (int i = 1; i <= 9; i++)
		l << mk(QString::number(i).toLatin1().constData(), "PreviewInput", QString::number(i));
	for (int i = 1; i <= 4; i++)
		l << mk(QString("Ctrl+%1").arg(i).toLatin1().constData(), "ReplayCamera", QString(), QString::number(i));
	return l;
}

void Shortcuts::install()
{
	qApp->installEventFilter(this);
	applyGlobalHook();
}

static bool typingInto(QObject *o)
{
	QWidget *w = QApplication::focusWidget();
	for (QObject *x : {o, static_cast<QObject *>(w)}) {
		if (qobject_cast<QLineEdit *>(x) || qobject_cast<QTextEdit *>(x) || qobject_cast<QPlainTextEdit *>(x) ||
		    qobject_cast<QAbstractSpinBox *>(x) || qobject_cast<QKeySequenceEdit *>(x) || qobject_cast<KeyCapture *>(x))
			return true;
		if (auto *cb = qobject_cast<QComboBox *>(x); cb && cb->isEditable())
			return true;
	}
	return false;
}

bool Shortcuts::trigger(quint32 scan, int mods, int qtKey, bool keypad)
{
	mods &= MOD_MASK;
	bool handled = false;
	for (const Shortcut &s : items) {
		bool match;
		if (s.scan)
			match = s.scan == scan && s.mods == mods; /* pela tecla física */
		else /* atalhos sugeridos: pelo caractere (fora do teclado numérico) */
			match = !keypad && qtKey &&
				s.key == QKeySequence(QKeyCombination(Qt::KeyboardModifiers(mods), Qt::Key(qtKey)));
		if (!match)
			continue;
		QString err;
		if (!runFunction(s.function, s.input, s.value, &err))
			blog(LOG_WARNING, "atalho %s: %s", s.display().toUtf8().constData(), err.toUtf8().constData());
		handled = true;
	}
	return handled;
}

bool Shortcuts::eventFilter(QObject *o, QEvent *e)
{
	if (e->type() != QEvent::KeyPress || items.isEmpty())
		return false;
	auto *ke = static_cast<QKeyEvent *>(e);
	/* o mesmo aperto chega em vários objetos (janela, widget): só uma vez, e sem repetição */
	if (ke->isAutoRepeat() || !o->isWidgetType() ||
	    (static_cast<QWidget *>(o) != QApplication::focusWidget() && QApplication::focusWidget() != nullptr))
		return false;
	if (typingInto(o))
		return false;
	const int key = ke->key();
	if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta ||
	    key == Qt::Key_NumLock || key == Qt::Key_CapsLock || key == Qt::Key_ScrollLock)
		return false;
	return trigger(ke->nativeScanCode(), int(ke->modifiers()), key, ke->modifiers() & Qt::KeypadModifier);
}

/* ---------------- global (Windows): com outro programa na frente ---------------- */

#ifdef _WIN32
static HHOOK g_hook = nullptr;
static QSet<quint32> g_down; /* teclas já apertadas (o Windows repete enquanto segura) */

static LRESULT CALLBACK lowLevelKeyboard(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION) {
		const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
		const quint32 scan = kb->scanCode | ((kb->flags & LLKHF_EXTENDED) ? 0x100 : 0);
		const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
		if (!down) {
			g_down.remove(scan);
		} else if (!g_down.contains(scan)) {
			g_down.insert(scan);
			/* com o programa na frente, quem cuida é o próprio programa (não roda duas vezes) */
			DWORD pid = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &pid);
			const DWORD vk = kb->vkCode;
			const bool modifierKey = vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_SHIFT ||
						 vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_MENU || vk == VK_LMENU ||
						 vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN || vk == VK_NUMLOCK ||
						 vk == VK_CAPITAL || vk == VK_SCROLL;
			if (pid != GetCurrentProcessId() && !modifierKey) {
				int mods = 0;
				if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
					mods |= Qt::ControlModifier;
				if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
					mods |= Qt::ShiftModifier;
				if (GetAsyncKeyState(VK_MENU) & 0x8000)
					mods |= Qt::AltModifier;
				if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
					mods |= Qt::MetaModifier;
				/* o gancho tem que voltar rápido: a função roda na thread da interface */
				QMetaObject::invokeMethod(
					&Shortcuts::get(), [scan, mods] { Shortcuts::get().trigger(scan, mods, 0, false); },
					Qt::QueuedConnection);
			}
		}
	}
	return CallNextHookEx(g_hook, code, wParam, lParam); /* a tecla continua indo para o outro programa */
}
#endif

void Shortcuts::applyGlobalHook()
{
#ifdef _WIN32
	if (globalOn && !g_hook) {
		g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboard, GetModuleHandleW(nullptr), 0);
		blog(LOG_INFO, "atalhos globais %s", g_hook ? "ligados" : "NÃO ligaram");
	} else if (!globalOn && g_hook) {
		UnhookWindowsHookEx(g_hook);
		g_hook = nullptr;
		g_down.clear();
		blog(LOG_INFO, "atalhos globais desligados");
	}
#endif
}

/* ---------------- botão que grava a tecla ---------------- */

KeyCapture::KeyCapture(const Shortcut &s, QWidget *parent) : QPushButton(parent), sc(s)
{
	setObjectName("keyCapture");
	setToolTip("Clique e aperte a tecla (com Ctrl, Shift ou Alt se quiser). Esc cancela.");
	setFocusPolicy(Qt::StrongFocus);
	connect(this, &QPushButton::clicked, this, &KeyCapture::startCapture);
	paint();
}

bool KeyCapture::sameKey(const Shortcut &o) const
{
	if (sc.scan && o.scan)
		return sc.scan == o.scan && sc.mods == o.mods;
	return !sc.key.isEmpty() && sc.key == o.key;
}

void KeyCapture::startCapture()
{
	capturing = true;
	setFocus();
	paint();
}

void KeyCapture::paint()
{
	if (capturing)
		setText("aperte a tecla…");
	else
		setText(isEmpty() ? QString("clique e aperte") : sc.display());
	setProperty("on", capturing);
	style()->unpolish(this);
	style()->polish(this);
}

void KeyCapture::keyPressEvent(QKeyEvent *e)
{
	if (!capturing) {
		QPushButton::keyPressEvent(e);
		return;
	}
	const int key = e->key();
	if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta ||
	    key == Qt::Key_NumLock || key == Qt::Key_CapsLock || key == Qt::Key_ScrollLock || e->isAutoRepeat())
		return; /* espera a tecla de verdade (travas e Ctrl/Shift/Alt sozinhos não contam) */
	if (key == Qt::Key_Escape && !(e->modifiers() & MOD_MASK)) {
		capturing = false;
		paint();
		return;
	}
	const int mods = int(e->modifiers()) & MOD_MASK;
	const bool keypad = e->modifiers() & Qt::KeypadModifier;
	sc.scan = e->nativeScanCode();
	sc.mods = mods;
	sc.label = Shortcuts::keyName(sc.scan, mods, key, keypad);
	sc.key = keypad ? QKeySequence() : QKeySequence(QKeyCombination(Qt::KeyboardModifiers(mods), Qt::Key(key)));
	capturing = false;
	paint();
}

void KeyCapture::focusOutEvent(QFocusEvent *e)
{
	if (capturing) {
		capturing = false;
		paint();
	}
	QPushButton::focusOutEvent(e);
}
