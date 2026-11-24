/* ReplayDeck Studio — programa de transmissão ao vivo com replay, sobre o motor libobs. */
#include "engine.hpp"
#include "replay-host.hpp"
#include "outputs.hpp"
#include "settings-dialog.hpp"
#include "main-window.hpp"
#include "dialogs.hpp"
#include "style.hpp"

#include <functional>

#include <QApplication>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QMessageBox>
#include <QTimer>
#include <QProcess>
#include <QTabWidget>

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
	if (qEnvironmentVariableIsSet("RDS_SELFTEST_SMALL"))
		w.resize(1100, 640);
	if (qEnvironmentVariableIsSet("RDS_SELFTEST_WIDEN"))
		w.resize(900, 1000);
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
	step(1200, [&e] {
		/* reproduz: lista de entradas remontada duas vezes seguidas (abrir projeto + juntar câmeras) */
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_DOUBLE")) {
			emit e.inputsChanged();
			emit e.inputsChanged();
		}
	});
	step(5600, [&e] {
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_OFF") && e.inputs().size() > 2)
			e.setInputEnabled(e.inputs()[2]->id, false); /* tela com uma entrada desativada */
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
	step(5800, [&w] {
		/* reproduz: janela estreita (2 colunas) que depois é alargada (4 colunas) */
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_WIDEN"))
			w.resize(1900, 1000);
	});
	step(6000, [&w] {
		/* reproduz o problema das entradas saindo do painel: rola a lista um pouco */
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_SCROLL"))
			for (QScrollArea *sa : w.findChildren<QScrollArea *>())
				if (sa->widget() && sa->widget()->objectName() == "tilesHost")
					sa->verticalScrollBar()->setValue(qEnvironmentVariableIntValue("RDS_SELFTEST_SCROLL"));
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
	Engine::beforeProjectLoad = replayhost::registerTypes; /* entradas Replay A/B do projeto */
	if (!Engine::get().start(error)) {
		QMessageBox::critical(nullptr, "ReplayDeck Studio", "Não foi possível iniciar.\n\n" + error +
								     "\n\nO arquivo de log está em config\\ReplayDeckStudio\\logs.");
		Engine::get().stop();
		return 1;
	}

	Outputs::get().load();
	int ret;
	{
		MainWindow w;
		w.showMaximized();
		if (qEnvironmentVariableIsSet("RDS_SELFTEST"))
			runSelfTest(w);
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_REPLAY"))
			replayhost::selfTest(&w);
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_SETTINGS")) {
			/* teste visual: Configurações na aba RDS_SELFTEST_TAB */
			QTimer::singleShot(500, [&w] {
				auto *d = new SettingsDialog(&w, qEnvironmentVariableIntValue("RDS_SELFTEST_TAB"));
				d->show();
			});
			QTimer::singleShot(1500, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_SETTINGS")}); });
			QTimer::singleShot(2000, [&w] { QApplication::closeAllWindows(); w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_OUTPUTS")) {
			/* teste das saídas: grava 3 s, transmite 4 s para RDS_SELFTEST_RTMP, muda para 720p */
			QTimer::singleShot(300, [] {
				Engine &e = Engine::get();
				OBSDataAutoRelease c = obs_data_create();
				obs_data_set_int(c, "color", 0xFF2050C0);
				if (Input *in = e.addInput("color_source_v3", "CAM 1", c))
					e.cutTo(in->id);
				OBSDataAutoRelease st = obs_data_create();
				obs_data_set_string(st, "local_file", qgetenv("RDS_SELFTEST_TONE").constData());
				obs_data_set_bool(st, "looping", true);
				if (Input *in = e.addInput("ffmpeg_source", "Microfone", st))
					e.setInputAudioAuto(in->id, false);
				StudioSettings s = Outputs::get().settings();
				s.recFolder = qEnvironmentVariable("RDS_SELFTEST_OUTPUTS");
				s.streamServer = qEnvironmentVariable("RDS_SELFTEST_RTMP");
				s.streamKey = "teste";
				QString err;
				Outputs::get().setSettings(s, err);
				const bool ok = Outputs::get().startRecording(err);
				blog(LOG_INFO, "[teste-saidas] gravar=%d %s", ok, err.toUtf8().constData());
			});
			QTimer::singleShot(3500, [] {
				blog(LOG_INFO, "[teste-saidas] gravando=%d arquivo=%s", Outputs::get().recording(),
				     Outputs::get().recordingFile().toUtf8().constData());
				Outputs::get().stopRecording();
				QString err;
				const bool ok = Outputs::get().startStreaming(err);
				blog(LOG_INFO, "[teste-saidas] transmitir=%d %s", ok, err.toUtf8().constData());
			});
			QTimer::singleShot(8000, [] {
				blog(LOG_INFO, "[teste-saidas] transmitindo=%d quadros=%d perdidos=%d", Outputs::get().streaming(),
				     Outputs::get().streamTotalFrames(), Outputs::get().streamDroppedFrames());
				Outputs::get().stopStreaming();
			});
			QTimer::singleShot(9500, [] {
				StudioSettings s = Outputs::get().settings();
				s.width = 1280;
				s.height = 720;
				s.fpsNum = 60;
				QString err;
				const bool ok = Outputs::get().setSettings(s, err);
				obs_video_info ovi;
				obs_get_video_info(&ovi);
				blog(LOG_INFO, "[teste-saidas] 720p=%d %s -> %ux%u %u fps", ok, err.toUtf8().constData(), ovi.base_width,
				     ovi.base_height, ovi.fps_num / ovi.fps_den);
			});
			QTimer::singleShot(11000, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_MIXER")) {
			/* teste visual do mixer: um som (RDS_SELFTEST_TONE) "sempre" e uma cor; abre a aba do mixer */
			QTimer::singleShot(300, [&w] {
				Engine &e = Engine::get();
				OBSDataAutoRelease st = obs_data_create();
				obs_data_set_string(st, "local_file", qgetenv("RDS_SELFTEST_TONE").constData());
				obs_data_set_bool(st, "looping", true);
				if (Input *in = e.addInput("ffmpeg_source", "Microfone", st))
					e.setInputAudioAuto(in->id, false);
				OBSDataAutoRelease c = obs_data_create();
				obs_data_set_int(c, "color", 0xFF805020);
				e.addInput("color_source_v3", "CAM 1", c);
				if (auto *tabs = w.findChild<QTabWidget *>("rightTabs"))
					tabs->setCurrentIndex(1);
			});
			QTimer::singleShot(2500, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_MIXER")}); });
			QTimer::singleShot(3000, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_NEW")) {
			/* teste visual da janela Nova entrada (RDS_SELFTEST_NEW=tipo, RDS_SELFTEST_SHOT=arquivo) */
			QTimer::singleShot(2000, [&w] {
				auto *d = new NewInputDialog(qEnvironmentVariable("RDS_SELFTEST_NEW"), "Teste", &w);
				d->show();
			});
			QTimer::singleShot(2100, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_SHOT")}); });
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
		Outputs::get().shutdown(); /* fecha o arquivo da gravação e a transmissão */
		replayhost::shutdown(); /* para a gravação do replay antes de fechar a janela */
	}
	replayhost::finish();
	Engine::get().stop();
	return ret;
}
