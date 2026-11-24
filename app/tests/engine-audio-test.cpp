/* Teste: áudio como no vMix. Microfone (só áudio) fica sempre na mistura; entrada com imagem
 * segue o vídeo (Auto). Sem Auto e fora do ar, toca pela cena de áudio (canal 5) sem dobrar
 * quando vai ao ar. Volume da entrada x volume geral. Tudo salvo no projeto. */
#include "../src/engine.hpp"

#include <obs.h>
#include <obs-nix-platform.h>

#include <QApplication>
#include <QDir>
#include <QSet>
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char *what)
{
	printf("%s %s\n", ok ? "OK:" : "FALHA:", what);
	if (!ok)
		fails++;
}

static void *fakeCreate(obs_data_t *, obs_source_t *)
{
	return (void *)1;
}
static void fakeDestroy(void *) {}
static const char *fakeName(void *)
{
	return "teste";
}
static uint32_t fakeSize(void *)
{
	return 16;
}

static QSet<obs_source_t *> bed()
{
	QSet<obs_source_t *> s;
	obs_source_t *b = obs_get_output_source(5);
	if (!b)
		return s;
	obs_scene_enum_items(
		obs_scene_from_source(b),
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			static_cast<QSet<obs_source_t *> *>(p)->insert(obs_sceneitem_get_source(item));
			return true;
		},
		&s);
	obs_source_release(b);
	return s;
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
	struct obs_source_info mic = {};
	mic.id = "teste_mic";
	mic.type = OBS_SOURCE_TYPE_INPUT;
	mic.output_flags = OBS_SOURCE_AUDIO;
	mic.get_name = fakeName;
	mic.create = fakeCreate;
	mic.destroy = fakeDestroy;
	obs_register_source(&mic);
	struct obs_source_info cam = mic;
	cam.id = "teste_cam_som";
	cam.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO;
	cam.get_width = fakeSize;
	cam.get_height = fakeSize;
	obs_register_source(&cam);

	while (!e.inputs().isEmpty())
		e.removeInput(e.inputs().first()->id);
	Input *m = e.addInput("teste_mic", "MIC");
	Input *c = e.addInput("teste_cam_som", "CAM");
	Input *c2 = e.addInput("teste_cam_som", "CAM 2");
	check(!m->audioAuto && c->audioAuto, "padrão: microfone sempre, câmera segue o vídeo (Auto)");
	check(bed().contains(m->source) && !bed().contains(c->source), "microfone toca mesmo fora do ar; câmera não");

	e.setInputAudioAuto(c->id, false);
	check(bed().contains(c->source), "câmera sem Auto: toca mesmo fora do ar");
	e.cutTo(c->id);
	check(!bed().contains(c->source), "câmera no ar: sai da cena de áudio (toca pelo programa, sem dobrar)");
	e.cutTo(c2->id);
	check(bed().contains(c->source), "saiu do ar: volta para a cena de áudio");

	e.setInputAudio(m->id, false);
	check(!bed().contains(m->source), "microfone desligado (On): sai da mistura");
	e.setInputAudio(m->id, true);

	e.setInputVolume(m->id, 0.5f);
	e.setMasterVolume(0.5f);
	check(qAbs(obs_source_get_volume(m->source) - 0.25f) < 0.001f, "volume = entrada x geral");
	e.setInputMonitor(m->id, true);
	check(obs_source_get_monitoring_type(m->source) == OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT, "fone liga o monitoramento");

	const QString path = QDir::tempPath() + "/rds-audio-test.rdsproj";
	e.saveProject(path);
	e.loadProject(path);
	Input *m2 = e.inputs().size() == 3 ? e.inputs()[0] : nullptr;
	Input *cl = e.inputs().size() == 3 ? e.inputs()[1] : nullptr;
	check(m2 && qAbs(m2->volume - 0.5f) < 0.001f && m2->monitor && qAbs(e.masterVolume() - 0.5f) < 0.001f &&
		      qAbs(obs_source_get_volume(m2->source) - 0.25f) < 0.001f,
	      "salvar e abrir mantém volume, fone e volume geral");
	check(cl && !cl->audioAuto && bed().contains(cl->source) && bed().contains(m2->source),
	      "salvar e abrir mantém o Auto e a cena de áudio");

	e.stop();
	printf(fails ? "FALHOU\n" : "TUDO OK\n");
	return fails ? 1 : 0;
}
