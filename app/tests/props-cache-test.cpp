/* Teste: com as opções preparadas antes (cache), a janela de nova entrada mostra tudo na hora,
 * sem "Procurando…" e sem buscar nada na hora do clique. */
#include "../src/props-cache.hpp"
#include "../src/properties-view.hpp"

#include <obs.h>
#include <obs-nix-platform.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "xcb");
	QApplication app(argc, argv);
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	if (auto *x11 = app.nativeInterface<QNativeInterface::QX11Application>())
		obs_set_nix_platform_display(x11->display());
	if (!obs_startup("en-US", nullptr, nullptr))
		return 2;
	obs_add_safe_module("obs-ffmpeg");
	obs_load_all_modules();
	obs_post_load_modules();

	PropsCache::get().start({"ffmpeg_source"});
	QElapsedTimer t;
	t.start();
	while (!PropsCache::get().isReady("ffmpeg_source") && t.elapsed() < 5000)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
	if (!PropsCache::get().isReady("ffmpeg_source")) {
		printf("FALHA: o cache não ficou pronto\n");
		return 1;
	}

	const int before = PropertiesView::reloadCount();
	PropertiesView *view = PropertiesView::create("ffmpeg_source");
	const bool instant = !view->isLoading() && PropertiesView::reloadCount() == before &&
			     !view->findChildren<QComboBox *>().isEmpty();
	printf(instant ? "OK: opções na hora\n" : "FALHA: a janela ainda teve que buscar/esperar\n");

	/* depois de usar, o cache prepara de novo sozinho */
	t.restart();
	while (!PropsCache::get().isReady("ffmpeg_source") && t.elapsed() < 5000)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
	const bool refilled = PropsCache::get().isReady("ffmpeg_source");
	printf(refilled ? "OK: cache preparado de novo\n" : "FALHA: cache não se refez\n");

	delete view;
	PropsCache::get().stop();
	obs_shutdown();
	return instant && refilled ? 0 : 1;
}
