/* ReplayDeck Studio: a "interface do OBS" que o código do replay usa, respondida pelo próprio Studio.
 *
 * O replay veio do plugin ReplayDeck, que conversava com a janela do OBS por estas funções.
 * Aqui não existe OBS: o Studio responde com o modelo dele (igual ao vMix):
 *   - cenas         = as entradas (cada entrada tem a sua cena);
 *   - cena atual    = a entrada no Programa; prévia = a entrada na Prévia;
 *   - transições    = Corte e Fade do Studio.
 * As funções têm ligação C++ (nomes próprios do Studio): nunca se confundem com a
 * biblioteca obs-frontend-api do OBS. A implementação fica em src/replay-host.cpp. */
#pragma once

#include <obs.h>
#include <util/darray.h>

enum obs_frontend_event {
	OBS_FRONTEND_EVENT_STREAMING_STARTING,
	OBS_FRONTEND_EVENT_STREAMING_STARTED,
	OBS_FRONTEND_EVENT_STREAMING_STOPPING,
	OBS_FRONTEND_EVENT_STREAMING_STOPPED,
	OBS_FRONTEND_EVENT_RECORDING_STARTING,
	OBS_FRONTEND_EVENT_RECORDING_STARTED,
	OBS_FRONTEND_EVENT_RECORDING_STOPPING,
	OBS_FRONTEND_EVENT_RECORDING_STOPPED,
	OBS_FRONTEND_EVENT_SCENE_CHANGED,
	OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED,
	OBS_FRONTEND_EVENT_TRANSITION_CHANGED,
	OBS_FRONTEND_EVENT_TRANSITION_STOPPED,
	OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED,
	OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED,
	OBS_FRONTEND_EVENT_SCENE_COLLECTION_LIST_CHANGED,
	OBS_FRONTEND_EVENT_PROFILE_CHANGED,
	OBS_FRONTEND_EVENT_PROFILE_LIST_CHANGED,
	OBS_FRONTEND_EVENT_EXIT,
	OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING,
	OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED,
	OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING,
	OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED,
	OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED,
	OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED,
	OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED,
	OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP,
	OBS_FRONTEND_EVENT_FINISHED_LOADING,
	OBS_FRONTEND_EVENT_RECORDING_PAUSED,
	OBS_FRONTEND_EVENT_RECORDING_UNPAUSED,
	OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED,
	OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED,
	OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED,
	OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED,
	OBS_FRONTEND_EVENT_TBAR_VALUE_CHANGED,
};

struct obs_frontend_source_list {
	DARRAY(obs_source_t *) sources;
};

static inline void obs_frontend_source_list_free(struct obs_frontend_source_list *source_list)
{
	for (size_t i = 0; i < source_list->sources.num; i++)
		obs_source_release(source_list->sources.array[i]);
	da_free(source_list->sources);
}

typedef void (*obs_frontend_event_cb)(enum obs_frontend_event event, void *private_data);

void obs_frontend_add_event_callback(obs_frontend_event_cb callback, void *private_data);
void obs_frontend_remove_event_callback(obs_frontend_event_cb callback, void *private_data);

/* cenas = entradas (cada item com uma referência: libere com obs_frontend_source_list_free) */
void obs_frontend_get_scenes(struct obs_frontend_source_list *sources);
obs_source_t *obs_frontend_get_current_scene(void);          /* Programa (referência) */
void obs_frontend_set_current_scene(obs_source_t *scene);    /* leva ao Programa com a transição atual */
obs_source_t *obs_frontend_get_current_preview_scene(void);  /* Prévia (referência) */
void obs_frontend_set_current_preview_scene(obs_source_t *scene);
bool obs_frontend_preview_program_mode_active(void);         /* sempre: o Studio tem prévia e programa */

/* transições = Corte e Fade */
void obs_frontend_get_transitions(struct obs_frontend_source_list *sources);
obs_source_t *obs_frontend_get_current_transition(void);     /* referência */
void obs_frontend_set_current_transition(obs_source_t *transition);
int obs_frontend_get_transition_duration(void);
void obs_frontend_set_transition_duration(int duration);

void *obs_frontend_get_main_window(void);
void obs_frontend_open_source_properties(obs_source_t *source);
