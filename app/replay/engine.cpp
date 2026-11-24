#include "engine.hpp"
#include "studio-host.hpp"
#include "i18n.hpp"
#include <algorithm>
#include <filesystem>
#include <util/platform.h>
extern "C" {
#include <libavcodec/codec_id.h>
}
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>

bool exportClip(Session &s, int cam, int64_t in, int64_t out, const std::string &basePath, std::string &outPath,
		std::string &err);

static ReplayEngine *g_engine = nullptr;
static std::mutex g_engineMtx;

std::shared_ptr<PlaybackChannel> engineChannel(int i)
{
	std::lock_guard<std::mutex> lk(g_engineMtx);
	if (!g_engine || i < 0 || i > 1)
		return nullptr;
	return g_engine->channelPtr(i);
}

ReplayEngine &engine()
{
	return *g_engine;
}
bool engineAlive()
{
	return g_engine != nullptr;
}
void engineCreate()
{
	if (!g_engine)
		g_engine = new ReplayEngine();
}
void engineDestroy()
{
	ReplayEngine *e;
	{
		std::lock_guard<std::mutex> lk(g_engineMtx);
		e = g_engine;
		g_engine = nullptr;
	}
	delete e;
}

ReplayEngine::ReplayEngine()
{
	channels[0] = std::make_shared<PlaybackChannel>(0);
	channels[1] = std::make_shared<PlaybackChannel>(1);
	loadConfig();
	loadEvents();
}

ReplayEngine::~ReplayEngine()
{
	if (exportThread.joinable())
		exportThread.join();
	stopRecording();
	channels[0]->setSession(nullptr);
	channels[1]->setSession(nullptr);
}

double recEstimateMbps(const RecConfig &rc)
{
	if (rc.cq <= 0)
		return rc.bitrateMbps;
	/* intra-frame 1080p30 típico por nível de qualidade */
	double base = rc.cq <= 14 ? 150 : rc.cq <= 18 ? 95 : rc.cq <= 22 ? 60 : 38;
	struct obs_video_info ovi = {};
	if (obs_get_video_info(&ovi) && ovi.fps_num && ovi.fps_den)
		base *= ((double)ovi.base_width * ovi.base_height * ovi.fps_num / ovi.fps_den) / (1920.0 * 1080.0 * 30.0);
	return base;
}

/* ---------------- configuração ---------------- */

static std::string configFile()
{
	/* pasta alternativa (usada nos testes automáticos) */
	if (const char *over = getenv("REPLAY_CONFIG_DIR")) {
		os_mkdirs(over);
		return std::string(over) + "/config.json";
	}
	char *dir = rdsReplayConfigPath(""); /* Studio: <config>/replay/ */
	std::string d = dir ? dir : "";
	bfree(dir);
	if (!d.empty())
		os_mkdirs(d.c_str());
	return d + "config.json";
}

ReplayConfig ReplayEngine::config()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	return cfg;
}

void ReplayEngine::setConfig(const ReplayConfig &c)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	cfg = c;
	applyChannelConfig();
	saveConfig();
}

void ReplayEngine::applyChannelConfig()
{
	for (auto &ch : channels) {
		ch->setAudioPair(cfg.playbackPair);
		ch->setEventTransition(cfg.eventTransition, cfg.eventDurationMs, cfg.eventMatchSpeed);
		ch->setStingerLead(cfg.eventTransition == 3 && !cfg.stingerSource.empty() ? cfg.stingerCutMs : 0);
		ch->setSmoothSlow(cfg.smoothSlow);
	}
}

void ReplayEngine::loadConfig()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	const std::string file = configFile();
	if (file.empty())
		return;
	obs_data_t *d = obs_data_create_from_json_file_safe(file.c_str(), "bak");
	if (!d)
		return;
	obs_data_set_default_string(d, "encoder", "auto");
	obs_data_set_default_int(d, "bitrate_mbps", 80);
	obs_data_set_default_int(d, "mjpeg_quality", 3);
	obs_data_set_default_int(d, "rec_cq", 0);
	obs_data_set_default_int(d, "cache_limit_gb", 0);
	obs_data_set_default_int(d, "min_free_gb", 5);
	obs_data_set_default_int(d, "track_a", 1);
	for (int i = 0; i < MAX_CAMERAS; i++) {
		char key[16];
		snprintf(key, sizeof(key), "cam%d", i + 1);
		cfg.cameras[i] = obs_data_get_string(d, key);
	}
	cfg.outputDir = obs_data_get_string(d, "output_dir");
	cfg.rec.encoder = obs_data_get_string(d, "encoder");
	cfg.rec.bitrateMbps = (int)obs_data_get_int(d, "bitrate_mbps");
	cfg.rec.mjpegQuality = (int)obs_data_get_int(d, "mjpeg_quality");
	cfg.rec.cq = (int)obs_data_get_int(d, "rec_cq");
	cfg.smoothSlow = obs_data_get_bool(d, "smooth_slow");
	cfg.cacheLimitGB = (int)obs_data_get_int(d, "cache_limit_gb");
	cfg.minFreeGB = (int)obs_data_get_int(d, "min_free_gb");
	cfg.trackA = (int)obs_data_get_int(d, "track_a");
	cfg.trackB = (int)obs_data_get_int(d, "track_b");
	cfg.playbackPair = (int)obs_data_get_int(d, "playback_pair");
	obs_data_set_default_bool(d, "auto_in", true);
	obs_data_set_default_bool(d, "auto_out", true);
	obs_data_set_default_int(d, "event_transition", 1);
	cfg.playEventsMode = (int)obs_data_get_int(d, "play_events_mode");
	obs_data_set_default_int(d, "event_duration_ms", 500);
	cfg.replayScene = obs_data_get_string(d, "replay_scene");
	obs_data_set_default_int(d, "zoom_anim_ms", 2000);
	cfg.zoomAnimMs = (int)obs_data_get_int(d, "zoom_anim_ms");
	cfg.autoIn = obs_data_get_bool(d, "auto_in");
	cfg.autoOut = obs_data_get_bool(d, "auto_out");
	cfg.outHoldMs = (int)obs_data_get_int(d, "out_hold_ms");
	cfg.inTransition = obs_data_get_string(d, "in_transition");
	cfg.inDurationMs = (int)obs_data_get_int(d, "in_duration_ms");
	cfg.outTransition = obs_data_get_string(d, "out_transition");
	cfg.outDurationMs = (int)obs_data_get_int(d, "out_duration_ms");
	cfg.eventTransition = (int)obs_data_get_int(d, "event_transition");
	cfg.eventDurationMs = (int)obs_data_get_int(d, "event_duration_ms");
	cfg.eventMatchSpeed = obs_data_get_bool(d, "event_match_speed");
	obs_data_set_default_int(d, "stinger_duration_ms", 1000);
	obs_data_set_default_int(d, "stinger_cut_ms", 500);
	obs_data_set_default_bool(d, "stinger_cover_overlays", true);
	obs_data_set_default_bool(d, "api_enabled", true);
	obs_data_set_default_int(d, "api_http_port", 8088);
	obs_data_set_default_int(d, "api_tcp_port", 8099);
	obs_data_set_default_bool(d, "zoom_reset_mark", true);
	obs_data_set_default_bool(d, "zoom_reset_events", true);
	cfg.stingerSource = obs_data_get_string(d, "stinger_source");
	cfg.stingerDurationMs = (int)obs_data_get_int(d, "stinger_duration_ms");
	cfg.stingerCutMs = (int)obs_data_get_int(d, "stinger_cut_ms");
	cfg.stingerCoverOverlays = obs_data_get_bool(d, "stinger_cover_overlays");
	cfg.zoomResetOnMark = obs_data_get_bool(d, "zoom_reset_mark");
	cfg.zoomResetOnEvents = obs_data_get_bool(d, "zoom_reset_events");
	cfg.apiEnabled = obs_data_get_bool(d, "api_enabled");
	cfg.apiHttpPort = (int)obs_data_get_int(d, "api_http_port");
	cfg.apiTcpPort = (int)obs_data_get_int(d, "api_tcp_port");
	cfg.apiLocalOnly = obs_data_get_bool(d, "api_local_only");
	applyChannelConfig();
	obs_data_release(d);
}

void ReplayEngine::saveConfig()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	obs_data_t *d = obs_data_create();
	for (int i = 0; i < MAX_CAMERAS; i++) {
		char key[16];
		snprintf(key, sizeof(key), "cam%d", i + 1);
		obs_data_set_string(d, key, cfg.cameras[i].c_str());
	}
	obs_data_set_string(d, "output_dir", cfg.outputDir.c_str());
	obs_data_set_string(d, "encoder", cfg.rec.encoder.c_str());
	obs_data_set_int(d, "bitrate_mbps", cfg.rec.bitrateMbps);
	obs_data_set_int(d, "mjpeg_quality", cfg.rec.mjpegQuality);
	obs_data_set_int(d, "rec_cq", cfg.rec.cq);
	obs_data_set_bool(d, "smooth_slow", cfg.smoothSlow);
	obs_data_set_int(d, "cache_limit_gb", cfg.cacheLimitGB);
	obs_data_set_int(d, "min_free_gb", cfg.minFreeGB);
	obs_data_set_int(d, "track_a", cfg.trackA);
	obs_data_set_int(d, "track_b", cfg.trackB);
	obs_data_set_int(d, "playback_pair", cfg.playbackPair);
	obs_data_set_string(d, "replay_scene", cfg.replayScene.c_str());
	obs_data_set_int(d, "zoom_anim_ms", cfg.zoomAnimMs);
	obs_data_set_bool(d, "auto_in", cfg.autoIn);
	obs_data_set_bool(d, "auto_out", cfg.autoOut);
	obs_data_set_int(d, "out_hold_ms", cfg.outHoldMs);
	obs_data_set_string(d, "in_transition", cfg.inTransition.c_str());
	obs_data_set_int(d, "in_duration_ms", cfg.inDurationMs);
	obs_data_set_string(d, "out_transition", cfg.outTransition.c_str());
	obs_data_set_int(d, "out_duration_ms", cfg.outDurationMs);
	obs_data_set_int(d, "play_events_mode", cfg.playEventsMode);
	obs_data_set_int(d, "event_transition", cfg.eventTransition);
	obs_data_set_int(d, "event_duration_ms", cfg.eventDurationMs);
	obs_data_set_bool(d, "event_match_speed", cfg.eventMatchSpeed);
	obs_data_set_string(d, "stinger_source", cfg.stingerSource.c_str());
	obs_data_set_int(d, "stinger_duration_ms", cfg.stingerDurationMs);
	obs_data_set_int(d, "stinger_cut_ms", cfg.stingerCutMs);
	obs_data_set_bool(d, "stinger_cover_overlays", cfg.stingerCoverOverlays);
	obs_data_set_bool(d, "zoom_reset_mark", cfg.zoomResetOnMark);
	obs_data_set_bool(d, "zoom_reset_events", cfg.zoomResetOnEvents);
	obs_data_set_bool(d, "api_enabled", cfg.apiEnabled);
	obs_data_set_int(d, "api_http_port", cfg.apiHttpPort);
	obs_data_set_int(d, "api_tcp_port", cfg.apiTcpPort);
	obs_data_set_bool(d, "api_local_only", cfg.apiLocalOnly);
	const std::string file = configFile();
	if (!file.empty())
		obs_data_save_json_safe(d, file.c_str(), "tmp", "bak");
	obs_data_release(d);
}

/* ---------------- gravação ---------------- */

bool ReplayEngine::startRecording(std::string &err)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (isRecording)
		return true;

	if (cfg.outputDir.empty()) {
		err = tlc("Escolha a pasta de gravação em Configurar.");
		return false;
	}

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		err = tlc("Vídeo do programa não inicializado.");
		return false;
	}

	char stamp[64];
	time_t now = time(nullptr);
	struct tm tmv;
#ifdef _WIN32
	localtime_s(&tmv, &now);
#else
	localtime_r(&now, &tmv);
#endif
	strftime(stamp, sizeof(stamp), "Replay_%Y-%m-%d_%H-%M-%S", &tmv);

	auto s = std::make_shared<Session>();
	s->dir = cfg.outputDir + "/" + stamp;
	s->fpsNum = ovi.fps_num;
	s->fpsDen = ovi.fps_den;
	if (os_mkdirs(s->dir.c_str()) == MKDIR_ERROR) {
		err = tl("Não foi possível criar a pasta %1").arg(QString::fromStdString(s->dir)).toStdString();
		return false;
	}
	s->startTs = os_gettime_ns();
	s->startWallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				 std::chrono::system_clock::now().time_since_epoch())
				 .count();

	std::vector<std::unique_ptr<CameraRecorder>> recs;
	obs_data_t *meta = obs_data_create();
	obs_data_set_int(meta, "fps_num", s->fpsNum);
	obs_data_set_int(meta, "fps_den", s->fpsDen);
	obs_data_set_int(meta, "start_wall_ms", s->startWallMs);

	int count = 0;
	std::string camErr;
	for (int i = 0; i < MAX_CAMERAS; i++) {
		if (cfg.cameras[i].empty())
			continue;
		obs_source_t *src = rdsFindSource(cfg.cameras[i].c_str()); /* entrada do Studio com esse nome */
		if (!src) {
			rlog(LOG_WARNING, "câmera %d: fonte '%s' não encontrada", i + 1, cfg.cameras[i].c_str());
			continue;
		}
		auto rec = std::make_shared<Recording>();
		char base[32];
		snprintf(base, sizeof(base), "/cam%d", i + 1);
		auto cr = std::make_unique<CameraRecorder>(i, src, cfg.rec, s->startTs, rec, s->dir + base);
		obs_source_release(src);

		std::string e;
		if (!cr->start(e)) {
			rlog(LOG_ERROR, "câmera %d: %s", i + 1, e.c_str());
			camErr = tl("Câmera %1: %2").arg(i + 1).arg(QString::fromStdString(e)).toStdString();
			cr->stop();
			continue;
		}
		s->cams[i] = rec;

		obs_data_t *cm = obs_data_create();
		obs_data_set_string(cm, "source", cfg.cameras[i].c_str());
		obs_data_set_string(cm, "file", rec->path.c_str());
		obs_data_set_string(cm, "encoder", cr->encoderName().c_str());
		obs_data_set_int(cm, "width", rec->width);
		obs_data_set_int(cm, "height", rec->height);
		obs_data_set_obj(meta, base + 1, cm);
		obs_data_release(cm);

		recs.push_back(std::move(cr));
		count++;
	}

	if (!count) {
		obs_data_release(meta);
		err = camErr.empty() ? std::string(tlc("Nenhuma câmera configurada.")) : camErr;
		return false;
	}

	auto audio = std::make_shared<AudioRecorder>();
	if (audio->start(s->dir + "/audio.wav", cfg.trackA, cfg.trackB, s->startTs))
		s->audio = audio;
	else
		rlog(LOG_WARNING, "áudio não pôde ser iniciado");

	obs_data_save_json(meta, (s->dir + "/session.json").c_str());
	obs_data_release(meta);

	session = s;
	sessionCache[s->dir] = s;
	recorders = std::move(recs);
	pendingIn = -1;

	channels[0]->setSession(s);
	channels[1]->setSession(s);
	/* se a câmera atual de um canal não está gravando, usa a primeira que está */
	for (auto &ch : channels) {
		if (s->cams[ch->state().angle])
			continue;
		for (int i = 0; i < MAX_CAMERAS; i++) {
			if (s->cams[i]) {
				ch->setAngle(i);
				break;
			}
		}
	}
	isRecording = true;
	rlog(LOG_INFO, "sessão iniciada em %s (%d câmeras)", s->dir.c_str(), count);
	return true;
}

void ReplayEngine::stopRecording()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (!isRecording)
		return;
	for (auto &r : recorders)
		r->stop();
	recorders.clear();
	if (session && session->audio)
		session->audio->stop();
	isRecording = false;
	/* a sessão continua aberta para playback até a próxima gravação */
	rlog(LOG_INFO, "sessão encerrada");
}

std::string ReplayEngine::sessionDir()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	return session ? session->dir : std::string();
}

uint64_t ReplayEngine::droppedFrames()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	uint64_t n = 0;
	for (auto &r : recorders)
		n += r->droppedFrames();
	return n;
}

/* ---------------- transporte ---------------- */

void ReplayEngine::play()
{
	ffMult = 1.0;
	const int d = reverseFlag.load() ? -1 : 1;
	forSelected([d](PlaybackChannel &c) {
		c.setMultiplier(1.0);
		c.play(d);
	});
}

/* atalho "play reverso": liga/desliga o modo reverso */
void ReplayEngine::playReverse()
{
	setReverse(!reverseFlag.load());
}

void ReplayEngine::setReverse(bool on)
{
	reverseFlag = on;
	const int d = on ? -1 : 1;
	/* se já está tocando, inverte na hora */
	forSelected([d](PlaybackChannel &c) {
		const ChannelState st = c.state();
		if (st.playing && !st.liveMode)
			c.play(d);
	});
}

void ReplayEngine::pause()
{
	ffMult = 1.0;
	forSelected([](PlaybackChannel &c) { c.pause(); });
}

void ReplayEngine::togglePlay()
{
	const int sel = selection.load();
	ChannelState st = channels[(sel & SEL_A) ? 0 : 1]->state();
	if (st.playing && !st.liveMode)
		pause();
	else
		play();
}

void ReplayEngine::setSpeedPercent(int pct)
{
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	speedPct = pct;
	/* a velocidade vale para os dois canais, como o T-bar do vMix */
	channels[0]->setSpeed(pct / 100.0);
	channels[1]->setSpeed(pct / 100.0);
}

void ReplayEngine::step(int frames)
{
	forSelected([frames](PlaybackChannel &c) { c.step(frames); });
}

void ReplayEngine::jumpSeconds(double sec)
{
	forSelected([sec](PlaybackChannel &c) {
		ChannelState st = c.state();
		c.seekRelative(sec * st.fps);
	});
}

void ReplayEngine::fastForward()
{
	/* cada toque dobra: 2x, 4x, 8x, 16x e volta para 2x */
	ffMult = ffMult >= 16.0 ? 2.0 : ffMult * 2.0;
	const double m = ffMult;
	forSelected([m](PlaybackChannel &c) {
		c.play(1);
		c.setMultiplier(m);
	});
}

void ReplayEngine::shuttle(int dir, double mult)
{
	ffMult = mult;
	forSelected([dir, mult](PlaybackChannel &c) {
		c.setMultiplier(mult);
		c.play(dir < 0 ? -1 : 1);
	});
}

void ReplayEngine::goLive()
{
	ffMult = 1.0;
	forSelected([](PlaybackChannel &c) { c.goLive(); });
}

void ReplayEngine::setAngle(int cam)
{
	forSelected([cam](PlaybackChannel &c) { c.setAngle(cam); });
}

/* ---------------- gravações no disco ---------------- */

static std::filesystem::path fsPath(const std::string &utf8)
{
#if defined(_WIN32)
	return std::filesystem::u8path(utf8);
#else
	return std::filesystem::path(utf8);
#endif
}

static uint64_t folderBytes(const std::string &dir)
{
	uint64_t total = 0;
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator(fsPath(dir), ec);
	     !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
		std::error_code e2;
		if (it->is_regular_file(e2))
			total += (uint64_t)it->file_size(e2);
	}
	return total;
}

std::vector<SessionInfo> ReplayEngine::listSessions()
{
	std::string base, recDir;
	std::vector<std::string> evDirs;
	{
		std::lock_guard<std::recursive_mutex> lk(mtx);
		base = cfg.outputDir;
		if (isRecording && session)
			recDir = session->dir;
		for (auto &l : lists)
			for (auto &e : l)
				evDirs.push_back(e.sessionDir);
	}
	std::vector<SessionInfo> out;
	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator(fsPath(base), ec);
	     !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
		std::error_code e2;
		if (!it->is_directory(e2) || !std::filesystem::exists(it->path() / "session.json", e2))
			continue;
		SessionInfo si;
#if defined(_WIN32)
		si.name = it->path().filename().u8string();
#else
		si.name = it->path().filename().string();
#endif
		si.dir = base + "/" + si.name;
		si.bytes = folderBytes(si.dir);
		si.recording = !recDir.empty() && fsPath(recDir) == fsPath(si.dir);
		for (const std::string &d : evDirs)
			si.events += fsPath(d) == fsPath(si.dir) ? 1 : 0;
		out.push_back(si);
	}
	/* o nome tem data e hora: ordem alfabética = mais antiga primeiro */
	std::sort(out.begin(), out.end(), [](const SessionInfo &a, const SessionInfo &b) { return a.name < b.name; });
	return out;
}

bool ReplayEngine::deleteSession(const std::string &dir, std::string &err)
{
	{
		std::lock_guard<std::recursive_mutex> lk(mtx);
		if (isRecording && session && fsPath(session->dir) == fsPath(dir)) {
			err = tlc("É a gravação em andamento.");
			return false;
		}
		/* tira dos canais (fecha os arquivos) e apaga os eventos dela */
		const bool isLast = session && fsPath(session->dir) == fsPath(dir);
		for (auto &ch : channels)
			if (ch->usesSession(dir) || isLast)
				ch->setSession(isRecording ? session : nullptr);
		if (isLast)
			session.reset();
		for (auto it = sessionCache.begin(); it != sessionCache.end();)
			it = fsPath(it->first) == fsPath(dir) ? sessionCache.erase(it) : std::next(it);
		bool changed = false;
		for (auto &l : lists) {
			const size_t before = l.size();
			l.erase(std::remove_if(l.begin(), l.end(),
					       [&](const ReplayEvent &e) { return fsPath(e.sessionDir) == fsPath(dir); }),
				l.end());
			changed = changed || l.size() != before;
		}
		if (changed) {
			saveEvents();
			evVersion++;
		}
	}
	/* o canal solta os arquivos no próximo quadro */
	os_sleep_ms(80);
	std::error_code ec;
	std::filesystem::remove_all(fsPath(dir), ec);
	if (ec) {
		err = ec.message();
		rlog(LOG_WARNING, "não foi possível apagar %s: %s", dir.c_str(), err.c_str());
		return false;
	}
	rlog(LOG_INFO, "gravação apagada: %s", dir.c_str());
	return true;
}

std::string ReplayEngine::enforceDiskLimits()
{
	const ReplayConfig c = config();
	if (c.cacheLimitGB <= 0 && c.minFreeGB <= 0)
		return std::string();
	const uint64_t GB = 1024ull * 1024ull * 1024ull;
	std::error_code ec;
	const auto sp = std::filesystem::space(fsPath(c.outputDir), ec);
	if (ec)
		return std::string();
	uint64_t freeB = (uint64_t)sp.available;
	std::vector<SessionInfo> ss = listSessions();
	uint64_t total = 0;
	for (auto &s : ss)
		total += s.bytes;
	auto over = [&] {
		return (c.cacheLimitGB > 0 && total > (uint64_t)c.cacheLimitGB * GB) ||
		       (c.minFreeGB > 0 && freeB < (uint64_t)c.minFreeGB * GB);
	};
	int deleted = 0;
	for (auto &s : ss) {
		if (!over())
			break;
		if (s.recording)
			continue;
		std::string err;
		if (deleteSession(s.dir, err)) {
			total -= std::min(total, s.bytes);
			freeB += s.bytes;
			deleted++;
		}
	}
	std::string msg;
	if (deleted)
		msg = tl("Limite de disco: %1 gravação(ões) antiga(s) apagada(s)").arg(deleted).toStdString();
	/* nada mais para apagar e o disco quase cheio: para antes de encher o PC */
	if (recording() && c.minFreeGB > 0 && freeB < (uint64_t)c.minFreeGB * GB) {
		stopRecording();
		msg = tl("Gravação parada: o disco chegou ao espaço livre mínimo (%1 GB)").arg(c.minFreeGB).toStdString();
		rlog(LOG_WARNING, "%s", msg.c_str());
	}
	return msg;
}

uint64_t ReplayEngine::clearAllCache(std::string &err)
{
	if (recording())
		stopRecording();
	uint64_t freed = 0;
	for (const SessionInfo &s : listSessions()) {
		std::string e;
		if (deleteSession(s.dir, e))
			freed += s.bytes;
		else
			err += s.name + ": " + e + "\n";
	}
	{
		std::lock_guard<std::recursive_mutex> lk(mtx);
		for (auto &ch : channels)
			ch->setSession(nullptr);
		session.reset();
		sessionCache.clear();
		for (auto &l : lists)
			l.clear();
		saveEvents();
		evVersion++;
	}
	/* temporários do plugin (ícones da interface, recriados quando precisar) */
	std::error_code ec;
	const auto tmp = std::filesystem::temp_directory_path(ec) / "obs-instant-replay-ui";
	if (!ec)
		std::filesystem::remove_all(tmp, ec);
	rlog(LOG_INFO, "cache limpo: %.1f MB liberados", (double)freed / (1024.0 * 1024.0));
	return freed;
}

void ReplayEngine::zoomMove(int axis, int dir)
{
	forSelected([axis, dir](PlaybackChannel &c) { c.setZoomMove(axis, dir); });
}

void ReplayEngine::zoomStep(float factor, float dx, float dy)
{
	forSelected([=](PlaybackChannel &c) { c.zoomStep(factor, dx, dy); });
}

void ReplayEngine::zoomReset()
{
	forSelected([](PlaybackChannel &c) { c.resetZoom(); });
}

void ReplayEngine::zoomResetAll()
{
	for (auto &c : channels)
		if (c)
			c->resetZoom();
}

void ReplayEngine::zoomSet(float z, float x, float y)
{
	forSelected([=](PlaybackChannel &c) { c.zoomSet(z, x, y); });
}

const std::vector<ZoomPreset> &zoomPresets()
{
	static const std::vector<ZoomPreset> p = {
		/* fixos */
		{"center2", "2x", "centro 2x", false, false, {}, {2.0f, 0.5f, 0.5f}, 1},
		{"close3", "3x", "close 3x no centro", false, false, {}, {3.0f, 0.5f, 0.5f}, 1},
		{"left", "Esq", "lado esquerdo 2x", false, false, {}, {2.0f, 0.25f, 0.5f}, 1},
		{"right", "Dir", "lado direito 2x", false, false, {}, {2.0f, 0.75f, 0.5f}, 1},
		{"goal_left", "Gol E", "gol esquerdo 2.5x", false, false, {}, {2.5f, 0.0f, 0.5f}, 1},
		{"goal_right", "Gol D", "gol direito 2.5x", false, false, {}, {2.5f, 1.0f, 0.5f}, 1},
		{"center15", "1.5x", "centro 1.5x", false, false, {}, {1.5f, 0.5f, 0.5f}, 1},
		{"top", "Topo", "parte de cima 2x", false, false, {}, {2.0f, 0.5f, 0.0f}, 1},
		{"bottom", "Baixo", "parte de baixo 2x", false, false, {}, {2.0f, 0.5f, 1.0f}, 1},
		/* com movimento */
		{"push_in", "Aproximar", "aproximar suave até 2.2x", true, false, {}, {2.2f, 0.5f, 0.5f}, 1},
		{"pull_out", "Afastar", "afastar suave até a imagem inteira", true, false, {}, {1.0f, 0.5f, 0.5f}, 1},
		{"pan_right", "Pan →", "panorâmica da esquerda para a direita", true, true, {1.8f, 0.0f, 0.5f}, {1.8f, 1.0f, 0.5f}, 2},
		{"pan_left", "Pan ←", "panorâmica da direita para a esquerda", true, true, {1.8f, 1.0f, 0.5f}, {1.8f, 0.0f, 0.5f}, 2},
		{"tilt_up", "Subir", "subir (de baixo para cima)", true, true, {1.8f, 0.5f, 1.0f}, {1.8f, 0.5f, 0.0f}, 1.5f},
		{"tilt_down", "Descer", "descer (de cima para baixo)", true, true, {1.8f, 0.5f, 0.0f}, {1.8f, 0.5f, 1.0f}, 1.5f},
		{"into_goal_left", "Entrar gol E", "entrar no gol esquerdo", true, true, {1.0f, 0.5f, 0.5f}, {3.0f, 0.0f, 0.5f}, 1},
		{"into_goal_right", "Entrar gol D", "entrar no gol direito", true, true, {1.0f, 0.5f, 0.5f}, {3.0f, 1.0f, 0.5f}, 1},
	};
	return p;
}

void ReplayEngine::zoomPreset(int index)
{
	const auto &all = zoomPresets();
	if (index < 0 || index >= (int)all.size())
		return;
	const ZoomPreset p = all[(size_t)index];
	if (!p.moving) {
		zoomSet(p.to[0], p.to[1], p.to[2]);
		return;
	}
	const double sec = config().zoomAnimMs / 1000.0 * p.slow;
	forSelected([&p, sec](PlaybackChannel &c) { c.zoomAnimate(p.hasFrom ? p.from : nullptr, p.to, sec); });
}

void ReplayEngine::zoomPresetByKey(const std::string &key)
{
	const auto &all = zoomPresets();
	for (size_t i = 0; i < all.size(); i++)
		if (key == all[i].key) {
			zoomPreset((int)i);
			return;
		}
}

void ReplayEngine::seekTo(int64_t frame)
{
	forSelected([frame](PlaybackChannel &c) { c.seek((double)frame, false); });
}

void ReplayEngine::syncBToA()
{
	ChannelState a = channels[0]->state();
	if (a.liveMode) {
		channels[1]->goLive();
		return;
	}
	channels[1]->seek(a.pos, false);
	if (a.playing)
		channels[1]->play(a.dir);
}

/* ---------------- eventos ---------------- */

ReplayEvent *ReplayEngine::findEvent(int list, int64_t id)
{
	if (list < 0 || list >= MAX_EVENT_LISTS)
		return nullptr;
	for (auto &e : lists[list])
		if (e.id == id)
			return &e;
	return nullptr;
}

bool ReplayEngine::hasPendingIn()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	return pendingIn >= 0;
}

void ReplayEngine::addEvent(int64_t in, int64_t out)
{
	/* chamado com mtx travado */
	if (in < 0)
		in = 0;
	if (out <= in)
		return;
	ReplayEvent e;
	e.id = nextEventId++;
	e.in = in;
	e.out = out;
	e.angleMask = 1u << channels[0]->state().angle;
	e.sessionDir = session->dir;
	lists[activeListIdx.load()].push_back(e);
	evVersion++;
	saveEvents();

	/* deixa os canais selecionados parados no início do evento, prontos:
	 * o play toca até o Out (ou repete, com Loop ligado) */
	PlayRange r;
	r.in = in;
	r.out = out;
	r.session = session;
	forSelected([r](PlaybackChannel &c) { c.playList({r}, false); });
}

void ReplayEngine::markIn()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (!session)
		return;
	pendingIn = session->liveFrame();
	if (cfg.zoomResetOnMark)
		zoomResetAll();
	evVersion++;
}

void ReplayEngine::markOut()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (!session)
		return;
	const int64_t live = session->liveFrame();
	if (live < 0)
		return;
	/* sem Mark In prévio: assume 5 s antes */
	int64_t in = pendingIn >= 0 ? pendingIn : live - (int64_t)(5 * session->fps());
	pendingIn = -1;
	if (cfg.zoomResetOnMark)
		zoomResetAll();
	addEvent(in, live);
}

void ReplayEngine::markCancel()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	pendingIn = -1;
	evVersion++;
}

void ReplayEngine::autoEvent(int seconds)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (!session)
		return;
	const int64_t live = session->liveFrame();
	if (live < 0)
		return;
	pendingIn = -1;
	if (cfg.zoomResetOnMark)
		zoomResetAll();
	addEvent(live - (int64_t)(seconds * session->fps()), live);
}

void ReplayEngine::setActiveList(int list)
{
	if (list < 0 || list >= MAX_EVENT_LISTS)
		return;
	activeListIdx = list;
	evVersion++;
}

std::vector<ReplayEvent> ReplayEngine::events(int list)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (list < 0 || list >= MAX_EVENT_LISTS)
		return {};
	return lists[list];
}

std::vector<PlayRange> ReplayEngine::rangesFor(const ReplayEvent &ev, bool allAngles)
{
	std::vector<PlayRange> out;
	PlayRange r;
	r.session = sessionFor(ev.sessionDir);
	if (!r.session)
		return out; /* a pasta da gravação não existe mais */
	r.in = ev.in;
	r.out = ev.out;
	r.speed = (ev.speedPct > 0 ? ev.speedPct : speedPct.load()) / 100.0;
	if (r.speed <= 0)
		r.speed = -1;
	if (!allAngles || ev.angleMask == 0) {
		r.angle = ev.firstAngle();
		out.push_back(r);
		return out;
	}
	for (int i = 0; i < MAX_CAMERAS; i++) {
		if (ev.angleMask & (1u << i)) {
			r.angle = i;
			out.push_back(r);
		}
	}
	return out;
}

void ReplayEngine::playEvent(int list, int64_t id)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	ReplayEvent *e = findEvent(list, id);
	if (!e)
		return;
	const auto ranges = rangesFor(*e, false);
	const bool rev = reverseFlag.load();
	if (cfg.zoomResetOnEvents)
		zoomReset();
	forSelected([&ranges, rev](PlaybackChannel &c) {
		c.playList(ranges, true);
		if (rev)
			c.play(-1);
	});
}

void ReplayEngine::playEventAllAngles(int list, int64_t id)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	ReplayEvent *e = findEvent(list, id);
	if (!e)
		return;
	const auto ranges = rangesFor(*e, true);
	if (cfg.zoomResetOnEvents)
		zoomReset();
	forSelected([&ranges](PlaybackChannel &c) { c.playList(ranges, true); });
}

bool ReplayEngine::playLastEvent()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	auto &l = lists[activeListIdx.load()];
	if (l.empty())
		return false;
	playEventAllAngles(activeListIdx.load(), l.back().id);
	return true;
}

bool ReplayEngine::recordingStats(uint64_t &bytes, double &seconds)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (!isRecording || !session)
		return false;
	bytes = 0;
	for (auto &c : session->cams)
		if (c)
			bytes += c->bytesWritten();
	if (session->audio)
		bytes += (uint64_t)session->audio->samplesWritten() * REPLAY_AUDIO_CHANNELS * sizeof(float);
	const int64_t live = session->liveFrame();
	seconds = live < 0 ? 0 : (double)live / session->fps();
	return true;
}

void ReplayEngine::playEventsConfigured(int list, int64_t selectedId)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (list < 0 || list >= MAX_EVENT_LISTS || lists[list].empty())
		return;
	switch (cfg.playEventsMode) {
	case 1:
		playEvents(list, selectedId);
		break;
	case 2:
		playEventAllAngles(list, selectedId ? selectedId : lists[list].back().id);
		break;
	default:
		playEvents(list, 0);
		break;
	}
}

void ReplayEngine::cueEvent(int list, int64_t id)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	ReplayEvent *e = findEvent(list, id);
	if (!e)
		return;
	const auto ranges = rangesFor(*e, false);
	forSelected([&ranges](PlaybackChannel &c) { c.playList(ranges, false); });
}

void ReplayEngine::playEvents(int list, int64_t fromId)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (list < 0 || list >= MAX_EVENT_LISTS)
		return;
	std::vector<PlayRange> ranges;
	bool started = fromId == 0;
	for (auto &ev : lists[list]) {
		if (!started && ev.id == fromId)
			started = true;
		if (!started)
			continue;
		for (auto &r : rangesFor(ev, true))
			ranges.push_back(r);
	}
	if (ranges.empty())
		return;
	if (cfg.zoomResetOnEvents)
		zoomReset();
	forSelected([&ranges](PlaybackChannel &c) { c.playList(ranges, true); });
}

void ReplayEngine::restartClip()
{
	forSelected([](PlaybackChannel &c) { c.restart(); });
}

void ReplayEngine::setLoop(bool on)
{
	loopFlag = on;
	channels[0]->setLoop(on);
	channels[1]->setLoop(on);
}

void ReplayEngine::setAudio(bool on)
{
	audioFlag = on;
	channels[0]->setAudioEnabled(on);
	channels[1]->setAudioEnabled(on);
}

std::string ReplayEngine::exportStatus()
{
	std::lock_guard<std::mutex> lk(exportMtx);
	return exportMsg;
}

void ReplayEngine::exportEvents(int list, int64_t id)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (exportBusy || !session || list < 0 || list >= MAX_EVENT_LISTS)
		return;
	std::vector<ReplayEvent> evs;
	for (auto &ev : lists[list])
		if (id == 0 || ev.id == id)
			evs.push_back(ev);
	if (evs.empty())
		return;
	if (exportThread.joinable())
		exportThread.join();

	std::vector<std::shared_ptr<Session>> sess;
	for (auto &ev : evs)
		sess.push_back(sessionFor(ev.sessionDir));
	const int fallbackAngle = channels[0]->state().angle;
	exportBusy = true;
	{
		std::lock_guard<std::mutex> l2(exportMtx);
		exportMsg = tlc("Exportando…");
	}
	exportThread = std::thread([this, sess, evs, fallbackAngle] {
		int ok = 0, fail = 0;
		std::string lastErr, lastDir;
		for (size_t i = 0; i < evs.size(); i++) {
			const ReplayEvent &ev = evs[i];
			const std::shared_ptr<Session> &s = sess[i];
			if (!s) {
				fail++;
				lastErr = tlc("gravação não encontrada");
				continue;
			}
			const std::string dir = s->dir + "/export";
			os_mkdirs(dir.c_str());
			lastDir = dir;
			uint32_t mask = ev.angleMask ? ev.angleMask : (1u << fallbackAngle);
			for (int c = 0; c < MAX_CAMERAS; c++) {
				if (!(mask & (1u << c)) || !s->cams[c])
					continue;
				char name[96];
				snprintf(name, sizeof(name), "/Evento%04lld_Cam%d", (long long)ev.id, c + 1);
				std::string outPath, err;
				if (exportClip(*s, c, ev.in, ev.out, dir + name, outPath, err)) {
					ok++;
				} else {
					fail++;
					lastErr = err;
					rlog(LOG_ERROR, "exportação falhou (%s): %s", name, err.c_str());
				}
				std::lock_guard<std::mutex> l2(exportMtx);
				exportMsg = tl("Exportando… %1").arg(ok + fail).toStdString();
			}
		}
		std::lock_guard<std::mutex> l2(exportMtx);
		exportMsg = tl("%1 clipe(s) exportado(s) em %2").arg(ok).arg(QString::fromStdString(lastDir)).toStdString();
		if (fail)
			exportMsg += tl(" — %1 com erro: %2").arg(fail).arg(QString::fromStdString(lastErr)).toStdString();
		exportBusy = false;
	});
}

void ReplayEngine::deleteEvent(int list, int64_t id)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (list < 0 || list >= MAX_EVENT_LISTS)
		return;
	auto &l = lists[list];
	for (size_t i = 0; i < l.size(); i++) {
		if (l[i].id == id) {
			l.erase(l.begin() + (ptrdiff_t)i);
			break;
		}
	}
	evVersion++;
	saveEvents();
}

void ReplayEngine::setEventAngleChecked(int list, int64_t id, int cam, bool on)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	ReplayEvent *e = findEvent(list, id);
	if (!e || cam < 0 || cam >= MAX_CAMERAS)
		return;
	if (on)
		e->angleMask |= (1u << cam);
	else
		e->angleMask &= ~(1u << cam);
	saveEvents();
}

void ReplayEngine::setEventAngleComment(int list, int64_t id, int cam, const std::string &comment)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	ReplayEvent *e = findEvent(list, id);
	if (!e || cam < 0 || cam >= MAX_CAMERAS)
		return;
	e->comments[cam] = comment;
	saveEvents();
}

void ReplayEngine::setEventSpeed(int list, int64_t id, int pct)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	if (ReplayEvent *e = findEvent(list, id)) {
		e->speedPct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
		evVersion++;
		saveEvents();
	}
}

void ReplayEngine::setEventInOutFromPlayhead(int list, int64_t id, bool isIn)
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	ReplayEvent *e = findEvent(list, id);
	if (!e)
		return;
	const int sel = selection.load();
	const int64_t p = (int64_t)channels[(sel & SEL_A) ? 0 : 1]->state().pos;
	if (isIn && p < e->out)
		e->in = p;
	else if (!isIn && p > e->in)
		e->out = p;
	evVersion++;
	saveEvents();
}

static std::string eventsFile()
{
	const std::string c = configFile();
	if (c.empty())
		return c;
	return c.substr(0, c.size() - strlen("config.json")) + "events.json";
}

/* Os eventos ficam num arquivo global: continuam na lista entre gravações e
 * entre aberturas do OBS, até serem apagados. */
void ReplayEngine::saveEvents()
{
	/* chamado com mtx travado */
	const std::string file = eventsFile();
	if (file.empty())
		return;
	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "next_id", nextEventId);
	obs_data_array_t *arr = obs_data_array_create();
	for (auto &e : lists[0]) {
		obs_data_t *o = obs_data_create();
		obs_data_set_int(o, "id", e.id);
		obs_data_set_int(o, "in", e.in);
		obs_data_set_int(o, "out", e.out);
		obs_data_set_int(o, "angle_mask", e.angleMask);
		obs_data_set_int(o, "speed_pct", e.speedPct);
		obs_data_set_string(o, "session_dir", e.sessionDir.c_str());
		for (int c = 0; c < MAX_CAMERAS; c++) {
			if (e.comments[c].empty())
				continue;
			char ck[24];
			snprintf(ck, sizeof(ck), "comment_cam%d", c + 1);
			obs_data_set_string(o, ck, e.comments[c].c_str());
		}
		obs_data_array_push_back(arr, o);
		obs_data_release(o);
	}
	obs_data_set_array(root, "events", arr);
	obs_data_array_release(arr);
	obs_data_save_json_safe(root, file.c_str(), "tmp", "bak");
	obs_data_release(root);
}

void ReplayEngine::loadEvents()
{
	std::lock_guard<std::recursive_mutex> lk(mtx);
	const std::string file = eventsFile();
	if (file.empty())
		return;
	obs_data_t *root = obs_data_create_from_json_file_safe(file.c_str(), "bak");
	if (!root)
		return;
	obs_data_array_t *arr = obs_data_get_array(root, "events");
	const size_t n = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < n; i++) {
		obs_data_t *o = obs_data_array_item(arr, i);
		ReplayEvent e;
		e.id = obs_data_get_int(o, "id");
		e.in = obs_data_get_int(o, "in");
		e.out = obs_data_get_int(o, "out");
		e.angleMask = (uint32_t)obs_data_get_int(o, "angle_mask");
		e.speedPct = (int)obs_data_get_int(o, "speed_pct");
		e.sessionDir = obs_data_get_string(o, "session_dir");
		for (int c = 0; c < MAX_CAMERAS; c++) {
			char ck[24];
			snprintf(ck, sizeof(ck), "comment_cam%d", c + 1);
			e.comments[c] = obs_data_get_string(o, ck);
		}
		obs_data_release(o);
		/* se a pasta da gravação foi apagada, o clipe não existe mais */
		if (os_file_exists((e.sessionDir + "/session.json").c_str()))
			lists[0].push_back(e);
		if (e.id >= nextEventId)
			nextEventId = e.id + 1;
	}
	obs_data_array_release(arr);
	const int64_t nid = obs_data_get_int(root, "next_id");
	if (nid > nextEventId)
		nextEventId = nid;
	obs_data_release(root);
	evVersion++;
}

/* Sessão de um evento: a atual, uma já aberta, ou reaberta do disco. */
std::shared_ptr<Session> ReplayEngine::sessionFor(const std::string &dir)
{
	/* chamado com mtx travado */
	if (session && (dir.empty() || session->dir == dir))
		return session;
	auto it = sessionCache.find(dir);
	if (it != sessionCache.end())
		return it->second;

	obs_data_t *meta = obs_data_create_from_json_file((dir + "/session.json").c_str());
	if (!meta)
		return nullptr;
	auto s = std::make_shared<Session>();
	s->dir = dir;
	s->fpsNum = (uint32_t)obs_data_get_int(meta, "fps_num");
	s->fpsDen = (uint32_t)obs_data_get_int(meta, "fps_den");
	s->startWallMs = obs_data_get_int(meta, "start_wall_ms");
	if (!s->fpsNum || !s->fpsDen) {
		s->fpsNum = 30;
		s->fpsDen = 1;
	}
	int count = 0;
	for (int i = 0; i < MAX_CAMERAS; i++) {
		char key[16];
		snprintf(key, sizeof(key), "cam%d", i + 1);
		obs_data_t *cm = obs_data_get_obj(meta, key);
		if (!cm)
			continue;
		const std::string file = obs_data_get_string(cm, "file");
		auto rec = std::make_shared<Recording>();
		rec->width = (int)obs_data_get_int(cm, "width");
		rec->height = (int)obs_data_get_int(cm, "height");
		obs_data_release(cm);
		const size_t dot = file.rfind('.');
		const std::string ext = dot == std::string::npos ? "" : file.substr(dot);
		rec->codecId = ext == ".h265"    ? (int)AV_CODEC_ID_HEVC
			       : ext == ".mjpeg" ? (int)AV_CODEC_ID_MJPEG
						 : (int)AV_CODEC_ID_H264;
		/* usa o nome dentro da pasta: funciona mesmo se a pasta mudou de lugar */
		if (rec->openExisting(dir + "/" + key + ext, dir + "/" + key + ".idx")) {
			s->cams[i] = rec;
			count++;
		}
	}
	obs_data_release(meta);
	if (!count)
		return nullptr;
	auto audio = std::make_shared<AudioRecorder>();
	if (audio->openExisting(dir + "/audio.wav"))
		s->audio = audio;
	sessionCache[dir] = s;
	rlog(LOG_INFO, "gravação anterior reaberta: %s (%d câmeras)", dir.c_str(), count);
	return s;
}
