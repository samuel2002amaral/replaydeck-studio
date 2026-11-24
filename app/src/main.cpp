/* ReplayDeck Studio — programa de transmissão ao vivo com replay, sobre o motor libobs. */
#include "engine.hpp"
#include "replay-host.hpp"
#include "outputs.hpp"
#include "vertical-canvas.hpp"
#include "shortcuts.hpp"
#include "virtualcam.hpp"
#include "settings-dialog.hpp"
#include "external-output.hpp"
#include "main-window.hpp"
#include "transitions-panel.hpp"
#include "functions.hpp"
#include "gt/gt-source.hpp"
#include "overlay-channels.hpp"
#include "dialogs.hpp"
#include "style.hpp"

#include <functional>

#include <QApplication>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
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
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_LOOK") && e.inputs().size() > 2) {
			/* aparência: uma entrada minimizada em barra e outra com cor */
			e.setInputCollapsed(e.inputs()[1]->id, true);
			e.setInputColor(e.inputs()[2]->id, "laranja");
			OBSDataAutoRelease c = obs_data_create();
			obs_data_set_int(c, "color", 0xFF30A0F0);
			if (Input *x = e.addInput("color_source_v3", "GOL CASA", c)) {
				e.setInputCollapsed(x->id, true);
				e.setInputColor(x->id, "roxo");
			}
			if (Input *x = e.addInput("color_source_v3", "ESCALAÇÃO FORA", c)) {
				e.setInputCollapsed(x->id, true);
				e.setInputColor(x->id, "roxo");
			}
			if (Input *x = e.addInput("color_source_v3", "PLACAR", c))
				e.setInputColor(x->id, "azul");
		}
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

	VerticalCanvas::registerSource();
	Outputs::get().load();
	Shortcuts::get().load();
	Shortcuts::get().install();
	int ret;
	{
		MainWindow w;
		w.showMaximized();
		if (qEnvironmentVariableIsSet("RDS_SELFTEST"))
			runSelfTest(w);
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_REPLAY"))
			replayhost::selfTest(&w);
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_TRANS")) {
			/* transições: stinger da aba Transições leva a Prévia (azul) ao Programa (vermelho → azul);
			 * a função Stinger1 e o projeto salvo/reaberto mantêm a lista */
			QTimer::singleShot(800, [] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSDataAutoRelease r = obs_data_create();
				obs_data_set_int(r, "color", 0xFF2020C0);
				Input *red = e.addInput("color_source_v3", "VERMELHO", r);
				OBSDataAutoRelease b = obs_data_create();
				obs_data_set_int(b, "color", 0xFFC05020);
				Input *blue = e.addInput("color_source_v3", "AZUL", b);
				e.cutTo(red->id);
				e.setPreview(blue->id);
				const QString file = qEnvironmentVariable("RDS_SELFTEST_TRANS");
				OBSDataAutoRelease st = obs_data_create();
				obs_data_set_string(st, "path", file.toUtf8().constData());
				obs_data_set_int(st, "tp_type", 0);
				obs_data_set_int(st, "transition_point", videoDurationMs(file) / 2);
				obs_data_set_bool(st, "preload", true);
				StudioTransition *t = e.addTransition("obs_stinger_transition", "Stinger 1", st);
				OBSDataAutoRelease sl = obs_data_create();
				e.addTransition("slide_transition", "Deslizar", sl, 700);
				blog(LOG_INFO, "[trans] criadas=%d stinger=%d video=%dms", (int)e.userTransitions().size(),
				     t && t->isStinger(), videoDurationMs(file));
			});
			QTimer::singleShot(2500, [] {
				QString err;
				const bool ok = runFunction("Stinger1", QString(), QString(), &err);
				blog(LOG_INFO, "[trans] Stinger1 ok=%d %s fixa=%d", ok, err.toUtf8().constData(),
				     obs_transition_fixed(Engine::get().nthTransition(1, true)->source));
			});
			QTimer::singleShot(5000, [] {
				Engine &e = Engine::get();
				Input *p = e.input(e.programId());
				blog(LOG_INFO, "[trans] programa=%s", p ? p->name.toUtf8().constData() : "-");
				const QString proj = QDir::temp().filePath("rds-trans-teste.rdsproj");
				e.saveProject(proj);
				e.loadProject(proj);
				QStringList names;
				for (StudioTransition *t : e.userTransitions())
					names << QString("%1(%2,%3ms)").arg(t->name).arg(obs_source_get_id(t->source)).arg(t->durationMs);
				blog(LOG_INFO, "[trans] projeto reaberto: %s", names.join(" ").toUtf8().constData());
			});
			QTimer::singleShot(6000, [&w] {
				if (auto *tabs = w.findChild<QTabWidget *>("rightTabs"))
					tabs->setCurrentIndex(3);
			});
			QTimer::singleShot(qMax(7500, qEnvironmentVariableIntValue("RDS_SELFTEST_HOLD") * 1000), [&w] {
				w.grab().save(QDir::temp().filePath("rds-trans.png"));
				w.close();
			});
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_GT")) {
			/* título GT: importar (vira entrada), overlay 1 liga, desliga com a saída animada
			 * (fica na tela até a animação acabar), e volta ao abrir o projeto */
			QTimer::singleShot(800, [&w] {
				Engine &e = Engine::get();
				OBSSourceAutoRelease t = gtLibraryAdd(qEnvironmentVariable("RDS_SELFTEST_GT"));
				Input *in = nullptr;
				for (Input *x : e.inputs())
					if (x->source == t)
						in = x;
				blog(LOG_INFO, "[gt] entrada=%s titulo=%d paginas=%d saida=%dms", in ? in->name.toUtf8().constData() : "-",
				     !gtTitleName(t).isEmpty(), (int)gtPages(t).size(), gtOutLengthMs(t));
				overlays().assignOn(0, obs_source_get_name(t));
				blog(LOG_INFO, "[gt] overlay1=%d canal=%d estado=%d", e.overlay(1) == (in ? in->id : -1),
				     overlays().channelOf(obs_source_get_name(t)), gtState(t));
			});
			QTimer::singleShot(2000, [] {
				Engine &e = Engine::get();
				Input *in = e.input(e.overlay(1));
				overlays().setOn(0, false);
				OBSSourceAutoRelease o = obs_get_output_source(1);
				blog(LOG_INFO, "[gt] desligou: overlay1=%d ainda_na_tela=%d estado=%d", e.overlay(1), o != nullptr,
				     in ? gtState(in->source) : -1);
			});
			QTimer::singleShot(4500, [] {
				Engine &e = Engine::get();
				OBSSourceAutoRelease o = obs_get_output_source(1);
				blog(LOG_INFO, "[gt] depois da saida: na_tela=%d", o != nullptr);
				const QString proj = QDir::temp().filePath("rds-gt-teste.rdsproj");
				e.saveProject(proj);
				e.loadProject(proj);
				std::vector<obs_source_t *> lib = gtLibraryList();
				int ov = -1;
				if (!lib.empty()) {
					OBSDataAutoRelease st = obs_source_get_settings(lib[0]);
					ov = (int)obs_data_get_int(st, "overlay");
				}
				blog(LOG_INFO, "[gt] projeto reaberto: titulos=%d nome=%s overlay=%d", (int)lib.size(),
				     lib.empty() ? "-" : gtDisplayName(lib[0]).toUtf8().constData(), ov);
				for (auto *s : lib)
					obs_source_release(s);
			});
			QTimer::singleShot(5500, [&w] {
				if (auto *tabs = w.findChild<QTabWidget *>("rightTabs"))
					tabs->setCurrentIndex(2);
			});
			QTimer::singleShot(7000, [&w] {
				w.grab().save(QDir::temp().filePath("rds-gt.png"));
				w.close();
			});
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_VCAM")) {
			/* câmera virtual: ligar com uma entrada, trocar, desligar (sem travar nem cair) */
			QTimer::singleShot(800, [&w] {
				Engine &e = Engine::get();
				OBSDataAutoRelease c = obs_data_create();
				obs_data_set_int(c, "color", 0xFF2050C0);
				Input *in = e.addInput("color_source_v3", "CAM 1", c);
				QString err;
				blog(LOG_INFO, "[vcam] disponível=%d", VirtualCam::get().available());
				const bool ok = VirtualCam::get().start(in ? in->id : 0, &w, err);
				blog(LOG_INFO, "[vcam] entrada: ok=%d ativa=%d %s", ok, VirtualCam::get().active(), err.toUtf8().constData());
				err.clear();
				const bool ok2 = VirtualCam::get().start(0, &w, err);
				blog(LOG_INFO, "[vcam] programa: ok=%d %s", ok2, err.toUtf8().constData());
				VirtualCam::get().stop();
				blog(LOG_INFO, "[vcam] desligada, ativa=%d", VirtualCam::get().active());
			});
			QTimer::singleShot(2500, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_VIEW")) {
			/* a "visão" de uma entrada só (a mesma da câmera virtual), gravada num arquivo:
			 * Programa = vermelho, entrada compartilhada = azul; o arquivo tem que sair azul */
			static obs_view_t *view = nullptr;
			static obs_output_t *out = nullptr;
			QTimer::singleShot(800, [] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSDataAutoRelease r = obs_data_create();
				obs_data_set_int(r, "color", 0xFF0000FF);
				Input *red = e.addInput("color_source_v3", "VERMELHO", r);
				OBSDataAutoRelease b = obs_data_create();
				obs_data_set_int(b, "color", 0xFFFF0000);
				Input *blue = e.addInput("color_source_v3", "AZUL", b);
				e.cutTo(red->id);
				view = obs_view_create();
				obs_view_set_source(view, 0, blue->scene);
				obs_video_info ovi;
				obs_get_video_info(&ovi);
				video_t *v = obs_view_add2(view, &ovi);
				OBSDataAutoRelease es = obs_data_create();
				obs_encoder_t *enc = obs_video_encoder_create("obs_x264", "visao", es, nullptr);
				obs_encoder_set_video(enc, v);
				OBSDataAutoRelease os = obs_data_create();
				obs_data_set_string(os, "path", qgetenv("RDS_SELFTEST_VIEW").constData());
				out = obs_output_create("ffmpeg_muxer", "visao", os, nullptr);
				obs_output_set_video_encoder(out, enc); /* o codificador já lê da visão (fica vivo até o fim) */
				OBSDataAutoRelease as = obs_data_create();
				obs_encoder_t *aenc = obs_audio_encoder_create("ffmpeg_aac", "visao-audio", as, 0, nullptr);
				obs_encoder_set_audio(aenc, obs_get_audio());
				obs_output_set_audio_encoder(out, aenc, 0);
				blog(LOG_INFO, "[visao] gravando=%d", obs_output_start(out));
			});
			QTimer::singleShot(3000, [] {
				blog(LOG_INFO, "[visao] 1 parar saída");
				if (obs_output_active(out))
					obs_output_force_stop(out);
				blog(LOG_INFO, "[visao] 2 soltar saída");
				obs_output_release(out);
				blog(LOG_INFO, "[visao] 3 tirar visão");
				obs_view_remove(view);
				blog(LOG_INFO, "[visao] 4 limpar fonte");
				obs_view_set_source(view, 0, nullptr);
				blog(LOG_INFO, "[visao] 5 destruir visão");
				obs_view_destroy(view);
				blog(LOG_INFO, "[visao] parada");
			});
			QTimer::singleShot(3500, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_EXT")) {
			/* Externo numa janela: a interface tem que continuar respondendo */
			auto *beat = new QTimer(&w);
			QObject::connect(beat, &QTimer::timeout, [] { blog(LOG_INFO, "[ext] interface viva"); });
			beat->start(500);
			QTimer::singleShot(1000, [] {
				Engine &e = Engine::get();
				OBSDataAutoRelease c = obs_data_create();
				obs_data_set_int(c, "color", 0xFF20C0E0);
				if (Input *in = e.addInput("color_source_v3", "AMARELO", c))
					e.cutTo(in->id);
				blog(LOG_INFO, "[ext] abrindo janela");
				auto *x = new ExternalOutput(qEnvironmentVariableIsSet("RDS_SELFTEST_EXT_VERTICAL")
								     ? ExternalOutput::What::Vertical
								     : ExternalOutput::What::Program,
							     0, nullptr);
				/* recria a janela nativa depois de aberta (o que o Windows faz ao mudar bordas):
				 * o vídeo tem que voltar sozinho */
				QTimer::singleShot(800, x, [x] {
					x->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
					x->show();
					blog(LOG_INFO, "[ext] janela recriada");
				});
				blog(LOG_INFO, "[ext] janela aberta");
			});
			QTimer::singleShot(3000, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_EXT")}); });
			QTimer::singleShot(5000, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_UI")) {
			/* teste de mouse (xdotool): 4 entradas A-D; registra a ordem e quem está minimizada */
			QTimer::singleShot(300, [] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				const uint32_t colors[] = {0xFF2020E0, 0xFF20C020, 0xFFE02020, 0xFF20C0E0};
				for (int i = 0; i < 4; i++) {
					OBSDataAutoRelease c = obs_data_create();
					obs_data_set_int(c, "color", colors[i]);
					e.addInput("color_source_v3", QString(QChar('A' + i)), c);
				}
				auto report = [] {
					QStringList o;
					for (Input *in : Engine::get().inputs())
						o << in->name + (in->collapsed ? "(barra)" : "");
					blog(LOG_INFO, "[ui] ordem: %s", o.join(",").toUtf8().constData());
				};
				QObject::connect(&e, &Engine::inputsChanged, report);
				QObject::connect(&e, &Engine::inputChanged, report);
				report();
			});
			QTimer::singleShot(qEnvironmentVariableIntValue("RDS_SELFTEST_UI") * 1000, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_SETTINGS")) {
			/* teste visual: Configurações na aba RDS_SELFTEST_TAB */
			QTimer::singleShot(500, [&w] {
				auto *d = new SettingsDialog(&w, qEnvironmentVariableIntValue("RDS_SELFTEST_TAB"));
				d->show();
			});
			const int hold = qMax(2, qEnvironmentVariableIntValue("RDS_SELFTEST_HOLD")) * 1000;
			QTimer::singleShot(1500, [] { QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST_SETTINGS")}); });
			QTimer::singleShot(hold, [&w] { QApplication::closeAllWindows(); w.close(); });
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
				s.destinations.clear();
				StreamDestination hd;
				hd.name = "Horizontal";
				hd.server = qEnvironmentVariable("RDS_SELFTEST_RTMP");
				hd.key = "teste";
				s.destinations << hd;
				if (qEnvironmentVariableIsSet("RDS_SELFTEST_RTMP2")) {
					StreamDestination vd;
					vd.name = "Vertical";
					vd.server = qEnvironmentVariable("RDS_SELFTEST_RTMP2");
					vd.key = "teste";
					vd.vertical = true;
					s.destinations << vd;
				}
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
				blog(LOG_INFO, "[teste-saidas] destinos no ar=%d", Outputs::get().liveCount());
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
		VirtualCam::get().shutdown();
		Outputs::get().shutdown(); /* fecha o arquivo da gravação e a transmissão */
		VerticalCanvas::get().shutdown();
		replayhost::shutdown(); /* para a gravação do replay antes de fechar a janela */
	}
	replayhost::finish();
	Engine::get().stop();
	return ret;
}
