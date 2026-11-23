/* Teste: "Desativar entrada" vale só para a entrada. Com a mesma câmera em duas entradas,
 * desativar uma deixa a outra ao vivo; o aparelho só desliga quando todas estão desativadas.
 * Usa uma câmera de mentira com o mesmo comando activate(bool) do driver do Windows (win-dshow). */
#include "../src/engine.hpp"

#include <obs.h>
#include <obs-module.h>
#include <obs-nix-platform.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <cstdio>

static int fails = 0;
static int activateCalls = 0;
static void check(bool ok, const char *what)
{
	printf("%s %s\n", ok ? "OK:" : "FALHA:", what);
	if (!ok)
		fails++;
}

/* ---- câmera de mentira ---- */
struct FakeCam {
	obs_source_t *source;
};
static void fakeActivate(void *data, calldata_t *cd)
{
	auto *cam = static_cast<FakeCam *>(data);
	activateCalls++;
	obs_data_t *st = obs_source_get_settings(cam->source);
	obs_data_set_bool(st, "active", calldata_bool(cd, "active")); /* igual ao SetActive do win-dshow */
	obs_data_release(st);
}
static void *fakeCreate(obs_data_t *, obs_source_t *source)
{
	auto *cam = new FakeCam{source};
	proc_handler_add(obs_source_get_proc_handler(source), "void activate(bool active)", fakeActivate, cam);
	return cam;
}
static void fakeDestroy(void *data)
{
	delete static_cast<FakeCam *>(data);
}
static const char *fakeName(void *)
{
	return "Câmera de teste";
}
static uint32_t fakeSize(void *)
{
	return 16;
}
static void fakeDefaults(obs_data_t *st)
{
	obs_data_set_default_bool(st, "active", true);
}

static bool visible(Input *in)
{
	obs_sceneitem_t *found = nullptr;
	obs_scene_enum_items(
		obs_scene_from_source(in->scene),
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			*static_cast<obs_sceneitem_t **>(p) = item;
			return false;
		},
		&found);
	return found && obs_sceneitem_visible(found);
}

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "xcb");
	QApplication app(argc, argv);
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	if (auto *x11 = app.nativeInterface<QNativeInterface::QX11Application>())
		obs_set_nix_platform_display(x11->display());
	QString err;
	Engine &e = Engine::get();
	if (!e.start(err)) {
		printf("FALHA ao iniciar: %s\n", qPrintable(err));
		return 2;
	}
	struct obs_source_info info = {};
	info.id = "dshow_input";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = fakeName;
	info.create = fakeCreate;
	info.destroy = fakeDestroy;
	info.get_width = fakeSize;
	info.get_height = fakeSize;
	info.get_defaults = fakeDefaults;
	obs_register_source(&info);

	while (!e.inputs().isEmpty())
		e.removeInput(e.inputs().first()->id);

	Input *a = e.addInput("dshow_input", "CAM 1");
	Input *b = e.addSharedInput(a->id, "CAM 4");
	obs_source_t *cam = a->source.Get();
	check(b && b->source.Get() == cam, "(preparação) as duas usam a mesma câmera");
	check(Engine::deviceActive(cam), "(preparação) câmera ligada");

	e.setInputEnabled(a->id, false);
	check(!visible(a) && !a->enabled, "desativar a entrada 1: ela fica preta");
	check(visible(b) && b->enabled, "a entrada 4 continua ao vivo");
	check(Engine::deviceActive(cam) && activateCalls == 0, "a câmera NÃO desliga (a outra ainda usa)");

	e.setInputEnabled(b->id, false);
	check(!Engine::deviceActive(cam) && activateCalls == 1, "desativar as duas: aí sim a câmera desliga");

	e.setInputEnabled(a->id, true);
	check(Engine::deviceActive(cam) && visible(a) && !visible(b), "ativar só a 1: câmera liga e só a 1 aparece");

	const QString path = QDir::tempPath() + "/rds-enable-test.rdsproj";
	e.saveProject(path);
	e.loadProject(path);
	check(e.inputs().size() == 2 && e.inputs()[0]->enabled && !e.inputs()[1]->enabled &&
		      visible(e.inputs()[0]) && !visible(e.inputs()[1]) && Engine::deviceActive(e.inputs()[0]->source),
	      "salvar e abrir mantém cada entrada como estava");

	/* projeto da versão anterior: câmera desligada pelo botão do driver, sem "desativada" por entrada */
	e.setInputEnabled(e.inputs()[0]->id, false);
	e.saveProject(path);
	{
		OBSDataAutoRelease root = obs_data_create_from_json_file(QFile::encodeName(path).constData());
		OBSDataArrayAutoRelease arr = obs_data_get_array(root, "entradas");
		for (size_t i = 0; i < obs_data_array_count(arr); i++) {
			OBSDataAutoRelease o = obs_data_array_item(arr, i);
			obs_data_erase(o, "desativada");
		}
		obs_data_save_json(root, QFile::encodeName(path).constData());
	}
	e.loadProject(path);
	check(e.inputs().size() == 2 && !e.inputs()[0]->enabled && !e.inputs()[1]->enabled &&
		      !Engine::deviceActive(e.inputs()[0]->source),
	      "projeto antigo com câmera desativada abre com as entradas desativadas");

	e.setInputEnabled(e.inputs()[1]->id, true);
	obs_source_t *cam2 = e.inputs()[1]->source.Get();
	check(Engine::deviceActive(cam2), "(preparação) ativar a 4 liga a câmera");
	e.removeInput(e.inputs()[1]->id);
	check(e.inputs().size() == 1 && !Engine::deviceActive(e.inputs()[0]->source),
	      "remover a única entrada ativa: a câmera desliga (a que sobrou está desativada)");

	e.stop();
	printf(fails ? "FALHOU\n" : "TUDO OK\n");
	return fails ? 1 : 0;
}
