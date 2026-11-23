/* ReplayDeck Studio — programa de transmissão ao vivo com replay, sobre o motor libobs. */
#include "engine.hpp"
#include "main-window.hpp"
#include "dialogs.hpp"
#include "style.hpp"

#include <functional>

#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QTimer>
#include <QProcess>

#include <obs.h>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#include <QGuiApplication>
#endif

/* Teste automático (RDS_SELFTEST=1): cria entradas, faz transições, salva e reabre o projeto, fecha. */
static void runSelfTest(MainWindow &w)
{
	auto step = [](int ms, std::function<void()> fn) { QTimer::singleShot(ms, fn); };
	Engine &e = Engine::get();
	step(500, [&e] {
		for (int i = 0; i < 3; i++) {
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_int(s, "color", i == 0 ? 0xFF0000FF : i == 1 ? 0xFF00FF00 : 0xFFFF0000);
			obs_data_set_int(s, "width", 1920);
			obs_data_set_int(s, "height", 1080);
			e.addInput("color_source_v3", QString("COR %1").arg(i + 1), s);
		}
		blog(LOG_INFO, "[teste] %d entradas", (int)e.inputs().size());
	});
	step(1500, [&e] { e.setPreview(e.inputs()[1]->id); e.transition(TransitionKind::Cut); blog(LOG_INFO, "[teste] corte: pgm=%d pvw=%d", e.programId(), e.previewId()); });
	step(2500, [&e] { e.setPreview(e.inputs()[2]->id); e.transition(TransitionKind::Fade); });
	step(3500, [&e] { e.toggleOverlay(1, e.inputs()[0]->id); e.tbarMove(0.3f); e.tbarRelease(0.3f); blog(LOG_INFO, "[teste] fade/tbar: pgm=%d pvw=%d ov1=%d", e.programId(), e.previewId(), e.overlay(1)); });
	step(4500, [&e] { e.transition(TransitionKind::FadeToBlack); e.transition(TransitionKind::FadeToBlack); });
	step(5000, [&e] {
		const QString p = e.configDir() + "/teste.rdsproj";
		blog(LOG_INFO, "[teste] salvar=%d", e.saveProject(p));
		blog(LOG_INFO, "[teste] abrir=%d entradas=%d pgm=%d", e.loadProject(p), (int)e.inputs().size(), e.programId());
		e.setPreview(e.inputs()[0]->id);
		e.toggleOverlay(2, e.inputs()[1]->id);
	});
	step(6500, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST")}); });
	step(7000, [&w] { w.close(); });
}

int main(int argc, char *argv[])
{
	QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
#if !defined(_WIN32) && !defined(__APPLE__)
	qputenv("QT_QPA_PLATFORM", "xcb"); /* o motor desenha via X11 no Linux */
#endif
	QApplication app(argc, argv);
	QApplication::setApplicationName("ReplayDeck Studio");
	QApplication::setOrganizationName("ReplayDeck");
	QApplication::setApplicationVersion(RDS_VERSION);
	applyStyle(app);

#if !defined(_WIN32) && !defined(__APPLE__)
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	if (auto *x11 = app.nativeInterface<QNativeInterface::QX11Application>())
		obs_set_nix_platform_display(x11->display());
#endif

	QString error;
	if (!Engine::get().start(error)) {
		QMessageBox::critical(nullptr, "ReplayDeck Studio", "Não foi possível iniciar.\n\n" + error +
								     "\n\nO arquivo de log está em config\\ReplayDeckStudio\\logs.");
		Engine::get().stop();
		return 1;
	}

	int ret;
	{
		MainWindow w;
		w.showMaximized();
		if (qEnvironmentVariableIsSet("RDS_SELFTEST"))
			runSelfTest(w);
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_NEW")) {
			/* teste visual da janela Nova entrada (RDS_SELFTEST_NEW=tipo, RDS_SELFTEST_SHOT=arquivo) */
			QTimer::singleShot(500, [&w] {
				auto *d = new NewInputDialog(qEnvironmentVariable("RDS_SELFTEST_NEW"), "Teste", &w);
				d->show();
			});
			QTimer::singleShot(3000, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_SHOT")}); });
			QTimer::singleShot(3500, [&w] { QApplication::closeAllWindows(); w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_ADD")) {
			/* teste visual da janela Adicionar entrada (tipo pelo número da linha) */
			QTimer::singleShot(500, [&w] {
				auto *d = new AddInputDialog(&w);
				d->show();
				if (auto *list = d->findChild<QListWidget *>("typeList"))
					list->setCurrentRow(qEnvironmentVariableIntValue("RDS_SELFTEST_ROW"));
			});
			QTimer::singleShot(2500, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_ADD")}); });
			QTimer::singleShot(3000, [&w] { w.close(); });
		}
		ret = app.exec();
	}
	Engine::get().stop();
	return ret;
}
