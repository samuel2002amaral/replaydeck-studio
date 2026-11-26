#include "video-display.hpp"

#include <graphics/matrix4.h>

#include <QMouseEvent>
#include <QApplication>
#include <QTimer>

#include <algorithm>
#include <QWindow>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

/* Toda tela de vídeo custa um desenho por quadro na placa de vídeo. As que não aparecem
 * (aba escondida, entrada rolada para fora da lista, janela minimizada, entrada minimizada em
 * barra) param de desenhar — o que vai ao ar não muda nada. */
static QList<VideoDisplay *> g_displays;
static QTimer *g_visTimer = nullptr;

void VideoDisplay::checkAll()
{
	for (VideoDisplay *d : g_displays)
		d->updateEnabled();
}

void VideoDisplay::updateEnabled()
{
	QWidget *top = window();
	static const bool always = qEnvironmentVariableIsSet("RDS_NO_GATE"); /* diagnóstico: comparar */
	const bool exposed = top && top->windowHandle() && top->windowHandle()->isExposed();
	const bool on = always || (isVisible() && exposed && !top->isMinimized() && !visibleRegion().isEmpty());
	if (on == drawing && display)
		return;
	drawing = on;
	if (display)
		obs_display_set_enabled(display, on);
	static const bool dbg = qEnvironmentVariableIsSet("RDS_DEBUG_GATE");
	if (dbg)
		blog(LOG_INFO, "[tela] %p %s", (void *)this, on ? "desenha" : "parada");
}

VideoDisplay::VideoDisplay(QWidget *parent) : QWidget(parent)
{
	g_displays.append(this);
	if (!g_visTimer) {
		g_visTimer = new QTimer(qApp);
		QObject::connect(g_visTimer, &QTimer::timeout, &VideoDisplay::checkAll);
		g_visTimer->start(250);
	}
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumSize(32, 18);
}

VideoDisplay::~VideoDisplay()
{
	g_displays.removeOne(this);
	destroyDisplay();
}

void VideoDisplay::destroyDisplay()
{
	if (display) {
		obs_display_remove_draw_callback(display, draw, this);
		obs_display_destroy(display);
		display = nullptr;
	}
}

static QSize pixelSize(QWidget *w)
{
	const qreal r = w->devicePixelRatioF();
	return QSize(qMax(1, int(w->width() * r)), qMax(1, int(w->height() * r)));
}

void VideoDisplay::createDisplay()
{
	if (display || !windowHandle() || !windowHandle()->isExposed())
		return;
	const QSize size = pixelSize(this);
	gs_init_data info = {};
	info.cx = (uint32_t)size.width();
	info.cy = (uint32_t)size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
#ifdef _WIN32
	info.window.hwnd = (HWND)windowHandle()->winId();
#elif defined(__APPLE__)
	info.window.view = (id)windowHandle()->winId();
#else
	if (obs_get_nix_platform() != OBS_NIX_PLATFORM_X11_EGL)
		return;
	info.window.id = (uint32_t)windowHandle()->winId();
	info.window.display = obs_get_nix_platform_display();
#endif
	/* fora da imagem: a cor do painel (#1c212a, em 0xAABBGGRR); preto só onde é vídeo */
	display = obs_display_create(&info, background);
	if (display) {
		obs_display_add_draw_callback(display, draw, this);
		drawing = true;
		updateEnabled();
	}
}

void VideoDisplay::paintEvent(QPaintEvent *)
{
	createDisplay();
}

bool VideoDisplay::event(QEvent *e)
{
	/* a janela nativa foi recriada (mudou de tela cheia para janela, bordas, outro pai):
	 * o vídeo estava preso à janela antiga; liga de novo na nova */
	if (e->type() == QEvent::WinIdChange) {
		destroyDisplay();
		QTimer::singleShot(0, this, [this] { createDisplay(); });
	}
	return QWidget::event(e);
}

void VideoDisplay::setBackgroundColor(uint32_t abgr)
{
	background = abgr;
	if (display)
		obs_display_set_background_color(display, abgr);
}

void VideoDisplay::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	/* mudou de monitor (outra escala: 100%, 150%): o tamanho em pixels muda sem mudar o da janela */
	if (!screenHooked && window()->windowHandle()) {
		screenHooked = true;
		connect(window()->windowHandle(), &QWindow::screenChanged, this, [this] {
			createDisplay();
			if (display) {
				const QSize s = pixelSize(this);
				obs_display_resize(display, (uint32_t)s.width(), (uint32_t)s.height());
				obs_display_update_color_space(display);
			}
		});
	}
	createDisplay();
	updateEnabled();
}

void VideoDisplay::hideEvent(QHideEvent *e)
{
	QWidget::hideEvent(e);
	if (display && drawing) {
		drawing = false;
		obs_display_set_enabled(display, false);
	}
}

void VideoDisplay::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	createDisplay();
	if (display) {
		const QSize s = pixelSize(this);
		obs_display_resize(display, (uint32_t)s.width(), (uint32_t)s.height());
	}
}

void VideoDisplay::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton)
		emit clicked();
	QWidget::mousePressEvent(e);
}

void VideoDisplay::mouseDoubleClickEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton)
		emit doubleClicked();
	QWidget::mouseDoubleClickEvent(e);
}

float VideoDisplay::imageScale(float imgW, float imgH) const
{
	if (imgW < 1 || imgH < 1)
		return 1.0f;
	return std::min((float)width() / imgW, (float)height() / imgH);
}

QPointF VideoDisplay::toImage(const QPointF &p, float imgW, float imgH) const
{
	const float s = imageScale(imgW, imgH);
	const float ox = ((float)width() - imgW * s) / 2.0f, oy = ((float)height() - imgH * s) / 2.0f;
	return QPointF((p.x() - ox) / s, (p.y() - oy) / s);
}

void VideoDisplay::showSource(obs_source_t *src)
{
	std::lock_guard<std::mutex> l(m);
	weak = obs_source_get_weak_source(src);
	mode = Mode::Source;
	hasContent = src != nullptr;
}

void VideoDisplay::showProgram()
{
	std::lock_guard<std::mutex> l(m);
	weak = nullptr;
	mode = Mode::Program;
	hasContent = true;
}

void VideoDisplay::clear()
{
	std::lock_guard<std::mutex> l(m);
	weak = nullptr;
	mode = Mode::Source;
	hasContent = false;
}

/* desenha a imagem 16:9 centralizada, com tarja preta onde sobrar (thread de vídeo) */
void VideoDisplay::draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<VideoDisplay *>(data);
	std::lock_guard<std::mutex> l(self->m);
	if (!self->hasContent)
		return;

	OBSSourceAutoRelease src;
	float bw, bh;
	if (self->mode == Mode::Program) {
		obs_video_info ovi;
		if (!obs_get_video_info(&ovi))
			return;
		bw = (float)ovi.base_width;
		bh = (float)ovi.base_height;
	} else {
		/* uma fonte é desenhada no tamanho dela (câmera 640x480, 1280x720...) e encaixada na tela */
		src = obs_weak_source_get_source(self->weak);
		if (!src)
			return;
		bw = self->forced.isValid() ? (float)self->forced.width() : (float)obs_source_get_width(src);
		bh = self->forced.isValid() ? (float)self->forced.height() : (float)obs_source_get_height(src);
		if (bw < 1.0f || bh < 1.0f)
			return;
	}
	const float scale = std::min((float)cx / bw, (float)cy / bh);
	const int w = (int)(bw * scale), h = (int)(bh * scale);
	const int x = ((int)cx - w) / 2, y = ((int)cy - h) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(x, y, w, h);
	gs_ortho(0.0f, bw, 0.0f, bh, -100.0f, 100.0f);

	/* a área da imagem é preta (é o que vai ao ar); o resto fica com a cor da página */
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	struct vec4 black;
	vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &black);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, (uint32_t)bw, (uint32_t)bh);

	if (self->mode == Mode::Program)
		obs_render_main_texture();
	else
		obs_source_video_render(src);
	if (self->overlay)
		self->overlay(bw, bh, bw / (float)std::max(1, w));

	gs_projection_pop();
	gs_viewport_pop();
}
