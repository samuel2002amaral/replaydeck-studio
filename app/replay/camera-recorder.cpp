#include "i18n.hpp"
#include "camera-recorder.hpp"
#include <util/platform.h>
#include <util/threading.h>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
}

#define MAX_QUEUE 30

static const char *autoOrder[] = {"h264_nvenc", "h264_qsv", "h264_amf", "mjpeg"};

CameraRecorder::CameraRecorder(int index_, obs_source_t *source_, const RecConfig &cfg_, uint64_t startTs_,
			       std::shared_ptr<Recording> rec_, const std::string &basePath_)
	: index(index_), source(obs_source_get_ref(source_)), cfg(cfg_), startTs(startTs_), rec(std::move(rec_)),
	  basePath(basePath_)
{
}

CameraRecorder::~CameraRecorder()
{
	stop();
	obs_source_release(source);
}

/* Studio: os erros do FFmpeg vão para o log (antes só aparecia "Unknown error occurred") */
static void ffmpegLog(void *, int level, const char *fmt, va_list args)
{
	if (level > AV_LOG_WARNING)
		return;
	char buf[512];
	vsnprintf(buf, sizeof(buf), fmt, args);
	size_t n = strlen(buf);
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = 0;
	if (n)
		blog(LOG_WARNING, "[ReplayDeck] FFmpeg: %s", buf);
}

bool CameraRecorder::openEncoder(const char *name, std::string &err, bool withSlices, bool withCq)
{
	static bool logHooked = false;
	if (!logHooked) {
		logHooked = true;
		av_log_set_callback(ffmpegLog);
	}
	const AVCodec *codec = avcodec_find_encoder_by_name(name);
	if (!codec) {
		err = std::string("encoder indisponível: ") + name;
		return false;
	}

	const bool isMjpeg = codec->id == AV_CODEC_ID_MJPEG;
	const bool isHevc = codec->id == AV_CODEC_ID_HEVC;
	const std::string n = name;

	AVCodecContext *c = avcodec_alloc_context3(codec);
	c->width = (int)width;
	c->height = (int)height;
	c->time_base = AVRational{(int)fpsDen, (int)fpsNum};
	c->framerate = AVRational{(int)fpsNum, (int)fpsDen};
	c->gop_size = isMjpeg ? 0 : 1; /* somente intra */
	c->max_b_frames = 0;
	c->colorspace = AVCOL_SPC_BT709;
	c->color_primaries = AVCOL_PRI_BT709;
	c->color_trc = AVCOL_TRC_BT709;

	if (isMjpeg) {
		/* MJPEG = 4:2:2 real, por CPU. */
		c->pix_fmt = AV_PIX_FMT_YUVJ422P;
		c->color_range = AVCOL_RANGE_JPEG;
		c->flags |= AV_CODEC_FLAG_QSCALE;
		/* qualidade constante → escala do MJPEG (2 = melhor) */
		const int q = cfg.cq > 0 ? (cfg.cq <= 14 ? 2 : cfg.cq <= 18 ? 3 : cfg.cq <= 22 ? 4 : 6) : cfg.mjpegQuality;
		c->global_quality = FF_QP2LAMBDA * q;
		c->thread_count = 4;
		c->thread_type = FF_THREAD_SLICE;
	} else {
		c->pix_fmt = AV_PIX_FMT_NV12;
		c->color_range = AVCOL_RANGE_MPEG;
		/* qualidade constante: cada quadro com a mesma qualidade, sem estourar
		 * em cena com muito movimento (a taxa varia sozinha) */
		const bool cqp = withCq && cfg.cq > 0;
		if (!cqp) {
			c->bit_rate = (int64_t)cfg.bitrateMbps * 1000000;
			c->rc_max_rate = c->bit_rate;
			c->rc_buffer_size = (int)(c->bit_rate / 4);
		}
		/* fatias: o replay decodifica cada quadro em paralelo (ré e avanço rápido mais leves) */
		c->slices = withSlices ? 4 : 0;

		if (n.find("nvenc") != std::string::npos) {
			av_opt_set(c->priv_data, "preset", "p4", 0);
			av_opt_set(c->priv_data, "tune", "hq", 0);
			if (cqp) {
				av_opt_set(c->priv_data, "rc", "constqp", 0);
				av_opt_set_int(c->priv_data, "qp", cfg.cq, 0);
			} else {
				av_opt_set(c->priv_data, "rc", "cbr", 0);
			}
			av_opt_set_int(c->priv_data, "forced-idr", 1, 0);
			av_opt_set_int(c->priv_data, "delay", 0, 0);
			av_opt_set_int(c->priv_data, "zerolatency", 1, 0);
		} else if (n.find("qsv") != std::string::npos) {
			av_opt_set_int(c->priv_data, "async_depth", 1, 0);
			/* H.264: 0 = todo I é IDR; HEVC: 1 = todo I é IDR */
			av_opt_set_int(c->priv_data, "idr_interval", isHevc ? 1 : 0, 0);
			if (cqp) {
				c->flags |= AV_CODEC_FLAG_QSCALE;
				c->global_quality = FF_QP2LAMBDA * cfg.cq;
			}
		} else if (n.find("amf") != std::string::npos) {
			av_opt_set(c->priv_data, "usage", "lowlatency", 0);
			if (isHevc)
				av_opt_set(c->priv_data, "header_insertion_mode", "idr", 0);
			if (cqp) {
				av_opt_set(c->priv_data, "rc", "cqp", 0);
				av_opt_set_int(c->priv_data, "qp_i", cfg.cq, 0);
				av_opt_set_int(c->priv_data, "qp_p", cfg.cq, 0);
			}
		}
	}

	int ret = avcodec_open2(c, codec, nullptr);
	if (ret < 0 && withSlices && !isMjpeg) {
		/* encoder que não aceita fatias: tenta de novo sem */
		avcodec_free_context(&c);
		return openEncoder(name, err, false, withCq);
	}
	if (ret < 0 && withCq && cfg.cq > 0 && !isMjpeg) {
		/* driver sem qualidade constante: usa a taxa fixa no mesmo encoder */
		avcodec_free_context(&c);
		rlog(LOG_WARNING, "cam %d: %s sem qualidade constante, usando %d Mbps", index + 1, name, cfg.bitrateMbps);
		return openEncoder(name, err, true, false);
	}
	if (ret < 0) {
		char buf[128];
		av_strerror(ret, buf, sizeof(buf));
		err = std::string(name) + ": " + buf;
		avcodec_free_context(&c);
		return false;
	}

	rlog(LOG_INFO, "cam %d: %s %ux%u, %s%s", index + 1, name, width, height,
	     isMjpeg ? "MJPEG" : (withCq && cfg.cq > 0 ? "qualidade constante" : "taxa fixa"),
	     !isMjpeg && withSlices ? ", 4 fatias" : "");
	enc = c;
	avPixFmt = c->pix_fmt;
	encName = name;
	return true;
}

bool CameraRecorder::start(std::string &err)
{
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		err = "vídeo do OBS não inicializado";
		return false;
	}

	width = obs_source_get_width(source);
	height = obs_source_get_height(source);
	if (!width || !height) {
		width = ovi.base_width;
		height = ovi.base_height;
	}
	width &= ~1u;
	height &= ~1u;
	fpsNum = ovi.fps_num;
	fpsDen = ovi.fps_den;
	frameIntervalNs = 1000000000ULL * fpsDen / fpsNum;

	/* --- Studio: primeiro o codificador do motor na placa de vídeo (zero cópia) --- */
	{
		std::string gerr;
		const char *forced = getenv("RDS_REPLAY_OBS_ENCODER"); /* testes: obs_x264 */
		const bool wantNv = cfg.encoder == "auto" || cfg.encoder.find("nvenc") != std::string::npos;
		const char *id = forced ? forced
				 : cfg.encoder.find("hevc") != std::string::npos ? "obs_nvenc_hevc_tex"
										  : "obs_nvenc_h264_tex";
		if ((forced || wantNv) && startGpu(id, gerr))
			return true;
		if (!gerr.empty())
			rlog(LOG_INFO, "cam %d: %s", index + 1, gerr.c_str());
		/* NVENC recebendo o quadro pela memória: ainda comprime na placa */
		if (!forced && wantNv) {
			std::string serr;
			const char *soft = strstr(id, "hevc") ? "obs_nvenc_hevc_soft" : "obs_nvenc_h264_soft";
			if (startGpu(soft, serr))
				return true;
			if (!serr.empty())
				rlog(LOG_INFO, "cam %d: %s — usando o caminho antigo (FFmpeg)", index + 1, serr.c_str());
		}
	}

	/* --- encoder --- */
	bool ok = false;
	std::string lastErr;
	if (cfg.encoder != "auto")
		ok = openEncoder(cfg.encoder.c_str(), lastErr);
	if (!ok) {
		if (cfg.encoder != "auto")
			rlog(LOG_WARNING, "cam %d: %s — tentando alternativas", index + 1, lastErr.c_str());
		for (const char *name : autoOrder) {
			if (openEncoder(name, lastErr)) {
				ok = true;
				break;
			}
			rlog(LOG_INFO, "cam %d: %s", index + 1, lastErr.c_str());
		}
	}
	if (!ok) {
		err = "nenhum encoder intra-frame pôde ser aberto (" + lastErr + ")";
		return false;
	}

	const char *ext = enc->codec_id == AV_CODEC_ID_HEVC    ? ".h265"
			  : enc->codec_id == AV_CODEC_ID_MJPEG ? ".mjpeg"
							       : ".h264";
	rec->codecId = enc->codec_id;
	rec->width = (int)width;
	rec->height = (int)height;
	if (!rec->open(basePath + ext, basePath + ".idx")) {
		err = "não foi possível criar " + basePath + ext;
		return false;
	}
	pkt = av_packet_alloc();

	/* --- view dedicado para esta câmera --- */
	ovi.gpu_conversion = true;
	ovi.base_width = ovi.output_width = width;
	ovi.base_height = ovi.output_height = height;
	view = obs_view_create();
	obs_view_set_source(view, 0, source);
	video = obs_view_add2(view, &ovi);
	if (!video) {
		err = "obs_view_add2 falhou";
		return false;
	}

	const bool mjpeg = enc->codec_id == AV_CODEC_ID_MJPEG;
	conv.format = mjpeg ? VIDEO_FORMAT_I422 : VIDEO_FORMAT_NV12;
	conv.width = width;
	conv.height = height;
	conv.range = mjpeg ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
	conv.colorspace = VIDEO_CS_709;

	stopping = false;
	thread = std::thread(&CameraRecorder::encodeLoop, this);

	/* Saída "raw" do OBS ligada ao view desta câmera: é ela que faz o OBS
	 * renderizar o view e entregar os quadros já convertidos. */
	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "owner", (long long)(intptr_t)this);
	char outName[48];
	snprintf(outName, sizeof(outName), "instant-replay-cam%d", index + 1);
	output = obs_output_create("instant_replay_capture", outName, settings, nullptr);
	obs_data_release(settings);
	if (!output) {
		err = "não foi possível criar a saída de captura";
		return false;
	}
	obs_output_set_media(output, video, obs_get_audio());
	obs_output_set_video_conversion(output, &conv);
	if (!obs_output_start(output)) {
		const char *e = obs_output_get_last_error(output);
		err = std::string("captura não iniciou") + (e ? std::string(": ") + e : std::string());
		return false;
	}

	rlog(LOG_INFO, "cam %d: gravando '%s' %ux%u @ %u/%u com %s -> %s", index + 1, obs_source_get_name(source),
	     width, height, fpsNum, fpsDen, encName.c_str(), rec->path.c_str());
	return true;
}

void CameraRecorder::stop()
{
	if (output) {
		/* Studio: só para se ligou de verdade; parar uma saída que não ligou deixa o motor
		 * esperando um fim que nunca vem e soltar a saída trava o programa */
		if (obs_output_active(output))
			obs_output_force_stop(output);
		obs_output_release(output);
		output = nullptr;
	}
	if (obsEnc) {
		obs_encoder_release(obsEnc);
		obsEnc = nullptr;
	}
	if (thread.joinable()) {
		{
			std::lock_guard<std::mutex> lk(qMtx);
			stopping = true;
		}
		qCv.notify_all();
		thread.join();
	}
	if (view) {
		obs_view_remove(view);
		obs_view_set_source(view, 0, nullptr);
		obs_view_destroy(view);
		view = nullptr;
		video = nullptr;
	}
	if (enc) {
		/* descarrega o que ainda estiver dentro do encoder */
		avcodec_send_frame(enc, nullptr);
		drainPackets();
		avcodec_free_context(&enc);
	}
	if (pkt)
		av_packet_free(&pkt);
	for (AVFrame *f : queue)
		av_frame_free(&f);
	queue.clear();
	for (AVFrame *f : pool)
		av_frame_free(&f);
	pool.clear();
	if (rec)
		rec->closeWriter();
}

AVFrame *CameraRecorder::getFrame()
{
	/* chamado com qMtx travado */
	if (!pool.empty()) {
		AVFrame *f = pool.back();
		pool.pop_back();
		return f;
	}
	AVFrame *f = av_frame_alloc();
	f->format = avPixFmt;
	f->width = (int)width;
	f->height = (int)height;
	if (av_frame_get_buffer(f, 32) < 0) {
		av_frame_free(&f);
		return nullptr;
	}
	return f;
}

/* ---- saída raw do OBS que alimenta o gravador ---- */

struct CaptureOutput {
	obs_output_t *output;
	CameraRecorder *owner;
};

static const char *co_name(void *)
{
	return tlc("ReplayDeck (captura)");
}

static void *co_create(obs_data_t *settings, obs_output_t *output)
{
	return new CaptureOutput{output, (CameraRecorder *)(intptr_t)obs_data_get_int(settings, "owner")};
}

static void co_destroy(void *data)
{
	delete static_cast<CaptureOutput *>(data);
}

static bool co_start(void *data)
{
	auto *co = static_cast<CaptureOutput *>(data);
	if (!obs_output_can_begin_data_capture(co->output, 0))
		return false;
	return obs_output_begin_data_capture(co->output, 0);
}

static void co_stop(void *data, uint64_t)
{
	auto *co = static_cast<CaptureOutput *>(data);
	obs_output_end_data_capture(co->output);
}

static void co_raw_video(void *data, struct video_data *frame)
{
	auto *co = static_cast<CaptureOutput *>(data);
	if (co->owner)
		co->owner->rawFrame(frame);
}

/* ---- Studio: saída que recebe os pacotes já comprimidos pelo codificador do motor ---- */

static bool coe_start(void *data)
{
	auto *co = static_cast<CaptureOutput *>(data);
	if (!obs_output_can_begin_data_capture(co->output, 0))
		return false;
	if (!obs_output_initialize_encoders(co->output, 0))
		return false;
	return obs_output_begin_data_capture(co->output, 0);
}

static void coe_packet(void *data, struct encoder_packet *pkt)
{
	auto *co = static_cast<CaptureOutput *>(data);
	if (co->owner && pkt && pkt->type == OBS_ENCODER_VIDEO)
		co->owner->encodedPacket(pkt);
}

static bool encoderRegistered(const char *id)
{
	const char *t = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &t); i++)
		if (strcmp(t, id) == 0)
			return true;
	return false;
}

bool CameraRecorder::startGpu(const char *id, std::string &err)
{
	if (!encoderRegistered(id)) {
		err = std::string(id) + " indisponível";
		return false;
	}
	const bool hevc = strstr(id, "hevc") != nullptr;
	const bool x264 = strcmp(id, "obs_x264") == 0;
	const int q = cfg.cq > 0 ? cfg.cq : 18;

	/* só quadros-chave (cada quadro abre sozinho: ré, quadro a quadro), qualidade constante,
	 * cabeçalho em todo quadro, sem atraso */
	obs_data_t *s = obs_data_create();
	if (x264) {
		obs_data_set_string(s, "rate_control", "CRF");
		obs_data_set_int(s, "crf", q);
		obs_data_set_string(s, "preset", "veryfast");
		obs_data_set_string(s, "tune", "zerolatency");
		obs_data_set_string(s, "x264opts", "keyint=1:min-keyint=1:scenecut=0:bframes=0");
		obs_data_set_bool(s, "repeat_headers", true);
	} else {
		if (cfg.cq > 0) {
			obs_data_set_string(s, "rate_control", "CQP");
			obs_data_set_int(s, "cqp", q);
		} else {
			obs_data_set_string(s, "rate_control", "CBR");
			obs_data_set_int(s, "bitrate", cfg.bitrateMbps * 1000);
			obs_data_set_int(s, "max_bitrate", cfg.bitrateMbps * 1000);
		}
		obs_data_set_string(s, "preset", "p4");
		obs_data_set_string(s, "tune", "ll");
		obs_data_set_string(s, "multipass", "disabled");
		obs_data_set_int(s, "bf", 0);
		obs_data_set_bool(s, "lookahead", false);
		obs_data_set_bool(s, "repeat_headers", true);
		obs_data_set_string(s, "opts", "keyint=1");
	}
	char name[48];
	snprintf(name, sizeof(name), "replay-cam%d", index + 1);
	obsEnc = obs_video_encoder_create(id, name, s, nullptr);
	obs_data_release(s);
	if (!obsEnc) {
		err = std::string(id) + ": não criou";
		return false;
	}

	/* visão própria desta câmera, já convertida na placa (NV12) */
	struct obs_video_info ovi;
	obs_get_video_info(&ovi);
	ovi.gpu_conversion = true;
	ovi.base_width = ovi.output_width = width;
	ovi.base_height = ovi.output_height = height;
	view = obs_view_create();
	obs_view_set_source(view, 0, source);
	video = obs_view_add2(view, &ovi);
	if (!video) {
		err = "obs_view_add2 falhou";
		stop();
		return false;
	}
	obs_encoder_set_video(obsEnc, video);

	const char *ext = hevc ? ".h265" : ".h264";
	rec->codecId = hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
	rec->width = (int)width;
	rec->height = (int)height;
	if (!rec->open(basePath + ext, basePath + ".idx")) {
		err = "não foi possível criar " + basePath + ext;
		stop();
		return false;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "owner", (long long)(intptr_t)this);
	snprintf(name, sizeof(name), "instant-replay-cam%d", index + 1);
	output = obs_output_create("instant_replay_capture_enc", name, settings, nullptr);
	obs_data_release(settings);
	if (!output) {
		err = "não foi possível criar a saída de captura";
		stop();
		return false;
	}
	obs_output_set_video_encoder(output, obsEnc);
	if (!obs_output_start(output)) {
		const char *e = obs_output_get_last_error(output);
		err = std::string(id) + " não ligou" + (e ? std::string(": ") + e : std::string());
		stop();
		return false;
	}
	encName = id;
	rlog(LOG_INFO, "cam %d: gravando '%s' %ux%u @ %u/%u com %s na placa de vídeo (sem cópia) -> %s", index + 1,
	     obs_source_get_name(source), width, height, fpsNum, fpsDen, id, rec->path.c_str());
	return true;
}

void CameraRecorder::encodedPacket(struct encoder_packet *pkt)
{
	/* mesmo relógio das outras câmeras: horário do quadro (dts = pts, tudo quadro-chave) */
	const int64_t tsNs = pkt->dts_usec * 1000;
	if (tsNs < (int64_t)startTs)
		return;
	const int64_t frameNo = (tsNs - (int64_t)startTs + (int64_t)frameIntervalNs / 2) / (int64_t)frameIntervalNs;
	rec->append(frameNo, pkt->data, pkt->size);
}

void registerReplayCaptureOutput()
{
	struct obs_output_info enc = {};
	enc.id = "instant_replay_capture_enc";
	enc.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
	enc.get_name = co_name;
	enc.create = co_create;
	enc.destroy = co_destroy;
	enc.start = coe_start;
	enc.stop = co_stop;
	enc.encoded_packet = coe_packet;
	obs_register_output(&enc);

	struct obs_output_info info = {};
	info.id = "instant_replay_capture";
	info.flags = OBS_OUTPUT_VIDEO;
	info.get_name = co_name;
	info.create = co_create;
	info.destroy = co_destroy;
	info.start = co_start;
	info.stop = co_stop;
	info.raw_video = co_raw_video;
	obs_register_output(&info);
}

void CameraRecorder::onRawFrame(struct video_data *frame)
{
	if (frame->timestamp < startTs)
		return;
	/* Todas as câmeras usam o mesmo relógio (o do render do OBS), então o
	 * mesmo nº de quadro = o mesmo instante em todos os ângulos. */
	const int64_t frameNo = (int64_t)((frame->timestamp - startTs + frameIntervalNs / 2) / frameIntervalNs);

	AVFrame *f;
	{
		std::lock_guard<std::mutex> lk(qMtx);
		if (stopping)
			return;
		if (queue.size() >= MAX_QUEUE) {
			dropped++;
			return;
		}
		f = getFrame();
	}
	if (!f)
		return;
	/* o encoder pode ainda segurar uma referência ao buffer reciclado */
	if (av_frame_make_writable(f) < 0) {
		av_frame_free(&f);
		return;
	}

	const uint8_t *src[4] = {frame->data[0], frame->data[1], frame->data[2], frame->data[3]};
	int srcLs[4] = {(int)frame->linesize[0], (int)frame->linesize[1], (int)frame->linesize[2],
			(int)frame->linesize[3]};
	av_image_copy(f->data, f->linesize, src, srcLs, (AVPixelFormat)avPixFmt, (int)width, (int)height);
	f->pts = frameNo;

	{
		std::lock_guard<std::mutex> lk(qMtx);
		queue.push_back(f);
	}
	qCv.notify_one();
}

void CameraRecorder::drainPackets()
{
	while (avcodec_receive_packet(enc, pkt) == 0) {
		rec->append(pkt->pts, pkt->data, (size_t)pkt->size);
		av_packet_unref(pkt);
	}
}

void CameraRecorder::encodeLoop()
{
	char name[32];
	snprintf(name, sizeof(name), "replay-enc-%d", index + 1);
	os_set_thread_name(name);

	for (;;) {
		AVFrame *f = nullptr;
		{
			std::unique_lock<std::mutex> lk(qMtx);
			qCv.wait(lk, [&] { return stopping || !queue.empty(); });
			if (queue.empty())
				break; /* stopping */
			f = queue.front();
			queue.pop_front();
		}

		f->pict_type = AV_PICTURE_TYPE_I;
		int ret = avcodec_send_frame(enc, f);
		if (ret < 0 && ret != AVERROR(EAGAIN))
			dropped++;
		drainPackets();

		std::lock_guard<std::mutex> lk(qMtx);
		pool.push_back(f);
	}
}
