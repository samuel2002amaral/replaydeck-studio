#include "audio-recorder.hpp"
#include <util/platform.h>
#include <algorithm>
#include <cstring>

#define WAV_HEADER_SIZE 80
#define BYTES_PER_FRAME (REPLAY_AUDIO_CHANNELS * sizeof(float))

AudioRecorder::~AudioRecorder()
{
	stop();
	if (rf)
		fclose(rf);
}

static void put32(uint8_t *p, uint32_t v)
{
	memcpy(p, &v, 4);
}
static void put16(uint8_t *p, uint16_t v)
{
	memcpy(p, &v, 2);
}
static void put64(uint8_t *p, uint64_t v)
{
	memcpy(p, &v, 8);
}

void AudioRecorder::writeHeader(bool final)
{
	uint8_t h[WAV_HEADER_SIZE] = {0};
	const uint64_t dataSize = (uint64_t)written.load() * BYTES_PER_FRAME;
	const uint64_t riffSize = dataSize + WAV_HEADER_SIZE - 8;
	const bool rf64 = final && riffSize > 0xFFFFFFF0ULL;

	memcpy(h, rf64 ? "RF64" : "RIFF", 4);
	put32(h + 4, (rf64 || !final) ? 0xFFFFFFFFu : (uint32_t)riffSize);
	memcpy(h + 8, "WAVE", 4);

	/* espaço reservado: vira o chunk ds64 se o arquivo passar de 4 GB */
	memcpy(h + 12, rf64 ? "ds64" : "JUNK", 4);
	put32(h + 16, 28);
	if (rf64) {
		put64(h + 20, riffSize);
		put64(h + 28, dataSize);
		put64(h + 36, (uint64_t)written.load());
		put32(h + 44, 0);
	}

	memcpy(h + 48, "fmt ", 4);
	put32(h + 52, 16);
	put16(h + 56, 3); /* WAVE_FORMAT_IEEE_FLOAT */
	put16(h + 58, REPLAY_AUDIO_CHANNELS);
	put32(h + 60, REPLAY_AUDIO_RATE);
	put32(h + 64, REPLAY_AUDIO_RATE * BYTES_PER_FRAME);
	put16(h + 68, BYTES_PER_FRAME);
	put16(h + 70, 32);

	memcpy(h + 72, "data", 4);
	put32(h + 76, (rf64 || !final) ? 0xFFFFFFFFu : (uint32_t)dataSize);

	os_fseeki64(wf, 0, SEEK_SET);
	fwrite(h, 1, sizeof(h), wf);
	fflush(wf);
}

bool AudioRecorder::start(const std::string &path_, int trackA, int trackB, uint64_t startTs_)
{
	path = path_;
	startTs = startTs_;
	tracks[0] = trackA;
	tracks[1] = trackB;

	wf = os_fopen(path.c_str(), "wb");
	if (!wf)
		return false;
	writeHeader(false);
	os_fseeki64(wf, WAV_HEADER_SIZE, SEEK_SET);
	rf = os_fopen(path.c_str(), "rb");

	struct audio_convert_info aci = {};
	aci.samples_per_sec = REPLAY_AUDIO_RATE;
	aci.format = AUDIO_FORMAT_FLOAT_PLANAR;
	aci.speakers = SPEAKERS_STEREO;

	for (int i = 0; i < 2; i++) {
		if (tracks[i] < 1 || tracks[i] > MAX_AUDIO_MIXES)
			continue;
		if (i == 1 && tracks[1] == tracks[0])
			continue;
		connected[i] = audio_output_connect(obs_get_audio(), (size_t)(tracks[i] - 1), &aci, audioCb, &taps[i]);
	}
	return connected[0] || connected[1];
}

bool AudioRecorder::openExisting(const std::string &path_)
{
	path = path_;
	const int64_t size = os_get_file_size(path.c_str());
	if (size <= WAV_HEADER_SIZE)
		return false;
	rf = os_fopen(path.c_str(), "rb");
	if (!rf)
		return false;
	written = (size - WAV_HEADER_SIZE) / (int64_t)BYTES_PER_FRAME;
	return true;
}

void AudioRecorder::stop()
{
	for (int i = 0; i < 2; i++) {
		if (connected[i]) {
			audio_output_disconnect(obs_get_audio(), (size_t)(tracks[i] - 1), audioCb, &taps[i]);
			connected[i] = false;
		}
	}
	std::lock_guard<std::mutex> lk(mtx);
	if (wf) {
		writeHeader(true);
		fclose(wf);
		wf = nullptr;
	}
}

void AudioRecorder::audioCb(void *param, size_t, struct audio_data *data)
{
	Tap *t = static_cast<Tap *>(param);
	t->self->onAudio(t->slot, data);
}

void AudioRecorder::onAudio(int slot, struct audio_data *data)
{
	std::lock_guard<std::mutex> lk(mtx);
	if (!wf)
		return;

	size_t frames = data->frames;
	size_t skip = 0;
	std::vector<float> &p = pending[slot];

	if (!aligned[slot]) {
		/* alinha a primeira amostra com o quadro 0 do vídeo */
		if (data->timestamp < startTs) {
			skip = (size_t)((startTs - data->timestamp) * REPLAY_AUDIO_RATE / 1000000000ULL);
			if (skip >= frames)
				return;
		} else {
			size_t pad = (size_t)((data->timestamp - startTs) * REPLAY_AUDIO_RATE / 1000000000ULL);
			p.insert(p.end(), pad * 2, 0.0f);
		}
		aligned[slot] = true;
	}

	const float *l = (const float *)data->data[0];
	const float *r = data->data[1] ? (const float *)data->data[1] : l;
	if (!l)
		return;
	for (size_t i = skip; i < frames; i++) {
		p.push_back(l[i]);
		p.push_back(r[i]);
	}

	/* junta as duas trilhas em blocos de 4 canais */
	const bool both = connected[0] && connected[1];
	size_t n;
	if (both)
		n = std::min(pending[0].size(), pending[1].size()) / 2;
	else
		n = pending[connected[0] ? 0 : 1].size() / 2;
	if (!n)
		return;

	scratch.resize(n * REPLAY_AUDIO_CHANNELS);
	for (size_t i = 0; i < n; i++) {
		for (int s = 0; s < 2; s++) {
			const bool has = connected[s];
			scratch[i * 4 + s * 2] = has ? pending[s][i * 2] : 0.0f;
			scratch[i * 4 + s * 2 + 1] = has ? pending[s][i * 2 + 1] : 0.0f;
		}
	}
	for (int s = 0; s < 2; s++)
		if (connected[s])
			pending[s].erase(pending[s].begin(), pending[s].begin() + (ptrdiff_t)(n * 2));

	fwrite(scratch.data(), sizeof(float), scratch.size(), wf);
	fflush(wf);
	written += (int64_t)n;
}

size_t AudioRecorder::read(int64_t samplePos, size_t frames, float *out)
{
	const int64_t avail = written.load();
	if (samplePos < 0 || samplePos >= avail)
		return 0;
	if ((int64_t)frames > avail - samplePos)
		frames = (size_t)(avail - samplePos);

	std::lock_guard<std::mutex> lk(readMtx);
	if (!rf)
		return 0;
	if (os_fseeki64(rf, WAV_HEADER_SIZE + samplePos * (int64_t)BYTES_PER_FRAME, SEEK_SET) != 0)
		return 0;
	return fread(out, BYTES_PER_FRAME, frames, rf);
}
