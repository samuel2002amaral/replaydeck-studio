#pragma once
#include "common.hpp"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

/* Áudio da sessão: WAV (RF64 se passar de 4 GB) 48 kHz, 4 canais, 32-bit float
 * sem compressão. Canais 1-2 = trilha A do OBS, canais 3-4 = trilha B. */
class AudioRecorder {
public:
	~AudioRecorder();

	/* trackA/trackB: 1..6 (trilhas do OBS); trackB = 0 desativa (silêncio). */
	bool start(const std::string &path, int trackA, int trackB, uint64_t startTs);
	void stop();
	bool openExisting(const std::string &path);

	/* Lê `frames` amostras (4 canais intercalados) a partir de samplePos. */
	size_t read(int64_t samplePos, size_t frames, float *out);
	int64_t samplesWritten() const { return written.load(); }

private:
	struct Tap {
		AudioRecorder *self;
		int slot;
	};
	static void audioCb(void *param, size_t mixIdx, struct audio_data *data);
	void onAudio(int slot, struct audio_data *data);
	void writeHeader(bool final);

	std::string path;
	uint64_t startTs = 0;
	int tracks[2] = {0, 0};
	Tap taps[2] = {{this, 0}, {this, 1}};
	bool connected[2] = {false, false};
	bool aligned[2] = {false, false};

	std::mutex mtx;
	std::vector<float> pending[2]; /* estéreo intercalado */
	std::vector<float> scratch;
	FILE *wf = nullptr;
	FILE *rf = nullptr;
	std::mutex readMtx;
	std::atomic<int64_t> written{0};
};
