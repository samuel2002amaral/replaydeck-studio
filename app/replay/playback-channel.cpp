#include <algorithm>
#include "playback-channel.hpp"
#include <util/platform.h>
#include <util/threading.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

PlaybackChannel::PlaybackChannel(int id_) : id(id_)
{
	frame = av_frame_alloc();
	swFrame = av_frame_alloc();
	pkt = av_packet_alloc();
	thread = std::thread(&PlaybackChannel::loop, this);
}

PlaybackChannel::~PlaybackChannel()
{
	quit = true;
	if (thread.joinable())
		thread.join();
	if (dec)
		avcodec_free_context(&dec);
	av_frame_free(&frame);
	av_frame_free(&swFrame);
	av_buffer_unref(&hwDev);
	av_packet_free(&pkt);
}

/* ---------------- comandos (qualquer thread) ---------------- */

void PlaybackChannel::setSession(std::shared_ptr<Session> s)
{
	std::lock_guard<std::mutex> lk(mtx);
	session = s;
	liveSession = std::move(s);
	pos = 0;
	playing = false;
	liveMode = true;
	list.clear();
	refresh = true;
}

bool PlaybackChannel::playingSession(const std::string &dir)
{
	std::lock_guard<std::mutex> lk(mtx);
	return session && session->dir == dir;
}

bool PlaybackChannel::usesSession(const std::string &dir)
{
	std::lock_guard<std::mutex> lk(mtx);
	if ((session && session->dir == dir) || (liveSession && liveSession->dir == dir))
		return true;
	for (const PlayRange &r : list)
		if (r.session && r.session->dir == dir)
			return true;
	return false;
}

void PlaybackChannel::addOutput(obs_source_t *source, obs_source_t *video)
{
	{
		std::lock_guard<std::mutex> lk(outMtx);
		outputs.push_back({source, video ? video : source});
	}
	std::lock_guard<std::mutex> lk(mtx);
	refresh = true;
}

/* ---------------- zoom ---------------- */

void PlaybackChannel::clampZoom(float &z, float &x, float &y)
{
	z = std::max(1.0f, std::min(5.0f, z));
	const float half = 0.5f / z;
	x = std::max(half, std::min(1.0f - half, x));
	y = std::max(half, std::min(1.0f - half, y));
}

void PlaybackChannel::setZoomMove(int axis, int dir)
{
	if (axis >= 0 && axis < 3)
		mv[axis] = dir < 0 ? -1 : dir > 0 ? 1 : 0;
}

void PlaybackChannel::zoomStep(float factor, float dx, float dy)
{
	animOn = false;
	float z = zz.load() * factor, x = zx.load() + dx / zz.load(), y = zy.load() + dy / zz.load();
	clampZoom(z, x, y);
	zz = z;
	zx = x;
	zy = y;
}

void PlaybackChannel::zoomSet(float z, float x, float y)
{
	animOn = false;
	clampZoom(z, x, y);
	zz = z;
	zx = x;
	zy = y;
}

void PlaybackChannel::zoomAnimate(const float *from, const float *to, double seconds)
{
	std::lock_guard<std::mutex> lk(animMtx);
	if (from) {
		animA[0] = from[0];
		animA[1] = from[1];
		animA[2] = from[2];
	} else {
		animA[0] = zz.load();
		animA[1] = zx.load();
		animA[2] = zy.load();
	}
	clampZoom(animA[0], animA[1], animA[2]);
	animB[0] = to[0];
	animB[1] = to[1];
	animB[2] = to[2];
	clampZoom(animB[0], animB[1], animB[2]);
	animT = 0;
	animDur = seconds > 0.05 ? seconds : 0.05;
	zz = animA[0];
	zx = animA[1];
	zy = animA[2];
	animOn = true;
}

void PlaybackChannel::resetZoom()
{
	animOn = false;
	for (auto &m : mv)
		m = 0;
	zz = 1.0f;
	zx = 0.5f;
	zy = 0.5f;
}

void PlaybackChannel::integrateZoom(double dt)
{
	const int mx = mv[0].load(), my = mv[1].load(), mz = mv[2].load();
	if (mx || my || mz) {
		animOn = false; /* mexer na mão interrompe o movimento automático */
	} else if (animOn.load()) {
		std::lock_guard<std::mutex> lk(animMtx);
		animT += dt;
		const double k = std::min(1.0, animT / animDur);
		const double e = k < 0.5 ? 2 * k * k : 1 - std::pow(-2 * k + 2, 2) / 2; /* começa e termina devagar */
		/* zoom interpolado em escala logarítmica: velocidade constante na tela */
		float z = (float)std::exp(std::log(animA[0]) + (std::log(animB[0]) - std::log(animA[0])) * e);
		float x = (float)(animA[1] + (animB[1] - animA[1]) * e);
		float y = (float)(animA[2] + (animB[2] - animA[2]) * e);
		clampZoom(z, x, y);
		zz = z;
		zx = x;
		zy = y;
		if (k >= 1.0)
			animOn = false;
		return;
	} else {
		return;
	}
	float z = zz.load(), x = zx.load(), y = zy.load();
	if (mz)
		z *= (float)std::exp(mz * 0.9 * dt); /* dobra em ~0,8 s */
	/* metade da área visível por segundo: mesma sensação em qualquer zoom */
	x += (float)(mx * 0.5 * dt / z);
	y += (float)(my * 0.5 * dt / z);
	clampZoom(z, x, y);
	zz = z;
	zx = x;
	zy = y;
}

void PlaybackChannel::removeOutput(obs_source_t *source)
{
	std::lock_guard<std::mutex> lk(outMtx);
	for (size_t i = 0; i < outputs.size(); i++) {
		if (outputs[i].src == source) {
			outputs.erase(outputs.begin() + (ptrdiff_t)i);
			break;
		}
	}
}

void PlaybackChannel::leaveLive()
{
	/* chamado com mtx travado */
	if (liveMode && session) {
		int64_t l = session->liveFrame();
		pos = l < 0 ? 0 : (double)l;
	}
	liveMode = false;
}

void PlaybackChannel::play(int direction)
{
	std::lock_guard<std::mutex> lk(mtx);
	leaveLive();
	dir = direction < 0 ? -1 : 1;
	/* play de novo depois que o trecho terminou: recomeça */
	if (dir > 0 && !list.empty() && pos >= (double)list[listIdx].out) {
		listIdx = 0;
		applyItem();
	}
	/* reverso a partir do início do clipe: começa pelo fim */
	if (dir < 0 && !list.empty() && pos <= (double)list[listIdx].in)
		pos = (double)list[listIdx].out;
	playing = true;
}

void PlaybackChannel::pause()
{
	std::lock_guard<std::mutex> lk(mtx);
	leaveLive();
	playing = false;
	mult = 1.0;
}

void PlaybackChannel::setSpeed(double s)
{
	std::lock_guard<std::mutex> lk(mtx);
	speed = s < 0 ? 0 : (s > 1 ? 1 : s);
}

void PlaybackChannel::setMultiplier(double m)
{
	std::lock_guard<std::mutex> lk(mtx);
	mult = m < 1 ? 1 : m;
}

void PlaybackChannel::step(int frames)
{
	std::lock_guard<std::mutex> lk(mtx);
	leaveLive();
	playing = false;
	list.clear();
	pos = std::floor(pos) + frames;
	if (pos < 0)
		pos = 0;
}

void PlaybackChannel::seek(double f, bool keepPlaying)
{
	std::lock_guard<std::mutex> lk(mtx);
	liveMode = false;
	list.clear();
	pos = f < 0 ? 0 : f;
	if (!keepPlaying)
		playing = false;
}

void PlaybackChannel::seekRelative(double frames)
{
	std::lock_guard<std::mutex> lk(mtx);
	leaveLive();
	list.clear();
	pos += frames;
	if (pos < 0)
		pos = 0;
}

void PlaybackChannel::goLive()
{
	std::lock_guard<std::mutex> lk(mtx);
	session = liveSession;
	liveMode = true;
	playing = false;
	list.clear();
	mult = 1.0;
	dir = 1;
	refresh = true;
}

void PlaybackChannel::setAngle(int cam)
{
	std::lock_guard<std::mutex> lk(mtx);
	if (cam >= 0 && cam < MAX_CAMERAS)
		angle = cam;
}

void PlaybackChannel::cue(int64_t f)
{
	seek((double)f, false);
}

void PlaybackChannel::playRange(int64_t in, int64_t out)
{
	PlayRange r;
	r.in = in;
	r.out = out;
	playList({r}, true);
}

void PlaybackChannel::applyItem()
{
	/* chamado com mtx travado */
	const PlayRange &r = list[listIdx];
	if (r.session && r.session != session) {
		session = r.session; /* clipe de uma gravação anterior */
		refresh = true;
	}
	stingerSent = false;
	pos = (double)(r.in < 0 ? 0 : r.in);
	if (r.angle >= 0 && r.angle < MAX_CAMERAS)
		angle = r.angle;
	if (r.speed > 0)
		speed = r.speed > 1 ? 1 : r.speed;
}

void PlaybackChannel::playList(std::vector<PlayRange> ranges, bool startPlaying)
{
	std::lock_guard<std::mutex> lk(mtx);
	if (ranges.empty())
		return;
	liveMode = false;
	list = std::move(ranges);
	listIdx = 0;
	dir = 1;
	mult = 1.0;
	applyItem();
	playing = startPlaying;
}

void PlaybackChannel::setLoop(bool on)
{
	std::lock_guard<std::mutex> lk(mtx);
	loopOn = on;
}

void PlaybackChannel::setEventTransition(int type, int ms, bool matchSpeed)
{
	xfType = type;
	xfMs = ms < 0 ? 0 : ms;
	xfMatch = matchSpeed;
}

void PlaybackChannel::setAudioEnabled(bool on)
{
	std::lock_guard<std::mutex> lk(mtx);
	audioOn = on;
}

bool PlaybackChannel::restart()
{
	std::lock_guard<std::mutex> lk(mtx);
	if (list.empty() || liveMode)
		return false;
	listIdx = 0;
	applyItem();
	return true;
}

ChannelState PlaybackChannel::state()
{
	std::lock_guard<std::mutex> lk(mtx);
	ChannelState st;
	st.hasSession = !!session;
	st.hasRange = !list.empty();
	st.finished = finishedCount;
	st.stingers = stingerCount;
	if (liveSession) {
		st.recLive = liveSession->liveFrame();
		st.recFps = liveSession->fps();
	}
	if (session) {
		st.wallStartMs = session->startWallMs;
		st.live = session->liveFrame();
		st.fps = session->fps();
	}
	st.pos = liveMode ? (double)(st.live < 0 ? 0 : st.live) : pos;
	st.playing = playing;
	st.liveMode = liveMode;
	st.dir = dir;
	st.speed = speed;
	st.mult = mult;
	st.angle = angle;
	return st;
}

/* ---------------- thread do canal ---------------- */

void PlaybackChannel::loop()
{
	os_set_thread_name(id == 0 ? "replay-play-A" : "replay-play-B");
	uint64_t next = os_gettime_ns();
	uint64_t lastTick = next;

	while (!quit) {
		uint64_t interval = 33333333ULL;
		{
			std::lock_guard<std::mutex> lk(mtx);
			if (session)
				interval = 1000000000ULL * session->fpsDen / session->fpsNum;
		}
		next += interval;
		const uint64_t now = os_gettime_ns();
		if (now > next + interval * 5)
			next = now + interval; /* ficou muito para trás: ressincroniza */
		os_sleepto_ns(next);
		/* 100% = tempo real: se a máquina atrasou mais de 1 quadro (decodificação lenta), pula os
		 * quadros perdidos em vez de mostrar todos atrasados (antes o replay ficava mais lento que o real) */
		const uint64_t t = os_gettime_ns();
		double ticks = 1.0;
		if (t > next + interval) {
			const uint64_t missed = (t - next) / interval;
			ticks += (double)std::min<uint64_t>(missed, 3);
			next += missed * interval;
		}
		(void)lastTick;
		integrateZoom((double)interval * ticks / 1e9);
		tick(ticks);
		static const int slowMs = getenv("RDS_REPLAY_ATRASO_MS") ? atoi(getenv("RDS_REPLAY_ATRASO_MS")) : 0;
		if (slowMs > 0)
			os_sleep_ms((uint32_t)slowMs); /* teste: simula uma máquina que não decodifica a tempo */
	}
}

void PlaybackChannel::tick(double ticks)
{
	std::shared_ptr<Session> s;
	int64_t target;
	int a;
	bool wantAudio = false;
	bool force;
	bool slowBlend = false;
	double frac = 0;
	int64_t liveNow = 0;

	{
		std::lock_guard<std::mutex> lk(mtx);
		s = session;
		if (!s)
			return;
		int64_t live = s->liveFrame();
		int64_t first = s->firstFrame();
		if (live < 0 || first < 0)
			return;

		if (liveMode) {
			pos = (double)live;
		} else if (playing) {
			const double rate = speed * mult;
			pos += dir * rate * ticks;
			if (!list.empty() && dir > 0 && !stingerSent && stingerLeadMs.load() > 0 && rate > 0 &&
			    (listIdx + 1 < list.size() || (loopOn && list.size() > 1))) {
				const double ms = ((double)list[listIdx].out - pos) / rate * 1000.0 / s->fps();
				if (ms <= (double)stingerLeadMs.load()) {
					stingerCount++;
					stingerSent = true;
				}
			}
			if (!list.empty() && dir > 0 && pos >= (double)list[listIdx].out) {
				if (listIdx + 1 < list.size()) {
					listIdx++;
					applyItem();
					xfPending = true;
				} else if (loopOn) {
					const bool multi = list.size() > 1;
					listIdx = 0;
					applyItem();
					xfPending = multi;
				} else {
					pos = (double)list[listIdx].out;
					playing = false;
					finishedCount++;
				}
			}
			if (!list.empty() && dir < 0 && playing && pos <= (double)list[listIdx].in) {
				pos = (double)list[listIdx].in;
				playing = false;
				finishedCount++;
			}
			wantAudio = audioOn && playing && dir > 0 && std::fabs(rate - 1.0) < 1e-6 && pos < (double)live;
		}
		if (session != s) {
			/* a virada de evento trocou de gravação */
			s = session;
			live = s->liveFrame();
			first = s->firstFrame();
			if (live < 0 || first < 0)
				return;
		}
		if (pos > (double)live)
			pos = (double)live; /* encostou no "ao vivo": segue junto */
		if (pos < (double)first) {
			pos = (double)first;
			if (dir < 0)
				playing = false;
		}

		s = session; /* pode ter mudado na virada de evento */
		target = (int64_t)std::floor(pos);
		frac = pos - (double)target;
		liveNow = live;
		slowBlend = smoothSlow.load() && playing && !liveMode && speed * mult < 0.999;
		a = angle;
		force = refresh;
		refresh = false;

		if (xfPending) {
			xfPending = false;
			const double fps = s->fps();
			if ((xfType.load() == 1 || xfType.load() == 2) && xfMs.load() > 0 && cur.valid) {
				prev = cur; /* último quadro do evento anterior */
				xfActive = true;
				xfStartPos = pos;
				xfTicks = 0;
				xfFrames = xfMs.load() * fps / 1000.0;
				if (xfFrames < 1)
					xfFrames = 1;
			}
		}
		if (!playing && xfActive)
			xfActive = false; /* pausou no meio: mostra o quadro limpo */
	}

	const int64_t prevShown = shownFrame;
	holdEmit = slowBlend && !xfActive;
	/* câmera lenta suave: o quadro seguinte já decodificado vira o atual */
	if (holdEmit && !force && target != shownFrame && nbFrame == target && nbAngle == a && nbSession == s.get()) {
		std::swap(cur, nb);
		nbFrame = -1;
		shownFrame = target;
		shownAngle = a;
		shownSession = s.get();
	} else if (force || xfActive || target != shownFrame || a != shownAngle || s.get() != shownSession) {
		showFrame(*s, a, target, xfActive && target == prevShown && a == shownAngle);
	}
	if (holdEmit && shownFrame == target && cur.valid) {
		const int64_t nf = target + 1;
		if (nf <= liveNow && (nbFrame != nf || nbAngle != a || nbSession != s.get())) {
			if (decodeTo(*s, a, nf, nb)) {
				nbFrame = nf;
				nbAngle = a;
				nbSession = s.get();
			} else {
				nbFrame = -1;
			}
		}
		if (nbFrame == nf)
			emitMix(cur, nb, frac);
		else
			emitPic(cur);
	}
	holdEmit = false;

	if (xfActive) {
		double alpha;
		if (xfMatch.load()) {
			std::lock_guard<std::mutex> lk(mtx);
			alpha = (pos - xfStartPos) / xfFrames;
		} else {
			alpha = (double)(++xfTicks) / xfFrames;
		}
		if (alpha >= 1.0) {
			xfActive = false;
			emitPic(cur);
		} else {
			emitBlended(alpha < 0 ? 0 : alpha);
		}
	}

	if (wantAudio)
		outputAudio(*s, target);
	else
		audioCont = false;
}

bool PlaybackChannel::ensureDecoder(int codecId)
{
	if (dec && decCodec == codecId)
		return true;
	if (dec)
		avcodec_free_context(&dec);

	const AVCodec *codec = avcodec_find_decoder((AVCodecID)codecId);
	if (!codec)
		return false;
	dec = avcodec_alloc_context3(codec);
	dec->flags |= AV_CODEC_FLAG_LOW_DELAY;
	dec->thread_count = 4;
	dec->thread_type = FF_THREAD_SLICE;
#if defined(__APPLE__) || defined(_WIN32)
	/* decodificar na placa/chip: a 60 quadros/s a CPU não dá conta de cada quadro inteiro (intra)
	 * a tempo e o replay a 100% anda devagar. Mac: VideoToolbox; Windows: D3D11VA (NVIDIA/AMD/Intel).
	 * Se a placa não aceitar o formato, o FFmpeg volta sozinho para a CPU. */
#ifdef __APPLE__
	static const AVHWDeviceType hwType = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#else
	static const AVHWDeviceType hwType = AV_HWDEVICE_TYPE_D3D11VA;
#endif
	if ((codecId == AV_CODEC_ID_H264 || codecId == AV_CODEC_ID_HEVC) && !getenv("RDS_REPLAY_CPU")) {
		if (!hwDev && av_hwdevice_ctx_create(&hwDev, hwType, nullptr, nullptr, 0) < 0)
			hwDev = nullptr;
		if (hwDev) {
			dec->hw_device_ctx = av_buffer_ref(hwDev);
			dec->get_format = [](AVCodecContext *, const enum AVPixelFormat *fmts) {
				for (const enum AVPixelFormat *f = fmts; *f != AV_PIX_FMT_NONE; f++)
					if (*f == AV_PIX_FMT_VIDEOTOOLBOX || *f == AV_PIX_FMT_D3D11)
						return *f;
				/* sem a placa: o primeiro formato de CPU */
				for (const enum AVPixelFormat *f = fmts; *f != AV_PIX_FMT_NONE; f++)
					if (!(av_pix_fmt_desc_get(*f)->flags & AV_PIX_FMT_FLAG_HWACCEL))
						return *f;
				return fmts[0];
			};
		}
	}
#endif
	if (avcodec_open2(dec, codec, nullptr) < 0) {
		avcodec_free_context(&dec);
		return false;
	}
	decCodec = codecId;
	blog(LOG_INFO, "[ReplayDeck] canal %d: decodificando %s %s", id + 1, codec->name,
#ifdef __APPLE__
	     dec->hw_device_ctx ? "no chip (VideoToolbox)" : "pela CPU");
#else
	     dec->hw_device_ctx ? "na placa de vídeo (D3D11VA)" : "pela CPU");
#endif
	return true;
}

void PlaybackChannel::showFrame(Session &s, int a, int64_t target, bool reuse)
{
	/* ninguém está exibindo este canal: não gasta CPU decodificando */
	{
		std::lock_guard<std::mutex> lk(outMtx);
		bool visible = false;
		for (const Out &o : outputs)
			visible = visible || obs_source_showing(o.src);
		if (!visible) {
			shownFrame = -1;
			return;
		}
	}

	if (reuse && cur.valid)
		return; /* mesmo quadro durante a transição: só reblenda */

	shownFrame = target;
	shownAngle = a;
	shownSession = &s;

	if (!decodeTo(s, a, target, cur))
		return;
	if (!xfActive && !holdEmit)
		emitPic(cur);
}

/* lê e decodifica um quadro da câmera "a" para "out" */
bool PlaybackChannel::decodeTo(Session &s, int a, int64_t target, Pic &out)
{
	std::shared_ptr<Recording> rec = s.cams[a];
	if (!rec)
		return false;

	int64_t actual = 0;
	if (!rec->read(target, buf, actual))
		return false;
	const size_t size = buf.size();
	buf.resize(size + AV_INPUT_BUFFER_PADDING_SIZE, 0);

	if (!ensureDecoder(rec->codecId))
		return false;

	pkt->data = buf.data();
	pkt->size = (int)size;
	if (avcodec_send_packet(dec, pkt) < 0) {
		avcodec_flush_buffers(dec);
		return false;
	}
	int ret = avcodec_receive_frame(dec, frame);
	if (ret == AVERROR(EAGAIN)) {
		/* decoder segurou o quadro (reordenação): força a saída */
		avcodec_send_packet(dec, nullptr);
		ret = avcodec_receive_frame(dec, frame);
		avcodec_flush_buffers(dec);
	}
	if (ret < 0)
		return false;

	AVFrame *src = frame;
	if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX || frame->format == AV_PIX_FMT_D3D11) {
		/* traz o quadro decodificado na placa/chip para a memória (NV12) */
		av_frame_unref(swFrame);
		if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
			av_frame_unref(frame);
			return false;
		}
		av_frame_copy_props(swFrame, frame);
		av_frame_unref(frame);
		src = swFrame;
	}

	enum video_format fmt;
	bool full = src->color_range == AVCOL_RANGE_JPEG;
	switch (src->format) {
	case AV_PIX_FMT_YUVJ420P:
		full = true;
		[[fallthrough]];
	case AV_PIX_FMT_YUV420P:
		fmt = VIDEO_FORMAT_I420;
		break;
	case AV_PIX_FMT_YUVJ422P:
		full = true;
		[[fallthrough]];
	case AV_PIX_FMT_YUV422P:
		fmt = VIDEO_FORMAT_I422;
		break;
	case AV_PIX_FMT_YUVJ444P:
		full = true;
		[[fallthrough]];
	case AV_PIX_FMT_YUV444P:
		fmt = VIDEO_FORMAT_I444;
		break;
	case AV_PIX_FMT_NV12:
		fmt = VIDEO_FORMAT_NV12;
		break;
	default:
		av_frame_unref(src);
		return false;
	}

	storePic(src, fmt, full, out);
	av_frame_unref(src);
	return true;
}

void PlaybackChannel::storePic(const AVFrame *f, enum video_format fmt, bool full, Pic &p)
{
	p.format = fmt;
	p.width = (uint32_t)f->width;
	p.height = (uint32_t)f->height;
	p.full = full;
	const uint32_t h = p.height;
	const uint32_t h2 = (h + 1) / 2;
	switch (fmt) {
	case VIDEO_FORMAT_NV12:
		p.planes = 2;
		p.rows[0] = h;
		p.rows[1] = h2;
		break;
	case VIDEO_FORMAT_I420:
		p.planes = 3;
		p.rows[0] = h;
		p.rows[1] = p.rows[2] = h2;
		break;
	default: /* I422 / I444 */
		p.planes = 3;
		p.rows[0] = p.rows[1] = p.rows[2] = h;
		break;
	}
	for (int i = 0; i < p.planes; i++) {
		const uint32_t ls = (uint32_t)std::abs(f->linesize[i]);
		p.linesize[i] = ls;
		p.plane[i].resize((size_t)ls * p.rows[i]);
		memcpy(p.plane[i].data(), f->data[i], p.plane[i].size());
	}
	p.valid = true;
}

void PlaybackChannel::emitPic(const Pic &p)
{
	if (!p.valid)
		return;
	struct obs_source_frame f = {};
	f.format = p.format;
	for (int i = 0; i < p.planes; i++) {
		f.data[i] = const_cast<uint8_t *>(p.plane[i].data());
		f.linesize[i] = p.linesize[i];
	}
	f.width = p.width;
	f.height = p.height;
	f.timestamp = os_gettime_ns();
	f.full_range = p.full;
	video_format_get_parameters_for_format(VIDEO_CS_709, p.full ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL, f.format,
					       f.color_matrix, f.color_range_min, f.color_range_max);

	std::lock_guard<std::mutex> lk(outMtx);
	for (const Out &o : outputs)
		obs_source_output_video(o.video, &f);
}

/* a + (b − a) × alpha, plano a plano (mesmo formato e tamanho) */
void PlaybackChannel::emitMix(const Pic &a, const Pic &b, double alpha)
{
	if (!a.valid || !b.valid || a.format != b.format || a.width != b.width || a.height != b.height ||
	    a.planes != b.planes) {
		emitPic(a);
		return;
	}
	const int wb = (int)(alpha * 256.0 + 0.5);
	if (wb <= 0) {
		emitPic(a);
		return;
	}
	if (wb >= 256) {
		emitPic(b);
		return;
	}
	const int wa = 256 - wb;
	mix = a;
	for (int i = 0; i < a.planes; i++) {
		const uint8_t *pa = a.plane[i].data();
		const uint8_t *pb = b.plane[i].data();
		uint8_t *o = mix.plane[i].data();
		const size_t n = std::min(mix.plane[i].size(), b.plane[i].size());
		for (size_t k = 0; k < n; k++)
			o[k] = (uint8_t)((pa[k] * wa + pb[k] * wb + 128) >> 8);
	}
	emitPic(mix);
}

/* Mistura o quadro anterior (prev) com o atual (cur).
 *  mix:  prev -> cur
 *  fade: prev -> preto -> cur */
void PlaybackChannel::emitBlended(double alpha)
{
	if (!prev.valid || !cur.valid || prev.format != cur.format || prev.width != cur.width ||
	    prev.height != cur.height || prev.planes != cur.planes) {
		emitPic(cur);
		return;
	}
	mix = cur;
	const bool dip = xfType.load() == 2;
	const uint8_t blackY = cur.full ? 0 : 16;

	for (int i = 0; i < cur.planes; i++) {
		const uint8_t *pa = prev.plane[i].data();
		const uint8_t *pb = cur.plane[i].data();
		uint8_t *o = mix.plane[i].data();
		const size_t n = mix.plane[i].size();
		const uint8_t black = i == 0 ? blackY : 128; /* croma neutro = 128 */

		if (!dip) {
			const int wb = (int)(alpha * 256.0 + 0.5);
			const int wa = 256 - wb;
			for (size_t k = 0; k < n; k++)
				o[k] = (uint8_t)((pa[k] * wa + pb[k] * wb) >> 8);
		} else {
			const bool firstHalf = alpha < 0.5;
			const uint8_t *src = firstHalf ? pa : pb;
			const double t = firstHalf ? alpha * 2.0 : (alpha - 0.5) * 2.0;
			const int wSrc = (int)((firstHalf ? 1.0 - t : t) * 256.0 + 0.5);
			const int wBlk = 256 - wSrc;
			const int blk = black * wBlk;
			for (size_t k = 0; k < n; k++)
				o[k] = (uint8_t)((src[k] * wSrc + blk) >> 8);
		}
	}
	emitPic(mix);
}

void PlaybackChannel::outputAudio(Session &s, int64_t frameNo)
{
	if (!s.audio) {
		audioCont = false;
		return;
	}
	const int64_t s0 = frameNo * REPLAY_AUDIO_RATE * (int64_t)s.fpsDen / (int64_t)s.fpsNum;
	const int64_t s1 = (frameNo + 1) * REPLAY_AUDIO_RATE * (int64_t)s.fpsDen / (int64_t)s.fpsNum;
	const size_t n = (size_t)(s1 - s0);

	abuf.resize(n * REPLAY_AUDIO_CHANNELS);
	const size_t got = s.audio->read(s0, n, abuf.data());
	if (!got) {
		audioCont = false;
		return;
	}

	/* toca o par de canais escolhido nas configurações */
	const size_t off = (size_t)audioPair.load() * 2;
	aL.resize(got);
	aR.resize(got);
	for (size_t i = 0; i < got; i++) {
		aL[i] = abuf[i * REPLAY_AUDIO_CHANNELS + off];
		aR[i] = abuf[i * REPLAY_AUDIO_CHANNELS + off + 1];
	}

	if (!audioCont) {
		audioBaseTs = os_gettime_ns();
		audioSamplesOut = 0;
		audioCont = true;
	}

	struct obs_source_audio au = {};
	au.data[0] = (const uint8_t *)aL.data();
	au.data[1] = (const uint8_t *)aR.data();
	au.frames = (uint32_t)got;
	au.speakers = SPEAKERS_STEREO;
	au.format = AUDIO_FORMAT_FLOAT_PLANAR;
	au.samples_per_sec = REPLAY_AUDIO_RATE;
	au.timestamp = audioBaseTs + audioSamplesOut * 1000000000ULL / REPLAY_AUDIO_RATE;
	audioSamplesOut += got;

	std::lock_guard<std::mutex> lk(outMtx);
	for (const Out &o : outputs)
		obs_source_output_audio(o.src, &au);
}
