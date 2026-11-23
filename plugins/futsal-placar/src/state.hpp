/* Estado do placar de futsal: único para o programa inteiro, protegido por mutex.
 * Fonte (thread de vídeo), dock (thread do Qt), atalhos e obs-websocket leem e alteram por aqui. */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

struct obs_data;
typedef struct obs_data obs_data_t;

enum Period { P_FIRST = 0, P_SECOND, P_EXTRA1, P_EXTRA2, P_PENALTIES, P_COUNT };

struct Team {
	std::string name;
	std::string sigla;
	uint32_t bg = 0xFF1E5AA8; /* ARGB */
	uint32_t fg = 0xFFFFFFFF;
	int score = 0;
	int fouls = 0;    /* faltas acumuladas no período (a partir da 6ª: tiro livre direto sem barreira) */
	int timeouts = 0; /* pedidos de tempo usados no tempo atual (1 por tempo) */
	int penalties = 0; /* gols na disputa de pênaltis */
};

struct Snapshot {
	Team team[2];
	int period = P_FIRST;
	int periodMinutes = 20;
	int extraMinutes = 5;
	bool countDown = true;
	bool running = false;
	int64_t elapsedMs = 0; /* tempo jogado no período */
	int64_t lengthMs = 0;  /* duração do período */
	int timeoutTeam = -1;  /* pedido de tempo em andamento: time 0/1 */
	int64_t timeoutLeftMs = 0;
	std::string competition;
	uint64_t version = 0;
};

class Placar {
public:
	static Placar &get();

	Snapshot snapshot();
	uint64_t version() const { return ver.load(); }

	/* jogo */
	void addScore(int team, int delta);
	void addFoul(int team, int delta);
	void addPenalty(int team, int delta);
	void startTimeout(int team);
	void cancelTimeout();
	void setTeam(int team, const std::string &name, const std::string &sigla);
	void setColors(int team, uint32_t bg, uint32_t fg);
	void setCompetition(const std::string &text);
	void swapTeams();
	void newGame();

	/* relógio */
	void startClock();
	void stopClock();
	void toggleClock();
	void setClockDisplay(int64_t shownMs); /* o valor mostrado (regressivo ou não) */
	void adjustClock(int64_t deltaShownMs);
	void resetPeriodClock();
	void setPeriod(int p);
	void nextPeriod();
	void setConfig(int periodMinutes, int extraMinutes, bool countDown);

	/* chamado a cada quadro: para o relógio no fim do período e encerra o pedido de tempo */
	void tick();

	void save(obs_data_t *d);
	void load(obs_data_t *d);

	static std::string periodName(int p, bool shortName);
	static std::string formatClock(int64_t ms, bool countDown); /* regressivo: décimos no último minuto */

private:
	Placar();
	int64_t lengthMsLocked() const;
	int64_t elapsedLocked() const;
	void bump() { ver++; }

	std::mutex m;
	std::atomic<uint64_t> ver{1};
	Team team[2];
	int period = P_FIRST;
	int periodMinutes = 20;
	int extraMinutes = 5;
	bool countDown = true;
	bool running = false;
	int64_t baseMs = 0;     /* tempo jogado até o último start */
	uint64_t startNs = 0;   /* quando o relógio foi iniciado */
	int timeoutTeam = -1;
	uint64_t timeoutStartNs = 0;
	std::string competition;
};
