/* Placar de Futsal: fonte gráfica + painel de controle + atalhos + controle remoto (obs-websocket). */
#include "placar-dock.hpp"
#include "state.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMainWindow>

#include <functional>
#include <string>
#include <vector>

#if __has_include(<obs-websocket-api.h>)
#include <obs-websocket-api.h>
#define PLACAR_WEBSOCKET 1
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("futsal-placar", "pt-BR")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Placar de Futsal";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Placar e cronômetro de futsal com faltas acumuladas, pedidos de tempo e pênaltis";
}

void registerPlacarSource();

/* ---------- ações (usadas pelos atalhos e pelo controle remoto) ---------- */

struct Action {
	const char *id;
	const char *desc;
	std::function<void()> fn;
	obs_hotkey_id hk = OBS_INVALID_HOTKEY_ID;
};

static std::vector<Action> &actions()
{
	static std::vector<Action> a = {
		{"gol_casa", "Placar: gol do time da esquerda", [] { Placar::get().addScore(0, 1); }},
		{"gol_visitante", "Placar: gol do time da direita", [] { Placar::get().addScore(1, 1); }},
		{"menos_gol_casa", "Placar: tirar gol do time da esquerda", [] { Placar::get().addScore(0, -1); }},
		{"menos_gol_visitante", "Placar: tirar gol do time da direita", [] { Placar::get().addScore(1, -1); }},
		{"falta_casa", "Placar: falta do time da esquerda", [] { Placar::get().addFoul(0, 1); }},
		{"falta_visitante", "Placar: falta do time da direita", [] { Placar::get().addFoul(1, 1); }},
		{"menos_falta_casa", "Placar: tirar falta do time da esquerda", [] { Placar::get().addFoul(0, -1); }},
		{"menos_falta_visitante", "Placar: tirar falta do time da direita", [] { Placar::get().addFoul(1, -1); }},
		{"tempo_casa", "Placar: pedido de tempo do time da esquerda", [] { Placar::get().startTimeout(0); }},
		{"tempo_visitante", "Placar: pedido de tempo do time da direita", [] { Placar::get().startTimeout(1); }},
		{"relogio", "Placar: iniciar/pausar relógio", [] { Placar::get().toggleClock(); }},
		{"relogio_iniciar", "Placar: iniciar relógio", [] { Placar::get().startClock(); }},
		{"relogio_pausar", "Placar: pausar relógio", [] { Placar::get().stopClock(); }},
		{"relogio_menos_1s", "Placar: relógio −1 s", [] { Placar::get().adjustClock(-1000); }},
		{"relogio_mais_1s", "Placar: relógio +1 s", [] { Placar::get().adjustClock(1000); }},
		{"proximo_periodo", "Placar: próximo período", [] { Placar::get().nextPeriod(); }},
		{"zerar_periodo", "Placar: zerar relógio do período", [] { Placar::get().resetPeriodClock(); }},
		{"inverter", "Placar: inverter times", [] { Placar::get().swapTeams(); }},
	};
	return a;
}

static void hotkeyCb(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const size_t i = reinterpret_cast<size_t>(data);
	if (i < actions().size())
		actions()[i].fn();
}

static void saveCb(obs_data_t *saveData, bool saving, void *)
{
	if (saving) {
		obs_data_t *o = obs_data_create();
		Placar::get().save(o);
		obs_data_t *hk = obs_data_create();
		for (auto &a : actions()) {
			obs_data_array_t *arr = obs_hotkey_save(a.hk);
			obs_data_set_array(hk, a.id, arr);
			obs_data_array_release(arr);
		}
		obs_data_set_obj(o, "hotkeys", hk);
		obs_data_release(hk);
		obs_data_set_obj(saveData, "futsal_placar", o);
		obs_data_release(o);
		return;
	}
	obs_data_t *o = obs_data_get_obj(saveData, "futsal_placar");
	if (!o)
		return;
	Placar::get().load(o);
	obs_data_t *hk = obs_data_get_obj(o, "hotkeys");
	if (hk) {
		for (auto &a : actions()) {
			obs_data_array_t *arr = obs_data_get_array(hk, a.id);
			obs_hotkey_load(a.hk, arr);
			obs_data_array_release(arr);
		}
		obs_data_release(hk);
	}
	obs_data_release(o);
}

/* ---------- obs-websocket: Stream Deck, Companion, celular ---------- */
#ifdef PLACAR_WEBSOCKET
static void wsAction(obs_data_t *req, obs_data_t *res, void *)
{
	const std::string id = obs_data_get_string(req, "acao");
	for (auto &a : actions()) {
		if (id == a.id) {
			a.fn();
			obs_data_set_bool(res, "ok", true);
			return;
		}
	}
	std::string list;
	for (auto &a : actions())
		list += std::string(list.empty() ? "" : ", ") + a.id;
	obs_data_set_bool(res, "ok", false);
	obs_data_set_string(res, "erro", ("ação desconhecida; use: " + list).c_str());
}

static void wsSet(obs_data_t *req, obs_data_t *res, void *)
{
	auto &p = Placar::get();
	const Snapshot st = p.snapshot();
	for (int t = 0; t < 2; t++) {
		const char *key = t == 0 ? "casa" : "visitante";
		obs_data_t *o = obs_data_get_obj(req, key);
		if (!o)
			continue;
		const std::string name = obs_data_has_user_value(o, "nome") ? obs_data_get_string(o, "nome") : st.team[t].name;
		const std::string sigla = obs_data_has_user_value(o, "sigla") ? obs_data_get_string(o, "sigla") : st.team[t].sigla;
		p.setTeam(t, name, sigla);
		if (obs_data_has_user_value(o, "gols"))
			p.addScore(t, (int)obs_data_get_int(o, "gols") - st.team[t].score);
		if (obs_data_has_user_value(o, "faltas"))
			p.addFoul(t, (int)obs_data_get_int(o, "faltas") - st.team[t].fouls);
		obs_data_release(o);
	}
	if (obs_data_has_user_value(req, "periodo"))
		p.setPeriod((int)obs_data_get_int(req, "periodo"));
	if (obs_data_has_user_value(req, "relogio_ms"))
		p.setClockDisplay(obs_data_get_int(req, "relogio_ms"));
	if (obs_data_has_user_value(req, "competicao"))
		p.setCompetition(obs_data_get_string(req, "competicao"));
	obs_data_set_bool(res, "ok", true);
}

static void wsGet(obs_data_t *, obs_data_t *res, void *)
{
	const Snapshot st = Placar::get().snapshot();
	for (int t = 0; t < 2; t++) {
		obs_data_t *o = obs_data_create();
		obs_data_set_string(o, "nome", st.team[t].name.c_str());
		obs_data_set_string(o, "sigla", st.team[t].sigla.c_str());
		obs_data_set_int(o, "gols", st.team[t].score);
		obs_data_set_int(o, "faltas", st.team[t].fouls);
		obs_data_set_int(o, "pedidos_de_tempo", st.team[t].timeouts);
		obs_data_set_int(o, "penaltis", st.team[t].penalties);
		obs_data_set_obj(res, t == 0 ? "casa" : "visitante", o);
		obs_data_release(o);
	}
	obs_data_set_int(res, "periodo", st.period);
	obs_data_set_string(res, "periodo_nome", Placar::periodName(st.period, false).c_str());
	obs_data_set_bool(res, "rolando", st.running);
	const int64_t shown = st.countDown ? st.lengthMs - st.elapsedMs : st.elapsedMs;
	obs_data_set_int(res, "relogio_ms", shown);
	obs_data_set_string(res, "relogio", Placar::formatClock(shown, st.countDown).c_str());
	obs_data_set_string(res, "competicao", st.competition.c_str());
}
#endif

bool obs_module_load(void)
{
	registerPlacarSource();

	auto &a = actions();
	for (size_t i = 0; i < a.size(); i++) {
		const std::string name = std::string("futsal_placar.") + a[i].id;
		a[i].hk = obs_hotkey_register_frontend(name.c_str(), a[i].desc, hotkeyCb, reinterpret_cast<void *>(i));
	}
	obs_frontend_add_save_callback(saveCb, nullptr);

	auto *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_add_dock_by_id("futsal-placar-dock", obs_module_text("Placar.Painel"), new PlacarDock(mainWin));

	blog(LOG_INFO, "[futsal-placar] carregado");
	return true;
}

void obs_module_post_load(void)
{
#ifdef PLACAR_WEBSOCKET
	obs_websocket_vendor vendor = obs_websocket_register_vendor("futsal-placar");
	if (!vendor) {
		blog(LOG_INFO, "[futsal-placar] obs-websocket não encontrado: controle remoto desligado");
		return;
	}
	obs_websocket_vendor_register_request(vendor, "Acao", wsAction, nullptr);
	obs_websocket_vendor_register_request(vendor, "Definir", wsSet, nullptr);
	obs_websocket_vendor_register_request(vendor, "Estado", wsGet, nullptr);
#endif
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(saveCb, nullptr);
}
