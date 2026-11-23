/* Teste: duas entradas com a MESMA câmera usam a mesma fonte (a câmera abre uma vez só).
 * Tirar uma não desliga a outra; salvar e abrir o projeto mantém as duas juntas. */
#include "../src/engine.hpp"

#include <obs.h>
#include <obs-nix-platform.h>

#include <QApplication>
#include <QDir>
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char *what)
{
	printf("%s %s\n", ok ? "OK:" : "FALHA:", what);
	if (!ok)
		fails++;
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
	while (!e.inputs().isEmpty())
		e.removeInput(e.inputs().first()->id);

	OBSDataAutoRelease s = obs_data_create();
	obs_data_set_int(s, "color", 0xFF0000FF);
	Input *a = e.addInput("color_source_v3", "CAM A", s);
	Input *b = e.addSharedInput(a->id, "CAM A (2)");
	check(b && b->source.Get() == a->source.Get(), "segunda entrada usa a mesma fonte");
	check(e.sharedFrom(b->id) == a->id, "sabe de quem a fonte é");

	const QString path = QDir::tempPath() + "/rds-share-test.rdsproj";
	e.saveProject(path);
	e.loadProject(path);
	check(e.inputs().size() == 2 && e.inputs()[0]->source.Get() == e.inputs()[1]->source.Get(),
	      "depois de salvar e abrir, continuam juntas");

	obs_source_t *shared = e.inputs()[1]->source.Get();
	e.removeInput(e.inputs()[0]->id);
	check(e.inputs().size() == 1 && !obs_source_removed(shared), "tirar uma entrada não desliga a outra");

	e.stop();
	printf(fails ? "FALHOU\n" : "TUDO OK\n");
	return fails ? 1 : 0;
}
