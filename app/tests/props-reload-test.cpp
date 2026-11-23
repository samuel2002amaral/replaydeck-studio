/* Teste: mudar uma opção que pede para "redesenhar" o formulário NÃO pode buscar as opções
 * de novo na fonte (na câmera isso refaz a busca de todos os dispositivos e trava). */
#include "../src/properties-view.hpp"

#include <obs.h>
#include <obs-nix-platform.h>
#include <QGuiApplication>

#include <QApplication>
#include <QCheckBox>
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

	PropertiesView view(nullptr, "ffmpeg_source");
	/* a janela não pode esperar a busca (na câmera ela leva segundos): ela acontece em segundo plano */
	const bool waitedOnOpen = PropertiesView::reloadCount() != 0 || !view.isLoading();
	QElapsedTimer t;
	t.start();
	while (view.isLoading() && t.elapsed() < 5000)
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
	const int afterOpen = PropertiesView::reloadCount();
	if (waitedOnOpen) {
		printf("FALHA: a busca das opções travou a abertura da janela\n");
		return 1;
	}
	/* "Arquivo local" tem callback que pede para redesenhar; o formulário é refeito, então procura de novo */
	auto findLocal = [&view]() -> QCheckBox * {
		for (QCheckBox *cb : view.findChildren<QCheckBox *>())
			if (cb->text().contains("Local", Qt::CaseInsensitive))
				return cb;
		return nullptr;
	};
	for (int i = 0; i < 2; i++) {
		QCheckBox *local = findLocal();
		if (!local) {
			printf("FALHA: checkbox de arquivo local não encontrada\n");
			return 1;
		}
		local->toggle();
		for (int k = 0; k < 5; k++)
			QCoreApplication::processEvents();
	}
	const int afterClicks = PropertiesView::reloadCount();
	printf("buscas ao abrir: %d, depois de 2 cliques: %d\n", afterOpen, afterClicks);
	const bool ok = afterOpen == 1 && afterClicks == 1;
	printf(ok ? "OK\n" : "FALHA: formulário buscou as opções de novo\n");
	obs_shutdown();
	return ok ? 0 : 1;
}
