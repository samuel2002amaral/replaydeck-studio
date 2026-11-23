#include "state.hpp"

#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <cstdio>
#include <utility>

static constexpr int64_t TIMEOUT_MS = 60 * 1000; /* pedido de tempo no futsal: 1 minuto */

Placar &Placar::get()
{
	static Placar p;
	return p;
}

Placar::Placar()
{
	team[0].name = "CASA";
	team[0].sigla = "CAS";
	team[0].bg = 0xFF1E5AA8;
	team[1].name = "VISITANTE";
	team[1].sigla = "VIS";
	team[1].bg = 0xFFB3261E;
}

int64_t Placar::lengthMsLocked() const
{
	if (period == P_PENALTIES)
		return 0;
	const int min = (period == P_EXTRA1 || period == P_EXTRA2) ? extraMinutes : periodMinutes;
	return (int64_t)min * 60 * 1000;
}

int64_t Placar::elapsedLocked() const
{
	int64_t e = baseMs;
	if (running)
		e += (int64_t)((os_gettime_ns() - startNs) / 1000000ULL);
	const int64_t len = lengthMsLocked();
	if (len > 0)
		e = std::min(e, len);
	return std::max<int64_t>(0, e);
}

Snapshot Placar::snapshot()
{
	std::lock_guard<std::mutex> l(m);
	Snapshot s;
	s.team[0] = team[0];
	s.team[1] = team[1];
	s.period = period;
	s.periodMinutes = periodMinutes;
	s.extraMinutes = extraMinutes;
	s.countDown = countDown;
	s.running = running;
	s.elapsedMs = elapsedLocked();
	s.lengthMs = lengthMsLocked();
	s.timeoutTeam = timeoutTeam;
	if (timeoutTeam >= 0) {
		const int64_t used = (int64_t)((os_gettime_ns() - timeoutStartNs) / 1000000ULL);
		s.timeoutLeftMs = std::max<int64_t>(0, TIMEOUT_MS - used);
	}
	s.competition = competition;
	s.version = ver.load();
	return s;
}

static bool validTeam(int t)
{
	return t == 0 || t == 1;
}

void Placar::addScore(int t, int delta)
{
	if (!validTeam(t))
		return;
	std::lock_guard<std::mutex> l(m);
	team[t].score = std::max(0, team[t].score + delta);
	bump();
}

void Placar::addFoul(int t, int delta)
{
	if (!validTeam(t))
		return;
	std::lock_guard<std::mutex> l(m);
	team[t].fouls = std::clamp(team[t].fouls + delta, 0, 99);
	bump();
}

void Placar::addPenalty(int t, int delta)
{
	if (!validTeam(t))
		return;
	std::lock_guard<std::mutex> l(m);
	team[t].penalties = std::max(0, team[t].penalties + delta);
	bump();
}

void Placar::startTimeout(int t)
{
	if (!validTeam(t))
		return;
	std::lock_guard<std::mutex> l(m);
	if (running) {
		baseMs = elapsedLocked();
		running = false;
	}
	team[t].timeouts = std::min(team[t].timeouts + 1, 9);
	timeoutTeam = t;
	timeoutStartNs = os_gettime_ns();
	bump();
}

void Placar::cancelTimeout()
{
	std::lock_guard<std::mutex> l(m);
	timeoutTeam = -1;
	bump();
}

void Placar::setTeam(int t, const std::string &name, const std::string &sigla)
{
	if (!validTeam(t))
		return;
	std::lock_guard<std::mutex> l(m);
	team[t].name = name;
	team[t].sigla = sigla;
	bump();
}

void Placar::setColors(int t, uint32_t bg, uint32_t fg)
{
	if (!validTeam(t))
		return;
	std::lock_guard<std::mutex> l(m);
	team[t].bg = bg;
	team[t].fg = fg;
	bump();
}

void Placar::setCompetition(const std::string &text)
{
	std::lock_guard<std::mutex> l(m);
	competition = text;
	bump();
}

void Placar::swapTeams()
{
	std::lock_guard<std::mutex> l(m);
	std::swap(team[0], team[1]);
	if (timeoutTeam >= 0)
		timeoutTeam = 1 - timeoutTeam;
	bump();
}

void Placar::newGame()
{
	std::lock_guard<std::mutex> l(m);
	for (auto &t : team) {
		t.score = 0;
		t.fouls = 0;
		t.timeouts = 0;
		t.penalties = 0;
	}
	period = P_FIRST;
	running = false;
	baseMs = 0;
	timeoutTeam = -1;
	bump();
}

void Placar::startClock()
{
	std::lock_guard<std::mutex> l(m);
	if (running || period == P_PENALTIES)
		return;
	if (lengthMsLocked() > 0 && baseMs >= lengthMsLocked())
		return; /* período terminou: zere ou passe para o próximo */
	timeoutTeam = -1;
	startNs = os_gettime_ns();
	running = true;
	bump();
}

void Placar::stopClock()
{
	std::lock_guard<std::mutex> l(m);
	if (!running)
		return;
	baseMs = elapsedLocked();
	running = false;
	bump();
}

void Placar::toggleClock()
{
	bool r;
	{
		std::lock_guard<std::mutex> l(m);
		r = running;
	}
	if (r)
		stopClock();
	else
		startClock();
}

void Placar::setClockDisplay(int64_t shownMs)
{
	std::lock_guard<std::mutex> l(m);
	const int64_t len = lengthMsLocked();
	int64_t e = countDown && len > 0 ? len - shownMs : shownMs;
	if (len > 0)
		e = std::clamp<int64_t>(e, 0, len);
	baseMs = std::max<int64_t>(0, e);
	if (running)
		startNs = os_gettime_ns();
	bump();
}

void Placar::adjustClock(int64_t deltaShownMs)
{
	std::lock_guard<std::mutex> l(m);
	const int64_t len = lengthMsLocked();
	int64_t e = elapsedLocked() + (countDown && len > 0 ? -deltaShownMs : deltaShownMs);
	if (len > 0)
		e = std::clamp<int64_t>(e, 0, len);
	baseMs = std::max<int64_t>(0, e);
	if (running)
		startNs = os_gettime_ns();
	bump();
}

void Placar::resetPeriodClock()
{
	std::lock_guard<std::mutex> l(m);
	running = false;
	baseMs = 0;
	bump();
}

void Placar::setPeriod(int p)
{
	if (p < 0 || p >= P_COUNT)
		return;
	std::lock_guard<std::mutex> l(m);
	if (p == period)
		return;
	/* faltas acumuladas zeram no 2º tempo; na prorrogação continuam as do 2º tempo.
	 * Pedidos de tempo: 1 por tempo, nenhum na prorrogação. */
	if (p == P_SECOND && period == P_FIRST) {
		for (auto &t : team) {
			t.fouls = 0;
			t.timeouts = 0;
		}
	}
	period = p;
	running = false;
	baseMs = 0;
	timeoutTeam = -1;
	bump();
}

void Placar::nextPeriod()
{
	int p;
	{
		std::lock_guard<std::mutex> l(m);
		p = period;
	}
	if (p + 1 < P_COUNT)
		setPeriod(p + 1);
}

void Placar::setConfig(int pm, int em, bool cd)
{
	std::lock_guard<std::mutex> l(m);
	periodMinutes = std::clamp(pm, 1, 99);
	extraMinutes = std::clamp(em, 1, 99);
	countDown = cd;
	bump();
}

void Placar::tick()
{
	std::lock_guard<std::mutex> l(m);
	if (running) {
		const int64_t len = lengthMsLocked();
		if (len > 0 && elapsedLocked() >= len) {
			baseMs = len;
			running = false;
			bump();
		}
	}
	if (timeoutTeam >= 0 && os_gettime_ns() - timeoutStartNs >= (uint64_t)TIMEOUT_MS * 1000000ULL) {
		timeoutTeam = -1;
		bump();
	}
}

static void saveTeam(obs_data_t *o, const Team &t)
{
	obs_data_set_string(o, "name", t.name.c_str());
	obs_data_set_string(o, "sigla", t.sigla.c_str());
	obs_data_set_int(o, "bg", t.bg);
	obs_data_set_int(o, "fg", t.fg);
	obs_data_set_int(o, "score", t.score);
	obs_data_set_int(o, "fouls", t.fouls);
	obs_data_set_int(o, "timeouts", t.timeouts);
	obs_data_set_int(o, "penalties", t.penalties);
}

static void loadTeam(obs_data_t *o, Team &t)
{
	if (!o)
		return;
	if (obs_data_has_user_value(o, "name"))
		t.name = obs_data_get_string(o, "name");
	if (obs_data_has_user_value(o, "sigla"))
		t.sigla = obs_data_get_string(o, "sigla");
	if (obs_data_has_user_value(o, "bg"))
		t.bg = (uint32_t)obs_data_get_int(o, "bg");
	if (obs_data_has_user_value(o, "fg"))
		t.fg = (uint32_t)obs_data_get_int(o, "fg");
	t.score = (int)obs_data_get_int(o, "score");
	t.fouls = (int)obs_data_get_int(o, "fouls");
	t.timeouts = (int)obs_data_get_int(o, "timeouts");
	t.penalties = (int)obs_data_get_int(o, "penalties");
}

void Placar::save(obs_data_t *d)
{
	std::lock_guard<std::mutex> l(m);
	for (int i = 0; i < 2; i++) {
		obs_data_t *o = obs_data_create();
		saveTeam(o, team[i]);
		obs_data_set_obj(d, i == 0 ? "home" : "away", o);
		obs_data_release(o);
	}
	obs_data_set_int(d, "period", period);
	obs_data_set_int(d, "period_minutes", periodMinutes);
	obs_data_set_int(d, "extra_minutes", extraMinutes);
	obs_data_set_bool(d, "count_down", countDown);
	obs_data_set_int(d, "elapsed_ms", elapsedLocked());
	obs_data_set_string(d, "competition", competition.c_str());
}

void Placar::load(obs_data_t *d)
{
	if (!d)
		return;
	std::lock_guard<std::mutex> l(m);
	for (int i = 0; i < 2; i++) {
		obs_data_t *o = obs_data_get_obj(d, i == 0 ? "home" : "away");
		loadTeam(o, team[i]);
		obs_data_release(o);
	}
	period = std::clamp((int)obs_data_get_int(d, "period"), 0, P_COUNT - 1);
	if (obs_data_has_user_value(d, "period_minutes"))
		periodMinutes = std::clamp((int)obs_data_get_int(d, "period_minutes"), 1, 99);
	if (obs_data_has_user_value(d, "extra_minutes"))
		extraMinutes = std::clamp((int)obs_data_get_int(d, "extra_minutes"), 1, 99);
	if (obs_data_has_user_value(d, "count_down"))
		countDown = obs_data_get_bool(d, "count_down");
	baseMs = std::max<int64_t>(0, obs_data_get_int(d, "elapsed_ms"));
	competition = obs_data_get_string(d, "competition");
	running = false; /* ao abrir, o relógio sempre começa parado */
	timeoutTeam = -1;
	bump();
}

std::string Placar::periodName(int p, bool shortName)
{
	switch (p) {
	case P_FIRST:
		return shortName ? "1ºT" : "1º tempo";
	case P_SECOND:
		return shortName ? "2ºT" : "2º tempo";
	case P_EXTRA1:
		return shortName ? "1ºP" : "1º prorrogação";
	case P_EXTRA2:
		return shortName ? "2ºP" : "2º prorrogação";
	case P_PENALTIES:
		return shortName ? "PÊN" : "Pênaltis";
	}
	return "";
}

std::string Placar::formatClock(int64_t ms, bool countDown)
{
	if (ms < 0)
		ms = 0;
	char buf[32];
	if (countDown && ms < 60 * 1000) {
		/* último minuto: segundos e décimos (59.9) */
		std::snprintf(buf, sizeof(buf), "%d.%d", (int)(ms / 1000), (int)((ms % 1000) / 100));
	} else {
		/* regressivo arredonda para cima (mostra 20:00 até o primeiro segundo passar) */
		const int64_t s = countDown ? (ms + 999) / 1000 : ms / 1000;
		std::snprintf(buf, sizeof(buf), "%02d:%02d", (int)(s / 60), (int)(s % 60));
	}
	return buf;
}
