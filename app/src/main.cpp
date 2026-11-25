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
#include "perf.hpp"
#include "vmix-keys.hpp"
#include "playlist.hpp"
#include "ptz.hpp"
#include "filters-window.hpp"
#include "plugins.hpp"
#include "multicorder.hpp"
#include "scripts.hpp"
#include "vmix-api.hpp"
#include "transitions-panel.hpp"
#include "functions.hpp"
#include "gt/gt-source.hpp"
#include "gt/gt-doc.hpp"
#include "overlay-channels.hpp"
#include "dialogs.hpp"
#include "crash.hpp"
#include "pack.hpp"
#include "mac-permissions.hpp"
#include "layout-view.hpp"
#include "style.hpp"

#include <functional>

#include <QApplication>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QMessageBox>
#include <QTimer>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QMouseEvent>
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
	step(6500, [] {
#ifdef __APPLE__
		QProcess::execute("screencapture", {"-x", qEnvironmentVariable("RDS_SELFTEST")});
#else
		QProcess::execute("import", {"-window", "root", qEnvironmentVariable("RDS_SELFTEST")});
#endif
	});
	step(7000, [&w] { w.close(); });
}

#ifdef __APPLE__
void rdsMacPrepareApplication(); /* mac-app.mm */
#endif

int main(int argc, char *argv[])
{
#ifdef __APPLE__
	rdsMacPrepareApplication(); /* antes do Qt: o navegador (CEF) precisa disso */
#endif
	QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
#if !defined(_WIN32) && !defined(__APPLE__)
	qputenv("QT_QPA_PLATFORM", "xcb"); /* o motor desenha via X11 no Linux */
#endif
	QApplication app(argc, argv);
	QApplication::setApplicationName("ReplayDeck Studio");
	QApplication::setOrganizationName("ReplayDeck");
	QApplication::setApplicationVersion(RDS_VERSION);
#ifndef _WIN32
	QApplication::setWindowIcon(QIcon(":/res/icon-256.png")); /* no Windows: o ícone do .exe (app.rc) */
#endif
	applyStyle(app);

#if !defined(_WIN32) && !defined(__APPLE__)
	obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
	if (auto *x11 = app.nativeInterface<QNativeInterface::QX11Application>())
		obs_set_nix_platform_display(x11->display());
#endif

	perf::tuneProcess();
	Engine::loadExtraModules = plugins::loadEnabled;
	QString error;
	Engine::onConfigDir = pack::applyPending; /* pacote importado de outro computador */
	Engine::beforeProjectLoad = replayhost::registerTypes; /* entradas Replay A/B do projeto */
	if (!Engine::get().start(error)) {
		QMessageBox::critical(nullptr, "ReplayDeck Studio", "Não foi possível iniciar.\n\n" + error +
								     "\n\nO arquivo de log está em config\\ReplayDeckStudio\\logs.");
		Engine::get().stop();
		return 1;
	}

	crash::install(Engine::get().configDir()); /* se o programa cair: guarda onde caiu */
	perf::logState();
	Scripts::get().load();
	Playlist::get().load();
	Ptz::get().load();
	VerticalCanvas::registerSource();
	Outputs::get().load();
	Shortcuts::get().load();
	Shortcuts::get().install();
	int ret;
	{
		MainWindow w;
		w.showMaximized();
		{
			bool selftest = false;
			for (const QString &k : QProcessEnvironment::systemEnvironment().keys())
				selftest = selftest || k.startsWith("RDS_SELFTEST");
			if (!selftest) {
				crash::showPrevious(&w);
				macperm::showAtStartup(&w); /* Mac: câmera, microfone e tela de uma vez */
			}
		}
		perf::startAutosave(&w);
		perf::startHangWatch();
		QTimer::singleShot(3000, [] { plugins::startupFinished(); }); /* abriu e ficou de pé */
		{
			const StudioSettings &cs = Outputs::get().settings();
			VmixApi::get().apply(cs.apiEnabled, cs.apiHttpPort, cs.apiTcpPort);
		}
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
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_PACOTE")) {
			/* pacote: exporta (com imagem e GT), "outro computador" sem os arquivos, importa e confere */
			QTimer::singleShot(800, [&w] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				const QString srcDir = QDir::temp().filePath("pacote-origem");
				QDir().mkpath(srcDir);
				QImage img(320, 180, QImage::Format_ARGB32);
				img.fill(QColor(200, 30, 30));
				img.save(srcDir + "/logo.png");
				OBSDataAutoRelease s = obs_data_create();
				obs_data_set_string(s, "file", (srcDir + "/logo.png").toUtf8().constData());
				e.addInput("image_source", "LOGO", s);
				const QString gt = qEnvironmentVariable("RDS_SELFTEST_PACOTE");
				if (QFile::exists(gt))
					OBSSourceAutoRelease g = gtLibraryAdd(gt);
				const QString out = QDir::temp().filePath("teste.rdspacote");
				QString err;
				const bool wrote = pack::writePack(out, true, nullptr, &err);
				blog(LOG_INFO, "[pacote-teste] exportou=%d tamanho=%lld %s", wrote, (long long)QFileInfo(out).size(),
				     err.toUtf8().constData());
				/* outro computador: os arquivos originais não existem */
				QDir(srcDir).removeRecursively();
				const QString gtAway = gt + ".longe";
				QFile::rename(gt, gtAway);
				const bool read = pack::readPack(out, nullptr, &err);
				pack::applyPending(e.configDir());
				const bool loaded = e.loadProject(e.defaultProjectPath());
				QString logoPath;
				bool gtOk = false;
				for (Input *in : e.inputs()) {
					OBSDataAutoRelease st = obs_source_get_settings(in->source);
					if (in->name == "LOGO")
						logoPath = QString::fromUtf8(obs_data_get_string(st, "file"));
					if (gtIsSource(in->source))
						gtOk = gtTitleOf(in->source) != nullptr;
				}
				blog(LOG_INFO, "[pacote-teste] importou=%d abriu=%d entradas=%d logo=%s existe=%d gt=%d", read, loaded,
				     (int)e.inputs().size(), logoPath.toUtf8().constData(), QFile::exists(logoPath), gtOk);
				QFile::rename(gtAway, gt);
				QTimer::singleShot(500, &w, [&w] { w.close(); });
			});
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_MAC")) {
			/* Mac: página web (navegador) e as opções da captura de tela (AppKit, thread principal) */
			QTimer::singleShot(800, [] {
				Engine &e = Engine::get();
				const QString html = QDir::temp().filePath("rds-teste.html");
				QFile f(html);
				if (f.open(QIODevice::WriteOnly))
					f.write("<html><body style='margin:0;background:#c01020'><h1 style='color:white;font:80px sans-serif'>"
						"NAVEGADOR OK</h1></body></html>");
				f.close();
				OBSDataAutoRelease s = obs_data_create();
				obs_data_set_bool(s, "is_local_file", true);
				obs_data_set_string(s, "local_file", html.toUtf8().constData());
				obs_data_set_int(s, "width", 1280);
				obs_data_set_int(s, "height", 720);
				const bool doScreen = !qEnvironmentVariableIsSet("RDS_SELFTEST_SEM_TELA");
				const bool doBrowser = !qEnvironmentVariableIsSet("RDS_SELFTEST_SEM_NAVEGADOR");
				if (doScreen) {
					blog(LOG_INFO, "[teste-mac] captura de tela: montando as opções…");
					/* como a janela de adicionar faz no Mac: uma fonte temporária para as opções */
					OBSDataAutoRelease def = obs_get_source_defaults("screen_capture");
					OBSSourceAutoRelease tmp = obs_source_create_private("screen_capture", "opções (temporária)", def);
					obs_properties_t *p = tmp ? obs_source_properties(tmp) : nullptr;
					int n = 0;
					for (obs_property_t *q = p ? obs_properties_first(p) : nullptr; q; obs_property_next(&q))
						n++;
					blog(LOG_INFO, "[teste-mac] captura de tela: opcoes=%d (thread principal)", n);
					obs_properties_destroy(p);
				}
				if (doBrowser) {
					Input *in = e.addInput("browser_source", "PAGINA", s);
					blog(LOG_INFO, "[teste-mac] navegador: entrada=%d", in != nullptr);
					if (in)
						e.cutTo(in->id);
					blog(LOG_INFO, "[teste-mac] navegador: no ar");
				}
			});
			QTimer::singleShot(6000, [] {
				Input *in = Engine::get().inputByName("PAGINA");
				blog(LOG_INFO, "[teste-mac] navegador depois de 5 s: %ux%u", in ? obs_source_get_width(in->source) : 0,
				     in ? obs_source_get_height(in->source) : 0);
				if (!qEnvironmentVariable("RDS_SELFTEST_MAC").isEmpty() && qEnvironmentVariable("RDS_SELFTEST_MAC") != "1") {
#ifdef __APPLE__
					QProcess::execute("screencapture", {"-x", qEnvironmentVariable("RDS_SELFTEST_MAC")});
#endif
				}
			});
			QTimer::singleShot(6500, [&w] {
				macperm::showDialog(&w); /* janela de permissões abre sem cair */
				blog(LOG_INFO, "[teste-mac] janela de permissões aberta");
			});
			QTimer::singleShot(7500, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_GTNEW")) {
			/* título GT novo em branco: acrescenta texto, retângulo, camada e cópia pelo editor */
			QTimer::singleShot(800, [&w] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSSourceAutoRelease src = gtLibraryNewBlank("PLACAR NOVO");
				blog(LOG_INFO, "[gtnovo] criado=%d", src.Get() != nullptr);
				if (src)
					gtOpenEditor(src, &w);
			});
			auto clickBtn = [&w](const QString &text) {
				for (QDialog *d : w.findChildren<QDialog *>())
					for (QPushButton *b : d->findChildren<QPushButton *>())
						if (b->text() == text && b->isVisible()) {
							b->click();
							return true;
						}
				return false;
			};
			QTimer::singleShot(1800, [clickBtn] {
				const bool t = clickBtn("＋ Texto");
				const bool r = clickBtn("＋ Retângulo");
				const bool c = clickBtn("＋ Círculo");
				const bool d = clickBtn("Duplicar"); /* cópia do círculo, um pouco deslocada */
				blog(LOG_INFO, "[gtnovo] botoes texto=%d retangulo=%d circulo=%d duplicar=%d", t, r, c, d);
			});
			QTimer::singleShot(2600, [] {
				QString err;
				const bool ok = runFunction("SetText", "PLACAR NOVO", "GOL DO TIME", &err) ||
						true; /* SetText sem SelectedName: só confere que o título existe */
				QMap<QString, QString> p;
				p.insert("SelectedName", "Texto1.Text");
				p.insert("Value", "GOL DO TIME");
				p.insert("Input", "PLACAR NOVO");
				const bool ok2 = runFunctionParams("SetText", p, &err);
				Input *in = Engine::get().inputByName("PLACAR NOVO");
				QStringList names;
				if (in)
					if (auto t = gtTitleOf(in->source))
						for (auto &L : t->layers)
							for (auto &el : L.elements)
								names << el.name;
				blog(LOG_INFO, "[gtnovo] elementos=%s SetText(Texto1.Text)=%d %s", names.join(",").toUtf8().constData(),
				     ok2, err.toUtf8().constData());
				(void)ok;
				if (in)
					Engine::get().cutTo(in->id);
			});
			QTimer::singleShot(3600, [] {
				QProcess::execute("import", {"-window", "root", QDir::temp().filePath("rds-gtnovo.png")});
				Engine &e = Engine::get();
				const QString proj = QDir::temp().filePath("rds-gtnovo.rdsproj");
				e.saveProject(proj);
				e.loadProject(proj);
				Input *in = e.inputByName("PLACAR NOVO");
				QString txt;
				int n = 0;
				if (in)
					if (auto t = gtTitleOf(in->source)) {
						for (auto &L : t->layers)
							n += (int)L.elements.size();
						txt = gtValues(in->source).value("Texto1.Text");
					}
				blog(LOG_INFO, "[gtnovo] reaberto: elementos=%d Texto1=%s tamanho=%ux%u", n, txt.toUtf8().constData(),
				     in ? obs_source_get_width(in->source) : 0, in ? obs_source_get_height(in->source) : 0);
			});
			QTimer::singleShot(4500, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_LAYERS")) {
			/* abas Posição e Camadas: CÂMERA com o PLACAR como camada 1; PLACAR menor no alto */
			QTimer::singleShot(700, [&w] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSDataAutoRelease g = obs_data_create();
				obs_data_set_int(g, "color", 0xFF2E7D32);
				Input *cam = e.addInput("color_source_v3", "CAMERA", g);
				OBSDataAutoRelease b = obs_data_create();
				obs_data_set_int(b, "color", 0xFFB04010);
				Input *plc = e.addInput("color_source_v3", "PLACAR", b);
				QString err;
				bool ok = runFunction("SetZoom", "PLACAR", "0.4", &err) &&
					  runFunction("SetPanY", "PLACAR", "0.75", &err) &&
					  runFunction("SetCrop", "PLACAR", "0,0.3,1,0.7", &err) &&
					  runFunction("SetLayer", "CAMERA", "1,PLACAR", &err);
				const bool loopRefused = !runFunction("SetLayer", "PLACAR", "1,CAMERA", &err);
				blog(LOG_INFO, "[camadas] funcoes=%d recusou_loop=%d camada1=%d pos_zoom=%.2f (%s)", ok, loopRefused,
				     cam->layers[0].inputId == plc->id, plc->pos.zoom, err.toUtf8().constData());
				/* camada 2 vazia (imagem sem arquivo = como o replay parado): só a moldura amarela */
				Input *vazio = e.addInput("image_source", "REPLAY PARADO");
				if (vazio) {
					e.setLayer(cam->id, 1, vazio->id);
					ItemPos vp;
					vp.zoom = 0.45;
					vp.panX = -0.45;
					vp.panY = -0.3;
					e.setLayerPosition(cam->id, 1, vp);
					e.setLayerBehind(cam->id, 1, true);
				}
				e.cutTo(cam->id);
				auto *iw = new InputWindow(cam->id, &w);
				iw->setObjectName("rdsInputWindow");
				iw->show();
			});
			QTimer::singleShot(2600, [&w] {
				auto *iw = w.findChild<InputWindow *>("rdsInputWindow");
				if (auto *tabs = iw ? iw->findChild<QTabWidget *>() : nullptr)
					tabs->setCurrentIndex(2);
				/* mouse: clica no PLACAR (camada 1), arrasta 200 px para a direita, puxa o canto ↘ */
				auto *lv = iw ? iw->findChild<LayoutView *>() : nullptr;
				Engine &e = Engine::get();
				Input *cam = e.inputByName("CAMERA");
				if (!lv || !cam)
					return;
				const float W = (float)e.baseWidth(), H = (float)e.baseHeight();
				auto toWidget = [lv, W, H](double x, double y) {
					const float sc = lv->imageScale(W, H);
					return QPointF(x * sc + (lv->width() - W * sc) / 2.0, y * sc + (lv->height() - H * sc) / 2.0);
				};
				auto send = [lv](QEvent::Type t, QPointF p, Qt::MouseButtons bs, Qt::KeyboardModifiers mods = {}) {
					QMouseEvent ev(t, p, lv->mapToGlobal(p), t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
						       bs, mods);
					QApplication::sendEvent(lv, &ev);
				};
				for (const LayoutView::Box &b : lv->boxes())
					if (b.which == 0) {
						/* ponto do PLACAR fora da camada 2 (que fica embaixo à esquerda) */
						const QPointF c = toWidget(b.f.visCx + 500, b.f.visCy - 300);
						const QPointF to = toWidget(b.f.visCx + 700, b.f.visCy - 300);
						send(QEvent::MouseButtonPress, c, Qt::LeftButton);
						send(QEvent::MouseMove, (c + to) / 2, Qt::LeftButton);
						send(QEvent::MouseMove, to, Qt::LeftButton);
						send(QEvent::MouseButtonRelease, to, Qt::NoButton);
					}
				const double panAfterMove = cam->layers[0].pos.panX;
				for (const LayoutView::Box &b : lv->boxes())
					if (b.which == 0) {
						const QPointF br = toWidget(b.f.visCx + b.f.visW / 2, b.f.visCy + b.f.visH / 2);
						/* canto ↘ puxado para dentro: fica com 3/4 do tamanho */
						const QPointF to = toWidget(b.f.visCx + b.f.visW / 4, b.f.visCy + b.f.visH / 4);
						send(QEvent::MouseButtonPress, br, Qt::LeftButton);
						send(QEvent::MouseMove, to, Qt::LeftButton);
						send(QEvent::MouseButtonRelease, to, Qt::NoButton);
					}
				blog(LOG_INFO,
				     "[camadas] mouse: escolhida=%d panX_depois_de_mover=%.3f zoom_depois_do_canto=%.3f panX=%.3f panY=%.3f",
				     lv->selected(), panAfterMove, cam->layers[0].pos.zoom, cam->layers[0].pos.panX,
				     cam->layers[0].pos.panY);
			});
			QTimer::singleShot(3600, [] {
				QProcess::execute("import", {"-window", "root", QDir::temp().filePath("rds-camadas.png")});
			});
			QTimer::singleShot(3800, [&w] {
				auto *iw = w.findChild<InputWindow *>("rdsInputWindow");
				if (auto *tabs = iw ? iw->findChild<QTabWidget *>() : nullptr)
					tabs->setCurrentIndex(1);
				Engine &e = Engine::get();
				if (Input *plc = e.inputByName("PLACAR"); plc && iw) {
					iw->close();
					auto *pw = new InputWindow(plc->id, &w);
					pw->setObjectName("rdsInputWindow2");
					pw->show();
					QTimer::singleShot(600, pw, [pw] {
						if (auto *tabs = pw->findChild<QTabWidget *>())
							tabs->setCurrentIndex(1);
					});
				}
			});
			QTimer::singleShot(5600, [] {
				QProcess::execute("import", {"-window", "root", QDir::temp().filePath("rds-posicao.png")});
				Engine &e = Engine::get();
				const QString proj = QDir::temp().filePath("rds-camadas-teste.rdsproj");
				e.saveProject(proj);
				e.loadProject(proj);
				Input *cam = e.inputByName("CAMERA");
				Input *plc = e.inputByName("PLACAR");
				blog(LOG_INFO, "[camadas] reaberto: camada1=%d zoom=%.2f panY=%.2f corteY1=%.2f",
				     cam && plc && cam->layers[0].inputId == plc->id, plc ? plc->pos.zoom : -1,
				     plc ? plc->pos.panY : -1, plc ? plc->pos.cropY1 : -1);
			});
			QTimer::singleShot(6500, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_APPVMIX")) {
			/* app externo feito para o vMix: um título PLACAR e as chaves importadas da config do app */
			QTimer::singleShot(600, [] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSDataAutoRelease b = obs_data_create();
				obs_data_set_int(b, "color", 0xFF2E7D32);
				Input *cam = e.addInput("color_source_v3", "CAMERA", b);
				e.cutTo(cam->id);
				OBSSourceAutoRelease gt = gtLibraryAdd(qEnvironmentVariable("RDS_SELFTEST_APPVMIX"));
				e.renameInput(e.inputs().last()->id, "PLACAR");
				for (const QString &extra : qEnvironmentVariable("RDS_SELFTEST_APPVMIX_EXTRA").split(',', Qt::SkipEmptyParts)) {
					OBSSourceAutoRelease g2 = gtLibraryAdd(qEnvironmentVariable("RDS_SELFTEST_APPVMIX"));
					e.renameInput(e.inputs().last()->id, extra.trimmed());
				}
				QStringList done, missing;
				QString err;
				applyVmixKeysFromFile(qEnvironmentVariable("RDS_SELFTEST_APPVMIX_CFG"), &done, &missing, &err);
				blog(LOG_INFO, "[appvmix] chaves: %s %s", done.join("; ").toUtf8().constData(), err.toUtf8().constData());
				blog(LOG_INFO, "[appvmix] pronto");
			});
			QTimer::singleShot(qMax(8000, qEnvironmentVariableIntValue("RDS_SELFTEST_HOLD") * 1000), [&w] {
				w.grab().save(QDir::temp().filePath("rds-appvmix.png"));
				w.close();
			});
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_PLAYLIST")) {
			/* playlist: vídeo até o fim -> imagem 1 s -> fim */
			QTimer::singleShot(800, [] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSDataAutoRelease v = obs_data_create();
				obs_data_set_string(v, "local_file", qEnvironmentVariable("RDS_SELFTEST_PLAYLIST").toUtf8().constData());
				Input *vid = e.addInput("ffmpeg_source", "VINHETA", v);
				OBSDataAutoRelease b = obs_data_create();
				obs_data_set_int(b, "color", 0xFFC05020);
				Input *blue = e.addInput("color_source_v3", "AZUL", b);
				Playlist &pl = Playlist::get();
				pl.items = {PlaylistItem{vid->id, 0}, PlaylistItem{blue->id, 1}};
				pl.loop = false;
				pl.transitionId = 0;
				QObject::connect(&pl, &Playlist::stateChanged, [] {
					Engine &en = Engine::get();
					Input *p = en.input(en.programId());
					blog(LOG_INFO, "[lista] item=%d programa=%s", Playlist::get().currentIndex(),
					     p ? p->name.toUtf8().constData() : "-");
				});
				QString err;
				runFunction("StartPlayList", QString(), QString(), &err);
			});
			QTimer::singleShot(6000, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_FILTERS")) {
			/* filtros: um filtro de cor numa entrada, janela de filtros, e volta ao reabrir o projeto */
			QTimer::singleShot(800, [&w] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSDataAutoRelease r = obs_data_create();
				obs_data_set_int(r, "color", 0xFF2020C0);
				Input *in = e.addInput("color_source_v3", "VERMELHO", r);
				OBSDataAutoRelease fs = obs_data_create();
				obs_data_set_double(fs, "brightness", -0.5);
				OBSSourceAutoRelease f = obs_source_create("color_filter_v2", "Correção de cor", fs, nullptr);
				obs_source_filter_add(in->source, f);
				FiltersWindow::open(in->source, in->name, &w);
			});
			QTimer::singleShot(3000, [] {
				for (QWidget *x : QApplication::topLevelWidgets())
					if (x->windowTitle().startsWith("Filtros"))
						x->grab().save(QDir::temp().filePath("rds-filtros.png"));
				Engine &e = Engine::get();
				const QString proj = QDir::temp().filePath("rds-filtros-teste.rdsproj");
				e.saveProject(proj);
				e.loadProject(proj);
				int n = 0;
				if (!e.inputs().isEmpty())
					obs_source_enum_filters(e.inputs().first()->source, [](obs_source_t *, obs_source_t *, void *p) { (*(int *)p)++; }, &n);
				blog(LOG_INFO, "[filtros] depois de reabrir: %d filtro(s)", n);
			});
			QTimer::singleShot(4500, [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_API")) {
			/* API do vMix e scripts: entradas VERMELHO, AZUL e um título; o teste de fora usa curl/nc */
			QTimer::singleShot(600, [] {
				Engine &e = Engine::get();
				while (!e.inputs().isEmpty())
					e.removeInput(e.inputs().first()->id);
				OBSDataAutoRelease r = obs_data_create();
				obs_data_set_int(r, "color", 0xFF2020C0);
				Input *red = e.addInput("color_source_v3", "VERMELHO", r);
				OBSDataAutoRelease b = obs_data_create();
				obs_data_set_int(b, "color", 0xFFC05020);
				e.addInput("color_source_v3", "AZUL", b);
				OBSSourceAutoRelease gt = gtLibraryAdd(qEnvironmentVariable("RDS_SELFTEST_API"));
				e.cutTo(red->id);
				Scripts &sc = Scripts::get();
				sc.remove("teste");
				sc.add("teste");
				sc.setCode("teste", "' teste\nAPI.Function(\"PreviewInput\", Input:=\"AZUL\")\nSleep(300)\n"
						    "For i = 1 To 3\n  Cut\n  Sleep(100)\nNext\n"
						    "SetText Input=3 SelectedName=Nome.Text Value=\"GOL DO TIME\"\n");
				for (Input *x : e.inputs())
					MultiCorder::get().lastSelection.insert(x->id);
				blog(LOG_INFO, "[api] pronto");
			});
			QTimer::singleShot(qMax(8000, qEnvironmentVariableIntValue("RDS_SELFTEST_HOLD") * 1000), [&w] { w.close(); });
		}
		if (qEnvironmentVariableIsSet("RDS_SELFTEST_GTSTINGER")) {
			/* stinger GT: um título .gtzip (entrada) como stinger entre vermelho e azul */
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
				OBSSourceAutoRelease gt = gtLibraryAdd(qEnvironmentVariable("RDS_SELFTEST_GTSTINGER"));
				e.cutTo(red->id);
				e.setPreview(blue->id);
				OBSDataAutoRelease st = obs_data_create();
				obs_data_set_string(st, "titulo", gtDisplayName(gt).toUtf8().constData());
				obs_data_set_int(st, "segurar_ms", 400);
				StudioTransition *t = e.addTransition("rds_gt_stinger", "Stinger GT", st);
				blog(LOG_INFO, "[gtst] criado=%d stinger=%d", t != nullptr, t && t->isStinger());
			});
			QTimer::singleShot(2500, [] {
				QString err;
				const bool ok = runFunction("Stinger1", QString(), QString(), &err);
				blog(LOG_INFO, "[gtst] Stinger1 ok=%d %s", ok, err.toUtf8().constData());
			});
			QTimer::singleShot(5500, [&w] {
				Engine &e = Engine::get();
				Input *p = e.input(e.programId());
				blog(LOG_INFO, "[gtst] programa=%s", p ? p->name.toUtf8().constData() : "-");
				if (auto *tabs = w.findChild<QTabWidget *>("rightTabs"))
					tabs->setCurrentIndex(3);
			});
			QTimer::singleShot(qMax(7000, qEnvironmentVariableIntValue("RDS_SELFTEST_HOLD") * 1000), [&w] {
				w.grab().save(QDir::temp().filePath("rds-gtst.png"));
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
		perf::stopHangWatch();
		Scripts::get().stopAll();
		Playlist::get().stop();
		VmixApi::get().shutdown();
		Shortcuts::get().shutdown();
		VirtualCam::get().shutdown();
		MultiCorder::get().shutdown();
		Outputs::get().shutdown(); /* fecha o arquivo da gravação e a transmissão */
		VerticalCanvas::get().shutdown();
		replayhost::shutdown(); /* para a gravação do replay antes de fechar a janela */
	}
	replayhost::finish();
	Engine::get().stop();
	perf::restore();
	if (pack::restartRequested()) /* importou um pacote: abre de novo já configurado */
		QProcess::startDetached(QCoreApplication::applicationFilePath(), {}, QCoreApplication::applicationDirPath());
	return ret;
}
