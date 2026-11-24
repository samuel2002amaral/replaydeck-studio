#include "interact-window.hpp"
#include "video-display.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPointer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

bool InteractWindow::canInteract(obs_source_t *source)
{
	return source && (obs_source_get_output_flags(source) & OBS_SOURCE_INTERACTION);
}

void InteractWindow::open(obs_source_t *source, const QString &title, QWidget *parent)
{
	/* uma janela por entrada: abrir de novo traz a mesma para a frente */
	static QMap<obs_source_t *, QPointer<InteractWindow>> open;
	QPointer<InteractWindow> &w = open[source];
	if (!w) {
		w = new InteractWindow(source, title, parent);
		w->setAttribute(Qt::WA_DeleteOnClose);
	}
	w->show();
	w->raise();
	w->activateWindow();
}

InteractWindow::InteractWindow(obs_source_t *source, const QString &title, QWidget *parent)
	: QWidget(parent, Qt::Window), weak(obs_source_get_weak_source(source))
{
	setWindowTitle("Interagir · " + title);
	setProperty("rdsTyping", true); /* atalhos do programa não disparam enquanto digita aqui */
	const uint32_t sw = std::max(1u, obs_source_get_width(source)), sh = std::max(1u, obs_source_get_height(source));
	/* tamanho inicial: a página em até 1280 de largura */
	const double k = std::min(1.0, 1280.0 / sw);
	resize((int)(sw * k), (int)(sh * k) + 28);
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	view = new VideoDisplay(this);
	view->showSource(source);
	view->setMouseTracking(true);
	view->setFocusPolicy(Qt::StrongFocus);
	view->installEventFilter(this);
	v->addWidget(view, 1);
	auto *hint = new QLabel("  Mouse e teclado vão para a página. O que você faz aqui aparece no ar se a entrada estiver no ar.");
	hint->setObjectName("muted");
	hint->setFixedHeight(28);
	v->addWidget(hint);
	view->setFocus();
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	if (src)
		obs_source_send_focus(src, true);
}

/* posição na janela -> pixel da página (a imagem é encaixada no meio, com faixas) */
bool InteractWindow::toSource(const QPointF &p, int *x, int *y) const
{
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	if (!src)
		return false;
	const double sw = obs_source_get_width(src), sh = obs_source_get_height(src);
	if (sw < 1 || sh < 1 || view->width() < 1 || view->height() < 1)
		return false;
	const double scale = std::min(view->width() / sw, view->height() / sh);
	const double ox = (view->width() - sw * scale) / 2.0, oy = (view->height() - sh * scale) / 2.0;
	*x = (int)std::clamp((p.x() - ox) / scale, 0.0, sw - 1);
	*y = (int)std::clamp((p.y() - oy) / scale, 0.0, sh - 1);
	return true;
}

static uint32_t mods(Qt::KeyboardModifiers m, Qt::MouseButtons b = Qt::NoButton)
{
	uint32_t r = 0;
	if (m & Qt::ShiftModifier)
		r |= INTERACT_SHIFT_KEY;
	if (m & Qt::ControlModifier)
		r |= INTERACT_CONTROL_KEY;
	if (m & Qt::AltModifier)
		r |= INTERACT_ALT_KEY;
	if (m & Qt::MetaModifier)
		r |= INTERACT_COMMAND_KEY;
	if (b & Qt::LeftButton)
		r |= INTERACT_MOUSE_LEFT;
	if (b & Qt::MiddleButton)
		r |= INTERACT_MOUSE_MIDDLE;
	if (b & Qt::RightButton)
		r |= INTERACT_MOUSE_RIGHT;
	return r;
}

bool InteractWindow::eventFilter(QObject *o, QEvent *e)
{
	if (o != view)
		return QWidget::eventFilter(o, e);
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	if (!src) {
		if (e->type() == QEvent::MouseButtonPress)
			close();
		return false;
	}
	switch (e->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick: {
		auto *me = static_cast<QMouseEvent *>(e);
		obs_mouse_event ev = {};
		if (!toSource(me->position(), &ev.x, &ev.y))
			return true;
		ev.modifiers = mods(me->modifiers(), me->buttons());
		obs_mouse_button_type btn = MOUSE_LEFT;
		if (me->button() == Qt::RightButton)
			btn = MOUSE_RIGHT;
		else if (me->button() == Qt::MiddleButton)
			btn = MOUSE_MIDDLE;
		const bool up = e->type() == QEvent::MouseButtonRelease;
		const int clicks = e->type() == QEvent::MouseButtonDblClick ? 2 : 1;
		view->setFocus();
		obs_source_send_mouse_click(src, &ev, btn, up, clicks);
		return true;
	}
	case QEvent::MouseMove: {
		auto *me = static_cast<QMouseEvent *>(e);
		obs_mouse_event ev = {};
		if (toSource(me->position(), &ev.x, &ev.y)) {
			ev.modifiers = mods(me->modifiers(), me->buttons());
			obs_source_send_mouse_move(src, &ev, false);
		}
		return true;
	}
	case QEvent::Leave: {
		obs_mouse_event ev = {};
		obs_source_send_mouse_move(src, &ev, true);
		return false;
	}
	case QEvent::Wheel: {
		auto *we = static_cast<QWheelEvent *>(e);
		obs_mouse_event ev = {};
		if (toSource(we->position(), &ev.x, &ev.y)) {
			ev.modifiers = mods(we->modifiers(), we->buttons());
			const QPoint d = we->angleDelta();
			obs_source_send_mouse_wheel(src, &ev, d.x(), d.y());
		}
		return true;
	}
	case QEvent::FocusIn:
	case QEvent::FocusOut:
		obs_source_send_focus(src, e->type() == QEvent::FocusIn);
		return false;
	case QEvent::KeyPress:
	case QEvent::KeyRelease: {
		auto *ke = static_cast<QKeyEvent *>(e);
		obs_key_event ev = {};
		const QByteArray text = ke->text().toUtf8();
		ev.modifiers = mods(ke->modifiers());
		ev.text = (char *)text.constData();
		ev.native_modifiers = ke->nativeModifiers();
		ev.native_scancode = ke->nativeScanCode();
		ev.native_vkey = ke->nativeVirtualKey();
		obs_source_send_key_click(src, &ev, e->type() == QEvent::KeyRelease);
		return true;
	}
	case QEvent::ShortcutOverride:
		e->accept(); /* Ctrl+C, Tab, setas... vão para a página, não para os atalhos */
		return true;
	default:
		return false;
	}
}
