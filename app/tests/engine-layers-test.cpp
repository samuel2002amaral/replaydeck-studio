/* Teste: posição da entrada (zoom, pan, corte, giro) e camadas (outras entradas por cima),
 * como no vMix. Confere a conta da posição, a ordem das camadas, a proteção contra uma entrada
 * dentro dela mesma, salvar/abrir o projeto e remover uma entrada que é camada de outra. */
#include "../src/engine.hpp"

#include <obs.h>
#include <obs-nix-platform.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <cmath>
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char *what)
{
	printf("%s %s\n", ok ? "OK:" : "FALHA:", what);
	if (!ok)
		fails++;
}
static bool near(double a, double b)
{
	return std::fabs(a - b) < 0.6;
}

static QList<obs_sceneitem_t *> items(Input *in)
{
	QList<obs_sceneitem_t *> out;
	obs_scene_enum_items(
		obs_scene_from_source(in->scene),
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			static_cast<QList<obs_sceneitem_t *> *>(p)->append(item);
			return true;
		},
		&out);
	return out; /* de baixo para cima */
}

static Input *color(Engine &e, const char *name, int w, int h)
{
	OBSDataAutoRelease s = obs_data_create();
	obs_data_set_int(s, "color", 0xFF0000FF);
	obs_data_set_int(s, "width", w);
	obs_data_set_int(s, "height", h);
	return e.addInput("color_source_v3", name, s);
}

static void wait(int ms)
{
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms)
		QApplication::processEvents(QEventLoop::AllEvents, 20);
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

	Input *a = color(e, "FUNDO", 1920, 1080);
	Input *b = color(e, "PLACAR", 1920, 1080);
	Input *c = color(e, "CAM", 1280, 720);
	check(a && b && c, "(preparação) três entradas");
	const int aid = a->id, bid = b->id, cid = c->id;

	/* --- posição --- */
	obs_sceneitem_t *main = items(a).first();
	ItemPos p;
	p.zoom = 0.5;
	p.panX = -0.5;
	p.panY = 0.5;
	e.setInputPosition(aid, p);
	vec2 pos, bounds;
	obs_sceneitem_get_pos(main, &pos);
	obs_sceneitem_get_bounds(main, &bounds);
	check(near(pos.x, 480) && near(pos.y, 270) && near(bounds.x, 960) && near(bounds.y, 540),
	      "zoom 0,5 + pan (-0,5; 0,5) = quarto de cima à esquerda");

	p = ItemPos();
	p.cropX1 = 0.5;
	e.setInputPosition(aid, p);
	obs_sceneitem_crop cr;
	obs_sceneitem_get_crop(main, &cr);
	obs_sceneitem_get_pos(main, &pos);
	obs_sceneitem_get_bounds(main, &bounds);
	check(cr.left == 960 && near(pos.x, 1440) && near(bounds.x, 960) && near(bounds.y, 1080),
	      "corte X1 = metade: some a metade esquerda e a direita fica no lugar");

	p = ItemPos();
	p.zoomX = 2.0;
	p.rotation = 90;
	e.setInputPosition(aid, p);
	obs_sceneitem_get_bounds(main, &bounds);
	check(near(bounds.x, 3840) && near(bounds.y, 1080) && near(obs_sceneitem_get_rot(main), 90),
	      "Zoom X estica só na largura; giro de 90°");

	/* câmera 1280x720 cabe na tela de 1920x1080 do mesmo jeito */
	p = ItemPos();
	p.zoom = 0.5;
	e.setInputPosition(cid, p);
	obs_sceneitem_t *cmain = items(c).first();
	obs_sceneitem_get_bounds(cmain, &bounds);
	check(near(bounds.x, 960) && near(bounds.y, 540), "imagem menor que a tela: o zoom vale sobre a tela cheia");

	/* a câmera muda de resolução: a posição continua valendo */
	{
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_int(s, "width", 640);
		obs_data_set_int(s, "height", 480);
		obs_source_update(c->source, s);
	}
	wait(800);
	obs_sceneitem_get_bounds(cmain, &bounds);
	check(near(bounds.x, 720) && near(bounds.y, 540), "imagem mudou de tamanho (4:3): posição refeita sozinha");

	e.setInputPosition(aid, ItemPos());
	obs_sceneitem_get_crop(main, &cr);
	obs_sceneitem_get_bounds(main, &bounds);
	check(cr.left == 0 && near(bounds.x, 1920) && obs_sceneitem_get_bounds_type(main) == OBS_BOUNDS_SCALE_INNER &&
		      near(obs_sceneitem_get_rot(main), 0),
	      "Redefinir todas: volta à tela cheia");

	/* --- camadas --- */
	e.setLayer(aid, 0, bid);
	e.setLayer(aid, 2, cid);
	QList<obs_sceneitem_t *> it = items(a);
	check(it.size() == 3 && obs_sceneitem_get_source(it[0]) == a->source.Get() &&
		      obs_sceneitem_get_source(it[1]) == b->scene.Get() && obs_sceneitem_get_source(it[2]) == c->scene.Get(),
	      "camadas 1 e 3: principal embaixo, camada 1 e depois a 3 por cima");

	e.setLayerBehind(aid, 2, true);
	it = items(a);
	check(it.size() == 3 && obs_sceneitem_get_source(it[0]) == c->scene.Get() &&
		      obs_sceneitem_get_source(it[1]) == a->source.Get() && obs_sceneitem_get_source(it[2]) == b->scene.Get(),
	      "camada 3 \"Atrás\": fica embaixo da imagem principal");
	e.setLayerBehind(aid, 2, false);
	it = items(a);
	check(obs_sceneitem_get_source(it[0]) == a->source.Get() && obs_sceneitem_get_source(it[2]) == c->scene.Get(),
	      "camada 3 de volta \"Na frente\"");
	e.setLayerBehind(aid, 2, true); /* fica atrás para o salvar/abrir */

	e.setLayer(bid, 0, aid);
	check(b->layers[0].inputId == 0 && e.layerWouldLoop(bid, aid) && e.layerWouldLoop(aid, aid),
	      "não deixa uma entrada aparecer dentro dela mesma");

	ItemPos lp;
	lp.zoom = 0.25;
	lp.panX = 0.75;
	lp.panY = -0.75;
	e.setLayerPosition(aid, 2, lp);
	obs_sceneitem_get_pos(it[2], &pos);
	check(near(pos.x, 1680) && near(pos.y, 945), "posição da camada (canto de baixo à direita)");

	e.setLayerOn(aid, 0, false);
	check(!obs_sceneitem_visible(it[1]) && obs_sceneitem_visible(it[2]), "desligar a camada 1 esconde só ela");
	check(e.layerInputsOf(aid) == QList<int>{cid}, "camadas ligadas de FUNDO: só CAM");

	ItemPos mp;
	mp.panY = 0.3;
	mp.zoom = 0.75;
	e.setInputPosition(aid, mp);
	e.setLayer(aid, 0, 0);
	e.setLayer(aid, 0, bid); /* volta (desligada continua desligada) */

	const QString path = QDir::tempPath() + "/rds-layers-test.rdsproj";
	check(e.saveProject(path), "salvar");
	check(e.loadProject(path), "abrir");
	a = e.input(aid);
	b = e.input(bid);
	c = e.input(cid);
	check(a && b && c, "as três voltaram");
	it = items(a);
	check(a && a->pos == mp && a->layers[0].inputId == bid && !a->layers[0].on && a->layers[2].inputId == cid &&
		      a->layers[2].pos == lp && c->pos.zoom == 0.5,
	      "salvar e abrir: posição e camadas iguais");
	check(it.size() == 3 && obs_sceneitem_get_source(it[0]) == c->scene.Get() &&
		      obs_sceneitem_get_source(it[1]) == a->source.Get() &&
		      obs_sceneitem_get_source(it[2]) == b->scene.Get() && !obs_sceneitem_visible(it[2]) &&
		      a->layers[2].behind,
	      "salvar e abrir: itens da cena na ordem certa (CAM atrás, PLACAR desligado na frente)");

	e.removeInput(cid);
	it = items(a);
	check(it.size() == 2 && a->layers[2].inputId == 0, "remover CAM tira ela da camada de FUNDO");

	/* o projeto antigo (sem posição/camadas) abre como sempre */
	e.setInputPosition(aid, ItemPos());
	e.setLayer(aid, 0, 0);
	e.saveProject(path);
	e.loadProject(path);
	a = e.input(aid);
	check(a && a->pos.isDefault() && items(a).size() == 1, "sem posição nem camadas: entrada como antes");

	e.stop();
	printf(fails ? "FALHOU\n" : "TUDO OK\n");
	return fails ? 1 : 0;
}
