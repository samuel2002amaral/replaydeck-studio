#pragma once
#include "common.hpp"
#include "recording.hpp"
#include "audio-recorder.hpp"
#include <memory>

/* Uma sessão de gravação: até 8 ângulos + 1 arquivo de áudio, todos na mesma
 * linha do tempo (nº de quadro desde o início da sessão). */
struct Session {
	std::string dir;
	uint32_t fpsNum = 30;
	uint32_t fpsDen = 1;
	uint64_t startTs = 0;
	int64_t startWallMs = 0; /* hora do relógio no quadro 0 */
	std::shared_ptr<Recording> cams[MAX_CAMERAS];
	std::shared_ptr<AudioRecorder> audio;

	double fps() const { return (double)fpsNum / (double)fpsDen; }

	int64_t liveFrame() const
	{
		int64_t m = -1;
		for (auto &c : cams)
			if (c) {
				int64_t l = c->lastFrame();
				if (l > m)
					m = l;
			}
		return m;
	}

	int64_t firstFrame() const
	{
		int64_t m = -1;
		for (auto &c : cams)
			if (c) {
				int64_t f = c->firstFrame();
				if (f >= 0 && (m < 0 || f < m))
					m = f;
			}
		return m;
	}
};
