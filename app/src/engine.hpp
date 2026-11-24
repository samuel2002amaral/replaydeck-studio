/* Motor do ReplayDeck Studio: tudo que fala com o libobs (o motor de vídeo do OBS).
 * A interface só conversa com esta classe.
 *
 * Modelo (igual ao vMix):
 *  - cada ENTRADA é uma cena do libobs com uma fonte principal (câmera, vídeo, imagem...);
 *  - PRÉVIA e PROGRAMA apontam para entradas;
 *  - o canal de saída 0 é a transição (Cortar, Fade, Stinger...) que mostra o Programa;
 *  - os canais 1 a 4 são os OVERLAYS, desenhados por cima do Programa. */
#pragma once

#include <obs.hpp>

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

struct Input {
	int id = 0; /* único e estável (usado no projeto salvo) */
	QString name;
	OBSSourceAutoRelease scene; /* a cena da entrada */
	OBSSourceAutoRelease source; /* a fonte principal dentro da cena */
	bool loop = false;
	bool audioOn = true;
	/* desativada: só ESTA entrada fica preta; a câmera só desliga quando todas que a usam estão desativadas */
	bool enabled = true;
	/* áudio (como no vMix): On = entra na mistura; Auto = só quando a entrada está no ar
	 * (padrão das entradas com imagem); sem Auto = sempre (microfone) */
	float volume = 1.0f; /* multiplicador (1 = 0 dB) */
	bool audioAuto = true;
	bool monitor = false; /* ouvir no fone (saída de monitoramento) */
	/* aparência na lista (como no vMix): cor do cabeçalho e minimizada em barra */
	QString color; /* chave de inputColors() ("" = padrão) */
	bool collapsed = false;
};

struct InputColor {
	const char *key;
	const char *label;
	const char *hex;
};
const QList<InputColor> &inputColors();
QString inputColorHex(const QString &key); /* cor do cabeçalho (padrão se vazio) */

enum class TransitionKind { Cut, Fade, Stinger, FadeToBlack };

/* transição criada pelo operador (aba Transições): stinger, deslizar, varrer, luma... */
struct StudioTransition {
	int id = 0;
	QString name;
	OBSSourceAutoRelease source;
	int durationMs = 1000; /* ignorada nas de duração fixa (stinger: a duração do vídeo) */
	bool isStinger() const;      /* stinger de vídeo ou GT */
	bool isVideoStinger() const; /* obs_stinger_transition (vídeo) */
	bool fixedDuration() const;
};

class Engine : public QObject {
	Q_OBJECT

public:
	static Engine &get();

	/* inicialização: devolve false com a mensagem em error */
	bool start(QString &error);
	void stop();

	uint32_t baseWidth() const { return baseW; }
	uint32_t baseHeight() const { return baseH; }
	uint32_t fpsNumerator() const { return fpsNum; }
	uint32_t fpsDenominator() const { return fpsDen; }
	/* resolução e quadros por segundo (só sem nada gravando/transmitindo) */
	bool resetVideo(uint32_t w, uint32_t h, uint32_t num, uint32_t den, QString &error);
	QString configDir() const { return cfgDir; }
	QString adapterName() const { return adapterUsed; } /* placa de vídeo em uso */

	/* entradas */
	const QVector<Input *> &inputs() const { return list; }
	Input *input(int id) const;
	int indexOf(int id) const;
	Input *addInput(const QString &sourceType, const QString &name, obs_data_t *settings = nullptr);
	Input *adoptSource(obs_source_t *source, const QString &name); /* fica com a referência */
	/* nova entrada que usa a MESMA fonte de outra (ex.: a mesma câmera: o Windows só deixa abrir uma vez) */
	Input *addSharedInput(int fromId, const QString &name);
	int sharedFrom(int id) const; /* id da primeira entrada com a mesma fonte, ou 0 */
	/* identifica o aparelho de uma fonte (câmera/placa); vazio se não for de aparelho */
	static QString deviceKey(const char *unversionedType, obs_data_t *settings);
	int inputUsingDevice(const char *unversionedType, const QString &key) const;
	/* junta entradas que abrem o mesmo aparelho separadamente numa fonte só (projetos antigos,
	 * ou quando o dispositivo de uma entrada é trocado para um que já está em uso) */
	void mergeSameDevice();
	static std::function<QString(const char *, obs_data_t *)> deviceKeyOverride; /* só para testes */
	void removeInput(int id);
	void renameInput(int id, const QString &name);
	void moveInput(int id, int newIndex);
	void setInputAudio(int id, bool on);
	void setInputLoop(int id, bool on);
	void setInputEnabled(int id, bool on);
	void setInputVolume(int id, float mul);
	void setInputAudioAuto(int id, bool on);
	void setInputMonitor(int id, bool on);
	void setInputColor(int id, const QString &key);
	void setInputCollapsed(int id, bool on);
	float masterVolume() const { return master; }
	void setMasterVolume(float mul);
	static bool hasAudio(const Input *in);
	/* liga/desliga o aparelho conforme as entradas que o usam (ligado se alguma estiver ativa) */
	void syncDevice(obs_source_t *source);
	static bool deviceActive(obs_source_t *source); /* câmera ligada? (true para o que não é câmera) */

	/* prévia e programa */
	int previewId() const { return pvw; }
	int programId() const { return pgm; }
	void setPreview(int id);
	void transition(TransitionKind kind); /* prévia vai para o programa e o programa vira prévia */
	void quickPlay(int id);               /* entrada direto para o programa, com fade */
	void cutTo(int id);                   /* entrada direto para o programa, corte seco */
	/* entrada para o programa com uma transição qualquer (usado pelo replay); ms <= 0 = corte */
	void takeToProgram(int id, obs_source_t *transition, int ms);
	Input *inputByScene(obs_source_t *scene) const;
	Input *inputByName(const QString &name) const;
	obs_source_t *cutTransition() const { return cutTr; }
	obs_source_t *fadeTransition() const { return fadeTr; }
	int transitionMs() const { return durationMs; }
	void setTransitionMs(int ms);
	bool fadedToBlack() const { return ftb; }
	/* T-bar: 0..1 enquanto arrasta; ao soltar, passa da metade = troca, senão volta */
	void tbarMove(float t);
	void tbarRelease(float t);

	/* transições do operador (aba Transições) */
	const QVector<StudioTransition *> &userTransitions() const { return trList; }
	StudioTransition *userTransition(int id) const;
	StudioTransition *addTransition(const QString &type, const QString &name, obs_data_t *settings, int ms = 1000);
	void removeTransition(int id);
	void renameTransition(int id, const QString &name);
	void setUserTransitionMs(int id, int ms);
	void moveTransition(int id, int delta); /* sobe/desce na lista (Transition1, Stinger1... seguem a ordem) */
	void transitionWith(int id); /* prévia para o programa com esta transição */
	StudioTransition *nthTransition(int n, bool stingerOnly) const; /* n a partir de 1 */
	static QString transitionTypeName(const char *type);

	/* overlays 1..4 (por cima do programa) */
	int overlay(int channel) const;
	void toggleOverlay(int channel, int id);

	/* projeto */
	bool saveProject(const QString &path);
	bool loadProject(const QString &path);
	QString projectPath() const { return projPath; }
	QString defaultProjectPath() const;

	static QString typeDisplayName(const char *id);

	/* ganchos (o replay se registra aqui; os testes do motor não usam) */
	static std::function<void()> beforeProjectLoad; /* tipos extras prontos antes de abrir o projeto */
	/* antes de tirar uma entrada de um overlay: devolve quantos ms esperar (ex.: o título GT
	 * anima a saída); 0 = tira na hora */
	static std::function<int(obs_source_t *)> beforeOverlayOff;
	/* plugins do OBS: carregados junto com os módulos do motor */
	static std::function<void()> loadExtraModules;

signals:
	void inputsChanged();
	void inputChanged(int id);
	void busChanged(); /* prévia, programa ou overlays mudaram */
	void inputRenamed(int id, const QString &oldName, const QString &newName);
	void transitionMsChanged();
	void audioChanged(); /* volume geral */
	void videoChanged(); /* resolução / fps */
	void transitionsChanged(); /* lista da aba Transições */

private:
	Engine() = default;
	void setProgramScene(obs_source_t *scene, TransitionKind kind);
	void setProgramSceneWith(obs_source_t *scene, obs_source_t *transition, int ms);
	void applyVolume(Input *in);
	void syncAudioBed();
	void clearAll();
	void clearTransitions();
	obs_source_t *transitionFor(TransitionKind kind);

	uint32_t baseW = 1920, baseH = 1080;
	uint32_t fpsNum = 30, fpsDen = 1;
	QString cfgDir;
	QString adapterWanted, adapterUsed;
	QString projPath;
	QVector<Input *> list;
	int nextId = 1;
	int pvw = 0, pgm = 0;
	int ov[5] = {0, 0, 0, 0, 0};
	unsigned ovGen[5] = {0, 0, 0, 0, 0}; /* muda a cada troca: a saída animada só limpa se nada mudou */
	int durationMs = 500;
	bool ftb = false;
	bool started = false;
	bool manual = false;

	OBSSourceAutoRelease cutTr, fadeTr;
	QVector<StudioTransition *> trList;
	int nextTrId = 1;
	OBSSourceAutoRelease blackScene;
	OBSSourceAutoRelease audioBed; /* canal 5: áudio das entradas "sempre" que não estão no ar */
	float master = 1.0f;
	obs_source_t *activeTr = nullptr;
};
