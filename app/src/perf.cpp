#include "perf.hpp"
#include "engine.hpp"

#include <util/base.h>

#include <QApplication>
#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0A00
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 /* Windows 10: ProcessPowerThrottling */
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>
#endif

namespace perf {

#ifdef _WIN32
static bool g_timer = false;
static bool g_throttleOff = false;
#endif

void tuneProcess()
{
#ifdef _WIN32
	/* Windows 11 põe processos em "EcoQoS" quando a janela não está na frente (ou o notebook
	 * está na bateria): as threads de vídeo perdem tempo e o programa engasga. Desliga. */
	PROCESS_POWER_THROTTLING_STATE pt = {};
	pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
	pt.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
#endif
	pt.StateMask = 0; /* 0 = não estrangular */
	g_throttleOff = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt)) != FALSE;

	/* acima do normal: o vídeo ao vivo vem antes de navegador, atualizações etc. (não "tempo real",
	 * que pode travar o mouse e o próprio Windows) */
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);

	/* relógio de 1 ms: os quadros saem no tempo certo (sem tremer) */
	g_timer = timeBeginPeriod(1) == TIMERR_NOERROR;

	/* não apaga a tela nem suspende o PC no meio do jogo */
	SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
}

void logState()
{
#ifdef _WIN32
	blog(LOG_INFO, "desempenho: prioridade %s, economia de energia do Windows %s, relógio 1 ms %s, sem suspender",
	     GetPriorityClass(GetCurrentProcess()) == ABOVE_NORMAL_PRIORITY_CLASS ? "acima do normal" : "normal",
	     g_throttleOff ? "desligada" : "não deu para desligar", g_timer ? "sim" : "não");
#endif
}

void restore()
{
#ifdef _WIN32
	SetThreadExecutionState(ES_CONTINUOUS);
	if (g_timer)
		timeEndPeriod(1);
	g_timer = false;
#endif
}

static std::atomic<bool> g_watchRun{false};
static std::atomic<qint64> g_lastBeat{0};
static std::thread g_watch;

void startHangWatch()
{
	if (g_watchRun)
		return;
	g_watchRun = true;
	g_lastBeat = QDateTime::currentMSecsSinceEpoch();
	auto *beat = new QTimer(qApp);
	QObject::connect(beat, &QTimer::timeout, [] { g_lastBeat = QDateTime::currentMSecsSinceEpoch(); });
	beat->start(250);
	g_watch = std::thread([] {
		bool reported = false;
		qint64 since = 0;
		while (g_watchRun) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			const qint64 now = QDateTime::currentMSecsSinceEpoch();
			const qint64 stalled = now - g_lastBeat;
			if (stalled > 2000 && !reported) {
				reported = true;
				since = g_lastBeat;
				blog(LOG_WARNING, "[vigia] a interface está parada há %lld ms (o vídeo continua no ar)",
				     (long long)stalled);
			} else if (reported && stalled < 1000) {
				reported = false;
				blog(LOG_WARNING, "[vigia] a interface voltou depois de %lld ms parada",
				     (long long)(g_lastBeat - since));
			}
		}
	});
}

void stopHangWatch()
{
	g_watchRun = false;
	if (g_watch.joinable())
		g_watch.join();
}

void startAutosave(QObject *owner)
{
	Engine &e = Engine::get();
	static bool dirty = false;
	auto mark = [] { dirty = true; };
	QObject::connect(&e, &Engine::inputsChanged, owner, mark);
	QObject::connect(&e, &Engine::inputChanged, owner, mark);
	QObject::connect(&e, &Engine::inputRenamed, owner, mark);
	QObject::connect(&e, &Engine::transitionsChanged, owner, mark);
	QObject::connect(&e, &Engine::transitionMsChanged, owner, mark);
	QObject::connect(&e, &Engine::audioChanged, owner, mark);
	auto *t = new QTimer(owner);
	QObject::connect(t, &QTimer::timeout, owner, [] {
		if (!dirty)
			return;
		dirty = false;
		/* se o programa travar ou faltar luz, o projeto de até 1 minuto atrás está salvo */
		Engine &en = Engine::get();
		en.saveProject(en.projectPath().isEmpty() ? en.defaultProjectPath() : en.projectPath());
	});
	t->start(60000);
}

} // namespace perf
