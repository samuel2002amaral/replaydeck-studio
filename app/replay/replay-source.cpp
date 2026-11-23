#include "i18n.hpp"
#include "engine.hpp"
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

/* Fonte "ReplayDeck" do OBS: exibe o canal A ou B. Coloque-a em qualquer
 * cena (tela cheia, PiP, multiview...).
 *
 * Os quadros vão para uma fonte interna (assíncrona); esta fonte a desenha
 * com o zoom digital do canal: recorta uma área da imagem e amplia, sempre
 * dentro do quadro (nunca aparece borda preta). */

#define FRAMES_ID "instant_replay_frames"

struct ReplaySource {
	obs_source_t *source;
	obs_source_t *frames = nullptr;
	gs_texrender_t *tr = nullptr;
	std::shared_ptr<PlaybackChannel> channel;
};

static gs_effect_t *g_zoomEffect = nullptr;

static const char *kZoomEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 uv_mul;
uniform float2 uv_add;

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
	vert_out.uv  = vert_in.uv * uv_mul + uv_add;
	return vert_out;
}

float4 PSDraw(VertInOut vert_in) : TARGET
{
	return image.Sample(def_sampler, vert_in.uv);
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

/* ---- fonte interna que só recebe os quadros ---- */

static const char *fr_name(void *)
{
	return tlc("ReplayDeck (quadros)");
}
static void *fr_create(obs_data_t *, obs_source_t *source)
{
	obs_source_set_async_unbuffered(source, true);
	return source;
}
static void fr_destroy(void *) {}

/* ---- fonte de cena ---- */

static const char *rs_name(void *)
{
	return tlc("ReplayDeck (saída)");
}

static void rs_update(void *data, obs_data_t *settings)
{
	auto *rs = static_cast<ReplaySource *>(data);
	int ch = (int)obs_data_get_int(settings, "channel");
	if (ch < 0 || ch > 1)
		ch = 0;
	std::shared_ptr<PlaybackChannel> next = engineChannel(ch);
	if (next == rs->channel)
		return;
	if (rs->channel)
		rs->channel->removeOutput(rs->source);
	rs->channel = next;
	if (next)
		next->addOutput(rs->source, rs->frames);
}

static void *rs_create(obs_data_t *settings, obs_source_t *source)
{
	auto *rs = new ReplaySource{source};
	rs->frames = obs_source_create_private(FRAMES_ID, "instant-replay-frames", nullptr);
	obs_enter_graphics();
	rs->tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (!g_zoomEffect) {
		char *err = nullptr;
		g_zoomEffect = gs_effect_create(kZoomEffect, "instant-replay-zoom.effect", &err);
		if (!g_zoomEffect)
			blog(LOG_WARNING, "[ReplayDeck] efeito de zoom: %s", err ? err : "?");
		bfree(err);
	}
	obs_leave_graphics();
	rs_update(rs, settings);
	return rs;
}

static void rs_destroy(void *data)
{
	auto *rs = static_cast<ReplaySource *>(data);
	if (rs->channel)
		rs->channel->removeOutput(rs->source);
	obs_source_release(rs->frames);
	obs_enter_graphics();
	gs_texrender_destroy(rs->tr);
	obs_leave_graphics();
	delete rs;
}

static uint32_t rs_width(void *data)
{
	auto *rs = static_cast<ReplaySource *>(data);
	return rs->frames ? obs_source_get_width(rs->frames) : 0;
}

static uint32_t rs_height(void *data)
{
	auto *rs = static_cast<ReplaySource *>(data);
	return rs->frames ? obs_source_get_height(rs->frames) : 0;
}

static void rs_render(void *data, gs_effect_t *)
{
	auto *rs = static_cast<ReplaySource *>(data);
	if (!rs->frames)
		return;
	const uint32_t w = obs_source_get_width(rs->frames), h = obs_source_get_height(rs->frames);
	if (!w || !h)
		return;
	const PlaybackChannel::Zoom zm = rs->channel ? rs->channel->zoom() : PlaybackChannel::Zoom{1.0f, 0.5f, 0.5f};
	if (zm.z <= 1.001f || !g_zoomEffect) {
		obs_source_video_render(rs->frames);
		return;
	}

	/* 1) quadro inteiro numa textura; 2) desenha só a área do zoom, ampliada */
	gs_texrender_reset(rs->tr);
	if (!gs_texrender_begin(rs->tr, w, h))
		return;
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(rs->frames);
	gs_blend_state_pop();
	gs_texrender_end(rs->tr);

	gs_texture_t *tex = gs_texrender_get_texture(rs->tr);
	if (!tex)
		return;
	struct vec2 mul, add;
	vec2_set(&mul, 1.0f / zm.z, 1.0f / zm.z);
	vec2_set(&add, zm.x - 0.5f / zm.z, zm.y - 0.5f / zm.z);
	gs_effect_set_texture(gs_effect_get_param_by_name(g_zoomEffect, "image"), tex);
	gs_effect_set_vec2(gs_effect_get_param_by_name(g_zoomEffect, "uv_mul"), &mul);
	gs_effect_set_vec2(gs_effect_get_param_by_name(g_zoomEffect, "uv_add"), &add);
	while (gs_effect_loop(g_zoomEffect, "Draw"))
		gs_draw_sprite(tex, 0, w, h);
}

static void rs_enum_active(void *data, obs_source_enum_proc_t cb, void *param)
{
	auto *rs = static_cast<ReplaySource *>(data);
	if (rs->frames)
		cb(rs->source, rs->frames, param);
}

static obs_properties_t *rs_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p =
		obs_properties_add_list(props, "channel", tlc("Canal de replay"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, tlc("Canal A"), 0);
	obs_property_list_add_int(p, tlc("Canal B"), 1);
	return props;
}

static void rs_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "channel", 0);
}

void registerReplaySource()
{
	struct obs_source_info fr = {};
	fr.id = FRAMES_ID;
	fr.type = OBS_SOURCE_TYPE_INPUT;
	fr.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_CAP_DISABLED;
	fr.get_name = fr_name;
	fr.create = fr_create;
	fr.destroy = fr_destroy;
	obs_register_source(&fr);

	struct obs_source_info info = {};
	info.id = "instant_replay_output";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.get_name = rs_name;
	info.create = rs_create;
	info.destroy = rs_destroy;
	info.update = rs_update;
	info.get_width = rs_width;
	info.get_height = rs_height;
	info.video_render = rs_render;
	info.enum_active_sources = rs_enum_active;
	info.get_properties = rs_properties;
	info.get_defaults = rs_defaults;
	info.icon_type = OBS_ICON_TYPE_MEDIA;
	obs_register_source(&info);
}
