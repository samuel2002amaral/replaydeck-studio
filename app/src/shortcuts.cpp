#include "shortcuts.hpp"

#include <atomic>
#include <future>
#include <thread>
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
#include <QTimer>

#ifdef _WIN32
#include <Windows.h>
static bool physicallyDown(quint32 scan);
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

/* tecla física: no Windows/Linux o scan code; no Mac o Qt não dá scan code, mas o "virtual key"
 * do Mac já é a posição física (0 = A): soma 0x1000 para nunca ser 0 */
static quint32 physicalKey(const QKeyEvent *e)
{
#ifdef __APPLE__
	return e->nativeVirtualKey() + 0x1000;
#else
	return e->nativeScanCode();
#endif
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

/* teclado numérico para o replay, como um controlador:
 *   7 Mark In   8 zoom +     9 Mark Out
 *   4 ré (segure) 5 play/pausa 6 play (segure)
 *   1 ◀ quadro  2 zoom −     3 quadro ▶
 *   0 último lance no ar     , ao vivo     Enter põe no ar
 *   + mais rápido  − mais lento  * evento de 10 s  / zoom inteiro   Shift+setas: move o zoom */
QList<Shortcut> Shortcuts::suggestedReplay()
{
#ifdef _WIN32
	const quint32 KP[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
	const quint32 DEC = 0x53, ENTER = 0x11C, ADD = 0x4E, SUB = 0x4A, MUL = 0x37, DIV = 0x135;
	const quint32 LEFT = 0x14B, RIGHT = 0x14D, UP = 0x148, DOWN = 0x150;
#elif defined(__APPLE__) /* códigos de tecla do Mac + 0x1000 (veja physicalKey) */
	const quint32 KP[10] = {0x1052, 0x1053, 0x1054, 0x1055, 0x1056, 0x1057, 0x1058, 0x1059, 0x105B, 0x105C};
	const quint32 DEC = 0x1041, ENTER = 0x104C, ADD = 0x1045, SUB = 0x104E, MUL = 0x1043, DIV = 0x104B;
	const quint32 LEFT = 0x107B, RIGHT = 0x107C, UP = 0x107E, DOWN = 0x107D;
#else /* X11 */
	const quint32 KP[10] = {90, 87, 88, 89, 83, 84, 85, 79, 80, 81};
	const quint32 DEC = 91, ENTER = 104, ADD = 86, SUB = 82, MUL = 63, DIV = 106;
	const quint32 LEFT = 113, RIGHT = 114, UP = 111, DOWN = 116;
#endif
	auto mk = [](quint32 scan, int mods, const QString &label, const char *fn, const QString &value = QString()) {
		Shortcut s;
		s.scan = scan;
		s.mods = mods;
		s.label = label;
		s.function = QString::fromLatin1(fn);
		s.value = value;
		return s;
	};
	const int SHIFT = Qt::ShiftModifier;
	return {
		mk(KP[7], 0, "Num 7", "ReplayMarkIn"),
		mk(KP[9], 0, "Num 9", "ReplayMarkOut"),
		mk(KP[8], 0, "Num 8", "ReplayHoldZoomIn"),
		mk(KP[2], 0, "Num 2", "ReplayHoldZoomOut"),
		mk(KP[4], 0, "Num 4", "ReplayHoldReverse"),
		mk(KP[6], 0, "Num 6", "ReplayHoldPlay"),
		mk(KP[5], 0, "Num 5", "ReplayPlayPause"),
		mk(KP[1], 0, "Num 1", "ReplayHoldJog", "-1"),
		mk(KP[3], 0, "Num 3", "ReplayHoldJog", "1"),
		mk(KP[0], 0, "Num 0", "ReplayPlayLastEvent"),
		mk(DEC, 0, "Num ,", "ReplayLive"),
		mk(ENTER, 0, "Num Enter", "ReplayToProgram"),
		mk(ADD, 0, "Num +", "ReplaySpeedUp", "10"),
		mk(SUB, 0, "Num -", "ReplaySpeedDown", "10"),
		mk(MUL, 0, "Num *", "ReplayMarkInOut", "10"),
		mk(DIV, 0, "Num /", "ReplayZoomReset"),
		mk(LEFT, SHIFT, "Shift+←", "ReplayHoldPanLeft"),
		mk(RIGHT, SHIFT, "Shift+→", "ReplayHoldPanRight"),
		mk(UP, SHIFT, "Shift+↑", "ReplayHoldPanUp"),
		mk(DOWN, SHIFT, "Shift+↓", "ReplayHoldPanDown"),
	};
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
		/* janela "Interagir" (página web): o teclado é da página */
		if (auto *xw = qobject_cast<QWidget *>(x); xw && xw->window() && xw->window()->property("rdsTyping").toBool())
			return true;
	}
	return false;
}

bool Shortcuts::trigger(quint32 scan, int mods, int qtKey, bool keypad)
{
	mods &= MOD_MASK;
	/* "soltou e apertou" no mesmo instante = a tecla continua segurada (repetição automática) */
	if (pendingRelease.remove(scan))
		return true;
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
		handled = true;
		const FunctionDef *def = findFunction(s.function);
		if (def && def->hold) {
			/* de segurar: começa agora e para quando a tecla subir */
			bool already = false;
			for (const Held &h : held)
				already |= h.scan == scan && h.function == s.function;
			if (already)
				continue;
			Held h{scan, s.function, s.value, nullptr};
			runHoldFunction(s.function, s.value, true);
			if (s.function == "ReplayHoldJog") {
				/* como a roda de um controlador: espera um pouco e vai andando enquanto segura */
				h.repeat = new QTimer(this);
				h.repeat->setInterval(300);
				const QString fn = s.function, val = s.value;
				connect(h.repeat, &QTimer::timeout, this, [t = h.repeat, fn, val] {
					t->setInterval(40);
					runHoldFunction(fn, val, true);
				});
				h.repeat->start();
			}
			held << h;
#ifdef _WIN32
			if (!watchdog) {
				watchdog = new QTimer(this);
				watchdog->setInterval(150);
				connect(watchdog, &QTimer::timeout, this, [this] {
					QList<quint32> gone;
					for (const Held &x : held)
						if (!physicallyDown(x.scan))
							gone << x.scan;
					for (quint32 s : gone) {
						blog(LOG_INFO, "atalho de segurar: tecla já solta, parando");
						finishRelease(s);
					}
					if (held.isEmpty())
						watchdog->stop();
				});
			}
			watchdog->start();
#endif
			continue;
		}
		QString err;
		if (!runFunction(s.function, s.input, s.value, &err))
			blog(LOG_WARNING, "atalho %s: %s", s.display().toUtf8().constData(), err.toUtf8().constData());
	}
	return handled;
}

#ifdef _WIN32
/* a tecla física está apertada agora? (vale com o Num Lock ligado ou desligado) */
static bool physicallyDown(quint32 scan)
{
	const UINT sc = (scan & 0xFF) | ((scan & 0x100) ? 0xE000 : 0);
	const UINT vk = MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
	UINT alt = 0;
	switch (scan) { /* teclado numérico: com Num Lock é VK_NUMPADx */
	case 0x47: alt = VK_NUMPAD7; break;
	case 0x48: alt = VK_NUMPAD8; break;
	case 0x49: alt = VK_NUMPAD9; break;
	case 0x4B: alt = VK_NUMPAD4; break;
	case 0x4C: alt = VK_NUMPAD5; break;
	case 0x4D: alt = VK_NUMPAD6; break;
	case 0x4F: alt = VK_NUMPAD1; break;
	case 0x50: alt = VK_NUMPAD2; break;
	case 0x51: alt = VK_NUMPAD3; break;
	case 0x52: alt = VK_NUMPAD0; break;
	case 0x53: alt = VK_DECIMAL; break;
	default: break;
	}
	if (!vk && !alt)
		return true; /* sem como saber: confia no aviso de soltar */
	return ((vk ? GetAsyncKeyState(vk) : 0) | (alt ? GetAsyncKeyState(alt) : 0)) & 0x8000;
}
#endif

void Shortcuts::release(quint32 scan)
{
	bool isHeld = false;
	for (const Held &h : held)
		isHeld |= h.scan == scan;
	if (!isHeld)
		return;
	pendingRelease.insert(scan);
	QTimer::singleShot(40, this, [this, scan] {
		if (pendingRelease.remove(scan))
			finishRelease(scan);
	});
}

void Shortcuts::finishRelease(quint32 scan)
{
	for (int i = held.size() - 1; i >= 0; i--) {
		if (held[i].scan != scan)
			continue;
		Held h = held.takeAt(i);
		delete h.repeat;
		runHoldFunction(h.function, h.value, false);
	}
}

void Shortcuts::releaseAll()
{
	pendingRelease.clear();
	while (!held.isEmpty())
		finishRelease(held.first().scan);
}

bool Shortcuts::eventFilter(QObject *o, QEvent *e)
{
	/* soltou a tecla (ou o programa perdeu o foco com ela apertada): para o que é de segurar */
	if ((e->type() == QEvent::KeyRelease || e->type() == QEvent::KeyPress) && qEnvironmentVariableIsSet("RDS_DEBUG_KEYS")) {
		auto *ke = static_cast<QKeyEvent *>(e);
		blog(LOG_INFO, "[tecla] %s scan=%u repete=%d em %s", e->type() == QEvent::KeyPress ? "aperta" : "solta",
		     physicalKey(ke), ke->isAutoRepeat(), o->metaObject()->className());
	}
	if (e->type() == QEvent::KeyRelease && !held.isEmpty()) {
		auto *ke = static_cast<QKeyEvent *>(e);
		if (!ke->isAutoRepeat())
			release(physicalKey(ke));
		return false;
	}
	if (e->type() == QEvent::ApplicationDeactivate && !held.isEmpty() && !globalOn) {
		releaseAll();
		return false;
	}
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
	return trigger(physicalKey(ke), int(ke->modifiers()), key, ke->modifiers() & Qt::KeypadModifier);
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
			QMetaObject::invokeMethod(&Shortcuts::get(), [scan] { Shortcuts::get().release(scan); }, Qt::QueuedConnection);
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

#ifdef _WIN32
/* O gancho roda numa thread só dele: o Windows espera o gancho responder a CADA tecla do
 * computador; na thread da interface, um momento ocupado (abrir câmera, janela) atrasava o
 * teclado do PC inteiro. Aqui ele responde na hora, sempre. */
static std::thread g_hookThread;
static std::atomic<DWORD> g_hookThreadId{0};

static void hookThreadMain(std::promise<bool> started)
{
	g_hookThreadId = GetCurrentThreadId();
	g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboard, GetModuleHandleW(nullptr), 0);
	started.set_value(g_hook != nullptr);
	if (!g_hook)
		return;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	UnhookWindowsHookEx(g_hook);
	g_hook = nullptr;
	g_down.clear();
}
#endif

void Shortcuts::shutdown()
{
	releaseAll();
	const bool was = globalOn;
	globalOn = false;
	applyGlobalHook();
	globalOn = was; /* a escolha continua salva */
}

void Shortcuts::applyGlobalHook()
{
#ifdef _WIN32
	if (globalOn && !g_hookThread.joinable()) {
		std::promise<bool> started;
		std::future<bool> ok = started.get_future();
		g_hookThread = std::thread(hookThreadMain, std::move(started));
		const bool on = ok.get();
		if (!on)
			g_hookThread.join();
		blog(LOG_INFO, "atalhos globais %s", on ? "ligados (thread própria)" : "NÃO ligaram");
	} else if (!globalOn && g_hookThread.joinable()) {
		PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
		g_hookThread.join();
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

/* o modificador da própria tecla (no Linux ele ainda não aparece em modifiers() ao apertar) */
static int modOfKey(int key)
{
	switch (key) {
	case Qt::Key_Control:
		return Qt::ControlModifier;
	case Qt::Key_Shift:
		return Qt::ShiftModifier;
	case Qt::Key_Alt:
		return Qt::AltModifier;
	case Qt::Key_Meta:
		return Qt::MetaModifier;
	default:
		return 0;
	}
}

void KeyCapture::paint(int heldMods)
{
	/* mostra a combinação se formando: segurando Ctrl aparece "Ctrl + …" */
	QString held;
	if (heldMods & Qt::ControlModifier)
		held += "Ctrl + ";
	if (heldMods & Qt::AltModifier)
		held += "Alt + ";
	if (heldMods & Qt::ShiftModifier)
		held += "Shift + ";
	if (heldMods & Qt::MetaModifier)
		held += "Win + ";
	if (capturing)
		setText(held.isEmpty() ? QString("aperte a tecla…") : held + QString::fromUtf8("…"));
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
	{
		/* espera a tecla de verdade (travas e Ctrl/Shift/Alt sozinhos não contam) */
		if (!e->isAutoRepeat())
			paint((int(e->modifiers()) | modOfKey(key)) & MOD_MASK);
		return;
	}
	if (key == Qt::Key_Escape && !(e->modifiers() & MOD_MASK)) {
		capturing = false;
		paint();
		return;
	}
	const int mods = int(e->modifiers()) & MOD_MASK;
	const bool keypad = e->modifiers() & Qt::KeypadModifier;
	sc.scan = physicalKey(e);
	sc.mods = mods;
	sc.label = Shortcuts::keyName(sc.scan, mods, key, keypad);
	sc.key = keypad ? QKeySequence() : QKeySequence(QKeyCombination(Qt::KeyboardModifiers(mods), Qt::Key(key)));
	capturing = false;
	paint();
}

void KeyCapture::keyReleaseEvent(QKeyEvent *e)
{
	if (capturing) {
		paint(int(e->modifiers()) & ~modOfKey(e->key()) & MOD_MASK);
		return;
	}
	QPushButton::keyReleaseEvent(e);
}

bool KeyCapture::event(QEvent *e)
{
	/* capturando: qualquer combinação (até Ctrl+N, que é do menu) vem para cá */
	if (capturing && e->type() == QEvent::ShortcutOverride) {
		e->accept();
		return true;
	}
	return QPushButton::event(e);
}

void KeyCapture::focusOutEvent(QFocusEvent *e)
{
	if (capturing) {
		capturing = false;
		paint();
	}
	QPushButton::focusOutEvent(e);
}
