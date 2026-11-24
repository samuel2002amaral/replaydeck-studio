/* Studio: os "canais de overlay" que o painel GT usa são os overlays 1 a 4 do programa
 * (implementado em src/gt-host.cpp). Mesma interface do plugin, por nome da fonte. */
#pragma once
#include <obs.h>
#include <string>

#define OVERLAY_COUNT 4

struct OverlaySlot {
	std::string source; /* nome da fonte no canal (vazio = nenhuma) */
	std::string transition;
	int durationMs = 300;
};

class OverlayChannels {
public:
	OverlaySlot slot(int i);
	void setSlot(int i, const OverlaySlot &s); /* só troca de nome: nada a fazer no Studio */
	void toggle(int i);
	void assignOn(int i, const std::string &source); /* coloca no overlay i (0..3) e liga */
	int channelOf(const std::string &source);       /* -1 se não está em nenhum */
	void setOn(int i, bool on);
	bool isOn(int i);
	void releaseAll() {}
};

OverlayChannels &overlays();
inline bool overlaysAlive()
{
	return true;
}
