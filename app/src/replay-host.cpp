/* O Studio respondendo ao replay: a "interface do OBS" (replay/shim/obs-frontend-api.h)
 * e as perguntas do replay (replay/studio-host.hpp), com o modelo do Studio:
 * cenas = entradas, cena atual = Programa, prévia = Prévia, transições = Corte e Fade. */
#include "replay-host.hpp"
#include "gt-host.hpp"
#include "dialogs.hpp"
#include "engine.hpp"

#include "../replay/air-control.hpp"
#include "../replay/engine.hpp"
#include "../replay/replay-dock.hpp"
#include "../replay/studio-host.hpp"
#include <obs-frontend-api.h>

#include <util/bmem.h>
#include <util/platform.h>

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QString>

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

void registerReplaySource();
void registerReplayCaptureOutput();

static const char *REPLAY_OUTPUT_ID = "instant_replay_output";

static std::vector<std::pair<obs_frontend_event_cb, void *>> g_callbacks;
static QPointer<QMainWindow> g_window;
static obs_weak_source_t *g_transition = nullptr; /* transição "atual" para o replay (vazio = Fade) */
static int g_transitionMs = -1;                   /* -1 = a duração do Studio */
static int g_lastPgm = -1, g_lastPvw = -1;
static bool g_started = false;
static QPointer<ReplayDock> g_dock;

static void emitEvent(obs_frontend_event ev)
{
	const auto copy = g_callbacks; /* um aviso pode tirar/pôr outros */
	for (const auto &cb : copy)
		cb.first(ev, cb.second);
}

/* ---------------- interface do OBS, respondida pelo Studio ---------------- */

void obs_frontend_add_event_callback(obs_frontend_event_cb callback, void *private_data)
{
	g_callbacks.emplace_back(callback, private_data);
}

void obs_frontend_remove_event_callback(obs_frontend_event_cb callback, void *private_data)
{
	g_callbacks.erase(std::remove(g_callbacks.begin(), g_callbacks.end(), std::make_pair(callback, private_data)),
			  g_callbacks.end());
}

void obs_frontend_get_scenes(struct obs_frontend_source_list *sources)
{
	for (Input *in : Engine::get().inputs()) {
		obs_source_t *s = obs_source_get_ref(in->scene);
		da_push_back(sources->sources, &s);
	}
}

static obs_source_t *sceneOf(int id)
{
	Input *in = Engine::get().input(id);
	return in ? obs_source_get_ref(in->scene) : nullptr;
}

obs_source_t *obs_frontend_get_current_scene(void)
{
	return sceneOf(Engine::get().programId());
}

obs_source_t *obs_frontend_get_current_preview_scene(void)
{
	return sceneOf(Engine::get().previewId());
}

static obs_source_t *currentTransition() /* sem referência */
{
	obs_source_t *t = obs_weak_source_get_source(g_transition);
	obs_source_release(t); /* as transições vivem no motor do Studio */
	return t ? t : Engine::get().fadeTransition();
}

static int currentDuration()
{
	return g_transitionMs >= 0 ? g_transitionMs : Engine::get().transitionMs();
}

void obs_frontend_set_current_scene(obs_source_t *scene)
{
	Engine &e = Engine::get();
	Input *in = e.inputByScene(scene);
	if (!in) {
		blog(LOG_WARNING, "[replay] cena '%s' não é uma entrada do Studio", scene ? obs_source_get_name(scene) : "");
		return;
	}
	obs_source_t *tr = currentTransition();
	e.takeToProgram(in->id, tr, tr == e.cutTransition() ? 0 : currentDuration());
}

void obs_frontend_set_current_preview_scene(obs_source_t *scene)
{
	if (Input *in = Engine::get().inputByScene(scene))
		Engine::get().setPreview(in->id);
}

bool obs_frontend_preview_program_mode_active(void)
{
	return true;
}

void obs_frontend_get_transitions(struct obs_frontend_source_list *sources)
{
	std::vector<obs_source_t *> all = {Engine::get().cutTransition(), Engine::get().fadeTransition()};
	for (StudioTransition *t : Engine::get().userTransitions())
		all.push_back(t->source);
	for (obs_source_t *t : all) {
		if (!t)
			continue;
		obs_source_t *r = obs_source_get_ref(t);
		da_push_back(sources->sources, &r);
	}
}

obs_source_t *obs_frontend_get_current_transition(void)
{
	return obs_source_get_ref(currentTransition());
}

void obs_frontend_set_current_transition(obs_source_t *transition)
{
	obs_weak_source_release(g_transition);
	g_transition = transition ? obs_source_get_weak_source(transition) : nullptr;
	emitEvent(OBS_FRONTEND_EVENT_TRANSITION_CHANGED);
}

int obs_frontend_get_transition_duration(void)
{
	return currentDuration();
}

void obs_frontend_set_transition_duration(int duration)
{
	/* voltou para a duração do Studio: segue o Studio de novo (o operador pode mudar depois) */
	g_transitionMs = duration == Engine::get().transitionMs() ? -1 : duration;
}

void *obs_frontend_get_main_window(void)
{
	return g_window.data();
}

void obs_frontend_open_source_properties(obs_source_t *source)
{
	for (Input *in : Engine::get().inputs())
		if (in->source.Get() == source) {
			auto *w = new InputWindow(in->id, g_window);
			w->show();
			return;
		}
}

/* ---------------- perguntas do replay ---------------- */

char *rdsReplayConfigPath(const char *file)
{
	const QString dir = Engine::get().configDir() + "/replay";
	QDir().mkpath(dir);
	return bstrdup(QDir::toNativeSeparators(dir + "/" + QString::fromUtf8(file)).toUtf8().constData());
}

char *rdsReplayDataFile(const char *file)
{
	const QString app = QCoreApplication::applicationDirPath();
	const QString rel = QString::fromUtf8(file);
	for (const QString &base : {app + "/../../data/replaydeck-studio/replay", app + "/replay-data",
				    app + "/../Resources/data/replaydeck-studio/replay" /* Mac: dentro do .app */}) {
		const QString p = QDir::cleanPath(base + "/" + rel);
		if (QFileInfo::exists(p))
			return bstrdup(p.toUtf8().constData());
	}
	return nullptr;
}

obs_source_t *rdsFindSource(const char *name)
{
	if (Input *in = Engine::get().inputByName(QString::fromUtf8(name)))
		return obs_source_get_ref(in->source);
	return obs_get_source_by_name(name);
}

obs_source_t *rdsFindScene(const char *name)
{
	if (Input *in = Engine::get().inputByName(QString::fromUtf8(name)))
		return obs_source_get_ref(in->scene);
	return nullptr;
}

std::vector<std::string> rdsCameraNames()
{
	std::vector<std::string> names;
	for (Input *in : Engine::get().inputs()) {
		if (!(obs_source_get_output_flags(in->source) & OBS_SOURCE_VIDEO) ||
		    strcmp(obs_source_get_unversioned_id(in->source), REPLAY_OUTPUT_ID) == 0)
			continue;
		const std::string n = in->name.toUtf8().constData();
		if (std::find(names.begin(), names.end(), n) == names.end())
			names.push_back(n);
	}
	return names;
}

static bool hasReplayInput(int channel)
{
	for (Input *in : Engine::get().inputs()) {
		if (strcmp(obs_source_get_unversioned_id(in->source), REPLAY_OUTPUT_ID) != 0)
			continue;
		OBSDataAutoRelease st = obs_source_get_settings(in->source);
		if ((int)obs_data_get_int(st, "channel") == channel)
			return true;
	}
	return false;
}

bool rdsHasReplayInputs()
{
	return hasReplayInput(0) && hasReplayInput(1);
}

QString rdsEnsureReplayInputs()
{
	QString created;
	for (int ch = 0; ch < 2; ch++) {
		if (hasReplayInput(ch))
			continue;
		OBSDataAutoRelease st = obs_data_create();
		obs_data_set_int(st, "channel", ch);
		const QString name = ch == 0 ? "Replay A" : "Replay B";
		if (Engine::get().addInput(REPLAY_OUTPUT_ID, name, st) && created.isEmpty())
			created = name;
	}
	return created;
}

QString rdsAddCameraDialog(QWidget *parent)
{
	QList<int> before;
	for (Input *in : Engine::get().inputs())
		before << in->id;
#ifdef __APPLE__
	NewInputDialog dlg("macos-avcapture", "Câmera / placa de captura", parent);
#elif defined(_WIN32)
	NewInputDialog dlg("dshow_input", "Câmera / placa de captura", parent);
#else
	NewInputDialog dlg("v4l2_input", "Câmera / placa de captura", parent);
#endif
	dlg.exec();
	for (Input *in : Engine::get().inputs())
		if (!before.contains(in->id))
			return in->name;
	return QString();
}

/* ---------------- ligar e desligar ---------------- */

static void transitionStopped(void *, calldata_t *)
{
	QMetaObject::invokeMethod(qApp, [] { emitEvent(OBS_FRONTEND_EVENT_TRANSITION_STOPPED); }, Qt::QueuedConnection);
}

namespace replayhost {

void registerTypes()
{
	registerReplaySource();
	registerReplayCaptureOutput();
	gthost::install(); /* títulos GT (entradas instant_replay_gt) */
	engineCreate(); /* as entradas Replay A/B do projeto já encontram os canais */
}

QWidget *start(QMainWindow *window)
{
	g_window = window;
	Engine &e = Engine::get();
	for (obs_source_t *t : {e.cutTransition(), e.fadeTransition()})
		signal_handler_connect(obs_source_get_signal_handler(t), "transition_stop", transitionStopped, nullptr);

	/* avisos do Studio no formato que o replay entende */
	QObject::connect(&e, &Engine::busChanged, window, [] {
		Engine &en = Engine::get();
		if (en.programId() != g_lastPgm) {
			g_lastPgm = en.programId();
			emitEvent(OBS_FRONTEND_EVENT_SCENE_CHANGED);
		}
		if (en.previewId() != g_lastPvw) {
			g_lastPvw = en.previewId();
			emitEvent(OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED);
		}
	});
	QObject::connect(&e, &Engine::inputsChanged, window, [] { emitEvent(OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED); });
	QObject::connect(&e, &Engine::transitionsChanged, window,
			 [] { emitEvent(OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED); });
	QObject::connect(&e, &Engine::transitionMsChanged, window,
			 [] { emitEvent(OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED); });
	/* câmera do replay renomeada junto com a entrada */
	QObject::connect(&e, &Engine::inputRenamed, window, [](int, const QString &oldName, const QString &newName) {
		if (!engineAlive())
			return;
		ReplayConfig cfg = engine().config();
		bool changed = false;
		for (int i = 0; i < MAX_CAMERAS; i++)
			if (cfg.cameras[i] == oldName.toUtf8().constData()) {
				cfg.cameras[i] = newName.toUtf8().constData();
				changed = true;
			}
		if (changed)
			engine().setConfig(cfg);
	});

	new AirControl(window);
	auto *dock = new ReplayDock(window);
	g_dock = dock;
	g_started = true;
	QTimer::singleShot(0, window, [] { emitEvent(OBS_FRONTEND_EVENT_FINISHED_LOADING); });
	return dock;
}

void shutdown()
{
	if (!g_started)
		return;
	g_started = false;
	emitEvent(OBS_FRONTEND_EVENT_EXIT);
	if (engineAlive())
		engine().stopRecording();
	delete AirControl::instance();
	Engine &e = Engine::get();
	for (obs_source_t *t : {e.cutTransition(), e.fadeTransition()})
		signal_handler_disconnect(obs_source_get_signal_handler(t), "transition_stop", transitionStopped, nullptr);
}

/* teste automático (RDS_SELFTEST_REPLAY=<png>): grava 2 entradas, marca evento, toca o último
 * evento (vai ao ar) e confere que volta sozinho para a entrada de antes */
void selfTest(QMainWindow *window)
{
	auto step = [window](int ms, std::function<void()> fn) { QTimer::singleShot(ms, window, fn); };
	auto pgmName = [] {
		Input *p = Engine::get().input(Engine::get().programId());
		return p ? p->name.toUtf8() : QByteArray("(nada)");
	};
	step(0, [] {
		Engine &e = Engine::get();
		while (!e.inputs().isEmpty())
			e.removeInput(e.inputs().first()->id);
		const uint32_t colors[] = {0xFF2020E0, 0xFF20C020};
		for (int i = 0; i < 2; i++) {
			OBSDataAutoRelease st = obs_data_create();
			obs_data_set_int(st, "color", colors[i]);
			obs_data_set_int(st, "width", 1920);
			obs_data_set_int(st, "height", 1080);
			e.addInput("color_source_v3", QString("CAM %1").arg(i + 1), st);
		}
		const QString made = rdsEnsureReplayInputs();
		blog(LOG_INFO, "[teste-replay] entradas do replay criadas: %s", made.toUtf8().constData());
		ReplayConfig cfg = engine().config();
		for (int i = 0; i < MAX_CAMERAS; i++)
			cfg.cameras[i].clear();
		cfg.cameras[0] = "CAM 1";
		cfg.cameras[1] = "CAM 2";
		cfg.outputDir = (QDir::tempPath() + "/rds-replay-teste").toUtf8().constData();
		engine().setConfig(cfg);
		e.cutTo(e.inputs().first()->id);
		std::string err;
		const bool ok = engine().startRecording(err);
		blog(LOG_INFO, "[teste-replay] gravando=%d %s", ok, err.c_str());
	});
	if (qEnvironmentVariableIsSet("RDS_SELFTEST_VELOCIDADE")) {
		/* 100% = tempo real, mesmo com a máquina lenta (RDS_REPLAY_ATRASO_MS) */
		static double p0 = 0;
		static qint64 t0 = 0;
		step(2500, [] {
			engine().seekTo(5);
			engine().play();
			t0 = QDateTime::currentMSecsSinceEpoch();
		});
		step(2700, [] { p0 = engine().channel(0).state().pos; t0 = QDateTime::currentMSecsSinceEpoch(); });
		step(3900, [] {
			const ChannelState st = engine().channel(0).state();
			const double secs = (QDateTime::currentMSecsSinceEpoch() - t0) / 1000.0;
			blog(LOG_INFO, "[teste-velocidade] andou %.1f quadros em %.2f s = %.2f quadros/s (gravação %.2f)",
			     st.pos - p0, secs, (st.pos - p0) / secs, st.fps);
			engine().goLive();
		});
	}
	if (qEnvironmentVariableIsSet("RDS_SELFTEST_MARK")) {
		/* Mark In/Out no replay (fora do ao vivo): marca onde o replay está, não o ao vivo */
		step(3000, [] {
			engine().seekTo(20);
			engine().pause();
		});
		step(3300, [] {
			blog(LOG_INFO, "[teste-mark] posição do replay: quadro que o Mark In usaria=%lld",
			     (long long)engine().markFrame());
			engine().markIn();
			engine().seekTo(50);
		});
		step(3600, [] {
			engine().markOut();
			const auto ev = engine().events(engine().activeList());
			if (!ev.empty())
				blog(LOG_INFO, "[teste-mark] evento marcado no replay: in=%lld out=%lld",
				     (long long)ev.back().in, (long long)ev.back().out);
			engine().goLive();
		});
		step(3800, [] {
			blog(LOG_INFO, "[teste-mark] ao vivo: Mark In usaria %lld (ao vivo)", (long long)engine().markFrame());
		});
	}
	step(4000, [] {
		engine().autoEvent(2);
		blog(LOG_INFO, "[teste-replay] eventos na lista: %d", (int)engine().events(engine().activeList()).size());
	});
	step(4300, [] { AirControl::instance()->toggleLast(); });
	step(5200, [pgmName] { blog(LOG_INFO, "[teste-replay] no ar durante o replay: %s", pgmName().constData()); });
	step(5300, [] {
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_REPLAY"))
			QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_REPLAY")});
	});
	step(10000, [pgmName] {
		blog(LOG_INFO, "[teste-replay] no ar depois do replay: %s", pgmName().constData());
		uint64_t bytes = 0;
		double sec = 0;
		engine().recordingStats(bytes, sec);
		blog(LOG_INFO, "[teste-replay] gravado: %.1f MB em %.1f s, quadros perdidos: %llu", bytes / 1e6, sec,
		     (unsigned long long)engine().droppedFrames());
	});
	step(10500, [window] { window->close(); });
}

void openSettings()
{
	if (g_dock)
		g_dock->openConfig();
}

void runWizard()
{
	if (g_dock)
		g_dock->runWizard();
}

QString statusText(bool &alarm, bool &recording)
{
	ReplayEngine &e = engine();
	recording = e.recording();
	alarm = false;
	if (!recording)
		return QString();
	const uint64_t lost = e.droppedFrames();
	const size_t backlog = e.diskBacklogMB();
	QString t = lost ? QString("⚠ Replay perdeu %1 quadros").arg(lost) : QString("● Replay gravando · 0 perdidos");
	if (backlog >= 64)
		t += QString(" · disco atrasado (%1 MB na fila)").arg(backlog);
	alarm = lost > 0 || backlog >= 256;
	return t;
}

void toggleRecording()
{
	if (g_dock)
		g_dock->toggleRecording();
}

void finish()
{
	engineDestroy();
	obs_weak_source_release(g_transition);
	g_transition = nullptr;
	g_callbacks.clear();
}

} // namespace replayhost
