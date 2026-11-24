/* "Stinger GT": um título GT do vMix (.gtzip) usado como stinger, igual a um vídeo.
 * O título anima a ENTRADA por cima do programa, a imagem troca por baixo quando ele cobre a tela,
 * e a SAÍDA revela a entrada nova. É uma transição do libobs (tipo rds_gt_stinger), então funciona
 * nos botões, atalhos, no replay e por cima dos overlays como o stinger de vídeo. */
#include "engine.hpp"
#include "gt-host.hpp"

#include "../replay/gt/gt-source.hpp"

#include <obs-module.h>
#include <graphics/matrix4.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

#define GT_STINGER_ID "rds_gt_stinger"

struct GtStinger {
	obs_source_t *source = nullptr;
	obs_source_t *gt = nullptr; /* cópia particular do título */
	std::mutex mtx;
	std::string applied; /* opções já aplicadas na cópia (json) */
	std::string title;   /* nome da entrada GT escolhida */
	std::string file;    /* ou um .gtzip solto */
	int holdMs = 200;
	int cutMs = 0; /* 0 = automático */
	/* calculados no início da transição */
	float cutT = 0.5f, outT = 1.0f;
	bool hasOut = false;
	std::atomic<bool> transitioning{false};
	std::atomic<bool> outStarted{false};
};

/* as opções que a cópia do título deve ter agora (a entrada GT pode ter mudado de texto) */
static obs_data_t *wantedSettings(GtStinger *s)
{
	obs_data_t *want = obs_data_create();
	bool found = false;
	if (!s->title.empty()) {
		if (Input *in = Engine::get().inputByName(QString::fromStdString(s->title))) {
			if (gtIsSource(in->source)) {
				OBSDataAutoRelease st = obs_source_get_settings(in->source);
				obs_data_apply(want, st);
				found = true;
			}
		}
	}
	if (!found)
		obs_data_set_string(want, "file", s->file.c_str());
	obs_data_set_bool(want, "auto_in", true); /* anima a entrada quando a transição começa */
	obs_data_set_int(want, "overlay", 0);
	return want;
}

static void refreshCopy(GtStinger *s)
{
	OBSDataAutoRelease want = wantedSettings(s);
	const char *js = obs_data_get_json(want);
	const std::string json = js ? js : "";
	if (s->gt && json == s->applied)
		return;
	if (!s->gt) {
		s->gt = obs_source_create_private(GT_SOURCE_ID, "rds-gt-stinger-titulo", want);
	} else {
		obs_source_update(s->gt, want);
	}
	s->applied = json;
}

static const char *gts_name(void *)
{
	return "Stinger GT (título .gtzip)";
}

static void gts_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<GtStinger *>(data);
	std::lock_guard<std::mutex> lk(s->mtx);
	s->title = obs_data_get_string(settings, "titulo");
	s->file = obs_data_get_string(settings, "file");
	s->holdMs = (int)obs_data_get_int(settings, "segurar_ms");
	s->cutMs = (int)obs_data_get_int(settings, "troca_ms");
}

static void *gts_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new GtStinger;
	s->source = source;
	gts_update(s, settings);
	obs_transition_enable_fixed(source, true, 1000);
	return s;
}

static void gts_destroy(void *data)
{
	auto *s = static_cast<GtStinger *>(data);
	obs_source_release(s->gt);
	delete s;
}

static void gts_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "segurar_ms", 200);
	obs_data_set_default_int(settings, "troca_ms", 0);
}

static obs_properties_t *gts_properties(void *)
{
	obs_properties_t *p = obs_properties_create();
	obs_property_t *list =
		obs_properties_add_list(p, "titulo", "Título GT", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, "(arquivo abaixo)", "");
	for (Input *in : Engine::get().inputs())
		if (gtIsSource(in->source)) {
			const QByteArray n = in->name.toUtf8();
			obs_property_list_add_string(list, n.constData(), n.constData());
		}
	obs_property_set_long_description(list, "Uma entrada de título GT do programa (os textos seguem a entrada)");
	obs_properties_add_path(p, "file", "Arquivo .gtzip", OBS_PATH_FILE, "Títulos GT (*.gtzip)", nullptr);
	obs_property_t *hold = obs_properties_add_int(p, "segurar_ms", "Tempo cobrindo a tela", 0, 10000, 50);
	obs_property_int_set_suffix(hold, " ms");
	obs_property_set_long_description(hold, "Entre o fim da entrada do título e o começo da saída");
	obs_property_t *cut = obs_properties_add_int(p, "troca_ms", "Troca da imagem em (0 = automático)", 0, 20000, 20);
	obs_property_int_set_suffix(cut, " ms");
	obs_property_set_long_description(
		cut, "Automático: no meio do tempo cobrindo a tela (ou no meio da entrada, se o título não tem saída)");
	return p;
}

static void gts_start(void *data)
{
	auto *s = static_cast<GtStinger *>(data);
	std::lock_guard<std::mutex> lk(s->mtx);
	refreshCopy(s);
	if (!s->gt)
		return;
	if (s->transitioning) /* começou de novo no meio: reinicia a animação */
		obs_source_remove_active_child(s->source, s->gt);
	const int in = std::max(0, gtInLengthMs(s->gt));
	const int out = std::max(0, gtOutLengthMs(s->gt));
	const int hold = std::max(0, s->holdMs);
	s->hasOut = out > 0;
	int total = s->hasOut ? in + hold + out : in + hold;
	if (total < 200)
		total = 200;
	total += 60; /* margem: o último quadro da saída chega a aparecer */
	int cut = s->cutMs > 0 ? s->cutMs : (s->hasOut ? in + hold / 2 : in / 2);
	cut = std::clamp(cut, 1, total - 1);
	s->cutT = (float)cut / (float)total;
	s->outT = s->hasOut ? (float)(in + hold) / (float)total : 2.0f;
	obs_transition_enable_fixed(s->source, true, (uint32_t)total);
	s->outStarted = false;
	s->transitioning = true;
	obs_source_add_active_child(s->source, s->gt); /* ativou: o título anima a entrada */
	blog(LOG_INFO, "[gt-stinger] '%s': entrada %d ms, cobrindo %d ms, saída %d ms, troca em %d ms (total %d)",
	     obs_source_get_name(s->source), in, hold, out, cut, total);
}

static void gts_stop(void *data)
{
	auto *s = static_cast<GtStinger *>(data);
	if (s->gt && s->transitioning)
		obs_source_remove_active_child(s->source, s->gt);
	s->transitioning = false;
}

static void gts_tick(void *data, float)
{
	auto *s = static_cast<GtStinger *>(data);
	if (!s->transitioning || !s->gt || s->outStarted)
		return;
	if (obs_transition_get_time(s->source) >= s->outT) {
		s->outStarted = true;
		gtPlayOut(s->gt);
	}
}

static void gts_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<GtStinger *>(data);
	const float t = obs_transition_get_time(s->source);
	const bool useA = s->transitioning ? t < s->cutT : t < 0.5f;
	if (!obs_transition_video_render_direct(s->source, useA ? OBS_TRANSITION_SOURCE_A : OBS_TRANSITION_SOURCE_B))
		return;
	if (!s->transitioning || !s->gt)
		return;
	const uint32_t gw = obs_source_get_width(s->gt), gh = obs_source_get_height(s->gt);
	const uint32_t cw = obs_source_get_width(s->source), ch = obs_source_get_height(s->source);
	if (!gw || !gh || !cw || !ch)
		return;
	gs_matrix_push();
	gs_matrix_scale3f((float)cw / (float)gw, (float)ch / (float)gh, 1.0f);
	obs_source_video_render(s->gt);
	gs_matrix_pop();
}

/* som: o da entrada antiga até a troca, depois o da nova (com uma passagem curta) */
static float gts_mixA(void *data, float t)
{
	auto *s = static_cast<GtStinger *>(data);
	const float w = 0.08f;
	return std::clamp((s->cutT + w - t) / (2 * w), 0.0f, 1.0f);
}

static float gts_mixB(void *data, float t)
{
	return 1.0f - gts_mixA(data, t);
}

static bool gts_audio(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
		      size_t channels, size_t sample_rate)
{
	auto *s = static_cast<GtStinger *>(data);
	return obs_transition_audio_render(s->source, ts_out, audio, mixers, channels, sample_rate, gts_mixA, gts_mixB);
}

static void gts_enum_active(void *data, obs_source_enum_proc_t cb, void *param)
{
	auto *s = static_cast<GtStinger *>(data);
	if (s->gt && s->transitioning)
		cb(s->source, s->gt, param);
}

static enum gs_color_space gts_space(void *data, size_t, const enum gs_color_space *)
{
	auto *s = static_cast<GtStinger *>(data);
	return obs_transition_video_get_color_space(s->source);
}

namespace gthost {

void registerGtStinger()
{
	struct obs_source_info info = {};
	info.id = GT_STINGER_ID;
	info.type = OBS_SOURCE_TYPE_TRANSITION;
	info.get_name = gts_name;
	info.create = gts_create;
	info.destroy = gts_destroy;
	info.update = gts_update;
	info.get_defaults = gts_defaults;
	info.get_properties = gts_properties;
	info.video_render = gts_render;
	info.video_tick = gts_tick;
	info.audio_render = gts_audio;
	info.enum_active_sources = gts_enum_active;
	info.transition_start = gts_start;
	info.transition_stop = gts_stop;
	info.video_get_color_space = gts_space;
	obs_register_source(&info);

	/* a entrada do título mudou de nome: o stinger acompanha */
	QObject::connect(&Engine::get(), &Engine::inputRenamed, [](int, const QString &from, const QString &to) {
		for (StudioTransition *t : Engine::get().userTransitions()) {
			if (strcmp(obs_source_get_unversioned_id(t->source), GT_STINGER_ID) != 0)
				continue;
			OBSDataAutoRelease st = obs_source_get_settings(t->source);
			if (QString::fromUtf8(obs_data_get_string(st, "titulo")) == from) {
				OBSDataAutoRelease u = obs_data_create();
				obs_data_set_string(u, "titulo", to.toUtf8().constData());
				obs_source_update(t->source, u);
			}
		}
	});
}

} // namespace gthost
