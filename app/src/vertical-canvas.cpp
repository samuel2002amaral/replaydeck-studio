#include "vertical-canvas.hpp"

#include <algorithm>

/* ---- fonte "espelho do Programa": desenha a imagem final do programa (com overlays) ---- */

static const char *MIRROR_ID = "rds_program_mirror";

static const char *mirrorName(void *)
{
	return "Programa (espelho)";
}
static void *mirrorCreate(obs_data_t *, obs_source_t *source)
{
	return source;
}
static void mirrorDestroy(void *) {}
static uint32_t mirrorWidth(void *)
{
	obs_video_info ovi;
	return obs_get_video_info(&ovi) ? ovi.base_width : 1920;
}
static uint32_t mirrorHeight(void *)
{
	obs_video_info ovi;
	return obs_get_video_info(&ovi) ? ovi.base_height : 1080;
}
static void mirrorRender(void *, gs_effect_t *)
{
	/* o quadro do programa já pronto neste ciclo (o programa é desenhado antes das outras visões) */
	obs_render_main_texture_src_color_only();
}

void VerticalCanvas::registerSource()
{
	struct obs_source_info info = {};
	info.id = MIRROR_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED;
	info.get_name = mirrorName;
	info.create = mirrorCreate;
	info.destroy = mirrorDestroy;
	info.get_width = mirrorWidth;
	info.get_height = mirrorHeight;
	info.video_render = mirrorRender;
	obs_register_source(&info);
}

VerticalCanvas &VerticalCanvas::get()
{
	static VerticalCanvas c;
	return c;
}

void VerticalCanvas::apply(bool fit, int offsetPct)
{
	if (!sceneSrc)
		return;
	obs_scene_t *scene = obs_scene_from_source(sceneSrc);
	obs_sceneitem_t *item = obs_scene_find_source(scene, obs_source_get_name(mirror));
	if (!item)
		return;
	const float sw = (float)mirrorWidth(nullptr), sh = (float)mirrorHeight(nullptr);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_crop crop = {};
	vec2 pos, scale;
	if (fit) {
		/* encaixar: a imagem inteira na largura, com faixas em cima e embaixo */
		const float s = (float)w / sw;
		vec2_set(&scale, s, s);
		vec2_set(&pos, 0.0f, ((float)h - sh * s) / 2.0f);
	} else {
		/* recortar: altura inteira, só a faixa vertical do meio (offset: -100 esquerda .. 100 direita) */
		const float s = (float)h / sh;
		const float visible = (float)w / s; /* largura do original que aparece */
		const float spare = std::max(0.0f, sw - visible);
		const float left = spare / 2.0f * (1.0f + std::clamp(offsetPct, -100, 100) / 100.0f);
		crop.left = (int)left;
		crop.right = (int)(spare - left);
		vec2_set(&scale, s, s);
		vec2_set(&pos, 0.0f, 0.0f);
	}
	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_pos(item, &pos);
}

bool VerticalCanvas::ensureScene(int width, int height, bool fit, int offsetPct)
{
	if (sceneSrc && (width != w || height != h) && !view) {
		sceneSrc = nullptr; /* outro tamanho: refaz */
		mirror = nullptr;
	}
	w = width;
	h = height;
	if (!sceneSrc) {
		mirror = obs_source_create_private(MIRROR_ID, "Programa (espelho)", nullptr);
		if (!mirror)
			return false;
		obs_scene_t *scene = obs_scene_create_private("Vertical 9:16");
		sceneSrc = obs_scene_get_source(scene);
		obs_source_get_ref(sceneSrc);
		obs_scene_release(scene);
		obs_scene_add(obs_scene_from_source(sceneSrc), mirror);
	}
	apply(fit, offsetPct);
	return true;
}

bool VerticalCanvas::start(int width, int height, bool fit, int offsetPct, QString &error)
{
	if (active()) {
		apply(fit, offsetPct);
		return true;
	}
	if (!ensureScene(width, height, fit, offsetPct)) {
		error = "Não foi possível criar o quadro vertical.";
		return false;
	}

	view = obs_view_create();
	obs_view_set_source(view, 0, sceneSrc);
	obs_video_info ovi;
	obs_get_video_info(&ovi);
	ovi.base_width = ovi.output_width = (uint32_t)w;
	ovi.base_height = ovi.output_height = (uint32_t)h;
	video = obs_view_add2(view, &ovi);
	if (!video) {
		error = "O motor não abriu o vídeo vertical.";
		stop();
		return false;
	}
	blog(LOG_INFO, "quadro vertical ligado: %dx%d (%s)", w, h, fit ? "encaixar" : "recortar");
	return true;
}

void VerticalCanvas::shutdown()
{
	stop();
	sceneSrc = nullptr; /* a cena antes da fonte que está dentro dela */
	mirror = nullptr;
}

void VerticalCanvas::stop()
{
	if (view) {
		obs_view_remove(view);
		obs_view_set_source(view, 0, nullptr);
		obs_view_destroy(view);
		view = nullptr;
	}
	video = nullptr; /* a cena fica (para ver o vertical numa tela); só o vídeo do motor para */
}
