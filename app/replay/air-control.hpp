#pragma once
#include <obs.h>
#include <obs-frontend-api.h>
#include "overlay-transition.hpp"
#include <QObject>
#include <string>
#include <cstdint>

class QTimer;

/* Põe o replay no ar e volta para a cena anterior, usando as transições
 * escolhidas. Roda na thread da interface (a API do OBS exige). */
class AirControl : public QObject {
	Q_OBJECT
public:
	explicit AirControl(QObject *parent = nullptr);
	~AirControl() override;

	static AirControl *instance();

	/* os dois podem ser chamados de qualquer thread (atalhos) */
	void requestIn();
	void requestOut();

	bool onAir() const;

	/* 0 = nada no ar; 1 = "último evento"; 2 = "Play Events"; 3 = outro */
	int runKind() const { return kind; }

public slots:
	void takeIn(int kind = 3);
	void takeOut();
	/* botões ⟲ e Play Events: inicia; se já está rodando, encerra antes da hora */
	void toggleLast();
	void toggleEvents(qlonglong selectedId = 0);
	void abortRun();

private slots:
	void poll();

private:
	static void frontendEvent(enum obs_frontend_event event, void *param);
public:
	/* As transições do OBS são fontes internas: o OBS não manda o aviso geral de
	 * início/fim para elas, só o aviso da própria fonte. Então cada uma é
	 * escutada diretamente (e de novo quando a lista de transições muda). */
	static void watchTransition(obs_source_t *t);
	static void startCover(obs_source_t *t);
	static void prepareCover(obs_source_t *t);
	static void onTransitionStart(void *p, calldata_t *cd) { transitionStarted(p, cd); }
	static void onTransitionStop(void *p, calldata_t *cd) { transitionStopped(p, cd); }
	void watchFrontendTransitions();
	/* troca a cena do programa com uma transição do OBS (nome; "" = a atual)
	 * e devolve a transição/duração anteriores depois */
	bool switchProgram(obs_source_t *scene, const std::string &transition, int durationMs);

private:
	static void transitionStarted(void *param, calldata_t *cd);
	static void transitionStopped(void *param, calldata_t *cd);
	obs_source_t *replayScene(int *channel); /* ref */
	void prepareEventTransition();
	void startStinger();
	void stopStinger();
	void restoreTransition();

	QTimer *timer;
	std::string prevSceneName; /* cena para onde voltar */
	obs_weak_source_t *savedTransition = nullptr;
	int savedDuration = 0;
	bool restorePending = false;
	bool armed = false;
	int kind = 0;
	int armedCh = 0;
	uint64_t lastFinished = 0;
	bool outScheduled = false;
	uint64_t lastStingers[2] = {0, 0};
	uint64_t stingerGen = 0;
	OverlayTransition overlay;
	int prepareTick = 0;
};
