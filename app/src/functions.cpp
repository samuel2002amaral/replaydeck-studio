#include "functions.hpp"
#include "engine.hpp"
#include "outputs.hpp"
#include "virtualcam.hpp"
#include "multicorder.hpp"
#include "playlist.hpp"
#include "ptz.hpp"
#include "external-output.hpp"
#include "vmix-keys.hpp"
#include <cstring>
#include "../replay/gt/gt-source.hpp"
#include "../replay/gt/gt-doc.hpp"

#include "../replay/air-control.hpp"
#include "../replay/engine.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QCoreApplication>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
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
		{"Merge", "Transição", "Merge: a Prévia vai para o Programa movendo o que as duas têm em comum", false, true},
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
		{"SetImage", "Título", "Título GT: troca uma imagem (SelectedName = campo; Value = arquivo ou link)", true, true, false, true},
		{"SetTextColour", "Título", "Título GT: cor do texto (SelectedName; Value = #RRGGBB)", true, true, false, true},
		{"SetColor", "Título", "Título GT: cor de uma forma (SelectedName; Value = #RRGGBB)", true, true, false, true},
		{"SetTextVisible", "Título", "Título GT: mostra/esconde um texto (SelectedName)", true, false},
		{"SetTextVisibleOn", "Título", "Título GT: mostra um texto (SelectedName)", true, false},
		{"SetTextVisibleOff", "Título", "Título GT: esconde um texto (SelectedName)", true, false},
		{"SetImageVisible", "Título", "Título GT: mostra/esconde uma imagem (SelectedName)", true, false},
		{"SetImageVisibleOn", "Título", "Título GT: mostra uma imagem (SelectedName)", true, false},
		{"SetImageVisibleOff", "Título", "Título GT: esconde uma imagem (SelectedName)", true, false},
		{"SetCountdown", "Título", "Título GT: cronômetro (Value = 00:10:00 ou segundos; SelectedName = campo)", true, true, false, true},
		{"StartCountdown", "Título", "Título GT: cronômetro começa", true, false},
		{"StopCountdown", "Título", "Título GT: cronômetro para e volta ao início", true, false},
		{"PauseCountdown", "Título", "Título GT: cronômetro pausa/continua", true, false},
		{"AdjustCountdown", "Título", "Título GT: cronômetro + ou − segundos (Value)", true, true},
		/* mídia (vídeos, listas) */
		{"Play", "Entrada", "Vídeo: tocar", true, false},
		{"Pause", "Entrada", "Vídeo: pausar", true, false},
		{"PlayPause", "Entrada", "Vídeo: tocar/pausar", true, false},
		{"Restart", "Entrada", "Vídeo: do início", true, false},
		{"SetPosition", "Entrada", "Vídeo: posição (Value = ms)", true, true},
		{"NextItem", "Entrada", "Lista/fotos: próximo", true, false},
		{"PreviousItem", "Entrada", "Lista/fotos: anterior", true, false},
		{"LoopOn", "Entrada", "Loop do vídeo liga", true, false},
		/* posição e camadas (abas Posição e Camadas da entrada) */
		{"SetZoom", "Posição", "Ampliação da entrada (Value = 1 tela cheia; 0,5 metade)", true, true},
		{"SetZoomX", "Posição", "Zoom X da entrada (só largura)", true, true},
		{"SetZoomY", "Posição", "Zoom Y da entrada (só altura)", true, true},
		{"SetPanX", "Posição", "Pan X da entrada (Value -2 a 2; 0,5 = metade direita)", true, true},
		{"SetPanY", "Posição", "Pan Y da entrada (Value -2 a 2; 0,5 = metade de cima)", true, true},
		{"SetRotation", "Posição", "Giro da entrada (Value = graus)", true, true},
		{"SetCrop", "Posição", "Corte da entrada (Value = X1,Y1,X2,Y2 de 0 a 1; ex.: 0.25,0,0.75,1)", true, true, false, true},
		{"SetCropX1", "Posição", "Corte X1 (0 a 1)", true, true},
		{"SetCropX2", "Posição", "Corte X2 (0 a 1)", true, true},
		{"SetCropY1", "Posição", "Corte Y1 (0 a 1)", true, true},
		{"SetCropY2", "Posição", "Corte Y2 (0 a 1)", true, true},
		{"ResetPosition", "Posição", "Posição da entrada volta à tela cheia", true, false},
		{"SetLayer", "Camadas", "Camada da entrada (Value = número,entrada; ex.: 1,PLACAR; 1,PGM = o Programa; 1, vazio = tira)", true, true, false, true},
		{"SetMultiViewOverlay", "Camadas", "Igual a SetLayer (nome antigo do vMix)", true, true, false, true},
		{"MultiViewOverlay", "Camadas", "Camada liga/desliga (Value = número 1 a 10)", true, true},
		{"MultiViewOverlayOn", "Camadas", "Camada liga (Value = número)", true, true},
		{"MultiViewOverlayOff", "Camadas", "Camada desliga (Value = número)", true, true},
		{"SetLayerBehind", "Camadas", "Camada atrás da imagem principal (Value = número,1) ou na frente (número,0)", true, true, false, true},
		{"SetLayerZoom", "Camadas", "Ampliação de uma camada (Value = número,zoom)", true, true, false, true},
		{"SetLayerPanX", "Camadas", "Pan X de uma camada (Value = número,pan)", true, true, false, true},
		{"SetLayerPanY", "Camadas", "Pan Y de uma camada (Value = número,pan)", true, true, false, true},
		{"LoopOff", "Entrada", "Loop do vídeo desliga", true, false},
		/* scripts */
		{"ScriptStart", "Script", "Roda um script (Value = nome)", false, true, false, true},
		{"ScriptStop", "Script", "Para um script (Value = nome)", false, true, false, true},
		{"ScriptStopAll", "Script", "Para todos os scripts", false, false},
		{"SetDynamicInput1", "Script", "Variável Dynamic1 = entrada (Value)", false, true, false, true},
		{"SetDynamicInput2", "Script", "Variável Dynamic2 = entrada", false, true, false, true},
		{"SetDynamicInput3", "Script", "Variável Dynamic3 = entrada", false, true, false, true},
		{"SetDynamicInput4", "Script", "Variável Dynamic4 = entrada", false, true, false, true},
		{"SetDynamicValue1", "Script", "Variável DynamicValue1 (Value)", false, true, false, true},
		{"SetDynamicValue2", "Script", "Variável DynamicValue2", false, true, false, true},
		{"SetDynamicValue3", "Script", "Variável DynamicValue3", false, true, false, true},
		{"SetDynamicValue4", "Script", "Variável DynamicValue4", false, true, false, true},
		/* outros do vMix */
		{"SetTransitionDuration1", "Transição", "Duração da transição 1 (Value = ms)", false, true},
		{"SetTransitionDuration2", "Transição", "Duração da transição 2", false, true},
		{"SetTransitionDuration3", "Transição", "Duração da transição 3", false, true},
		{"SetTransitionDuration4", "Transição", "Duração da transição 4", false, true},
		{"OverlayInput1Off", "Overlay", "Overlay 1 sai na hora", false, false},
		{"OverlayInput2Off", "Overlay", "Overlay 2 sai na hora", false, false},
		{"OverlayInput3Off", "Overlay", "Overlay 3 sai na hora", false, false},
		{"OverlayInput4Off", "Overlay", "Overlay 4 sai na hora", false, false},
		{"SetVolumeFade", "Áudio", "Volume com fade (Value = volume,ms; ex.: 0,2000)", true, true, false, true},
		{"MasterAudio", "Áudio", "Som geral liga/desliga", false, false},
		{"MasterAudioON", "Áudio", "Som geral liga", false, false},
		{"MasterAudioOFF", "Áudio", "Som geral desliga", false, false},
		{"StartStopExternal", "Saída", "Saída externa do Programa liga/desliga", false, false},
		{"Fullscreen", "Saída", "Programa em tela cheia liga/desliga", false, false},
		{"FullscreenOn", "Saída", "Programa em tela cheia", false, false},
		{"FullscreenOff", "Saída", "Fecha a tela cheia", false, false},
		{"Snapshot", "Saída", "Foto do Programa (PNG na pasta de gravação)", false, false},
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
		{"ReplayRecorded", "Replay", "Replay: sai do ao vivo (gravado)", false, false},
		{"ReplayLiveToggle", "Replay", "Replay: ao vivo / gravado", false, false},
		{"ReplayJumpToNow", "Replay", "Replay: vai para o agora", false, false},
		{"ReplayMarkInLive", "Replay", "Replay: Mark In no ao vivo", false, true},
		{"ReplayMarkInOutLive", "Replay", "Replay: evento dos últimos Value segundos", false, true},
		{"ReplayChangeSpeed", "Replay", "Replay: muda a velocidade (Value = +0.1, -0.25...)", false, true},
		{"ReplayFastBackward", "Replay", "Replay: ré rápida (Value = vezes)", false, true},
		{"ReplayStopEvents", "Replay", "Replay: para os eventos", false, false},
		{"ReplayStartRecording", "Replay", "Replay: começa a gravar", false, false},
		{"ReplayStopRecording", "Replay", "Replay: para de gravar", false, false},
		{"ReplayShowHide", "Replay", "Replay: entra/sai do Programa", false, false},
		{"ReplayPlayLastEventToOutput", "Replay", "Replay: último evento no ar", false, false},
		{"ReplayPlaySelectedEventToOutput", "Replay", "Replay: evento escolhido no ar", false, false},
		{"ReplayPlayAllEventsToOutput", "Replay", "Replay: todos os eventos no ar", false, false},
		{"ReplaySelectFirstEvent", "Replay", "Replay: escolhe o primeiro evento", false, false},
		{"ReplaySelectLastEvent", "Replay", "Replay: escolhe o último evento", false, false},
		{"ReplaySelectNextEvent", "Replay", "Replay: escolhe o próximo evento", false, false},
		{"ReplaySelectPreviousEvent", "Replay", "Replay: escolhe o evento anterior", false, false},
		{"ReplayPlaySelectedEvent", "Replay", "Replay: toca o evento escolhido", false, false},
		{"ReplayACamera1", "Replay", "Replay A: câmera 1", false, false},
		{"ReplayBCamera1", "Replay", "Replay B: câmera 1", false, false},
		{"ReplayACamera2", "Replay", "Replay A: câmera 2", false, false},
		{"ReplayBCamera2", "Replay", "Replay B: câmera 2", false, false},
		{"ReplayACamera3", "Replay", "Replay A: câmera 3", false, false},
		{"ReplayBCamera3", "Replay", "Replay B: câmera 3", false, false},
		{"ReplayACamera4", "Replay", "Replay A: câmera 4", false, false},
		{"ReplayBCamera4", "Replay", "Replay B: câmera 4", false, false},
		{"ReplayACamera5", "Replay", "Replay A: câmera 5", false, false},
		{"ReplayBCamera5", "Replay", "Replay B: câmera 5", false, false},
		{"ReplayACamera6", "Replay", "Replay A: câmera 6", false, false},
		{"ReplayBCamera6", "Replay", "Replay B: câmera 6", false, false},
		{"ReplayACamera7", "Replay", "Replay A: câmera 7", false, false},
		{"ReplayBCamera7", "Replay", "Replay B: câmera 7", false, false},
		{"ReplayACamera8", "Replay", "Replay A: câmera 8", false, false},
		{"ReplayBCamera8", "Replay", "Replay B: câmera 8", false, false},
		{"ReplaySelectEvents1", "Replay", "Replay: lista de eventos 1", false, false},
		{"ReplaySelectEvents2", "Replay", "Replay: lista de eventos 2", false, false},
		{"ReplaySelectEvents3", "Replay", "Replay: lista de eventos 3", false, false},
		{"ReplaySelectEvents4", "Replay", "Replay: lista de eventos 4", false, false},
		{"ReplaySelectEvents5", "Replay", "Replay: lista de eventos 5", false, false},
		{"ReplaySelectEvents6", "Replay", "Replay: lista de eventos 6", false, false},
		{"ReplaySelectEvents7", "Replay", "Replay: lista de eventos 7", false, false},
		{"ReplaySelectEvents8", "Replay", "Replay: lista de eventos 8", false, false},
		{"ReplaySelectEvents9", "Replay", "Replay: lista de eventos 9", false, false},
		{"ReplaySelectEvents10", "Replay", "Replay: lista de eventos 10", false, false},
		{"ReplaySelectEvents11", "Replay", "Replay: lista de eventos 11", false, false},
		{"ReplaySelectEvents12", "Replay", "Replay: lista de eventos 12", false, false},
		{"ReplaySelectEvents13", "Replay", "Replay: lista de eventos 13", false, false},
		{"ReplaySelectEvents14", "Replay", "Replay: lista de eventos 14", false, false},
		{"ReplaySelectEvents15", "Replay", "Replay: lista de eventos 15", false, false},
		{"ReplaySelectEvents16", "Replay", "Replay: lista de eventos 16", false, false},
		{"ReplaySelectEvents17", "Replay", "Replay: lista de eventos 17", false, false},
		{"ReplaySelectEvents18", "Replay", "Replay: lista de eventos 18", false, false},
		{"ReplaySelectEvents19", "Replay", "Replay: lista de eventos 19", false, false},
		{"ReplaySelectEvents20", "Replay", "Replay: lista de eventos 20", false, false},
		{"ReplaySpeedUp", "Replay", "Replay: mais rápido (Value = passo em %, padrão 10)", false, true},
		{"ReplaySpeedDown", "Replay", "Replay: mais lento (Value = passo em %, padrão 10)", false, true},
		/* PTZ (aba PTZ): Value = câmera (nome ou número; vazio = a escolhida no painel) */
		{"PTZPreset", "PTZ", "PTZ: vai para a posição gravada (Value = número; Input = câmera)", false, true},
		{"PTZHome", "PTZ", "PTZ: posição inicial (Value = câmera)", false, true, false, true},
		{"PTZHoldUp", "PTZ (segurar)", "Segure: PTZ sobe (Value = câmera)", false, true, true, true},
		{"PTZHoldDown", "PTZ (segurar)", "Segure: PTZ desce", false, true, true, true},
		{"PTZHoldLeft", "PTZ (segurar)", "Segure: PTZ para a esquerda", false, true, true, true},
		{"PTZHoldRight", "PTZ (segurar)", "Segure: PTZ para a direita", false, true, true, true},
		{"PTZHoldZoomIn", "PTZ (segurar)", "Segure: PTZ aproxima", false, true, true, true},
		{"PTZHoldZoomOut", "PTZ (segurar)", "Segure: PTZ afasta", false, true, true, true},
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
	Input *in = Engine::get().input(id);
	return in ? in->key : QString();
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
	if (Input *in = e.inputByKey(s))
		return in;
	for (Input *in : e.inputs())
		if (in->name.compare(s, Qt::CaseInsensitive) == 0)
			return in;
	return nullptr;
}

/* ---------- vMix: variáveis dinâmicas, campos de título, cronômetro, foto ---------- */

static QString g_dynInput[4], g_dynValue[4];

/* "Nome.Text" / "Nome.Source" / "Nome.Fill.Color" -> "Nome" */
static QString elementOfField(QString s)
{
	for (const char *suf : {".Fill.Bitmap", ".Fill.Color", ".Text", ".Source", ".Fill"})
		if (s.endsWith(QLatin1String(suf), Qt::CaseInsensitive)) {
			s.chop((int)strlen(suf));
			break;
		}
	return s;
}

/* acha o campo do título pedido por SelectedName (ou SelectedIndex) */
static bool findGtField(obs_source_t *src, const QMap<QString, QString> &params, int wantKind, GtField *out,
			QString *err)
{
	std::shared_ptr<GtTitle> t = gtTitleOf(src);
	if (!t) {
		*err = "o título ainda não carregou";
		return false;
	}
	const QString sel = params.value("SelectedName").trimmed();
	const QString el = elementOfField(sel);
	const int wantIdx = params.contains("SelectedIndex") ? params.value("SelectedIndex").toInt() : 0;
	int idx = 0;
	const std::vector<GtField> fields = t->fields();
	/* 1º: o id exato; 2º: o elemento com o tipo pedido; 3º: o elemento com qualquer tipo */
	for (int pass = 0; pass < 3; pass++)
		for (const GtField &f : fields) {
			const bool kindOk = wantKind < 0 || (int)f.kind == wantKind;
			if (sel.isEmpty()) {
				if (pass == 0 && kindOk && idx++ == wantIdx) {
					*out = f;
					return true;
				}
				continue;
			}
			if ((pass == 0 && f.id.compare(sel, Qt::CaseInsensitive) == 0) ||
			    (pass == 1 && kindOk && f.element.compare(el, Qt::CaseInsensitive) == 0) ||
			    (pass == 2 && f.element.compare(el, Qt::CaseInsensitive) == 0)) {
				*out = f;
				return true;
			}
		}
	/* sem campo editável (ex.: forma sem cor no arquivo): ainda dá para mexer no elemento */
	if (!sel.isEmpty()) {
		for (const GtLayer &L : t->layers)
			for (const GtElement &e : L.elements)
				if (e.name.compare(el, Qt::CaseInsensitive) == 0) {
					out->id = e.name;
					out->element = e.name;
					out->kind = e.kind == GtKind::Text ? GtField::Text : e.kind == GtKind::Image ? GtField::Image : GtField::Color;
					return true;
				}
	}
	QStringList have;
	for (const GtField &f : fields)
		if (wantKind < 0 || (int)f.kind == wantKind)
			have << f.id;
	*err = (sel.isEmpty() ? QString("não tem esse campo") : QString("não tem o campo \"%1\"").arg(sel)) +
	       QString(". Campos dele: %1").arg(have.isEmpty() ? QString("nenhum desse tipo") : have.join(", "));
	return false;
}

static bool elementVisible(obs_source_t *src, const QString &element)
{
	const GtValues v = gtValues(src);
	const QString k = element + ".Visible";
	if (v.contains(k))
		return v.value(k) != "False";
	if (std::shared_ptr<GtTitle> t = gtTitleOf(src))
		for (const GtLayer &L : t->layers)
			for (const GtElement &e : L.elements)
				if (e.name == element)
					return e.visible;
	return true;
}

static void setGtValue(obs_source_t *src, const QString &key, const QString &value)
{
	GtValues v = gtValues(src);
	if (v.value(key) == value && v.contains(key))
		return;
	v[key] = value;
	gtSetValues(src, v);
}

/* imagem de um link (http): baixa uma vez para a pasta de cache e usa o arquivo */
static void setGtImageFromUrl(int inputId, const QString &key, const QString &url)
{
	const QString dir = QDir(Engine::get().configDir()).filePath("cache-imagens");
	QDir().mkpath(dir);
	QString ext = QFileInfo(QUrl(url).path()).suffix().toLower();
	if (ext.isEmpty() || ext.size() > 5)
		ext = "png";
	const QString file =
		QDir(dir).filePath(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex() + "." + ext);
	auto apply = [inputId, key, file] {
		if (Input *in = Engine::get().input(inputId))
			setGtValue(in->source, key, QDir::toNativeSeparators(file));
	};
	if (QFileInfo::exists(file)) {
		apply();
		return;
	}
	static QNetworkAccessManager *net = new QNetworkAccessManager(QCoreApplication::instance());
	QNetworkRequest req{QUrl(url)};
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(15000);
	QNetworkReply *r = net->get(req);
	QObject::connect(r, &QNetworkReply::finished, r, [r, file, apply, url] {
		r->deleteLater();
		if (r->error() != QNetworkReply::NoError) {
			blog(LOG_WARNING, "SetImage: não baixou %s: %s", url.toUtf8().constData(),
			     r->errorString().toUtf8().constData());
			return;
		}
		QFile f(file);
		if (f.open(QIODevice::WriteOnly)) {
			f.write(r->readAll());
			f.close();
			apply();
		}
	});
}

/* cronômetro dos títulos (como o countdown do vMix) */
struct Countdown {
	int inputId = 0;
	QString field;
	qint64 totalMs = 0, elapsedMs = 0;
	bool running = false, hours = false;
	qint64 startedAt = 0, elapsedAtStart = 0;
	QString last;
};
static QMap<QString, Countdown> g_countdowns;

static QString fmtCountdown(const Countdown &c)
{
	const qint64 shown = c.totalMs > 0 ? std::max<qint64>(0, c.totalMs - c.elapsedMs) : c.elapsedMs;
	/* arredonda para cima na contagem regressiva (10:00 até o primeiro segundo passar) */
	qint64 s = c.totalMs > 0 ? (shown + 999) / 1000 : shown / 1000;
	const qint64 h = s / 3600, m = (s / 60) % 60, sec = s % 60;
	if (c.hours || h > 0)
		return QString("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
	return QString("%1:%2").arg(m + h * 60, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

static void tickCountdowns()
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (auto it = g_countdowns.begin(); it != g_countdowns.end();) {
		Countdown &c = it.value();
		Input *in = Engine::get().input(c.inputId);
		if (!in) {
			it = g_countdowns.erase(it);
			continue;
		}
		if (c.running) {
			c.elapsedMs = c.elapsedAtStart + (now - c.startedAt);
			if (c.totalMs > 0 && c.elapsedMs >= c.totalMs) {
				c.elapsedMs = c.totalMs;
				c.running = false;
			}
		}
		const QString text = fmtCountdown(c);
		if (text != c.last) {
			c.last = text;
			setGtValue(in->source, c.field, text);
		}
		++it;
	}
}

static Countdown &countdownFor(Input *in, const QString &field)
{
	static QTimer *timer = nullptr;
	if (!timer) {
		timer = new QTimer(QCoreApplication::instance());
		timer->setTimerType(Qt::PreciseTimer);
		QObject::connect(timer, &QTimer::timeout, tickCountdowns);
		timer->start(100);
	}
	Countdown &c = g_countdowns[QString("%1|%2").arg(in->id).arg(field)];
	c.inputId = in->id;
	c.field = field;
	return c;
}

/* "00:10:00", "10:00" ou segundos */
static qint64 parseDuration(const QString &v, bool *hours)
{
	const QStringList p = v.trimmed().split(':');
	*hours = p.size() >= 3;
	qint64 s = 0;
	for (const QString &x : p)
		s = s * 60 + (qint64)x.trimmed().toDouble();
	if (p.size() == 1)
		return (qint64)(v.trimmed().toDouble() * 1000.0);
	return s * 1000;
}

/* foto do Programa (o que está saindo), em PNG */
static bool snapshotProgram(const QString &file, QString *err)
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		*err = "vídeo não iniciado";
		return false;
	}
	const uint32_t w = ovi.base_width, h = ovi.base_height;
	QImage img((int)w, (int)h, QImage::Format_RGBA8888);
	bool ok = false;
	obs_enter_graphics();
	gs_texrender_t *tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *ss = gs_stagesurface_create(w, h, GS_RGBA);
	if (tr && ss && gs_texrender_begin(tr, w, h)) {
		vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_render_main_texture();
		gs_blend_state_pop();
		gs_texrender_end(tr);
		gs_stage_texture(ss, gs_texrender_get_texture(tr));
		uint8_t *data = nullptr;
		uint32_t line = 0;
		if (gs_stagesurface_map(ss, &data, &line)) {
			for (uint32_t y = 0; y < h; y++)
				memcpy(img.scanLine((int)y), data + (size_t)y * line, (size_t)w * 4);
			gs_stagesurface_unmap(ss);
			ok = true;
		}
	}
	gs_stagesurface_destroy(ss);
	gs_texrender_destroy(tr);
	obs_leave_graphics();
	if (!ok) {
		*err = "não deu para copiar a imagem";
		return false;
	}
	QDir().mkpath(QFileInfo(file).path());
	if (!img.save(file, "PNG")) {
		*err = "não deu para salvar " + file;
		return false;
	}
	blog(LOG_INFO, "foto do Programa: %s", file.toUtf8().constData());
	return true;
}

static int g_replaySelEvent = -1; /* evento escolhido pela API (índice na lista ativa) */
static float g_masterBeforeMute = -1.0f;

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
	/* variáveis dinâmicas do vMix: Input=Dynamic1, Value=DynamicValue1 */
	for (int i = 0; i < 4; i++) {
		if (p.value("Input").compare(QString("Dynamic%1").arg(i + 1), Qt::CaseInsensitive) == 0)
			p["Input"] = g_dynInput[i];
		if (value.compare(QString("DynamicValue%1").arg(i + 1), Qt::CaseInsensitive) == 0)
			value = g_dynValue[i];
	}
	/* Channel=1/2 do vMix: replay A ou B só nesta função */
	int prevSel = -1;
	if (name.startsWith("Replay", Qt::CaseInsensitive) && p.contains("Channel") && engineAlive()) {
		const int ch = p.value("Channel").toInt();
		if (ch == 1 || ch == 2) {
			prevSel = engine().getSelection();
			engine().setSelection(ch == 1 ? SEL_A : SEL_B);
		}
	}
	g_params = p;
	const bool ok = runFunction(name, p.value("Input"), value, error, parent);
	g_params.clear();
	if (prevSel >= 0 && engineAlive())
		engine().setSelection(prevSel);
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
		const QString spec = inputSpec.trimmed();
		/* chave do vMix desconhecida, mas o nome dela no vMix existe aqui: adota a chave */
		const QString vmixTitle = in ? QString() : vmixTitleOfKey(spec);
		Input *sameName = nullptr; /* existe com o nome, mas já ligada a outra entrada do vMix */
		if (!in && !vmixTitle.isEmpty()) {
			if (Input *byName = inputForVmixTitle(vmixTitle)) {
				if (vmixTitleOfKey(byName->key).isEmpty()) {
					e.setInputKey(byName->id, spec);
					blog(LOG_INFO, "chave do vMix %s dada à entrada \"%s\" (mesmo nome)", spec.toUtf8().constData(),
					     vmixTitle.toUtf8().constData());
					in = byName;
				} else {
					sameName = byName;
				}
			}
		}
		/* StartStopVirtualCam sem entrada = Programa */
		if (!in && !(spec.isEmpty() && QLatin1String(def->name) == QLatin1String("StartStopVirtualCam"))) {
			static const QRegularExpression guid(R"re(^\{?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-)re");
			const QString field = g_params.value("SelectedName");
			if (spec.isEmpty())
				return fail(QString("%1: não há entrada na Prévia").arg(def->name));
			if (sameName) {
				noteMissing(spec, vmixTitle, def->name, field, "nome repetido no vMix");
				return fail(QString("%1: no vMix há mais de uma entrada \"%2\". A \"%3\" do Studio já é a outra. "
						    "Crie mais uma entrada (ex.: \"%2 2\") e cole a chave %4 em ⚙ da entrada → Chave do vMix.")
						    .arg(def->name, vmixTitle, sameName->name, spec));
			}
			if (!vmixTitle.isEmpty()) {
				noteMissing(spec, vmixTitle, def->name, field, "entrada");
				return fail(QString("%1: falta a entrada \"%2\" no Studio. Crie ou importe um título com o nome \"%2\" "
						    "(o mesmo do vMix).%3")
						    .arg(def->name, vmixTitle,
							 field.isEmpty() ? QString() : QString(" O app quer o campo %1.").arg(field)));
			}
			if (guid.match(spec).hasMatch()) {
				noteMissing(spec, QString(), def->name, field, "chave sem nome");
				return fail(QString("%1: o Studio não sabe qual entrada é a chave %2. Faça Entradas → Importar chaves do "
						    "vMix (escolha o data/config.json do app ou o projeto .vmix) ou cole a chave em ⚙ da "
						    "entrada → Chave do vMix.")
						    .arg(def->name, spec));
			}
			noteMissing(spec, spec, def->name, field, "entrada");
			return fail(QString("%1: não existe entrada \"%2\" (nem com esse número)").arg(def->name, spec));
		}
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
			{
				QStringList have;
				for (const GtField &f : title->fields())
					if (f.kind == GtField::Text)
						have << f.id;
				noteMissing(in->key, in->name, "SetText", field, "campo");
				return fail(QString("SetText: o título \"%1\" não tem o campo \"%2\". Campos de texto dele: %3")
						    .arg(in->name, field, have.isEmpty() ? QString("nenhum") : have.join(", ")));
			}
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
	} else if (n == "PTZPreset" || n == "PTZHome") {
		Ptz &pz = Ptz::get();
		const int cam = pz.find(n == "PTZHome" ? value : inputSpec);
		if (cam < 0 || cam >= pz.cams.size())
			return fail("PTZ: câmera não encontrada (aba PTZ)");
		if (n == "PTZHome")
			pz.home(cam);
		else
			pz.preset(cam, std::max(1, (int)v), false);
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
					if (tid && (strstr(tid, "dshow") || strstr(tid, "v4l2") || strstr(tid, "decklink") || strstr(tid, "avcapture")))
						ids << x->id;
				}
			QString err;
			if (!mc.start(ids, err))
				return fail("MultiCorder: " + err);
		}
	} else if (n == "SetImage" || n == "SetTextColour" || n == "SetColor" || n.startsWith("SetTextVisible") ||
		   n.startsWith("SetImageVisible") || n.endsWith("Countdown")) {
		if (!gtIsSource(in->source))
			return fail(QString("%1: \"%2\" não é um título GT").arg(n, in->name));
		QString err;
		GtField f;
		const int kind = n == "SetImage" || n.startsWith("SetImageVisible") ? (int)GtField::Image
				 : n == "SetColor"                                   ? -1
										     : (int)GtField::Text;
		if (!findGtField(in->source, g_params, kind, &f, &err)) {
			noteMissing(in->key, in->name, n, g_params.value("SelectedName"), "campo");
			return fail(QString("%1: título \"%2\": %3").arg(n, in->name, err));
		}
		if (n == "SetImage") {
			const QString val = value.trimmed();
			QString key = f.id;
			if (!key.endsWith(".Source") && !key.endsWith(".Fill.Bitmap"))
				key = f.element + ".Source";
			if (val.startsWith("http://", Qt::CaseInsensitive) || val.startsWith("https://", Qt::CaseInsensitive))
				setGtImageFromUrl(in->id, key, val);
			else
				setGtValue(in->source, key, val);
		} else if (n == "SetTextColour" || n == "SetColor") {
			setGtValue(in->source, f.element + ".Fill.Color", value.trimmed());
		} else if (n.contains("Visible")) {
			const bool on = n.endsWith("On") ? true : n.endsWith("Off") ? false : !elementVisible(in->source, f.element);
			setGtValue(in->source, f.element + ".Visible", on ? "True" : "False");
		} else {
			if (f.kind != GtField::Text)
				return fail(n + ": o campo não é de texto");
			Countdown &c = countdownFor(in, f.id);
			const qint64 now = QDateTime::currentMSecsSinceEpoch();
			if (n == "SetCountdown") {
				c.totalMs = parseDuration(value, &c.hours);
				c.elapsedMs = c.elapsedAtStart = 0;
				c.running = false;
				c.last.clear();
			} else if (n == "StartCountdown") {
				if (!c.running) {
					c.running = true;
					c.startedAt = now;
					c.elapsedAtStart = c.elapsedMs;
				}
			} else if (n == "StopCountdown") {
				c.running = false;
				c.elapsedMs = c.elapsedAtStart = 0;
			} else if (n == "PauseCountdown") {
				if (c.running) {
					c.running = false;
				} else {
					c.running = true;
					c.startedAt = now;
					c.elapsedAtStart = c.elapsedMs;
				}
			} else if (n == "AdjustCountdown") {
				/* + segundos = mais tempo no relógio */
				const qint64 d = (qint64)(v * 1000.0);
				if (c.totalMs > 0)
					c.elapsedMs = std::clamp<qint64>(c.elapsedMs - d, 0, c.totalMs);
				else
					c.elapsedMs = std::max<qint64>(0, c.elapsedMs + d);
				c.elapsedAtStart = c.elapsedMs;
				c.startedAt = now;
			}
			tickCountdowns();
		}
	} else if (n == "Play" || n == "Pause" || n == "PlayPause" || n == "Restart" || n == "SetPosition" ||
		   n == "NextItem" || n == "PreviousItem") {
		if (!(obs_source_get_output_flags(in->source) & OBS_SOURCE_CONTROLLABLE_MEDIA))
			return fail(QString("%1: \"%2\" não é vídeo/lista").arg(n, in->name));
		const obs_media_state st = obs_source_media_get_state(in->source);
		if (n == "Play")
			obs_source_media_play_pause(in->source, false);
		else if (n == "Pause")
			obs_source_media_play_pause(in->source, true);
		else if (n == "PlayPause")
			obs_source_media_play_pause(in->source, st == OBS_MEDIA_STATE_PLAYING);
		else if (n == "Restart")
			obs_source_media_restart(in->source);
		else if (n == "SetPosition")
			obs_source_media_set_time(in->source, (int64_t)v);
		else if (n == "NextItem")
			obs_source_media_next(in->source);
		else
			obs_source_media_previous(in->source);
	} else if (n == "LoopOn" || n == "LoopOff") {
		e.setInputLoop(in->id, n == "LoopOn");
	} else if (n.startsWith("SetDynamicInput") || n.startsWith("SetDynamicValue")) {
		const int k = n.right(1).toInt() - 1;
		if (k < 0 || k > 3)
			return fail(n + ": 1 a 4");
		(n.startsWith("SetDynamicInput") ? g_dynInput : g_dynValue)[k] = value;
	} else if (n == "Merge") {
		if (v > 0)
			e.setTransitionMs((int)v);
		e.transition(TransitionKind::Merge);
	} else if (n.startsWith("SetTransitionDuration") && n.size() > 21) {
		const int k = n.right(1).toInt();
		if (StudioTransition *t = e.nthTransition(k, false))
			e.setUserTransitionMs(t->id, (int)v);
		else
			e.setTransitionMs((int)v);
	} else if (n.startsWith("OverlayInput") && n.endsWith("Off")) {
		const int ch = n.mid(12, 1).toInt();
		if (e.overlay(ch)) {
			/* "Off" do vMix: sai na hora, sem a animação de saída */
			auto keep = Engine::beforeOverlayOff;
			Engine::beforeOverlayOff = nullptr;
			e.toggleOverlay(ch, e.overlay(ch));
			Engine::beforeOverlayOff = keep;
		}
	} else if (n == "SetZoom" || n == "SetZoomX" || n == "SetZoomY" || n == "SetPanX" || n == "SetPanY" ||
		   n == "SetRotation" || n == "SetCropX1" || n == "SetCropX2" || n == "SetCropY1" || n == "SetCropY2" ||
		   n == "SetCrop" || n == "ResetPosition") {
		ItemPos p = in->pos;
		if (n == "SetZoom")
			p.zoom = std::clamp(v, 0.0, 5.0);
		else if (n == "SetZoomX")
			p.zoomX = std::clamp(v, 0.0, 5.0);
		else if (n == "SetZoomY")
			p.zoomY = std::clamp(v, 0.0, 5.0);
		else if (n == "SetPanX")
			p.panX = std::clamp(v, -2.0, 2.0);
		else if (n == "SetPanY")
			p.panY = std::clamp(v, -2.0, 2.0);
		else if (n == "SetRotation")
			p.rotation = std::clamp(v, -360.0, 360.0);
		else if (n == "SetCropX1")
			p.cropX1 = std::clamp(v, 0.0, 1.0);
		else if (n == "SetCropX2")
			p.cropX2 = std::clamp(v, 0.0, 1.0);
		else if (n == "SetCropY1")
			p.cropY1 = std::clamp(v, 0.0, 1.0);
		else if (n == "SetCropY2")
			p.cropY2 = std::clamp(v, 0.0, 1.0);
		else if (n == "ResetPosition")
			p = ItemPos();
		else {
			const QStringList parts = value.split(',');
			if (parts.size() != 4)
				return fail("SetCrop: Value = X1,Y1,X2,Y2 (de 0 a 1), ex.: 0.25,0,0.75,1");
			double c[4];
			for (int i = 0; i < 4; i++) {
				bool ok = false;
				c[i] = std::clamp(parts[i].trimmed().toDouble(&ok), 0.0, 1.0);
				if (!ok)
					return fail(QString("SetCrop: \"%1\" não é número").arg(parts[i]));
			}
			p.cropX1 = c[0];
			p.cropY1 = c[1];
			p.cropX2 = c[2];
			p.cropY2 = c[3];
		}
		e.setInputPosition(in->id, p);
	} else if (n == "SetLayer" || n == "SetMultiViewOverlay" || n == "SetLayerZoom" || n == "SetLayerPanX" ||
		   n == "SetLayerPanY" || n == "SetLayerBehind") {
		const int comma = value.indexOf(',');
		bool okNum = false;
		const int idx = value.left(comma < 0 ? value.size() : comma).trimmed().toInt(&okNum);
		if (!okNum || idx < 1 || idx > INPUT_LAYERS)
			return fail(QString("%1: Value começa com o número da camada (1 a %2), ex.: 1,PLACAR").arg(n).arg(INPUT_LAYERS));
		const QString rest = comma < 0 ? QString() : value.mid(comma + 1).trimmed();
		if (n == "SetLayer" || n == "SetMultiViewOverlay") {
			int target = 0;
			const QString up = rest.toUpper();
			if (up == "PGM" || up == "PROGRAMA" || up == "PROGRAM" || up == "OUTPUT") {
				e.setLayer(in->id, idx - 1, PROGRAM_LAYER); /* o Programa direto como camada */
				return true;
			}
			if (!rest.isEmpty()) {
				Input *li = resolveInput(rest);
				if (!li)
					return fail(QString("%1: não existe entrada \"%2\"").arg(n, rest));
				if (e.layerWouldLoop(in->id, li->id))
					return fail(QString("%1: \"%2\" já mostra \"%3\" dentro dela; uma não pode ficar dentro da outra")
							    .arg(n, li->name, in->name));
				target = li->id;
			}
			e.setLayer(in->id, idx - 1, target);
		} else if (n == "SetLayerBehind") {
			const QString r = rest.toLower();
			const bool behind = rest.isEmpty() ? !in->layers[idx - 1].behind
							   : (r == "1" || r == "true" || r == "on" || r == "atras" || r == "atrás");
			e.setLayerBehind(in->id, idx - 1, behind);
		} else {
			bool okV = false;
			const double lv = rest.toDouble(&okV);
			if (!okV)
				return fail(QString("%1: Value = número,valor (ex.: 1,0.5)").arg(n));
			ItemPos p = in->layers[idx - 1].pos;
			if (n == "SetLayerZoom")
				p.zoom = std::clamp(lv, 0.0, 5.0);
			else if (n == "SetLayerPanX")
				p.panX = std::clamp(lv, -2.0, 2.0);
			else
				p.panY = std::clamp(lv, -2.0, 2.0);
			e.setLayerPosition(in->id, idx - 1, p);
		}
	} else if (n == "MultiViewOverlay" || n == "MultiViewOverlayOn" || n == "MultiViewOverlayOff") {
		const int idx = (int)v;
		if (idx < 1 || idx > INPUT_LAYERS)
			return fail(QString("%1: Value = número da camada (1 a %2)").arg(n).arg(INPUT_LAYERS));
		const bool on = n == "MultiViewOverlayOn" || (n == "MultiViewOverlay" && !in->layers[idx - 1].on);
		e.setLayerOn(in->id, idx - 1, on);
	} else if (n == "SetVolumeFade") {
		const QStringList parts = value.split(',');
		const double target = std::clamp(parts.value(0).trimmed().toDouble(), 0.0, 100.0);
		const int ms = std::max(1, parts.value(1, "1000").trimmed().toInt());
		const int id = in->id;
		const double from = std::cbrt(std::max(0.0f, in->volume)) * 100.0;
		auto *t = new QTimer(QCoreApplication::instance());
		auto *clock = new QElapsedTimer;
		clock->start();
		QObject::connect(t, &QTimer::timeout, t, [t, clock, id, from, target, ms] {
			const double k = std::min(1.0, clock->elapsed() / (double)ms);
			const double vol = from + (target - from) * k;
			Engine::get().setInputVolume(id, (float)std::pow(vol / 100.0, 3.0));
			if (k >= 1.0 || !Engine::get().input(id)) {
				t->stop();
				t->deleteLater();
				delete clock;
			}
		});
		t->start(20);
	} else if (n == "MasterAudio" || n == "MasterAudioON" || n == "MasterAudioOFF") {
		const bool muted = g_masterBeforeMute >= 0.0f;
		const bool wantOn = n == "MasterAudioON" || (n == "MasterAudio" && muted);
		if (wantOn && muted) {
			e.setMasterVolume(g_masterBeforeMute);
			g_masterBeforeMute = -1.0f;
		} else if (!wantOn && !muted) {
			g_masterBeforeMute = e.masterVolume();
			e.setMasterVolume(0.0f);
		}
	} else if (n == "StartStopExternal" || n == "Fullscreen" || n == "FullscreenOn" || n == "FullscreenOff") {
		const bool open = ExternalOutput::programOpen();
		const bool want = n == "FullscreenOn" || ((n == "StartStopExternal" || n == "Fullscreen") && !open);
		if (want)
			ExternalOutput::openProgram();
		else
			ExternalOutput::closeProgram();
	} else if (n == "Snapshot") {
		QString dir = Outputs::get().settings().recFolder;
		if (dir.isEmpty())
			dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/ReplayDeck Studio";
		QString err;
		const QString file =
			QDir(dir).filePath("Foto " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss-zzz") + ".png");
		if (!snapshotProgram(file, &err))
			return fail("Snapshot: " + err);
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
		if (!t && st)
			return fail(QString("%1: não existe — crie na aba Transições").arg(n));
		if (t)
			e.transitionWith(t->id);
		else
			e.transition(TransitionKind::Fade); /* o vMix sempre tem as 4: sem uma criada, fade */
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
		else if (n == "ReplaySetSpeed") {
			/* vMix: Value de -1 a 1 (0.5 = 50%); aqui também aceita 0 a 100 */
			double pct = std::fabs(v) <= 2.0 ? v * 100.0 : v;
			if (pct < 0) {
				r.setReverse(true);
				pct = -pct;
			} else if (r.reverseOn() && std::fabs(v) <= 2.0) {
				r.setReverse(false);
			}
			r.setSpeedPercent((int)std::clamp(pct, 0.0, 100.0));
		} else if (n == "ReplayChangeSpeed") {
			const double d = std::fabs(v) <= 2.0 ? v * 100.0 : v;
			r.setSpeedPercent((int)std::clamp(r.speedPercent() + d, 0.0, 100.0));
		} else if (n == "ReplayRecorded")
			r.pause();
		else if (n == "ReplayLiveToggle") {
			if (r.speedPercent() > 0 && !air)
				r.pause();
			else
				r.goLive();
		} else if (n == "ReplayJumpToNow")
			r.goLive();
		else if (n == "ReplayMarkInLive") {
			r.goLive();
			r.markIn();
		} else if (n == "ReplayMarkInOutLive")
			r.autoEvent(v > 0 ? (int)v : 10);
		else if (n == "ReplayFastBackward")
			r.shuttle(-1, v > 0 ? v : 4.0);
		else if (n == "ReplayStopEvents") {
			if (air && air->onAir())
				air->abortRun();
			r.pause();
		} else if (n == "ReplayStartRecording" || n == "ReplayStopRecording") {
			if (n == "ReplayStopRecording")
				r.stopRecording();
			else if (!r.recording()) {
				std::string err;
				if (!r.startRecording(err))
					return fail(QString::fromStdString(err));
			}
		} else if (n == "ReplayShowHide") {
			if (air)
				air->onAir() ? air->takeOut() : air->takeIn();
		} else if (n == "ReplayPlayLastEventToOutput") {
			if (air && !air->onAir())
				air->toggleLast();
		} else if (n == "ReplayPlayAllEventsToOutput") {
			if (air && !air->onAir())
				air->toggleEvents(0);
		} else if (n == "ReplayPlaySelectedEventToOutput" || n == "ReplayPlaySelectedEvent") {
			const auto evs = r.events(r.activeList());
			if (evs.empty())
				return fail("Replay: sem eventos");
			const int i = g_replaySelEvent < 0 || g_replaySelEvent >= (int)evs.size() ? (int)evs.size() - 1 : g_replaySelEvent;
			if (n == "ReplayPlaySelectedEvent")
				r.playEvent(r.activeList(), evs[(size_t)i].id);
			else if (air && !air->onAir())
				air->toggleEvents(evs[(size_t)i].id);
		} else if (n.startsWith("ReplaySelect") && n.endsWith("Event")) {
			const int count = (int)r.events(r.activeList()).size();
			if (count == 0)
				return fail("Replay: sem eventos");
			int i = g_replaySelEvent < 0 || g_replaySelEvent >= count ? count - 1 : g_replaySelEvent;
			if (n == "ReplaySelectFirstEvent")
				i = 0;
			else if (n == "ReplaySelectLastEvent")
				i = count - 1;
			else if (n == "ReplaySelectNextEvent")
				i = std::min(count - 1, i + 1);
			else
				i = std::max(0, i - 1);
			g_replaySelEvent = i;
		} else if (n.startsWith("ReplaySelectEvents")) {
			r.setActiveList(n.mid(18).toInt() - 1);
			g_replaySelEvent = -1;
		} else if (n.startsWith("ReplayACamera") || n.startsWith("ReplayBCamera")) {
			const int cam = n.right(1).toInt() - 1;
			const int prev = r.getSelection();
			r.setSelection(n[6] == 'A' ? SEL_A : SEL_B);
			r.setAngle(cam);
			r.setSelection(prev);
		}
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
	if (def && def->hold && QLatin1String(def->name).startsWith(QLatin1String("PTZHold"))) {
		Ptz &pz = Ptz::get();
		const int cam = pz.find(value);
		if (cam < 0 || cam >= pz.cams.size())
			return false;
		const QString n = QString::fromLatin1(def->name);
		if (n.endsWith("ZoomIn") || n.endsWith("ZoomOut"))
			pz.zoom(cam, pressed ? (n.endsWith("ZoomIn") ? 1 : -1) : 0);
		else if (!pressed)
			pz.stop(cam);
		else
			pz.move(cam, n.endsWith("Left") ? -1 : n.endsWith("Right") ? 1 : 0,
				n.endsWith("Up") ? 1 : n.endsWith("Down") ? -1 : 0);
		return true;
	}
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
