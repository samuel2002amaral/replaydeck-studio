/* Transição "Merge" (como a do vMix) e a entrada "Programa (PGM)".
 *
 * Merge: o que existe nas duas entradas se MOVE para o lugar novo (posição, tamanho e corte),
 * em vez de sumir e aparecer. Ex.: CAM 1 em tela cheia -> entrada com a CAM 1 como camada
 * pequena no canto: a CAM 1 encolhe até o canto. O que só existe numa delas entra/sai suave.
 *
 * Cada entrada é uma cena com a imagem principal e as camadas (outras entradas). A "identidade"
 * de cada pedaço é a entrada que ele mostra: a imagem principal vale como a própria entrada, e
 * uma camada vale como a entrada que ela traz. Assim a CAM 1 principal casa com a camada CAM 1.
 *
 * Programa (PGM): mostra a imagem que está no ar, para usar como camada (multiview, VAR...).
 * Usa a cópia do quadro anterior: nunca lê a imagem enquanto ela está sendo desenhada. */
#include "merge-transition.hpp"

#include <obs.h>
#include <graphics/matrix4.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

/* ================================ Merge ================================ */

static const char *kMergeEffect = R"(
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
float4 PSDraw(VertInOut vert_in) : TARGET
{
	return image.Sample(def_sampler, vert_in.uv) * opacity;
}
technique Draw
{
	pass
	{
		vertex_shader = VSDefault(vert_in);
		pixel_shader  = PSDraw(vert_in);
	}
}
)";

namespace {

struct Elem {
	obs_source_t *key = nullptr; /* entrada que este pedaço mostra */
	obs_source_t *src = nullptr; /* o que desenhar */
	struct matrix4 n;            /* quadrado 0..1 (já cortado) -> tela */
	float cl = 0, ct = 0, cr = 0, cb = 0; /* corte em fração do tamanho */
	uint32_t w = 0, h = 0;
	bool used = false;
};

struct Merge {
	obs_source_t *source = nullptr;
	gs_effect_t *fx = nullptr;
	gs_eparam_t *pImage = nullptr, *pOpacity = nullptr;
	std::vector<gs_texrender_t *> pool;
	size_t poolUsed = 0;
};

} // namespace

static void collect(obs_source_t *sceneSrc, std::vector<Elem> &out)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	if (!scene)
		return;
	struct Ctx {
		obs_source_t *sceneSrc;
		std::vector<Elem> *out;
	} ctx{sceneSrc, &out};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			auto *c = static_cast<Ctx *>(p);
			if (!obs_sceneitem_visible(item))
				return true;
			obs_source_t *s = obs_sceneitem_get_source(item);
			const uint32_t w = obs_source_get_width(s), h = obs_source_get_height(s);
			if (!w || !h)
				return true;
			struct obs_sceneitem_crop crop;
			obs_sceneitem_get_crop(item, &crop);
			const float cw = (float)std::max(1, (int)w - crop.left - crop.right);
			const float ch = (float)std::max(1, (int)h - crop.top - crop.bottom);
			Elem e;
			e.src = s;
			e.key = obs_scene_from_source(s) ? s : c->sceneSrc; /* camada = a entrada dela; principal = esta entrada */
			e.w = w;
			e.h = h;
			e.cl = (float)crop.left / (float)w;
			e.ct = (float)crop.top / (float)h;
			e.cr = (float)crop.right / (float)w;
			e.cb = (float)crop.bottom / (float)h;
			struct matrix4 m;
			obs_sceneitem_get_draw_transform(item, &m);
			/* vetor-linha: escala primeiro (quadrado 0..1 -> área cortada) e depois a posição do item */
			e.n = m;
			vec4_mulf(&e.n.x, &m.x, cw);
			vec4_mulf(&e.n.y, &m.y, ch);
			c->out->push_back(e);
			return true;
		},
		&ctx);
}

static float smooth(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

static void lerpRow(struct vec4 &d, const struct vec4 &a, const struct vec4 &b, float t)
{
	d.x = a.x + (b.x - a.x) * t;
	d.y = a.y + (b.y - a.y) * t;
	d.z = a.z + (b.z - a.z) * t;
	d.w = a.w + (b.w - a.w) * t;
}

static void lerpMatrix(struct matrix4 &d, const struct matrix4 &a, const struct matrix4 &b, float t)
{
	lerpRow(d.x, a.x, b.x, t);
	lerpRow(d.y, a.y, b.y, t);
	lerpRow(d.z, a.z, b.z, t);
	lerpRow(d.t, a.t, b.t, t);
}

/* desenha a fonte inteira numa textura e põe na tela só a parte cortada, no quadrilátero n */
static void drawElem(Merge *m, const Elem &e, const struct matrix4 &n, float cl, float ct, float cr, float cb,
		     float opacity)
{
	if (opacity <= 0.002f)
		return;
	if (m->poolUsed >= m->pool.size())
		m->pool.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
	gs_texrender_t *tr = m->pool[m->poolUsed++];
	gs_texrender_reset(tr);
	if (!gs_texrender_begin(tr, e.w, e.h))
		return;
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)e.w, 0.0f, (float)e.h, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	obs_source_video_render(e.src);
	gs_blend_state_pop();
	gs_texrender_end(tr);
	gs_texture_t *tex = gs_texrender_get_texture(tr);
	if (!tex)
		return;

	const uint32_t x = (uint32_t)std::lround(cl * e.w), y = (uint32_t)std::lround(ct * e.h);
	const uint32_t cw = (uint32_t)std::max(1L, std::lround((1.0f - cl - cr) * e.w));
	const uint32_t ch = (uint32_t)std::max(1L, std::lround((1.0f - ct - cb) * e.h));
	struct matrix4 d = n;
	vec4_divf(&d.x, &n.x, (float)cw);
	vec4_divf(&d.y, &n.y, (float)ch);

	const bool linear = gs_get_linear_srgb();
	const bool prevSrgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear);
	if (linear)
		gs_effect_set_texture_srgb(m->pImage, tex);
	else
		gs_effect_set_texture(m->pImage, tex);
	gs_effect_set_float(m->pOpacity, std::clamp(opacity, 0.0f, 1.0f));
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA); /* a textura já vem pré-multiplicada */
	gs_matrix_push();
	gs_matrix_mul(&d);
	while (gs_effect_loop(m->fx, "Draw"))
		gs_draw_sprite_subregion(tex, 0, x, y, cw, ch);
	gs_matrix_pop();
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(prevSrgb);
}

static const char *mergeName(void *)
{
	return "Merge";
}

static void *mergeCreate(obs_data_t *, obs_source_t *source)
{
	auto *m = new Merge;
	m->source = source;
	obs_enter_graphics();
	char *err = nullptr;
	m->fx = gs_effect_create(kMergeEffect, "rds_merge.effect", &err);
	if (m->fx) {
		m->pImage = gs_effect_get_param_by_name(m->fx, "image");
		m->pOpacity = gs_effect_get_param_by_name(m->fx, "opacity");
	} else {
		blog(LOG_WARNING, "[merge] efeito não compilou: %s", err ? err : "?");
	}
	bfree(err);
	obs_leave_graphics();
	return m;
}

static void mergeDestroy(void *data)
{
	auto *m = static_cast<Merge *>(data);
	obs_enter_graphics();
	for (gs_texrender_t *t : m->pool)
		gs_texrender_destroy(t);
	if (m->fx)
		gs_effect_destroy(m->fx);
	obs_leave_graphics();
	delete m;
}

static void mergeRender(void *data, gs_effect_t *)
{
	auto *m = static_cast<Merge *>(data);
	const float t = obs_transition_get_time(m->source);
	obs_source_t *a = obs_transition_get_source(m->source, OBS_TRANSITION_SOURCE_A);
	obs_source_t *b = obs_transition_get_source(m->source, OBS_TRANSITION_SOURCE_B);

	std::vector<Elem> ea, eb;
	if (m->fx && a && b && obs_scene_from_source(a) && obs_scene_from_source(b)) {
		collect(a, ea);
		collect(b, eb);
	}
	if (!m->fx || (ea.empty() && eb.empty())) {
		/* não são entradas (ou nada visível): sem movimento, só a troca na metade */
		obs_transition_video_render_direct(m->source, t < 0.5f ? OBS_TRANSITION_SOURCE_A : OBS_TRANSITION_SOURCE_B);
		obs_source_release(a);
		obs_source_release(b);
		return;
	}

	const float e = smooth(t);
	/* o que é só da nova entra na 1ª metade por cima; o que é só da antiga sai na 2ª metade por baixo:
	 * no meio as duas estão inteiras (sem escurecer, como num fade) */
	const float inB = std::min(1.0f, e * 2.0f);
	const float outA = 1.0f - std::max(0.0f, (e - 0.5f) * 2.0f);
	m->poolUsed = 0;

	/* pares: cada pedaço da nova procura o mesmo na antiga */
	std::vector<int> match(eb.size(), -1);
	for (size_t i = 0; i < eb.size(); i++)
		for (size_t j = 0; j < ea.size(); j++)
			if (!ea[j].used && ea[j].key == eb[i].key) {
				ea[j].used = true;
				match[i] = (int)j;
				break;
			}

	/* 1) só da antiga, por baixo de tudo */
	for (const Elem &x : ea)
		if (!x.used)
			drawElem(m, x, x.n, x.cl, x.ct, x.cr, x.cb, outA);
	/* 2) a nova, na ordem dela; os pares andando da posição antiga para a nova */
	for (size_t i = 0; i < eb.size(); i++) {
		const Elem &nb = eb[i];
		if (match[i] < 0) {
			drawElem(m, nb, nb.n, nb.cl, nb.ct, nb.cr, nb.cb, inB);
			continue;
		}
		const Elem &na = ea[(size_t)match[i]];
		struct matrix4 n;
		lerpMatrix(n, na.n, nb.n, e);
		const float cl = na.cl + (nb.cl - na.cl) * e, ct = na.ct + (nb.ct - na.ct) * e;
		const float cr = na.cr + (nb.cr - na.cr) * e, cb = na.cb + (nb.cb - na.cb) * e;
		/* a imagem antiga do pedaço por baixo e a nova por cima, aparecendo: costuma ser a mesma */
		drawElem(m, na, n, cl, ct, cr, cb, 1.0f);
		drawElem(m, nb, n, cl, ct, cr, cb, e);
	}
	obs_source_release(a);
	obs_source_release(b);
}

static float mixA(void *, float t)
{
	return 1.0f - t;
}

static float mixB(void *, float t)
{
	return t;
}

static bool mergeAudio(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
		       size_t channels, size_t sample_rate)
{
	auto *m = static_cast<Merge *>(data);
	return obs_transition_audio_render(m->source, ts_out, audio, mixers, channels, sample_rate, mixA, mixB);
}

static enum gs_color_space mergeSpace(void *data, size_t, const enum gs_color_space *)
{
	auto *m = static_cast<Merge *>(data);
	return obs_transition_video_get_color_space(m->source);
}

/* ============================ Programa (PGM) ============================ */

static gs_texrender_t *g_pgmCopy = nullptr; /* só na thread de vídeo */
static std::atomic<uint64_t> g_pgmWanted{0}; /* última vez que alguém desenhou a entrada (ns) */
static bool g_pgmHooked = false;
static bool g_pgmReady = false;

static void pgmCopy(void *)
{
	/* ninguém mostra a entrada Programa há 1 s: não copia nada */
	if (os_gettime_ns() - g_pgmWanted.load(std::memory_order_relaxed) > 1000000000ull)
		return;
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;
	if (!g_pgmCopy)
		g_pgmCopy = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	/* o quadro do programa acabou de ficar pronto: copia para mostrar no próximo ciclo */
	gs_texrender_reset(g_pgmCopy);
	if (!gs_texrender_begin(g_pgmCopy, ovi.base_width, ovi.base_height))
		return;
	gs_ortho(0.0f, (float)ovi.base_width, 0.0f, (float)ovi.base_height, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_render_main_texture_src_color_only();
	gs_blend_state_pop();
	gs_texrender_end(g_pgmCopy);
	g_pgmReady = true;
}

static const char *pgmName(void *)
{
	return "Programa (PGM)";
}
static void *pgmCreate(obs_data_t *, obs_source_t *source)
{
	return source;
}
static void pgmDestroy(void *) {}
static uint32_t pgmWidth(void *)
{
	obs_video_info ovi;
	return obs_get_video_info(&ovi) ? ovi.base_width : 1920;
}
static uint32_t pgmHeight(void *)
{
	obs_video_info ovi;
	return obs_get_video_info(&ovi) ? ovi.base_height : 1080;
}
static void pgmRender(void *, gs_effect_t *)
{
	g_pgmWanted.store(os_gettime_ns(), std::memory_order_relaxed);
	gs_texture_t *tex = g_pgmCopy && g_pgmReady ? gs_texrender_get_texture(g_pgmCopy) : nullptr;
	if (!tex)
		return;
	/* fonte de desenho próprio: não há efeito ativo, então desenha com o padrão (com sRGB como o OBS) */
	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
	const bool linear = gs_get_linear_srgb();
	const bool prev = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear);
	if (linear)
		gs_effect_set_texture_srgb(img, tex);
	else
		gs_effect_set_texture(img, tex);
	while (gs_effect_loop(def, "Draw"))
		gs_draw_sprite(tex, 0, 0, 0);
	gs_enable_framebuffer_srgb(prev);
}

namespace merge {

void registerTypes()
{
	struct obs_source_info tr = {};
	tr.id = MERGE_TRANSITION_ID;
	tr.type = OBS_SOURCE_TYPE_TRANSITION;
	tr.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_COMPOSITE;
	tr.get_name = mergeName;
	tr.create = mergeCreate;
	tr.destroy = mergeDestroy;
	tr.video_render = mergeRender;
	tr.audio_render = mergeAudio;
	tr.video_get_color_space = mergeSpace;
	obs_register_source(&tr);

	struct obs_source_info pg = {};
	pg.id = PROGRAM_INPUT_ID;
	pg.type = OBS_SOURCE_TYPE_INPUT;
	pg.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	pg.get_name = pgmName;
	pg.create = pgmCreate;
	pg.destroy = pgmDestroy;
	pg.get_width = pgmWidth;
	pg.get_height = pgmHeight;
	pg.video_render = pgmRender;
	obs_register_source(&pg);

	if (!g_pgmHooked) {
		g_pgmHooked = true;
		obs_add_main_rendered_callback(pgmCopy, nullptr);
	}
}

void shutdown()
{
	if (g_pgmHooked) {
		obs_remove_main_rendered_callback(pgmCopy, nullptr);
		g_pgmHooked = false;
	}
	obs_enter_graphics();
	gs_texrender_destroy(g_pgmCopy);
	g_pgmCopy = nullptr;
	g_pgmReady = false;
	obs_leave_graphics();
}

} // namespace merge
