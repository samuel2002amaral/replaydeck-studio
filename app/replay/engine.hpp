#pragma once
#include <vector>
#include "camera-recorder.hpp"
#include "playback-channel.hpp"
#include "session.hpp"
#include <atomic>
#include <map>

/* Mbps estimados por câmera (qualidade constante varia com a imagem: média típica) */
double recEstimateMbps(const struct RecConfig &rc);

/* uma gravação (pasta Replay_AAAA-MM-DD_hh-mm-ss) na pasta de sessões */
struct SessionInfo {
	std::string dir;
	std::string name;
	uint64_t bytes = 0;
	int events = 0;
	bool recording = false; /* a que está gravando agora (não pode apagar) */
};

/* enquadramentos prontos do zoom */
struct ZoomPreset {
	const char *key;   /* nome no atalho e na API (ReplayZoomPreset Value=key) */
	const char *label; /* curto, para o botão */
	const char *desc;  /* atalho / dica */
	bool moving;       /* com movimento (anima) */
	bool hasFrom;      /* movimento parte de um ponto fixo (senão, de onde está) */
	float from[3];     /* z, x, y */
	float to[3];
	float slow;        /* multiplica a duração */
};
const std::vector<ZoomPreset> &zoomPresets();

struct ReplayConfig {
	std::string cameras[MAX_CAMERAS]; /* nomes das fontes do OBS */
	std::string outputDir;
	RecConfig rec;
	int trackA = 1;
	int trackB = 0;
	int playbackPair = 0; /* 0 = canais 1-2, 1 = canais 3-4 */

	/* ---- transições ---- */
	std::string replayScene;  /* vazio = cena que tem a saída A */
	bool autoIn = true;       /* põe no ar ao dar play */
	bool autoOut = true;      /* volta à cena anterior ao terminar */
	int outHoldMs = 0;        /* segura o último quadro antes de sair */
	std::string inTransition; /* vazio = transição atual do OBS */
	int inDurationMs = 0;     /* 0 = duração atual do OBS */
	std::string outTransition;
	int outDurationMs = 0;
	int playEventsMode = 0;   /* 0 lista inteira, 1 a partir do selecionado, 2 só o selecionado */
	int eventTransition = 1;  /* 0 corte, 1 mix, 2 fade p/ preto, 3 transição do OBS */
	std::string stingerSource; /* nome da transição do OBS (GT Stinger, Stinger...) usada entre eventos */
	int stingerDurationMs = 1000; /* duração, para transições sem duração própria */
	int stingerCutMs = 500;
	/* controle pelo app (API compatível com a do vMix) */
	bool apiEnabled = true;
	int apiHttpPort = 8088;
	int apiTcpPort = 8099;
	bool apiLocalOnly = false; /* true: só aceita deste computador */
	bool stingerCoverOverlays = true;
	/* zoom volta para a imagem inteira a cada marcação nova e a cada evento tocado
	 * (esquecer o zoom ligado e cortar com zoom errado) */
	bool zoomResetOnMark = true;
	bool zoomResetOnEvents = true;
	bool smoothSlow = false;
	/* disco: limite da pasta de gravações (GB, 0 = sem limite) e espaço livre mínimo (GB).
	 * Passou do limite → apaga as gravações mais antigas (nunca a que está gravando).
	 * Sem nada para apagar e disco abaixo do mínimo → para a gravação. */
	int cacheLimitGB = 0;
	int minFreeGB = 5; /* câmera lenta suave: mistura quadros vizinhos */ /* stinger da troca de cena passa por cima dos overlays/títulos */   /* quanto antes da virada a transição começa */
	int eventDurationMs = 500;
	int zoomAnimMs = 2000; /* duração das predefinições com movimento */
	bool eventMatchSpeed = false;
};

struct ReplayEvent {
	int64_t id = 0;
	int64_t in = 0;
	int64_t out = 0;
	uint32_t angleMask = 0;              /* ângulos marcados (bit 0 = câmera 1) */
	std::string comments[MAX_CAMERAS];   /* comentário por ângulo */
	int speedPct = 0;                    /* 0 = usa a velocidade atual */
	std::string sessionDir;              /* gravação a que o evento pertence */

	int firstAngle() const
	{
		for (int i = 0; i < MAX_CAMERAS; i++)
			if (angleMask & (1u << i))
				return i;
		return -1;
	}
};

enum ChannelSel { SEL_A = 1, SEL_B = 2, SEL_AB = 3 };

class ReplayEngine {
public:
	ReplayEngine();
	~ReplayEngine();

	/* configuração */
	ReplayConfig config();
	void setConfig(const ReplayConfig &c);
	void loadConfig();
	void saveConfig();

	/* gravação */
	bool startRecording(std::string &err);
	void stopRecording();
	bool recording() const { return isRecording.load(); }
	std::string sessionDir();
	uint64_t droppedFrames();

	/* canais */
	PlaybackChannel &channel(int i) { return *channels[i]; }
	std::shared_ptr<PlaybackChannel> channelPtr(int i) { return channels[i]; }
	void setSelection(int sel) { selection = sel; }
	int getSelection() const { return selection.load(); }

	/* transporte (aplica nos canais selecionados) */
	void play();
	void playReverse();
	void pause();
	void togglePlay();
	void setSpeedPercent(int pct);
	int speedPercent() const { return speedPct.load(); }
	void step(int frames);
	void jumpSeconds(double sec);
	void fastForward();
	/* Studio: toca numa direção (1 frente, -1 ré) com um multiplicador, enquanto um atalho está apertado */
	void shuttle(int dir, double mult);
	void goLive();
	void setAngle(int cam);
	/* gravações no disco */
	std::vector<SessionInfo> listSessions();
	bool deleteSession(const std::string &dir, std::string &err);
	/* aplica o limite e o espaço mínimo; devolve um aviso ("" = nada a fazer) */
	std::string enforceDiskLimits();
	/* limpa todo o cache do plugin no disco: todas as gravações (com os clipes exportados),
	 * a lista de eventos e os arquivos temporários. Para a gravação antes. */
	uint64_t clearAllCache(std::string &err);
	/* zoom digital nas saídas selecionadas: axis 0 = esquerda/direita, 1 = cima/baixo,
	 * 2 = aproximar/afastar; dir -1/0/+1 enquanto apertado */
	void zoomMove(int axis, int dir);
	void zoomStep(float factor, float dx = 0, float dy = 0);
	void zoomReset();
	void zoomResetAll(); /* os dois canais (A e B) */
	void zoomSet(float z, float x, float y);
	/* predefinição (índice em zoomPresets()); as com movimento usam cfg.zoomAnimMs */
	void zoomPreset(int index);
	void zoomPresetByKey(const std::string &key);
	void seekTo(int64_t frame);
	void syncBToA();

	/* eventos */
	void markIn();
	void markOut();
	void markCancel();
	void autoEvent(int seconds);
	void setActiveList(int list);
	int activeList() const { return activeListIdx.load(); }
	std::vector<ReplayEvent> events(int list);
	void playEvent(int list, int64_t id);
	void cueEvent(int list, int64_t id);
	void deleteEvent(int list, int64_t id);
	void setEventAngleChecked(int list, int64_t id, int cam, bool on);
	void setEventAngleComment(int list, int64_t id, int cam, const std::string &comment);
	void setEventSpeed(int list, int64_t id, int pct);
	/* Play Events: toca a lista em sequência (fromId = 0: lista inteira). */
	void playEvents(int list, int64_t fromId);
	void restartClip();
	/* Play Events conforme a configuração (selectedId = 0: sem seleção) */
	void playEventsConfigured(int list, int64_t selectedId);
	bool playLastEvent();
	/* toca todos os ângulos marcados do evento, em sequência */
	void playEventAllAngles(int list, int64_t id);
	/* bytes gravados e segundos decorridos da gravação atual */
	bool recordingStats(uint64_t &bytes, double &seconds);
	void setReverse(bool on);
	bool reverseOn() const { return reverseFlag.load(); }
	void setLoop(bool on);
	bool loopOn() const { return loopFlag.load(); }
	void setAudio(bool on);
	bool audioOn() const { return audioFlag.load(); }
	/* Exporta para MP4/MOV em segundo plano (id = 0: todos da lista). */
	void exportEvents(int list, int64_t id);
	std::string exportStatus();
	bool exporting() const { return exportBusy.load(); }
	void setEventInOutFromPlayhead(int list, int64_t id, bool isIn);
	uint64_t eventsVersion() const { return evVersion.load(); }
	bool hasPendingIn();

private:
	template<typename F> void forSelected(F f)
	{
		const int sel = selection.load();
		if (sel & SEL_A)
			f(*channels[0]);
		if (sel & SEL_B)
			f(*channels[1]);
	}
	void addEvent(int64_t in, int64_t out);
	void saveEvents();
	void loadEvents();
	void applyChannelConfig();
	std::shared_ptr<Session> sessionFor(const std::string &dir);
	std::map<std::string, std::shared_ptr<Session>> sessionCache;
	ReplayEvent *findEvent(int list, int64_t id);

	std::recursive_mutex mtx;
	ReplayConfig cfg;
	std::shared_ptr<Session> session;
	std::vector<std::unique_ptr<CameraRecorder>> recorders;
	std::atomic<bool> isRecording{false};

	std::shared_ptr<PlaybackChannel> channels[2];
	std::atomic<int> selection{SEL_A};
	std::atomic<int> speedPct{100};
	double ffMult = 1.0;

	std::vector<ReplayEvent> lists[MAX_EVENT_LISTS];
	std::atomic<int> activeListIdx{0};
	int64_t pendingIn = -1;
	int64_t nextEventId = 1;
	std::atomic<uint64_t> evVersion{0};
	std::atomic<bool> loopFlag{false};
	std::atomic<bool> reverseFlag{false};
	std::atomic<bool> audioFlag{true};
	std::vector<PlayRange> rangesFor(const ReplayEvent &ev, bool allAngles);

	std::thread exportThread;
	std::atomic<bool> exportBusy{false};
	std::mutex exportMtx;
	std::string exportMsg;
};

ReplayEngine &engine();
bool engineAlive();
/* Canal compartilhado: continua válido mesmo se o engine já foi destruído. */
std::shared_ptr<PlaybackChannel> engineChannel(int i);
void engineCreate();
void engineDestroy();
