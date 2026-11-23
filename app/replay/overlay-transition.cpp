#include "overlay-transition.hpp"
#include "common.hpp"


OverlayTransition::~OverlayTransition()
{
	release();
}

void OverlayTransition::release()
{
	stop();
	if (transition) {
		obs_transition_clear(transition);
		obs_source_release(transition);
		transition = nullptr;
	}
	if (sceneA) {
		obs_scene_release(sceneA);
		sceneA = nullptr;
	}
	if (sceneB) {
		obs_scene_release(sceneB);
		sceneB = nullptr;
	}
	templName.clear();
	templSettings.clear();
}

void OverlayTransition::prepare(obs_source_t *templ)
{
	if (!templ) {
		release();
		return;
	}
	obs_data_t *settings = obs_source_get_settings(templ);
	const char *json = obs_data_get_json(settings);
	const std::string name = obs_source_get_name(templ);
	const std::string js = json ? json : "";

	/* já preparada com as mesmas opções: não recarrega */
	if (transition && name == templName && js == templSettings) {
		obs_data_release(settings);
		return;
	}
	if (active) {
		/* não troca no meio de uma transição */
		obs_data_release(settings);
		return;
	}
	release();
	transition = obs_source_create_private(obs_source_get_id(templ), cloneName.c_str(), settings);
	obs_data_release(settings);
	if (!transition) {
		rlog(LOG_WARNING, "não foi possível criar a transição '%s' para os eventos", name.c_str());
		return;
	}
	/* a cópia que cobre os overlays não toca o som (o original já toca) */
	if (mute) {
		obs_source_set_muted(transition, true);
		obs_source_set_volume(transition, 0.0f);
	}
	sceneA = obs_scene_create_private("instant-replay-empty-a");
	sceneB = obs_scene_create_private("instant-replay-empty-b");
	obs_transition_set(transition, obs_scene_get_source(sceneA));
	templName = name;
	templSettings = js;
	flip = false;
	rlog(LOG_INFO, "%s preparada: '%s' (%s)", mute ? "cobertura do stinger" : "transição entre eventos", name.c_str(),
	     obs_source_get_id(transition));
}

bool OverlayTransition::start(int durationMs)
{
	if (!transition || !sceneA || !sceneB)
		return false;
	if (active)
		stop();
	/* sempre de A (vazia) para B (vazia): o OBS nem sempre troca as fontes
	 * no fim de uma transição, então reposiciona antes de começar */
	obs_transition_set(transition, obs_scene_get_source(sceneA));
	obs_source_t *dest = obs_scene_get_source(sceneB);
	obs_set_output_source(channel, transition);
	if (!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, (uint32_t)(durationMs > 0 ? durationMs : 1000),
				  dest)) {
		obs_set_output_source(channel, nullptr);
		return false;
	}
	active = true;
	return true;
}

bool OverlayTransition::finished() const
{
	if (!active || !transition)
		return true;
	return obs_transition_get_time(transition) >= 1.0f;
}

void OverlayTransition::stop()
{
	if (!active)
		return;
	active = false;
	obs_set_output_source(channel, nullptr);
}
