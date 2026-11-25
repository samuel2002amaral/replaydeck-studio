#define _USE_MATH_DEFINES
#include "i18n.hpp"
#include "gt-source.hpp"
#include "gt-render.hpp"
#include "../common.hpp"
#include <util/platform.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>

class QWidget;

/* ---------- efeito: desenha textura pré-multiplicada com opacidade ---------- */

static const char *kEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float opacity;
sampler_state def_sampler {
	Filter   = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};
struct VertInOut {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};
VertInOut VSDefault(VertInOut vert_in)
{
	VertInOut vert_out;
	vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv  = vert_in.uv;
	return vert_out;
}
float4 PSPremul(VertInOut vert_in) : TARGET
{
	return image.Sample(def_sampler, vert_in.uv) * opacity;
}
float4 PSStraight(VertInOut vert_in) : TARGET
{
	float4 c = image.Sample(def_sampler, vert_in.uv);
	if (c.a > 0.0)
		c.rgb /= c.a;
	c.a *= opacity;
	return c;
}
technique Premul
{
	pass
	{
		vertex_shader = VSDefault(vert_in);
		pixel_shader  = PSPremul(vert_in);
	}
}
technique Straight
{
	pass
	{
		vertex_shader = VSDefault(vert_in);
		pixel_shader  = PSStraight(vert_in);
	}
}
)";

static gs_effect_t *g_effect = nullptr;

static gs_effect_t *gtEffect()
{
	if (!g_effect) {
		char *err = nullptr;
		g_effect = gs_effect_create(kEffect, "instant-replay-gt.effect", &err);
		if (!g_effect)
			rlog(LOG_ERROR, "GT: efeito não compilou: %s", err ? err : "?");
		bfree(err);
	}
	return g_effect;
}

/* ---------- dados da fonte ---------- */

enum GtPlay { GT_HIDDEN = 0, GT_IN = 1, GT_SHOWN = 2, GT_OUT = 3, GT_PAGE = 4 };

struct GtItem {
	GtElement el; /* com os valores do editor aplicados */
	QImage img;
	gs_texture_t *tex = nullptr;
	bool dirty = true;
};

struct GtLayerRt {
	QRectF rect; /* com os valores do editor */
	double rotation = 0;
	bool visible = true;
	std::vector<GtItem> items;
	gs_texrender_t *tr = nullptr;
};

struct GtSource {
	obs_source_t *source;
	std::recursive_mutex mtx;
	std::shared_ptr<GtTitle> base;  /* como veio do arquivo */
	std::shared_ptr<GtTitle> title; /* com o que o editor acrescentou/apagou (ou o próprio base) */
	QString file;
	GtValues values;
	std::vector<GtLayerRt> layers;
	bool autoIn = true;

	std::atomic<int> play{GT_HIDDEN};
	uint64_t playStart = 0;
	QString page;
	std::vector<gs_texture_t *> trash; /* texturas a liberar no thread gráfico */
	std::vector<gs_texrender_t *> trashTr;
};

static void rebuildItems(GtSource *g)
{
	/* chamado com mtx travado */
	for (auto &L : g->layers) {
		for (auto &it : L.items)
			if (it.tex)
				g->trash.push_back(it.tex);
		if (L.tr)
			g->trashTr.push_back(L.tr);
	}
	g->layers.clear();
	g->title = gtStructured(g->base, g->values);
	if (!g->title)
		return;
	for (auto &L : g->title->layers) {
		GtLayerRt rt;
		rt.rect = gtLayerRect(L, g->values);
		rt.visible = gtLayerVisible(L, g->values);
		rt.rotation = gtLayerRotation(L, g->values);
		for (auto &e : L.elements) {
			GtItem it;
			it.el = gtApplyValues(e, g->values);
			if (it.el.visible)
				it.img = gtRenderElement(*g->title, it.el);
			rt.items.push_back(std::move(it));
		}
		g->layers.push_back(std::move(rt));
	}
}

static void loadFile(GtSource *g, const QString &file)
{
	auto t = std::make_shared<GtTitle>();
	QString err;
	if (file.isEmpty() || !t->load(file, err)) {
		if (!file.isEmpty())
			rlog(LOG_WARNING, "GT: %s", err.toUtf8().constData());
		t.reset();
	} else {
		rlog(LOG_INFO, "GT: título '%s' carregado (%zu camadas)", t->name.toUtf8().constData(), t->layers.size());
	}
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	g->base = t;
	g->file = file;
	rebuildItems(g);
}

static GtValues valuesFromData(obs_data_t *settings)
{
	GtValues v;
	obs_data_t *obj = obs_data_get_obj(settings, "values");
	if (!obj)
		return v;
	for (obs_data_item_t *it = obs_data_first(obj); it; obs_data_item_next(&it))
		v.insert(QString::fromUtf8(obs_data_item_get_name(it)), QString::fromUtf8(obs_data_item_get_string(it)));
	obs_data_release(obj);
	return v;
}

static void valuesToData(obs_data_t *settings, const GtValues &v)
{
	obs_data_t *obj = obs_data_create();
	for (auto it = v.begin(); it != v.end(); ++it)
		obs_data_set_string(obj, it.key().toUtf8().constData(), it.value().toUtf8().constData());
	obs_data_set_obj(settings, "values", obj);
	obs_data_release(obj);
}

/* ---------- callbacks ---------- */

static const char *gt_name(void *)
{
	return tlc("ReplayDeck GT (título .gtzip)");
}

static void gt_update(void *data, obs_data_t *settings)
{
	auto *g = static_cast<GtSource *>(data);
	const QString file = QString::fromUtf8(obs_data_get_string(settings, "file"));
	const GtValues v = valuesFromData(settings);
	bool reload;
	{
		std::lock_guard<std::recursive_mutex> lk(g->mtx);
		g->autoIn = obs_data_get_bool(settings, "auto_in");
		reload = file != g->file;
		g->values = v;
	}
	if (reload) {
		loadFile(g, file);
	} else {
		std::lock_guard<std::recursive_mutex> lk(g->mtx);
		rebuildItems(g);
	}
}

static void *gt_create(obs_data_t *settings, obs_source_t *source)
{
	auto *g = new GtSource();
	g->source = source;
	gt_update(g, settings);
	return g;
}

static void freeGpu(GtSource *g)
{
	obs_enter_graphics();
	for (auto *t : g->trash)
		gs_texture_destroy(t);
	for (auto *t : g->trashTr)
		gs_texrender_destroy(t);
	g->trash.clear();
	g->trashTr.clear();
	for (auto &L : g->layers) {
		for (auto &it : L.items)
			if (it.tex)
				gs_texture_destroy(it.tex);
		if (L.tr)
			gs_texrender_destroy(L.tr);
	}
	obs_leave_graphics();
}

static void gt_destroy(void *data)
{
	auto *g = static_cast<GtSource *>(data);
	freeGpu(g);
	delete g;
}

static void gt_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "auto_in", true);
}

static uint32_t gt_width(void *data)
{
	auto *g = static_cast<GtSource *>(data);
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->title ? (uint32_t)g->title->width : 0;
}

static uint32_t gt_height(void *data)
{
	auto *g = static_cast<GtSource *>(data);
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->title ? (uint32_t)g->title->height : 0;
}

static void startPlay(GtSource *g, int state, const QString &page = QString())
{
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	if (!g->title) {
		g->play = GT_HIDDEN; /* sem título carregado não há o que pôr no ar */
		return;
	}
	g->page = page;
	g->playStart = os_gettime_ns();
	g->play = state;
}

static void gt_activate(void *data)
{
	auto *g = static_cast<GtSource *>(data);
	if (g->autoIn)
		startPlay(g, GT_IN);
}

static void gt_deactivate(void *data)
{
	auto *g = static_cast<GtSource *>(data);
	if (g->autoIn)
		g->play = GT_HIDDEN;
}

/* ---------- animação ---------- */

struct GtXform {
	double dx = 0, dy = 0, scale = 1, opacity = 1, rotation = 0;
	bool hidden = false;
};

static const GtAnim *findAnim(const GtStoryboard *sb, const QString &name)
{
	if (!sb || name.isEmpty())
		return nullptr;
	for (auto &a : sb->anims)
		if (a.object == name)
			return &a;
	return nullptr;
}

/* inOut: true = entrando (vai do efeito ao estado final); false = saindo */
static GtXform animXform(const GtAnim *a, double t, bool entering, const QRectF &r, const QSizeF &parent)
{
	GtXform x;
	if (!a)
		return x;
	double p = a->duration > 0 ? (t - a->delay) / a->duration : (t >= a->delay ? 1 : 0);
	p = gtEase(a->interpolation, std::clamp(p, 0.0, 1.0));
	const double k = entering ? 1 - p : p; /* quanto do efeito está aplicado */

	const QString type = a->type;
	if (type == "Fly" || type == "Slide" || type == "FlyFade") {
		const QString d = a->direction.isEmpty() ? QString("Left") : a->direction;
		double ox = 0, oy = 0;
		if (d == "Left")
			ox = -(r.x() + r.width());
		else if (d == "Right")
			ox = parent.width() - r.x();
		else if (d == "Top")
			oy = -(r.y() + r.height());
		else if (d == "Bottom")
			oy = parent.height() - r.y();
		x.dx = ox * k;
		x.dy = oy * k;
		if (type == "FlyFade")
			x.opacity = 1 - k;
	} else if (type == "Zoom") {
		x.scale = 1 - k;
	} else if (type == "ZoomFade") {
		x.scale = 1 - k;
		x.opacity = 1 - k;
	} else {
		/* Fade e tipos desconhecidos: esmaecer */
		x.opacity = 1 - k;
	}
	if (x.scale <= 0.0001 || x.opacity <= 0.0001)
		x.hidden = true;
	return x;
}

static void drawTex(gs_texture_t *tex, gs_effect_t *eff, const char *tech, double x, double y, double w, double h,
		    const GtXform &xf)
{
	gs_effect_set_texture(gs_effect_get_param_by_name(eff, "image"), tex);
	gs_effect_set_float(gs_effect_get_param_by_name(eff, "opacity"), (float)xf.opacity);
	gs_matrix_push();
	gs_matrix_translate3f((float)(x + xf.dx + w / 2), (float)(y + xf.dy + h / 2), 0);
	if (xf.rotation != 0)
		gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, (float)(xf.rotation * M_PI / 180.0));
	gs_matrix_scale3f((float)xf.scale, (float)xf.scale, 1);
	gs_matrix_translate3f((float)(-w / 2), (float)(-h / 2), 0);
	gs_technique_t *t = gs_effect_get_technique(eff, tech);
	const size_t passes = gs_technique_begin(t);
	for (size_t i = 0; i < passes; i++) {
		if (gs_technique_begin_pass(t, i)) {
			gs_draw_sprite(tex, 0, (uint32_t)std::lround(w), (uint32_t)std::lround(h));
			gs_technique_end_pass(t);
		}
	}
	gs_technique_end(t);
	gs_matrix_pop();
}

static void gt_render(void *data, gs_effect_t *)
{
	auto *g = static_cast<GtSource *>(data);
	gs_effect_t *eff = gtEffect();
	if (!eff)
		return;
	std::lock_guard<std::recursive_mutex> lk(g->mtx);

	for (auto *t : g->trash)
		gs_texture_destroy(t);
	for (auto *t : g->trashTr)
		gs_texrender_destroy(t);
	g->trash.clear();
	g->trashTr.clear();

	if (!g->title)
		return;
	GtTitle &T = *g->title;

	/* fora do programa (prévia do Modo Estúdio, propriedades): mostra parado */
	int play = g->play.load();
	const bool active = obs_source_active(g->source);
	if (!active && g->autoIn)
		play = GT_SHOWN;
	if (play == GT_HIDDEN)
		return;

	const double t = (double)(os_gettime_ns() - g->playStart) / 1e9;
	const GtStoryboard *sb = nullptr;
	bool entering = true;
	if (play == GT_IN) {
		auto it = T.storyboards.find("TransitionIn");
		sb = it != T.storyboards.end() ? &it->second : nullptr;
		if (!sb || t > sb->length()) {
			g->play = GT_SHOWN;
			sb = nullptr;
		}
	} else if (play == GT_OUT) {
		auto it = T.storyboards.find("TransitionOut");
		sb = it != T.storyboards.end() ? &it->second : nullptr;
		entering = false;
		if (!sb || t > sb->length()) {
			g->play = GT_HIDDEN;
			return;
		}
	} else if (play == GT_PAGE) {
		auto it = T.storyboards.find(g->page);
		sb = it != T.storyboards.end() ? &it->second : nullptr;
		if (!sb || t > sb->length()) {
			g->play = GT_SHOWN;
			sb = nullptr;
		}
	}

	gs_blend_state_push();
	for (size_t li = 0; li < T.layers.size() && li < g->layers.size(); li++) {
		const GtLayer &L = T.layers[li];
		GtLayerRt &rt = g->layers[li];
		if (!rt.visible)
			continue;
		const QRectF LR = rt.rect;
		const int lw = std::max(1, (int)std::ceil(LR.width()));
		const int lh = std::max(1, (int)std::ceil(LR.height()));

		/* texturas novas ou alteradas */
		for (auto &it : rt.items) {
			if (!it.dirty)
				continue;
			if (it.tex)
				gs_texture_destroy(it.tex);
			it.tex = nullptr;
			if (!it.img.isNull()) {
				const uint8_t *bits = it.img.constBits();
				if (it.img.bytesPerLine() == it.img.width() * 4)
					it.tex = gs_texture_create((uint32_t)it.img.width(), (uint32_t)it.img.height(), GS_RGBA, 1,
								   &bits, 0);
			}
			it.dirty = false;
		}

		if (!rt.tr)
			rt.tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		gs_texrender_reset(rt.tr);
		if (!gs_texrender_begin(rt.tr, (uint32_t)lw, (uint32_t)lh))
			continue;
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)lw, 0.0f, (float)lh, -100.0f, 100.0f);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		for (auto &it : rt.items) {
			if (!it.tex || !it.el.visible)
				continue;
			GtXform xf = animXform(findAnim(sb, it.el.name), t, entering, it.el.rect, QSizeF(LR.size()));
			xf.opacity *= it.el.opacity;
			xf.rotation = it.el.rotation;
			if (xf.hidden)
				continue;
			drawTex(it.tex, eff, "Premul", it.el.rect.x(), it.el.rect.y(), it.img.width(), it.img.height(), xf);
		}
		gs_texrender_end(rt.tr);

		/* camada no título (a animação pode ser da camada inteira) */
		GtXform lx = animXform(findAnim(sb, L.name), t, entering, LR, QSizeF(T.width, T.height));
		if (lx.hidden)
			continue;
		lx.rotation = rt.rotation;
		gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
		drawTex(gs_texrender_get_texture(rt.tr), eff, "Straight", LR.x(), LR.y(), lw, lh, lx);
	}
	gs_blend_state_pop();
}

static void gt_tick(void *, float) {}

/* ---------- propriedades ---------- */

static bool btnIn(obs_properties_t *, obs_property_t *, void *data)
{
	startPlay(static_cast<GtSource *>(data), GT_IN);
	return false;
}

static bool btnOut(obs_properties_t *, obs_property_t *, void *data)
{
	startPlay(static_cast<GtSource *>(data), GT_OUT);
	return false;
}

static bool btnEdit(obs_properties_t *, obs_property_t *, void *data)
{
	gtOpenEditor(static_cast<GtSource *>(data)->source, nullptr);
	return false;
}

static obs_properties_t *gt_properties(void *)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_path(p, "file", tlc("Arquivo do título"), OBS_PATH_FILE, tlc("Títulos GT (*.gtzip)"), nullptr);
	obs_properties_add_bool(p, "auto_in", tlc("Animar a entrada quando for ao ar (e mostrar parado na prévia)"));
	obs_properties_add_button(p, "btn_in", tlc("▶ ENTRADA"), btnIn);
	obs_properties_add_button(p, "btn_out", tlc("← SAÍDA"), btnOut);
	obs_properties_add_button(p, "btn_edit", tlc("✎ Editar título (Texto / Imagens / Cor)"), btnEdit);
	return p;
}

void registerGtSource()
{
	struct obs_source_info info = {};
	info.id = GT_SOURCE_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = gt_name;
	info.create = gt_create;
	info.destroy = gt_destroy;
	info.update = gt_update;
	info.get_defaults = gt_defaults;
	info.get_width = gt_width;
	info.get_height = gt_height;
	info.video_render = gt_render;
	info.video_tick = gt_tick;
	info.activate = gt_activate;
	info.deactivate = gt_deactivate;
	info.get_properties = gt_properties;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

/* ---------- API para o painel / overlays ---------- */

static GtSource *gtData(obs_source_t *src)
{
	if (!src || strcmp(obs_source_get_id(src), GT_SOURCE_ID) != 0)
		return nullptr;
	return static_cast<GtSource *>(obs_obj_get_data(src));
}

bool gtIsSource(obs_source_t *src)
{
	return gtData(src) != nullptr;
}

void gtPlayIn(obs_source_t *src)
{
	if (GtSource *g = gtData(src))
		startPlay(g, GT_IN);
}

void gtPlayOut(obs_source_t *src)
{
	if (GtSource *g = gtData(src)) {
		const int s = g->play.load();
		if (s != GT_HIDDEN && s != GT_OUT)
			startPlay(g, GT_OUT);
	}
}

void gtToggle(obs_source_t *src)
{
	if (GtSource *g = gtData(src)) {
		const int s = g->play.load();
		startPlay(g, (s == GT_HIDDEN || s == GT_OUT) ? GT_IN : GT_OUT);
	}
}

void gtPlayPage(obs_source_t *src, const QString &page)
{
	if (GtSource *g = gtData(src))
		startPlay(g, GT_PAGE, page);
}

int gtState(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return 0;
	const int s = g->play.load();
	return s == GT_PAGE ? GT_SHOWN : s;
}

QString gtTitleName(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return QString();
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->title ? g->title->name : QString();
}

QString gtFile(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return QString();
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->file;
}

QStringList gtPages(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return {};
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->title ? g->title->pages() : QStringList();
}

QMap<QString, QString> gtValues(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return {};
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->values;
}

void gtSetValues(obs_source_t *src, const QMap<QString, QString> &values)
{
	GtSource *g = gtData(src);
	if (!g)
		return;
	{
		std::lock_guard<std::recursive_mutex> lk(g->mtx);
		g->values = values;
		rebuildItems(g);
	}
	/* guarda nas configurações sem recarregar o arquivo */
	obs_data_t *st = obs_source_get_settings(src);
	valuesToData(st, values);
	obs_data_release(st);
}

std::shared_ptr<GtTitle> gtBaseTitleOf(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return nullptr;
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->base;
}

std::shared_ptr<GtTitle> gtTitleOf(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return nullptr;
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	return g->title;
}

int gtInLengthMs(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return 0;
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	if (!g->title)
		return 0;
	auto it = g->title->storyboards.find("TransitionIn");
	return it == g->title->storyboards.end() ? 0 : (int)std::ceil(it->second.length() * 1000.0);
}

int gtOutLengthMs(obs_source_t *src)
{
	GtSource *g = gtData(src);
	if (!g)
		return 0;
	std::lock_guard<std::recursive_mutex> lk(g->mtx);
	if (!g->title)
		return 0;
	auto it = g->title->storyboards.find("TransitionOut");
	return it == g->title->storyboards.end() ? 0 : (int)std::ceil(it->second.length() * 1000.0);
}
