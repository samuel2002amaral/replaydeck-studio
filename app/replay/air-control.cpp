#include "air-control.hpp"
#include "studio-host.hpp"
#include "engine.hpp"
#include <QTimer>
#include <atomic>
#include <QThread>
#include <memory>
#include <map>
#include <vector>
#include <cstring>

static AirControl *g_air = nullptr;

AirControl *AirControl::instance()
{
	return g_air;
}

AirControl::AirControl(QObject *parent) : QObject(parent)
{
	g_air = this;
	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &AirControl::poll);
	timer->start(30);
	obs_frontend_add_event_callback(frontendEvent, this);
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_transition_start", transitionStarted, this);
	signal_handler_connect(sh, "source_transition_stop", transitionStopped, this);
	/* o vídeo da transição termina antes do áudio: é aí que a cobertura sai */
	signal_handler_connect(sh, "source_transition_video_stop", transitionStopped, this);
	QTimer::singleShot(0, this, [this] { watchFrontendTransitions(); });
}

/* ---------- stinger por cima de tudo ----------
 * Os overlays (canais 50-57) ficam acima da cena do programa (canal 0), então
 * cobririam o stinger da troca de cena. Para cada transição stinger existe uma
 * cópia particular, sem som, que roda entre duas cenas vazias e transparentes
 * no canal mais alto (63): ela desenha só o gráfico do stinger por cima de
 * tudo. Os overlays continuam no ar embaixo dele (sem sumir e voltar), e a
 * troca de cena acontece normalmente no programa. */

#define STINGER_TOP_CHANNEL 63

static std::map<std::string, std::unique_ptr<OverlayTransition>> g_covers;
static OverlayTransition *g_coverActive = nullptr;
static void prepareCovers();
static void releaseCovers();

static bool isStinger(obs_source_t *t)
{
	const char *id = obs_source_get_id(t);
	if (!id)
		return false;
	/* stingers têm duração fixa (a do vídeo/gráfico) */
	return strstr(id, "stinger") || strstr(id, "vplus_gt") || obs_transition_fixed(t);
}

static void AirControl_transitionStarted(void *p, calldata_t *cd);
static void AirControl_transitionStopped(void *p, calldata_t *cd);

static std::mutex g_watchMtx;
static std::vector<obs_weak_source_t *> g_watched;

static void unwatchAll()
{
	std::lock_guard<std::mutex> lk(g_watchMtx);
	for (auto *w : g_watched) {
		obs_source_t *s = obs_weak_source_get_source(w);
		if (s) {
			signal_handler_t *sh = obs_source_get_signal_handler(s);
			signal_handler_disconnect(sh, "transition_start", AirControl_transitionStarted, nullptr);
			signal_handler_disconnect(sh, "transition_stop", AirControl_transitionStopped, nullptr);
			signal_handler_disconnect(sh, "transition_video_stop", AirControl_transitionStopped, nullptr);
			obs_source_release(s);
		}
		obs_weak_source_release(w);
	}
	g_watched.clear();
}

void AirControl::watchTransition(obs_source_t *t)
{
	if (!t)
		return;
	std::lock_guard<std::mutex> lk(g_watchMtx);
	for (auto *w : g_watched)
		if (obs_weak_source_references_source(w, t))
			return;
	signal_handler_t *sh = obs_source_get_signal_handler(t);
	signal_handler_connect(sh, "transition_start", AirControl_transitionStarted, nullptr);
	signal_handler_connect(sh, "transition_stop", AirControl_transitionStopped, nullptr);
	signal_handler_connect(sh, "transition_video_stop", AirControl_transitionStopped, nullptr);
	g_watched.push_back(obs_source_get_weak_source(t));
}

void AirControl::watchFrontendTransitions()
{
	unwatchAll();
	struct obs_frontend_source_list tl = {};
	obs_frontend_get_transitions(&tl);
	for (size_t i = 0; i < tl.sources.num; i++)
		watchTransition(tl.sources.array[i]);
	obs_frontend_source_list_free(&tl);
	/* a transição do programa também (pode ser uma cópia da lista) */
	obs_source_t *program = obs_get_output_source(0);
	if (program && obs_source_get_type(program) == OBS_SOURCE_TYPE_TRANSITION)
		watchTransition(program);
	obs_source_release(program);
	prepareCovers();
}

void AirControl::transitionStarted(void *, calldata_t *cd)
{
	obs_source_t *t = (obs_source_t *)calldata_ptr(cd, "source");
	if (!t)
		return;
	/* as transições internas do replay (overlays, eventos) não contam */
	const char *nm = obs_source_get_name(t);
	if (nm && strncmp(nm, "instant-replay", 14) == 0)
		return;
	obs_source_t *program = obs_get_output_source(0);
	const bool isProgram = program == t;
	obs_source_release(program);
	/* só transições do OBS (painel "Transições de cena"); nunca as prévias
	 * internas de outros plugins, que não podem ir ao ar */
	bool listed = isProgram;
	if (!listed) {
		struct obs_frontend_source_list tl = {};
		obs_frontend_get_transitions(&tl);
		for (size_t i = 0; i < tl.sources.num && !listed; i++)
			listed = tl.sources.array[i] == t;
		obs_frontend_source_list_free(&tl);
	}
	if (!listed)
		return;
	const bool cover = isStinger(t) && engine().config().stingerCoverOverlays;
	rlog(LOG_INFO, "transição%s: '%s' (%s, duração fixa: %s) — cobre overlays: %s", isProgram ? " do programa" : "",
	     obs_source_get_name(t), obs_source_get_id(t), obs_transition_fixed(t) ? "sim" : "não", cover ? "sim" : "não");
	if (!cover)
		return;
	obs_source_t *ref = obs_source_get_ref(t);
	auto run = [ref] {
		startCover(ref);
		obs_source_release(ref);
	};
	AirControl *self = g_air;
	if (self && QThread::currentThread() != self->thread())
		QMetaObject::invokeMethod(self, run, Qt::QueuedConnection);
	else
		run();
}

void AirControl::startCover(obs_source_t *t)
{
	const std::string name = obs_source_get_name(t);
	auto &cov = g_covers[name];
	if (!cov)
		cov = std::make_unique<OverlayTransition>(STINGER_TOP_CHANNEL, "instant-replay-stinger-cover", true);
	if (g_coverActive && g_coverActive != cov.get())
		g_coverActive->stop();
	cov->prepare(t);
	const int ms = obs_frontend_get_transition_duration();
	if (cov->start(ms > 0 ? ms : 1000))
		g_coverActive = cov.get();
	else
		rlog(LOG_WARNING, "não foi possível pôr o stinger '%s' por cima dos overlays", name.c_str());
}

void AirControl::prepareCover(obs_source_t *t)
{
	auto &cov = g_covers[obs_source_get_name(t)];
	if (!cov)
		cov = std::make_unique<OverlayTransition>(STINGER_TOP_CHANNEL, "instant-replay-stinger-cover", true);
	if (cov.get() != g_coverActive)
		cov->prepare(t);
}

static void prepareCovers()
{
	struct obs_frontend_source_list tl = {};
	obs_frontend_get_transitions(&tl);
	for (size_t i = 0; i < tl.sources.num; i++) {
		obs_source_t *t = tl.sources.array[i];
		if (!isStinger(t))
			continue;
		AirControl::prepareCover(t); /* o GT precisa carregar o gráfico antes */
	}
	obs_frontend_source_list_free(&tl);
}

static void releaseCovers()
{
	g_coverActive = nullptr;
	g_covers.clear();
}

void AirControl::transitionStopped(void *, calldata_t *)
{
	/* a cópia tem a mesma duração e termina sozinha (ver poll) */
}

AirControl::~AirControl()
{
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_transition_start", transitionStarted, this);
	signal_handler_disconnect(sh, "source_transition_stop", transitionStopped, this);
	signal_handler_disconnect(sh, "source_transition_video_stop", transitionStopped, this);
	unwatchAll();
	releaseCovers();
	overlay.release();
	obs_frontend_remove_event_callback(frontendEvent, this);
	obs_weak_source_release(savedTransition);
	g_air = nullptr;
}

void AirControl::requestIn()
{
	QMetaObject::invokeMethod(this, [this] { takeIn(3); }, Qt::QueuedConnection);
}

void AirControl::requestOut()
{
	QMetaObject::invokeMethod(this, "takeOut", Qt::QueuedConnection);
}

/* ---------- acha a cena do replay e qual canal ela mostra ---------- */

struct OutputSearch {
	int channel = -1;
};

static bool findOutput(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<OutputSearch *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return true;
	if (strcmp(obs_source_get_id(src), "instant_replay_output") == 0) {
		obs_data_t *st = obs_source_get_settings(src);
		ctx->channel = (int)obs_data_get_int(st, "channel");
		obs_data_release(st);
		return false;
	}
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, findOutput, param);
	return ctx->channel < 0;
}

obs_source_t *AirControl::replayScene(int *channel)
{
	const ReplayConfig cfg = engine().config();
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	obs_source_t *found = nullptr;
	int foundCh = 0;

	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *sc = scenes.sources.array[i];
		OutputSearch ctx;
		obs_scene_enum_items(obs_scene_from_source(sc), findOutput, &ctx);
		if (!cfg.replayScene.empty()) {
			if (cfg.replayScene == obs_source_get_name(sc)) {
				found = sc;
				foundCh = ctx.channel < 0 ? 0 : ctx.channel;
				break;
			}
		} else if (ctx.channel >= 0 && (!found || (ctx.channel == 0 && foundCh != 0))) {
			/* automático: prefere a cena com a saída A */
			found = sc;
			foundCh = ctx.channel;
		}
	}
	if (found)
		obs_source_get_ref(found);
	obs_frontend_source_list_free(&scenes);
	if (channel)
		*channel = foundCh;
	return found;
}

/* ---------- troca a cena do programa com uma transição específica ---------- */

static obs_source_t *findTransition(const std::string &name)
{
	if (name.empty())
		return nullptr;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	obs_source_t *t = nullptr;
	for (size_t i = 0; i < list.sources.num; i++) {
		if (name == obs_source_get_name(list.sources.array[i])) {
			t = obs_source_get_ref(list.sources.array[i]);
			break;
		}
	}
	obs_frontend_source_list_free(&list);
	return t;
}

bool AirControl::switchProgram(obs_source_t *scene, const std::string &transition, int durationMs)
{
	obs_source_t *t = findTransition(transition);
	const bool changeT = t != nullptr;
	const bool changeD = durationMs > 0;

	if ((changeT || changeD) && !restorePending) {
		obs_source_t *cur = obs_frontend_get_current_transition();
		obs_weak_source_release(savedTransition);
		savedTransition = obs_source_get_weak_source(cur);
		obs_source_release(cur);
		savedDuration = obs_frontend_get_transition_duration();
		restorePending = true;
	}
	if (changeT)
		obs_frontend_set_current_transition(t);
	if (changeD)
		obs_frontend_set_transition_duration(durationMs);
	obs_source_release(t);

	obs_frontend_set_current_scene(scene);

	/* se nada disparar o fim da transição, devolve mesmo assim */
	if (restorePending)
		QTimer::singleShot((durationMs > 0 ? durationMs : 1000) + 15000, this, [this] { restoreTransition(); });
	return true;
}

void AirControl::restoreTransition()
{
	if (!restorePending)
		return;
	restorePending = false;
	obs_source_t *t = obs_weak_source_get_source(savedTransition);
	if (t)
		obs_frontend_set_current_transition(t);
	obs_source_release(t);
	if (savedDuration > 0)
		obs_frontend_set_transition_duration(savedDuration);
}

void AirControl::frontendEvent(enum obs_frontend_event event, void *param)
{
	auto *self = static_cast<AirControl *>(param);
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING || event == OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED ||
	    event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || event == OBS_FRONTEND_EVENT_TRANSITION_CHANGED) {
		QTimer::singleShot(0, self, [self] { self->watchFrontendTransitions(); });
	}
	if (event == OBS_FRONTEND_EVENT_TRANSITION_STOPPED && self->restorePending) {
		/* espera o OBS terminar de processar o evento */
		QTimer::singleShot(0, self, [self] { self->restoreTransition(); });
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP || event == OBS_FRONTEND_EVENT_EXIT) {
		self->prevSceneName.clear();
		obs_weak_source_release(self->savedTransition);
		self->savedTransition = nullptr;
		self->restorePending = false;
		self->overlay.release();
		if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP || event == OBS_FRONTEND_EVENT_EXIT) {
			unwatchAll();
			releaseCovers();
		}
	}
}

/* ---------- transição do OBS entre eventos (GT Stinger etc.) ---------- */

void AirControl::prepareEventTransition()
{
	const ReplayConfig cfg = engine().config();
	if (cfg.eventTransition != 3 || cfg.stingerSource.empty()) {
		overlay.release();
		return;
	}
	obs_source_t *templ = findTransition(cfg.stingerSource);
	if (templ)
		overlay.prepare(templ);
	obs_source_release(templ);
}

void AirControl::startStinger()
{
	const ReplayConfig cfg = engine().config();
	prepareEventTransition();
	if (!overlay.start(cfg.stingerDurationMs))
		return;
	/* segurança: tira a transição da tela mesmo que ela não avise o fim */
	const uint64_t gen = ++stingerGen;
	QTimer::singleShot((cfg.stingerDurationMs > 0 ? cfg.stingerDurationMs : 1000) + 10000, this, [this, gen] {
		if (gen == stingerGen)
			stopStinger();
	});
}

void AirControl::stopStinger()
{
	overlay.stop();
}

/* ---------- entrar / sair ---------- */

/* No Modo Estúdio o "programa" pode ser uma cópia interna da cena (mesmo nome,
 * outro objeto), então as cenas são comparadas pelo nome. */
static bool sameScene(obs_source_t *a, obs_source_t *b)
{
	if (!a || !b)
		return false;
	if (a == b)
		return true;
	const char *na = obs_source_get_name(a);
	const char *nb = obs_source_get_name(b);
	return na && nb && strcmp(na, nb) == 0;
}

bool AirControl::onAir() const
{
	if (!engineAlive())
		return false;
	obs_source_t *program = obs_frontend_get_current_scene();
	obs_source_t *rs = const_cast<AirControl *>(this)->replayScene(nullptr);
	const bool on = sameScene(program, rs);
	obs_source_release(program);
	obs_source_release(rs);
	return on;
}

void AirControl::takeIn(int k)
{
	if (!engineAlive())
		return;
	const ReplayConfig cfg = engine().config();
	int ch = 0;
	obs_source_t *rs = replayScene(&ch);
	if (!rs)
		ch = (engine().getSelection() & SEL_A) ? 0 : 1;

	if (rs && cfg.autoIn) {
		obs_source_t *program = obs_frontend_get_current_scene();
		if (!sameScene(program, rs)) {
			prevSceneName = program && obs_source_get_name(program) ? obs_source_get_name(program) : "";
			switchProgram(rs, cfg.inTransition, cfg.inDurationMs);
			rlog(LOG_INFO, "replay no ar (voltará para '%s')", prevSceneName.c_str());
		}
		obs_source_release(program);
	} else if (!rs && cfg.autoIn) {
		rlog(LOG_WARNING, "nenhuma cena com a fonte ReplayDeck (saída) — não dá para pôr no ar");
	}

	/* a partir de agora, quando o clipe terminar, encerra sozinho */
	kind = k > 0 ? k : 3;
	armed = true;
	armedCh = ch;
	lastFinished = engine().channel(ch).state().finished;
	outScheduled = false;
	obs_source_release(rs);
}

void AirControl::toggleLast()
{
	if (!engineAlive())
		return;
	if (kind == 1) {
		abortRun();
		return;
	}
	if (engine().playLastEvent())
		takeIn(1);
}

void AirControl::toggleEvents(qlonglong selectedId)
{
	if (!engineAlive())
		return;
	if (kind == 2) {
		abortRun();
		return;
	}
	if (engine().events(engine().activeList()).empty())
		return;
	engine().playEventsConfigured(engine().activeList(), (int64_t)selectedId);
	takeIn(2);
}

/* término antecipado: para o replay e faz a saída completa (transição + volta) */
void AirControl::abortRun()
{
	if (!engineAlive() || !kind)
		return;
	engine().channel(armedCh).pause();
	engine().pause();
	armed = false;
	takeOut();
}

void AirControl::takeOut()
{
	outScheduled = false;
	armed = false;
	kind = 0;
	if (!engineAlive())
		return;
	obs_source_t *rs = replayScene(nullptr);
	obs_source_t *program = obs_frontend_get_current_scene();
	/* a cena de verdade, pelo nome (não a cópia interna do Modo Estúdio) */
	obs_source_t *back = prevSceneName.empty() ? nullptr : rdsFindScene(prevSceneName.c_str());

	/* só sai se o replay ainda estiver no ar (o operador pode ter trocado) */
	if (rs && back && sameScene(program, rs) && !sameScene(back, rs)) {
		const ReplayConfig cfg = engine().config();
		switchProgram(back, cfg.outTransition, cfg.outDurationMs);
		rlog(LOG_INFO, "replay saiu do ar, de volta para '%s'", prevSceneName.c_str());
	} else {
		rlog(LOG_INFO, "saída do replay ignorada (cena do replay: %s, programa: %s, voltar para: '%s')",
		     rs ? obs_source_get_name(rs) : "nenhuma", program ? obs_source_get_name(program) : "nenhum",
		     prevSceneName.c_str());
	}
	obs_source_release(back);
	obs_source_release(program);
	obs_source_release(rs);
}

/* ---------- acompanha o canal que está na cena do replay ---------- */

void AirControl::poll()
{
	/* tira a cobertura do stinger quando ele termina (nunca fica presa) */
	if (g_coverActive && g_coverActive->finished()) {
		g_coverActive->stop();
		g_coverActive = nullptr;
	}

	if (!engineAlive())
		return;

	/* stinger entre eventos: o canal avisa um pouco antes da virada */
	bool fire = false;
	for (int i = 0; i < 2; i++) {
		const uint64_t n = engine().channel(i).state().stingers;
		if (n != lastStingers[i]) {
			lastStingers[i] = n;
			fire = true;
		}
	}
	if (fire && onAir())
		startStinger();
	if (overlay.running() && overlay.finished())
		stopStinger();
	/* mantém a transição carregada (o GT leva um tempo para preparar) */
	if (++prepareTick >= 66 && !overlay.running()) {
		prepareTick = 0;
		prepareEventTransition();
		/* se o usuário mexeu nas opções do stinger, recarrega a cópia já */
		if (!g_coverActive)
			prepareCovers();
	}

	if (!armed)
		return;
	const int ch = armedCh;
	const ChannelState st = engine().channel(ch).state();
	if (st.finished == lastFinished)
		return;
	lastFinished = st.finished;
	armed = false;

	const ReplayConfig cfg = engine().config();
	if (!cfg.autoOut) {
		kind = 0; /* terminou e fica onde está */
		return;
	}
	if (outScheduled)
		return;
	outScheduled = true;
	QTimer::singleShot(cfg.outHoldMs > 0 ? cfg.outHoldMs : 0, this, [this, ch] {
		/* deu play de novo durante a espera: fica no ar */
		const ChannelState now = engine().channel(ch).state();
		if (now.playing && !now.liveMode) {
			outScheduled = false;
			armed = true;
			lastFinished = now.finished;
			return;
		}
		takeOut();
	});
}

static void AirControl_transitionStarted(void *p, calldata_t *cd)
{
	AirControl::onTransitionStart(p, cd);
}

static void AirControl_transitionStopped(void *p, calldata_t *cd)
{
	AirControl::onTransitionStop(p, cd);
}
