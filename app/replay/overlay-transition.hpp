#pragma once
#include <obs.h>
#include <string>

/* Roda uma transição do OBS (GT Stinger, Stinger nativo...) por cima do
 * programa, sem trocar de cena: a transição acontece entre duas cenas vazias
 * e transparentes, então só o gráfico dela aparece. Usado na virada entre
 * eventos do replay. */
class OverlayTransition {
public:
	OverlayTransition(int channel = 62, const char *name = "instant-replay-event-transition", bool mute = false)
		: channel(channel),
		  cloneName(name),
		  mute(mute)
	{
	}
	~OverlayTransition();

	/* templ: transição criada pelo usuário em "Transições de cena".
	 * Deixa uma cópia particular preparada (o GT precisa carregar o gráfico). */
	void prepare(obs_source_t *templ);
	bool start(int durationMs);
	void stop();
	bool running() const { return active; }
	/* true quando a transição chegou ao fim */
	bool finished() const;
	void release();

private:
	int channel;
	std::string cloneName;
	bool mute;
	obs_source_t *transition = nullptr;
	obs_scene_t *sceneA = nullptr;
	obs_scene_t *sceneB = nullptr;
	std::string templName;
	std::string templSettings;
	bool active = false;
	bool flip = false;
};
