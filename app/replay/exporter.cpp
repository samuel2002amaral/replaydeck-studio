#include "session.hpp"
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

/* Exporta um trecho (in..out) de um ângulo para um arquivo que qualquer editor
 * abre. O vídeo é copiado sem recompressão (já é intra-frame); o áudio
 * (canais 1-2) vai em AAC.
 *   H.264 / HEVC -> .mp4      MJPEG -> .mov */

static std::string avErr(int ret)
{
	char buf[128];
	av_strerror(ret, buf, sizeof(buf));
	return buf;
}

/* SPS/PPS(/VPS) do primeiro quadro, para o cabeçalho do MP4 */
static bool extractExtradata(AVCodecID id, const std::vector<uint8_t> &first, size_t size, AVCodecParameters *par)
{
	const AVBitStreamFilter *f = av_bsf_get_by_name("extract_extradata");
	if (!f)
		return false;
	AVBSFContext *bsf = nullptr;
	if (av_bsf_alloc(f, &bsf) < 0)
		return false;
	bsf->par_in->codec_id = id;
	bool ok = false;
	AVPacket *p = av_packet_alloc();
	if (av_bsf_init(bsf) >= 0 && av_new_packet(p, (int)size) >= 0) {
		memcpy(p->data, first.data(), size);
		if (av_bsf_send_packet(bsf, p) >= 0 && av_bsf_receive_packet(bsf, p) >= 0) {
			size_t sdSize = 0;
			const uint8_t *sd = av_packet_get_side_data(p, AV_PKT_DATA_NEW_EXTRADATA, &sdSize);
			if (sd && sdSize) {
				par->extradata = (uint8_t *)av_mallocz(sdSize + AV_INPUT_BUFFER_PADDING_SIZE);
				memcpy(par->extradata, sd, sdSize);
				par->extradata_size = (int)sdSize;
				ok = true;
			}
		}
	}
	av_packet_free(&p);
	av_bsf_free(&bsf);
	return ok;
}

bool exportClip(Session &s, int cam, int64_t in, int64_t out, const std::string &basePath, std::string &outPath,
		std::string &err)
{
	std::shared_ptr<Recording> rec = s.cams[cam];
	if (!rec) {
		err = "câmera sem gravação";
		return false;
	}
	if (in < 0)
		in = 0;
	const int64_t last = rec->lastFrame();
	if (out > last + 1)
		out = last + 1;
	if (out <= in) {
		err = "trecho vazio";
		return false;
	}

	const AVCodecID vid = (AVCodecID)rec->codecId;
	const bool mjpeg = vid == AV_CODEC_ID_MJPEG;
	outPath = basePath + (mjpeg ? ".mov" : ".mp4");

	AVFormatContext *oc = nullptr;
	AVCodecContext *aenc = nullptr;
	AVFrame *af = nullptr;
	AVPacket *pkt = av_packet_alloc();
	std::vector<uint8_t> buf;
	std::vector<float> abuf;
	bool ok = false;
	bool headerWritten = false;
	int ret = 0;
	int64_t actual = 0;
	AVStream *vst = nullptr, *ast = nullptr;
	const AVRational vtb = {(int)s.fpsDen, (int)s.fpsNum};
	const AVRational atb = {1, REPLAY_AUDIO_RATE};

	ret = avformat_alloc_output_context2(&oc, nullptr, nullptr, outPath.c_str());
	if (ret < 0 || !oc) {
		err = "formato de saída: " + avErr(ret);
		goto done;
	}

	/* ---- vídeo ---- */
	if (!rec->read(in, buf, actual)) {
		err = "não foi possível ler o primeiro quadro";
		goto done;
	}
	vst = avformat_new_stream(oc, nullptr);
	vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	vst->codecpar->codec_id = vid;
	vst->codecpar->width = rec->width;
	vst->codecpar->height = rec->height;
	vst->codecpar->format = mjpeg ? AV_PIX_FMT_YUVJ422P : AV_PIX_FMT_YUV420P;
	vst->time_base = vtb;
	vst->avg_frame_rate = AVRational{(int)s.fpsNum, (int)s.fpsDen};
	if (!mjpeg) {
		if (!extractExtradata(vid, buf, buf.size(), vst->codecpar)) {
			err = "cabeçalho do vídeo (SPS/PPS) não encontrado";
			goto done;
		}
		if (vid == AV_CODEC_ID_HEVC)
			vst->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
	}

	/* ---- áudio (AAC estéreo, canais 1-2) ---- */
	if (s.audio) {
		const AVCodec *ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (ac) {
			aenc = avcodec_alloc_context3(ac);
			aenc->sample_rate = REPLAY_AUDIO_RATE;
			aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
			aenc->bit_rate = 192000;
			aenc->time_base = atb;
			av_channel_layout_default(&aenc->ch_layout, 2);
			if (oc->oformat->flags & AVFMT_GLOBALHEADER)
				aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			if (avcodec_open2(aenc, ac, nullptr) >= 0) {
				ast = avformat_new_stream(oc, nullptr);
				avcodec_parameters_from_context(ast->codecpar, aenc);
				ast->time_base = atb;
			} else {
				avcodec_free_context(&aenc);
			}
		}
	}

	ret = avio_open(&oc->pb, outPath.c_str(), AVIO_FLAG_WRITE);
	if (ret < 0) {
		err = "não foi possível criar " + outPath + ": " + avErr(ret);
		goto done;
	}
	ret = avformat_write_header(oc, nullptr);
	if (ret < 0) {
		err = "cabeçalho: " + avErr(ret);
		goto done;
	}
	headerWritten = true;

	/* ---- copia os quadros ---- */
	for (int64_t f = in; f < out; f++) {
		if (!rec->read(f, buf, actual))
			continue;
		if (av_new_packet(pkt, (int)buf.size()) < 0)
			break;
		memcpy(pkt->data, buf.data(), buf.size());
		pkt->pts = pkt->dts = f - in;
		pkt->duration = 1;
		pkt->flags |= AV_PKT_FLAG_KEY;
		pkt->stream_index = vst->index;
		av_packet_rescale_ts(pkt, vtb, vst->time_base);
		ret = av_interleaved_write_frame(oc, pkt);
		av_packet_unref(pkt);
		if (ret < 0) {
			err = "escrita do vídeo: " + avErr(ret);
			goto done;
		}
	}

	/* ---- áudio ---- */
	if (ast) {
		const int64_t s0 = in * REPLAY_AUDIO_RATE * (int64_t)s.fpsDen / (int64_t)s.fpsNum;
		const int64_t s1 = out * REPLAY_AUDIO_RATE * (int64_t)s.fpsDen / (int64_t)s.fpsNum;
		const int fs = aenc->frame_size > 0 ? aenc->frame_size : 1024;
		af = av_frame_alloc();
		af->format = AV_SAMPLE_FMT_FLTP;
		af->sample_rate = REPLAY_AUDIO_RATE;
		av_channel_layout_default(&af->ch_layout, 2);
		af->nb_samples = fs;
		av_frame_get_buffer(af, 0);
		abuf.resize((size_t)fs * REPLAY_AUDIO_CHANNELS);

		auto drain = [&]() {
			while (avcodec_receive_packet(aenc, pkt) == 0) {
				pkt->stream_index = ast->index;
				av_packet_rescale_ts(pkt, atb, ast->time_base);
				av_interleaved_write_frame(oc, pkt);
				av_packet_unref(pkt);
			}
		};

		for (int64_t p = s0; p < s1; p += fs) {
			const size_t got = s.audio->read(p, (size_t)fs, abuf.data());
			av_frame_make_writable(af);
			float *l = (float *)af->data[0];
			float *r = (float *)af->data[1];
			for (int i = 0; i < fs; i++) {
				const bool has = (size_t)i < got;
				l[i] = has ? abuf[(size_t)i * REPLAY_AUDIO_CHANNELS] : 0.0f;
				r[i] = has ? abuf[(size_t)i * REPLAY_AUDIO_CHANNELS + 1] : 0.0f;
			}
			af->pts = p - s0;
			if (avcodec_send_frame(aenc, af) < 0)
				break;
			drain();
		}
		avcodec_send_frame(aenc, nullptr);
		drain();
	}

	ok = true;

done:
	if (oc) {
		if (headerWritten)
			av_write_trailer(oc);
		if (oc->pb)
			avio_closep(&oc->pb);
		avformat_free_context(oc);
	}
	if (aenc)
		avcodec_free_context(&aenc);
	av_frame_free(&af);
	av_packet_free(&pkt);
	return ok;
}
