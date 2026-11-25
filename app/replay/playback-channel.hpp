#pragma once
#include "session.hpp"
#include <atomic>
#include <thread>

struct AVCodecContext;
struct AVFrame;
struct AVBufferRef;
struct AVPacket;

struct PlayRange {
	int64_t in = 0;
	int64_t out = 0;
	int angle = -1;    /* -1 = mantém o ângulo atual */
	double speed = -1; /* <=0 = mantém a velocidade atual */
	std::shared_ptr<Session> session; /* nulo = sessão atual */
};

struct ChannelState {
	double pos = 0;      /* quadro atual */
	int64_t live = -1;   /* último quadro gravado */
	bool hasSession = false;
	bool playing = false;
	bool liveMode = true;
	int dir = 1;
	double speed = 1.0;  /* 0..1 (slider de câmera lenta) */
	double mult = 1.0;   /* 1, 2, 4, 8... (fast forward) */
	int angle = 0;
	double fps = 30.0;
	int64_t wallStartMs = 0;
	bool hasRange = false;
	uint64_t finished = 0;
	uint64_t stingers = 0; /* avisos de "virada de evento chegando" */
	int64_t recLive = -1;  /* último quadro da sessão que está gravando */
	double recFps = 30.0; /* conta quantas vezes um clipe/sequência chegou ao fim */
};

/* Um canal de saída de replay (A ou B). Tem sua própria thread, que a cada
 * intervalo de quadro avança a posição (com velocidade/direção), decodifica o
 * quadro do ângulo selecionado e o entrega às fontes "Replay" do OBS. */
class PlaybackChannel {
public:
	explicit PlaybackChannel(int id);
	~PlaybackChannel();

	void setSession(std::shared_ptr<Session> s);
	/* a gravação (pasta) está em uso neste canal? */
	bool usesSession(const std::string &dir);
	bool playingSession(const std::string &dir); /* o que está na tela é desta gravação? */
	/* source: a fonte da cena (visibilidade e áudio); video: quem recebe os quadros
	 * (a fonte interna que a de cena desenha com zoom) */
	void addOutput(obs_source_t *source, obs_source_t *video = nullptr);
	void removeOutput(obs_source_t *source);

	/* zoom digital do replay: z = 1 (inteiro) .. 5; x, y = centro da área (0..1).
	 * Nunca sai da imagem (sem barras pretas). */
	struct Zoom {
		float z, x, y;
	};
	Zoom zoom() const { return {zz.load(), zx.load(), zy.load()}; }
	/* movimento contínuo enquanto o atalho/botão está apertado: eixo 0 = x, 1 = y, 2 = zoom */
	void setZoomMove(int axis, int dir);
	void zoomStep(float factor, float dx, float dy);
	void resetZoom();
	/* vai direto para um enquadramento */
	void zoomSet(float z, float x, float y);
	/* movimento suave (predefinições com movimento): from = nullptr parte de onde está */
	void zoomAnimate(const float *from, const float *to, double seconds);
	bool zoomAnimating() const { return animOn.load(); }

	void play(int direction);
	void pause();
	void setSpeed(double s);
	void setMultiplier(double m);
	void step(int frames);
	void seek(double frame, bool keepPlaying = false);
	void seekRelative(double frames);
	void goLive();
	void setAngle(int cam);
	void playRange(int64_t in, int64_t out);
	/* Fila de trechos (um evento, ou vários = "Play Events"). */
	void playList(std::vector<PlayRange> ranges, bool startPlaying);
	void setLoop(bool on);
	void setAudioEnabled(bool on);
	void setAudioPair(int pair) { audioPair = pair == 1 ? 1 : 0; }
	/* Transição entre eventos do Play Events: 0 corte, 1 mix, 2 fade p/ preto */
	void setEventTransition(int type, int ms, bool matchSpeed);
	/* avisa N ms antes de cada virada de evento (0 = desligado) */
	void setStingerLead(int ms) { stingerLeadMs = ms < 0 ? 0 : ms; }
	/* câmera lenta suave: entre dois quadros gravados mostra a mistura dos dois */
	void setSmoothSlow(bool on) { smoothSlow = on; }
	bool restart();
	void cue(int64_t frame);

	ChannelState state();

private:
	void loop();
	void tick();
	void showFrame(Session &s, int angle, int64_t target, bool reuse);
	void outputAudio(Session &s, int64_t frame);
	bool ensureDecoder(int codecId);
	void leaveLive();

	int id;
	std::thread thread;
	std::atomic<bool> quit{false};
	std::atomic<int> audioPair{0}; /* 0 = canais 1-2, 1 = canais 3-4 */

	std::mutex mtx;
	std::shared_ptr<Session> session;     /* a que está tocando */
	std::shared_ptr<Session> liveSession; /* a que está gravando */
	std::atomic<int> stingerLeadMs{0};
	uint64_t stingerCount = 0;
	bool stingerSent = false;
	double pos = 0;
	bool playing = false;
	bool liveMode = true;
	int dir = 1;
	double speed = 1.0;
	double mult = 1.0;
	int angle = 0;
	std::vector<PlayRange> list;
	size_t listIdx = 0;
	bool loopOn = false;
	bool audioOn = true;
	void applyItem();
	uint64_t finishedCount = 0;

	/* ---- transição entre eventos ---- */
	struct Pic {
		std::vector<uint8_t> plane[3];
		uint32_t linesize[3] = {0, 0, 0};
		uint32_t rows[3] = {0, 0, 0};
		int planes = 0;
		enum video_format format = VIDEO_FORMAT_NONE;
		uint32_t width = 0, height = 0;
		bool full = false;
		bool valid = false;
	};
	void storePic(const struct AVFrame *f, enum video_format fmt, bool full, Pic &p);
	std::atomic<bool> smoothSlow{false};
	Pic nb;               /* quadro seguinte (câmera lenta suave) */
	int64_t nbFrame = -1;
	int nbAngle = -1;
	const void *nbSession = nullptr;
	bool holdEmit = false;
	bool decodeTo(Session &s, int angle, int64_t frame, Pic &out);
	void emitMix(const Pic &a, const Pic &b, double alpha);
	void emitPic(const Pic &pic);
	void emitBlended(double alpha);
	std::atomic<int> xfType{1};
	std::atomic<int> xfMs{500};
	std::atomic<bool> xfMatch{false};
	bool xfPending = false;   /* mtx */
	bool xfActive = false;    /* thread do canal */
	double xfStartPos = 0;
	int xfTicks = 0;
	double xfFrames = 0;
	Pic cur, prev, mix;
	bool refresh = true;

	std::mutex outMtx;
	struct Out {
		obs_source_t *src;
		obs_source_t *video;
	};
	std::vector<Out> outputs;
	std::atomic<float> zz{1.0f}, zx{0.5f}, zy{0.5f};
	std::atomic<int> mv[3] = {{0}, {0}, {0}};
	std::mutex animMtx;
	std::atomic<bool> animOn{false};
	float animA[3] = {1, 0.5f, 0.5f}, animB[3] = {1, 0.5f, 0.5f};
	double animT = 0, animDur = 1;
	void integrateZoom(double dt);
	void clampZoom(float &z, float &x, float &y);

	/* somente na thread do canal */
	AVCodecContext *dec = nullptr;
	AVFrame *frame = nullptr;
	AVFrame *swFrame = nullptr;   /* Mac: quadro trazido do chip (VideoToolbox) */
	AVBufferRef *hwDev = nullptr; /* Mac: decodificação no chip */
	AVPacket *pkt = nullptr;
	int decCodec = 0;
	std::vector<uint8_t> buf;
	int64_t shownFrame = -1;
	int shownAngle = -1;
	Session *shownSession = nullptr;
	bool audioCont = false;
	uint64_t audioBaseTs = 0;
	uint64_t audioSamplesOut = 0;
	std::vector<float> abuf, aL, aR;
};
