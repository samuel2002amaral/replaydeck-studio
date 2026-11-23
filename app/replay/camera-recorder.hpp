#pragma once
#include "common.hpp"
#include "recording.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

struct RecConfig {
	std::string encoder = "auto"; /* auto, h264_nvenc, hevc_nvenc, h264_qsv, hevc_qsv, h264_amf, hevc_amf, mjpeg */
	int bitrateMbps = 80;
	int mjpegQuality = 3; /* 2 (melhor) .. 10 */
	int cq = 0;           /* >0: qualidade constante (QP); 0: taxa fixa (bitrateMbps) */
};

/* Grava continuamente uma fonte do OBS: obs_view dedicado -> quadros crus ->
 * encoder intra-frame (GPU) -> Recording. */
class CameraRecorder {
public:
	CameraRecorder(int index, obs_source_t *source, const RecConfig &cfg, uint64_t startTs,
		       std::shared_ptr<Recording> rec, const std::string &basePath);
	~CameraRecorder();

	bool start(std::string &err);
	void stop();

	uint64_t droppedFrames() const { return dropped.load(); }
	const std::string &encoderName() const { return encName; }
	void rawFrame(struct video_data *frame) { onRawFrame(frame); }

private:
	void onRawFrame(struct video_data *frame);
	void encodeLoop();
	bool openEncoder(const char *name, std::string &err, bool withSlices = true, bool withCq = true);
	void drainPackets();
	AVFrame *getFrame();

	int index;
	obs_source_t *source = nullptr;
	RecConfig cfg;
	uint64_t startTs;
	std::shared_ptr<Recording> rec;
	std::string basePath;
	std::string encName;

	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	obs_output_t *output = nullptr;
	struct video_scale_info conv = {};

	uint32_t width = 0, height = 0;
	uint32_t fpsNum = 30, fpsDen = 1;
	uint64_t frameIntervalNs = 0;

	AVCodecContext *enc = nullptr;
	AVPacket *pkt = nullptr;
	int avPixFmt = -1;

	std::thread thread;
	std::mutex qMtx;
	std::condition_variable qCv;
	std::deque<AVFrame *> queue;
	std::vector<AVFrame *> pool;
	bool stopping = false;
	std::atomic<uint64_t> dropped{0};
};

void registerReplayCaptureOutput();
