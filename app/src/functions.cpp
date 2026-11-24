#include "functions.hpp"
#include "engine.hpp"
#include "outputs.hpp"
#include "virtualcam.hpp"
#include "multicorder.hpp"
#include "playlist.hpp"
#include <cstring>
#include "../replay/gt/gt-source.hpp"
#include "../replay/gt/gt-doc.hpp"

#include "../replay/air-control.hpp"
#include "../replay/engine.hpp"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <functional>

const QList<FunctionDef> &functionList()
{
	static const QList<FunctionDef> list = {
		/* transições */
		{"Cut", "Transição", "Corte: a Prévia vai para o Programa", false, false},
		{"Fade", "Transição", "Fade: a Prévia vai para o Programa", false, true},
		{"QuickPlay", "Transição", "Entrada direto para o Programa, com fade", true, false},
		{"ActiveInput", "Transição", "Entrada direto para o Programa (corte)", true, false},
		{"SetFader", "Transição", "T-bar (Value 0 a 255; 255 completa)", false, true},
		{"CutDirect", "Transição", "Entrada direto para o Programa, corte seco", true, false},
		{"FadeToBlack", "Transição", "Fade para o preto (liga/desliga)", false, false},
		{"SetTransitionDuration", "Transição", "Duração das transições (Value = ms)", false, true},
		{"Stinger1", "Transição", "Stinger 1 (1º stinger da aba Transições)", false, false},
		{"Stinger2", "Transição", "Stinger 2", false, false},
		{"Stinger3", "Transição", "Stinger 3", false, false},
		{"Stinger4", "Transição", "Stinger 4", false, false},
		{"Transition1", "Transição", "Transição 1 (1ª da aba Transições)", false, false},
		{"Transition2", "Transição", "Transição 2", false, false},
		{"Transition3", "Transição", "Transição 3", false, false},
		{"Transition4", "Transição", "Transição 4", false, false},
		/* prévia */
		{"PreviewInput", "Prévia", "Entrada para a Prévia", true, false},
		{"PreviewInputNext", "Prévia", "Próxima entrada na Prévia", false, false},
		{"PreviewInputPrevious", "Prévia", "Entrada anterior na Prévia", false, false},
		/* overlays */
		{"OverlayInput1", "Overlay", "Overlay 1 liga/desliga", true, false},
		{"OverlayInput2", "Overlay", "Overlay 2 liga/desliga", true, false},
		{"OverlayInput3", "Overlay", "Overlay 3 liga/desliga", true, false},
		{"OverlayInput4", "Overlay", "Overlay 4 liga/desliga", true, false},
		{"OverlayInput1In", "Overlay", "Overlay 1 entra", true, false},
		{"OverlayInput2In", "Overlay", "Overlay 2 entra", true, false},
		{"OverlayInput3In", "Overlay", "Overlay 3 entra", true, false},
		{"OverlayInput4In", "Overlay", "Overlay 4 entra", true, false},
		{"OverlayInput1Out", "Overlay", "Overlay 1 sai", false, false},
		{"OverlayInput2Out", "Overlay", "Overlay 2 sai", false, false},
		{"OverlayInput3Out", "Overlay", "Overlay 3 sai", false, false},
		{"OverlayInput4Out", "Overlay", "Overlay 4 sai", false, false},
		{"OverlayInputAllOff", "Overlay", "Todos os overlays saem", false, false},
		/* títulos GT */
		{"TitleBeginAnimation", "Título", "Título GT: anima (Value = TransitionIn, TransitionOut ou o nome da página)", true, true, false, true},
		{"SetText", "Título", "Título GT: troca um texto (SelectedName = campo, ex.: Nome.Text; Value = texto)", true, true, false, true},
		/* scripts */
		{"ScriptStart", "Script", "Roda um script (Value = nome)", false, true, false, true},
		{"ScriptStop", "Script", "Para um script (Value = nome)", false, true, false, true},
		{"ScriptStopAll", "Script", "Para todos os scripts", false, false},
		/* áudio */
		{"Audio", "Áudio", "Áudio da entrada liga/desliga", true, false},
		{"AudioOn", "Áudio", "Áudio da entrada liga", true, false},
		{"AudioOff", "Áudio", "Áudio da entrada desliga", true, false},
		{"AudioAuto", "Áudio", "Auto (segue o vídeo) liga/desliga", true, false},
		{"SetVolume", "Áudio", "Volume da entrada (Value = 0 a 100)", true, true},
		{"SetMasterVolume", "Áudio", "Volume geral (Value = 0 a 100)", false, true},
		{"Solo", "Áudio", "Fone (monitorar) liga/desliga", true, false},
		/* entradas */
		{"Loop", "Entrada", "Loop do vídeo liga/desliga", true, false},
		{"Minimise", "Entrada", "Minimizar/expandir na lista", true, false},
		/* saídas */
		{"StartPlayList", "Saída", "Playlist começa (aba Lista)", false, false},
		{"StopPlayList", "Saída", "Playlist para", false, false},
		{"NextPlayListEntry", "Saída", "Playlist: próximo item", false, false},
		{"StartStopMultiCorder", "Saída", "MultiCorder liga/desliga (as entradas escolhidas da última vez)", false, false},
		{"StartMultiCorder", "Saída", "MultiCorder liga", false, false},
		{"StopMultiCorder", "Saída", "MultiCorder desliga", false, false},
		{"StartStopRecording", "Saídas", "Gravar: começa/para", false, false},
		{"StartRecording", "Saídas", "Gravar: começa", false, false},
		{"StopRecording", "Saídas", "Gravar: para", false, false},
		{"StartStopStreaming", "Saídas", "Transmitir: começa/para", false, false},
		{"StartStreaming", "Saídas", "Transmitir: começa", false, false},
		{"StopStreaming", "Saídas", "Transmitir: para", false, false},
		{"StartStopVirtualCam", "Saídas", "Câmera virtual: liga/desliga (Input = o que mostrar; vazio = Programa)", true, false},
		/* replay */
		{"ReplayStartStopRecording", "Replay", "Replay: começa/para de gravar", false, false},
		{"ReplayMarkIn", "Replay", "Replay: Mark In", false, false},
		{"ReplayMarkOut", "Replay", "Replay: Mark Out", false, false},
		{"ReplayMarkCancel", "Replay", "Replay: cancela o Mark In", false, false},
		{"ReplayMarkInOut", "Replay", "Replay: evento dos últimos segundos (Value = segundos)", false, true},
		{"ReplayPlay", "Replay", "Replay: play", false, false},
		{"ReplayPause", "Replay", "Replay: pausa", false, false},
		{"ReplayPlayPause", "Replay", "Replay: play/pausa", false, false},
		{"ReplayPlayReverse", "Replay", "Replay: reverso", false, false},
		{"ReplayFastForward", "Replay", "Replay: avanço rápido", false, false},
		{"ReplayLive", "Replay", "Replay: volta ao vivo", false, false},
		{"ReplayJumpFrames", "Replay", "Replay: pula quadros (Value = +/- quadros)", false, true},
		{"ReplayJumpSeconds", "Replay", "Replay: pula segundos (Value = +/- segundos)", false, true},
		{"ReplaySetSpeed", "Replay", "Replay: velocidade (Value = 0 a 100%)", false, true},
		{"ReplayCamera", "Replay", "Replay: ângulo (Value = câmera 1 a 8)", false, true},
		{"ReplaySelectChannelA", "Replay", "Replay: seleciona o canal A", false, false},
		{"ReplaySelectChannelB", "Replay", "Replay: seleciona o canal B", false, false},
		{"ReplaySelectChannelAB", "Replay", "Replay: seleciona A+B", false, false},
		{"ReplayPlayLastEvent", "Replay", "Replay: toca o último evento (põe no ar)", false, false},
		{"ReplayPlayAllEvents", "Replay", "Replay: Play Events (põe no ar)", false, false},
		{"ReplayToProgram", "Replay", "Replay: põe no ar", false, false},
		{"ReplayFromProgram", "Replay", "Replay: tira do ar", false, false},
		{"ReplayRestart", "Replay", "Replay: volta ao início do clipe", false, false},
		{"ReplayLoop", "Replay", "Replay: loop liga/desliga", false, false},
		{"ReplayZoomReset", "Replay", "Replay: zoom na imagem inteira", false, false},
		{"ReplaySpeedUp", "Replay", "Replay: mais rápido (Value = passo em %, padrão 10)", false, true},
		{"ReplaySpeedDown", "Replay", "Replay: mais lento (Value = passo em %, padrão 10)", false, true},
		/* de segurar: enquanto a tecla está apertada */
		{"ReplayHoldPlay", "Replay (segurar)", "Segure: toca para frente (na velocidade escolhida); soltou, pausa", false, false, true},
		{"ReplayHoldReverse", "Replay (segurar)", "Segure: toca de ré; soltou, pausa", false, false, true},
		{"ReplayHoldFastForward", "Replay (segurar)", "Segure: avanço rápido (Value = vezes, padrão 4); soltou, pausa", false, true, true},
		{"ReplayHoldFastRewind", "Replay (segurar)", "Segure: ré rápida (Value = vezes, padrão 4); soltou, pausa", false, true, true},
		{"ReplayHoldJog", "Replay (segurar)", "Quadro a quadro: toque anda 1; segurando vai andando (Value = +1 ou -1)", false, true, true},
		{"ReplayHoldZoomIn", "Replay (segurar)", "Segure: aproxima o zoom", false, false, true},
		{"ReplayHoldZoomOut", "Replay (segurar)", "Segure: afasta o zoom", false, false, true},
		{"ReplayHoldPanLeft", "Replay (segurar)", "Segure: move o zoom para a esquerda", false, false, true},
		{"ReplayHoldPanRight", "Replay (segurar)", "Segure: move o zoom para a direita", false, false, true},
		{"ReplayHoldPanUp", "Replay (segurar)", "Segure: move o zoom para cima", false, false, true},
		{"ReplayHoldPanDown", "Replay (segurar)", "Segure: move o zoom para baixo", false, false, true},
	};
	return list;
}

const FunctionDef *findFunction(const QString &name)
{
	for (const FunctionDef &f : functionList())
		if (name.compare(QLatin1String(f.name), Qt::CaseInsensitive) == 0)
			return &f;
	return nullptr;
}

/* parâmetros extras da chamada atual (SelectedName, Duration...) */
static QMap<QString, QString> g_params;
std::function<QString(const QString &, const QString &)> scriptHook;

QString inputKey(int id)
{
	return QString("5d1e0000-0000-4000-8000-%1").arg(id, 12, 10, QChar('0'));
}

/* "3" = terceira entrada; "CAM 1" = pelo nome; vazio = a da Prévia; também a chave do vMix */
static Input *resolveInput(const QString &spec)
{
	Engine &e = Engine::get();
	const QString s = spec.trimmed();
	if (s.isEmpty())
		return e.input(e.previewId());
	bool isNum = false;
	const int n = s.toInt(&isNum);
	if (isNum)
		return n >= 1 && n <= e.inputs().size() ? e.inputs()[n - 1] : nullptr;
	for (Input *in : e.inputs())
		if (in->name.compare(s, Qt::CaseInsensitive) == 0 || inputKey(in->id).compare(s, Qt::CaseInsensitive) == 0)
			return in;
	return nullptr;
}

bool runFunctionParams(const QString &name, const QMap<QString, QString> &params, QString *error, QWidget *parent)
{
	/* nomes do vMix sem diferenciar maiúsculas */
	QMap<QString, QString> p;
	for (auto it = params.begin(); it != params.end(); ++it) {
		QString k = it.key();
		for (const char *known : {"Input", "Value", "Duration", "SelectedName", "Mix", "SelectedIndex"})
			if (k.compare(QLatin1String(known), Qt::CaseInsensitive) == 0)
				k = QLatin1String(known);
		p.insert(k, it.value());
	}
	QString value = p.value("Value");
	/* Fade Duration=800 (vMix) = Value aqui */
	if (value.isEmpty() && p.contains("Duration"))
		value = p.value("Duration");
	g_params = p;
	const bool ok = runFunction(name, p.value("Input"), value, error, parent);
	g_params.clear();
	return ok;
}

bool runFunction(const QString &name, const QString &inputSpec, const QString &value, QString *error, QWidget *parent)
{
	auto fail = [error](const QString &msg) {
		if (error)
			*error = msg;
		return false;
	};
	const FunctionDef *def = findFunction(name);
	if (!def)
		return fail("Função desconhecida: " + name);
	Engine &e = Engine::get();
	Input *in = nullptr;
	if (def->input) {
		in = resolveInput(inputSpec);
		/* StartStopVirtualCam sem entrada = Programa */
		if (!in && !(inputSpec.trimmed().isEmpty() && QLatin1String(def->name) == QLatin1String("StartStopVirtualCam")))
			return fail(QString("%1: entrada \"%2\" não encontrada").arg(def->name, inputSpec));
	}
	bool numOk = true;
	const double v = value.trimmed().isEmpty() ? 0.0 : value.trimmed().toDouble(&numOk);
	if (def->value && !def->textValue && !value.trimmed().isEmpty() && !numOk)
		return fail(QString("%1: Value \"%2\" não é número").arg(def->name, value));
	const QString n = QString::fromLatin1(def->name);
	const bool replayUp = engineAlive();

	/* ---- transições e prévia ---- */
	if (n == "ActiveInput")
		e.cutTo(in->id);
	else if (n == "SetFader") {
		const float t = (float)std::clamp(v / 255.0, 0.0, 1.0);
		if (t >= 1.0f)
			e.tbarRelease(1.0f);
		else
			e.tbarMove(t);
	} else if (n == "TitleBeginAnimation" || n == "SetText") {
		if (!gtIsSource(in->source))
			return fail(QString("%1: \"%2\" não é um título GT").arg(n, in->name));
		if (n == "SetText") {
			QString field = g_params.value("SelectedName").trimmed();
			std::shared_ptr<GtTitle> title = gtTitleOf(in->source);
			if (!title)
				return fail("SetText: o título ainda não carregou");
			/* "Nome.Text", "Nome" ou o índice (SelectedIndex, como no vMix) */
			QString key;
			int textIdx = 0;
			const int wantIdx = g_params.contains("SelectedIndex") ? g_params.value("SelectedIndex").toInt() : -1;
			for (const GtField &f : title->fields()) {
				if (f.kind != GtField::Text)
					continue;
				if ((!field.isEmpty() && (f.id.compare(field, Qt::CaseInsensitive) == 0 ||
							  f.element.compare(field, Qt::CaseInsensitive) == 0)) ||
				    (field.isEmpty() && textIdx == wantIdx)) {
					key = f.id;
					break;
				}
				textIdx++;
			}
			if (key.isEmpty())
				return fail(QString("SetText: o título não tem o campo \"%1\"").arg(field));
			QMap<QString, QString> vals = gtValues(in->source);
			vals[key] = value;
			gtSetValues(in->source, vals);
		} else {
			const QString a = value.trimmed();
			if (a.isEmpty() || a.compare("TransitionIn", Qt::CaseInsensitive) == 0)
				gtPlayIn(in->source);
			else if (a.compare("TransitionOut", Qt::CaseInsensitive) == 0)
				gtPlayOut(in->source);
			else
				gtPlayPage(in->source, a);
		}
	} else if (n == "ScriptStart" || n == "ScriptStop" || n == "ScriptStopAll") {
		if (!scriptHook)
			return fail("Scripts não disponíveis");
		const QString r = scriptHook(n, value.trimmed());
		if (!r.isEmpty())
			return fail(r);
	} else if (n == "StartPlayList") {
		if (!Playlist::get().active() && !Playlist::get().start())
			return fail("Playlist vazia (monte na aba Lista)");
	} else if (n == "StopPlayList") {
		Playlist::get().stop();
	} else if (n == "NextPlayListEntry") {
		Playlist::get().next();
	} else if (n == "StartStopMultiCorder" || n == "StartMultiCorder" || n == "StopMultiCorder") {
		MultiCorder &mc = MultiCorder::get();
		const bool want = n == "StartMultiCorder" || (n == "StartStopMultiCorder" && !mc.active());
		if (!want) {
			mc.stop();
		} else if (!mc.active()) {
			QList<int> ids(mc.lastSelection.begin(), mc.lastSelection.end());
			if (ids.isEmpty()) /* nunca escolheu: as câmeras */
				for (Input *x : e.inputs()) {
					const char *tid = obs_source_get_unversioned_id(x->source);
					if (tid && (strstr(tid, "dshow") || strstr(tid, "v4l2") || strstr(tid, "decklink")))
						ids << x->id;
				}
			QString err;
			if (!mc.start(ids, err))
				return fail("MultiCorder: " + err);
		}
	} else if (n == "Cut")
		e.transition(TransitionKind::Cut);
	else if (n == "Fade") {
		if (v > 0)
			e.setTransitionMs((int)v);
		e.transition(TransitionKind::Fade);
	} else if (n == "QuickPlay")
		e.quickPlay(in->id);
	else if (n == "CutDirect")
		e.cutTo(in->id);
	else if (n == "FadeToBlack")
		e.transition(TransitionKind::FadeToBlack);
	else if (n == "SetTransitionDuration")
		e.setTransitionMs((int)v);
	else if (n.startsWith("Stinger") || n.startsWith("Transition")) {
		const bool st = n.startsWith("Stinger");
		const int k = n.right(1).toInt();
		StudioTransition *t = e.nthTransition(k, st);
		if (!t)
			return fail(QString("%1: não existe — crie na aba Transições").arg(n));
		e.transitionWith(t->id);
	}
	else if (n == "PreviewInput")
		e.setPreview(in->id);
	else if (n == "PreviewInputNext" || n == "PreviewInputPrevious") {
		if (e.inputs().isEmpty())
			return fail("Sem entradas");
		const int count = (int)e.inputs().size();
		const int cur = qMax(0, e.indexOf(e.previewId()));
		const int next = (cur + (n == "PreviewInputNext" ? 1 : count - 1)) % count;
		e.setPreview(e.inputs()[next]->id);
	}
	/* ---- overlays ---- */
	else if (n.startsWith("OverlayInput") && n != "OverlayInputAllOff") {
		const int ch = n.mid(12, 1).toInt();
		const QString mode = n.mid(13); /* "", "In", "Out" */
		const bool isOn = e.overlay(ch) != 0;
		if (mode == "Out") {
			if (isOn)
				e.toggleOverlay(ch, e.overlay(ch));
		} else if (mode == "In") {
			if (e.overlay(ch) != in->id)
				e.toggleOverlay(ch, in->id);
		} else {
			e.toggleOverlay(ch, in->id);
		}
	} else if (n == "OverlayInputAllOff") {
		for (int ch = 1; ch <= 4; ch++)
			if (e.overlay(ch))
				e.toggleOverlay(ch, e.overlay(ch));
	}
	/* ---- áudio e entradas ---- */
	else if (n == "Audio")
		e.setInputAudio(in->id, !in->audioOn);
	else if (n == "AudioOn")
		e.setInputAudio(in->id, true);
	else if (n == "AudioOff")
		e.setInputAudio(in->id, false);
	else if (n == "AudioAuto")
		e.setInputAudioAuto(in->id, !in->audioAuto);
	else if (n == "SetVolume") /* 0-100 como no vMix (escala de fader: 100 = 0 dB) */
		e.setInputVolume(in->id, (float)std::pow(std::clamp(v, 0.0, 100.0) / 100.0, 3.0));
	else if (n == "SetMasterVolume")
		e.setMasterVolume((float)std::pow(std::clamp(v, 0.0, 100.0) / 100.0, 3.0));
	else if (n == "Solo")
		e.setInputMonitor(in->id, !in->monitor);
	else if (n == "Loop")
		e.setInputLoop(in->id, !in->loop);
	else if (n == "Minimise")
		e.setInputCollapsed(in->id, !in->collapsed);
	/* ---- saídas ---- */
	else if (n == "StartStopRecording" || n == "StartRecording" || n == "StopRecording") {
		Outputs &o = Outputs::get();
		const bool want = n == "StartRecording" || (n == "StartStopRecording" && !o.recording());
		if (want && !o.recording()) {
			QString err;
			if (!o.startRecording(err))
				return fail(err);
		} else if (!want) {
			o.stopRecording();
		}
	} else if (n == "StartStopStreaming" || n == "StartStreaming" || n == "StopStreaming") {
		Outputs &o = Outputs::get();
		const bool want = n == "StartStreaming" || (n == "StartStopStreaming" && !o.streaming());
		if (want && !o.streaming()) {
			QString err;
			if (!o.startStreaming(err))
				return fail(err);
		} else if (!want) {
			o.stopStreaming();
		}
	} else if (n == "StartStopVirtualCam") {
		VirtualCam &vc = VirtualCam::get();
		if (vc.active()) {
			vc.stop();
		} else {
			QString err;
			if (!vc.start(in ? in->id : 0, parent, err))
				return fail(err);
		}
	}
	/* ---- replay ---- */
	else if (n.startsWith("Replay")) {
		if (!replayUp)
			return fail("Replay indisponível");
		ReplayEngine &r = engine();
		AirControl *air = AirControl::instance();
		if (n == "ReplayStartStopRecording") {
			if (r.recording()) {
				r.stopRecording();
			} else {
				std::string err;
				if (!r.startRecording(err))
					return fail(QString::fromStdString(err));
			}
		} else if (n == "ReplayMarkIn")
			r.markIn();
		else if (n == "ReplayMarkOut")
			r.markOut();
		else if (n == "ReplayMarkCancel")
			r.markCancel();
		else if (n == "ReplayMarkInOut")
			r.autoEvent(v > 0 ? (int)v : 10);
		else if (n == "ReplayPlay")
			r.play();
		else if (n == "ReplayPause")
			r.pause();
		else if (n == "ReplayPlayPause")
			r.togglePlay();
		else if (n == "ReplayPlayReverse")
			r.playReverse();
		else if (n == "ReplayFastForward")
			r.fastForward();
		else if (n == "ReplayLive")
			r.goLive();
		else if (n == "ReplayJumpFrames")
			r.step((int)v);
		else if (n == "ReplayJumpSeconds")
			r.jumpSeconds(v);
		else if (n == "ReplaySetSpeed")
			r.setSpeedPercent((int)std::clamp(v, 0.0, 100.0));
		else if (n == "ReplayCamera") {
			if (v < 1 || v > MAX_CAMERAS)
				return fail("ReplayCamera: Value = 1 a 8");
			r.setAngle((int)v - 1);
		} else if (n == "ReplaySelectChannelA")
			r.setSelection(SEL_A);
		else if (n == "ReplaySelectChannelB")
			r.setSelection(SEL_B);
		else if (n == "ReplaySelectChannelAB")
			r.setSelection(SEL_AB);
		else if (n == "ReplayPlayLastEvent") {
			if (air)
				air->toggleLast();
		} else if (n == "ReplayPlayAllEvents") {
			if (air)
				air->toggleEvents(0);
		} else if (n == "ReplayToProgram") {
			if (air)
				air->takeIn();
		} else if (n == "ReplayFromProgram") {
			if (air)
				air->takeOut();
		} else if (n == "ReplayRestart")
			r.restartClip();
		else if (n == "ReplayLoop")
			r.setLoop(!r.loopOn());
		else if (n == "ReplayZoomReset")
			r.zoomReset();
		else if (n == "ReplaySpeedUp" || n == "ReplaySpeedDown") {
			const int stepPct = v > 0 ? (int)v : 10;
			r.setSpeedPercent(r.speedPercent() + (n == "ReplaySpeedUp" ? stepPct : -stepPct));
		} else if (def->hold) {
			/* função de segurar chamada sem tecla (script, API): um toque = aperta e solta */
			runHoldFunction(n, value, true);
			runHoldFunction(n, value, false);
		}
	}
	blog(LOG_INFO, "função %s%s%s", def->name, in ? (" Input=" + in->name).toUtf8().constData() : "",
	     value.isEmpty() ? "" : (" Value=" + value).toUtf8().constData());
	return true;
}

/* Cut | Fade Value=500 | OverlayInput1In Input=3 | QuickPlay Input="CAM 1"
 * (também aceita a forma do vMix: Function=Cut&Input=3&Value=...) */
bool runFunctionLine(const QString &line, QString *error, QWidget *parent)
{
	QString l = line.trimmed();
	if (l.startsWith("Function=", Qt::CaseInsensitive))
		l = l.mid(9).replace('&', ' ');
	static const QRegularExpression re(R"(^(\w+)\s*(.*)$)");
	const auto m = re.match(l);
	if (!m.hasMatch()) {
		if (error)
			*error = "Linha sem função: " + line;
		return false;
	}
	QMap<QString, QString> params;
	static const QRegularExpression arg(R"((\w+)\s*=\s*(\"[^\"]*\"|[^\s&]+))");
	auto it = arg.globalMatch(m.captured(2));
	while (it.hasNext()) {
		const auto a = it.next();
		QString val = a.captured(2);
		if (val.startsWith('"'))
			val = val.mid(1, val.size() - 2);
		params.insert(a.captured(1), val);
	}
	return runFunctionParams(m.captured(1), params, error, parent);
}

bool runHoldFunction(const QString &name, const QString &value, bool pressed)
{
	const FunctionDef *def = findFunction(name);
	if (!def || !def->hold || !engineAlive())
		return false;
	ReplayEngine &r = engine();
	const QString n = QString::fromLatin1(def->name);
	const double v = value.trimmed().toDouble();
	if (n == "ReplayHoldPlay" || n == "ReplayHoldReverse") {
		if (pressed)
			r.shuttle(n == "ReplayHoldPlay" ? 1 : -1, 1.0);
		else
			r.pause();
	} else if (n == "ReplayHoldFastForward" || n == "ReplayHoldFastRewind") {
		if (pressed)
			r.shuttle(n == "ReplayHoldFastForward" ? 1 : -1, v > 1 ? v : 4.0);
		else
			r.pause();
	} else if (n == "ReplayHoldJog") {
		if (pressed) /* a repetição enquanto segura fica com o atalho (Shortcuts) */
			r.step(v < 0 ? -1 : 1);
	} else {
		/* zoom: eixo 0 = esquerda/direita, 1 = cima/baixo, 2 = aproximar/afastar */
		int axis = 2, dir = 1;
		if (n == "ReplayHoldZoomOut")
			dir = -1;
		else if (n == "ReplayHoldPanLeft")
			axis = 0, dir = -1;
		else if (n == "ReplayHoldPanRight")
			axis = 0, dir = 1;
		else if (n == "ReplayHoldPanUp")
			axis = 1, dir = -1;
		else if (n == "ReplayHoldPanDown")
			axis = 1, dir = 1;
		r.zoomMove(axis, pressed ? dir : 0);
	}
	blog(LOG_INFO, "função %s %s", def->name, pressed ? "apertada" : "solta");
	return true;
}
