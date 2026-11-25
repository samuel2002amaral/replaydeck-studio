#include "thumbs.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QApplication>

#include <util/platform.h>
#include <algorithm>
#include <cstring>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace {

struct Entry {
	obs_weak_source_t *weak = nullptr;
	int wantW = 214, wantH = 120; /* pedido pela interface */
	int w = 0, h = 0;             /* dos recursos atuais */
	std::atomic<bool> active{false};
	/* só a thread de vídeo mexe daqui para baixo */
	gs_texrender_t *tr = nullptr;
	gs_stagesurf_t *stage[2] = {nullptr, nullptr};
	bool staged[2] = {false, false};
	int cur = 0;
};

struct Hub {
	std::mutex m;
	std::map<ThumbView *, std::shared_ptr<Entry>> entries;
	std::vector<std::shared_ptr<Entry>> trash; /* recursos da placa: só se apagam na thread de vídeo */
	std::atomic<int> fps{10};
	uint64_t lastBatch = 0;
	bool installed = false;
	QObject *ui = nullptr; /* para mandar as imagens de volta à interface */
};

Hub &hub()
{
	static Hub h;
	return h;
}

void freeGpu(Entry &e)
{
	if (e.tr)
		gs_texrender_destroy(e.tr);
	for (auto *s : e.stage)
		if (s)
			gs_stagesurface_destroy(s);
	e.tr = nullptr;
	e.stage[0] = e.stage[1] = nullptr;
}

/* thread de vídeo, logo depois de desenhar o programa */
void rendered(void *)
{
	Hub &H = hub();
	const uint64_t now = os_gettime_ns();
	const uint64_t every = 1000000000ULL / (uint64_t)std::max(1, H.fps.load());
	if (now - H.lastBatch < every)
		return;
	H.lastBatch = now;

	std::lock_guard<std::mutex> lock(H.m); /* poucos ms a cada 100 ms; a interface só espera nisso */
	for (auto &t : H.trash)
		freeGpu(*t);
	H.trash.clear();
	if (H.entries.empty())
		return;
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;
	const float cw = (float)ovi.base_width, ch = (float)ovi.base_height;

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	for (auto &kv : H.entries) {
		Entry &e = *kv.second;
		if (e.w != e.wantW || e.h != e.wantH) {
			freeGpu(e);
			e.staged[0] = e.staged[1] = false;
			e.w = e.wantW;
			e.h = e.wantH;
		}
		/* 1) a imagem pedida na passada anterior já chegou: entrega (sem esperar a placa) */
		const int prev = e.cur ^ 1;
		if (e.staged[prev] && e.stage[prev]) {
			uint8_t *data = nullptr;
			uint32_t line = 0;
			if (gs_stagesurface_map(e.stage[prev], &data, &line)) {
				QImage img(e.w, e.h, QImage::Format_ARGB32);
				for (int y = 0; y < e.h; y++)
					memcpy(img.scanLine(y), data + (size_t)y * line, (size_t)e.w * 4);
				gs_stagesurface_unmap(e.stage[prev]);
				ThumbView *view = kv.first;
				QMetaObject::invokeMethod(
					H.ui,
					[view, img = std::move(img)]() mutable {
						std::lock_guard<std::mutex> l(hub().m);
						if (hub().entries.count(view))
							view->setImage(std::move(img));
					},
					Qt::QueuedConnection);
			}
			e.staged[prev] = false;
		}
		if (!e.active.load() || !e.weak)
			continue;
		obs_source_t *src = obs_weak_source_get_source(e.weak);
		if (!src)
			continue;
		/* 2) desenha pequena e pede a cópia para a próxima passada */
		if (!e.tr) {
			e.tr = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
			e.stage[0] = gs_stagesurface_create((uint32_t)e.w, (uint32_t)e.h, GS_BGRA);
			e.stage[1] = gs_stagesurface_create((uint32_t)e.w, (uint32_t)e.h, GS_BGRA);
		}
		gs_texrender_reset(e.tr);
		if (gs_texrender_begin(e.tr, (uint32_t)e.w, (uint32_t)e.h)) {
			struct vec4 black;
			vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
			gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
			/* cabe sem esticar (tela vertical também) */
			const float s = std::min(e.w / cw, e.h / ch);
			const int vw = (int)(cw * s + 0.5f), vh = (int)(ch * s + 0.5f);
			gs_set_viewport((e.w - vw) / 2, (e.h - vh) / 2, vw, vh);
			gs_ortho(0.0f, cw, 0.0f, ch, -100.0f, 100.0f);
			obs_source_video_render(src);
			gs_texrender_end(e.tr);
			gs_stage_texture(e.stage[e.cur], gs_texrender_get_texture(e.tr));
			e.staged[e.cur] = true;
			e.cur ^= 1;
		}
		obs_source_release(src);
	}
	gs_blend_state_pop();
}

void install()
{
	Hub &H = hub();
	if (H.installed)
		return;
	H.installed = true;
	H.ui = new QObject(qApp);
	obs_add_main_rendered_callback(rendered, nullptr);
}

} // namespace

ThumbView::ThumbView(QWidget *parent) : QWidget(parent)
{
	install();
	setAttribute(Qt::WA_OpaquePaintEvent);
	auto e = std::make_shared<Entry>();
	{
		std::lock_guard<std::mutex> l(hub().m);
		hub().entries[this] = e;
	}
	gate = new QTimer(this);
	connect(gate, &QTimer::timeout, this, &ThumbView::updateActive);
	gate->start(250);
}

ThumbView::~ThumbView()
{
	std::lock_guard<std::mutex> l(hub().m);
	auto it = hub().entries.find(this);
	if (it != hub().entries.end()) {
		it->second->active = false;
		obs_weak_source_release(it->second->weak);
		it->second->weak = nullptr;
		hub().trash.push_back(it->second);
		hub().entries.erase(it);
	}
}

void ThumbView::showSource(obs_source_t *source)
{
	{
	std::lock_guard<std::mutex> l(hub().m);
	auto &e = hub().entries[this];
	obs_weak_source_release(e->weak);
	e->weak = source ? obs_source_get_weak_source(source) : nullptr;
	/* tamanho da miniatura em pixels da tela (nítida, mas pequena) */
	const qreal dpr = std::min<qreal>(devicePixelRatioF(), 1.5);
	e->wantW = std::max(16, (int)(width() * dpr));
	e->wantH = std::max(9, (int)(height() * dpr));
	}
	hasSource = source != nullptr;
	updateActive();
}

void ThumbView::clear()
{
	{
		std::lock_guard<std::mutex> l(hub().m);
		auto &e = hub().entries[this];
		e->active = false;
		obs_weak_source_release(e->weak);
		e->weak = nullptr;
	}
	hasSource = false;
	img = QImage();
	update();
}

void ThumbView::updateActive()
{
	QWidget *top = window();
	const bool on = hasSource && isVisible() && top && !top->isMinimized() && !visibleRegion().isEmpty();
	std::lock_guard<std::mutex> l(hub().m);
	auto it = hub().entries.find(this);
	if (it != hub().entries.end())
		it->second->active = on;
}

void ThumbView::setImage(QImage i)
{
	img = std::move(i);
	update();
}

void ThumbView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	if (img.isNull()) {
		p.fillRect(rect(), Qt::black);
		return;
	}
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.drawImage(rect(), img);
}

void ThumbView::mousePressEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton)
		emit clicked();
	QWidget::mousePressEvent(e);
}

void ThumbView::mouseDoubleClickEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton)
		emit doubleClicked();
	QWidget::mouseDoubleClickEvent(e);
}

void ThumbView::setFps(int f)
{
	hub().fps = std::clamp(f, 1, 30);
}

int ThumbView::fps()
{
	return hub().fps.load();
}

void ThumbView::shutdown()
{
	Hub &H = hub();
	if (!H.installed)
		return;
	obs_remove_main_rendered_callback(rendered, nullptr);
	H.installed = false;
	obs_enter_graphics();
	{
		std::lock_guard<std::mutex> l(H.m);
		for (auto &t : H.trash)
			freeGpu(*t);
		H.trash.clear();
		for (auto &kv : H.entries)
			freeGpu(*kv.second);
	}
	obs_leave_graphics();
}
