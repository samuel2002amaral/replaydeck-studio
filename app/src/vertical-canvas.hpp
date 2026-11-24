/* Quadro vertical (9:16) para Reels, TikTok e Shorts: o Programa inteiro (com overlays e
 * transições) recortado ao centro ou encaixado com faixas, num vídeo próprio do motor. */
#pragma once

#include <obs.hpp>

#include <QString>

class VerticalCanvas {
public:
	static VerticalCanvas &get();
	static void registerSource(); /* fonte interna "espelho do Programa" (uma vez, ao ligar) */

	/* liga o quadro (se já estava ligado, só ajusta); fit = encaixar com faixas em vez de recortar */
	bool start(int width, int height, bool fit, int offsetPct, QString &error);
	void stop();
	void shutdown(); /* solta a cena antes de desligar o motor (ao fechar) */
	bool active() const { return video != nullptr; }
	video_t *videoOutput() const { return video; }
	obs_source_t *scene() const { return sceneSrc; } /* para ver numa tela (Externo) */
	void apply(bool fit, int offsetPct);          /* muda o enquadramento na hora */
	/* só a cena (para ver numa tela, sem gerar vídeo) */
	bool ensureScene(int width, int height, bool fit, int offsetPct);
	int width() const { return w; }
	int height() const { return h; }

private:
	VerticalCanvas() = default;
	OBSSourceAutoRelease sceneSrc;
	OBSSourceAutoRelease mirror;
	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	int w = 1080, h = 1920;
};
