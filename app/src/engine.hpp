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
};

enum class TransitionKind { Cut, Fade, Stinger, FadeToBlack };

class Engine : public QObject {
	Q_OBJECT

public:
	static Engine &get();

	/* inicialização: devolve false com a mensagem em error */
	bool start(QString &error);
	void stop();

	uint32_t baseWidth() const { return baseW; }
	uint32_t baseHeight() const { return baseH; }
	QString configDir() const { return cfgDir; }

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

signals:
	void inputsChanged();
	void inputChanged(int id);
	void busChanged(); /* prévia, programa ou overlays mudaram */
	void inputRenamed(int id, const QString &oldName, const QString &newName);
	void transitionMsChanged();

private:
	Engine() = default;
	void setProgramScene(obs_source_t *scene, TransitionKind kind);
	void setProgramSceneWith(obs_source_t *scene, obs_source_t *transition, int ms);
	void clearAll();
	obs_source_t *transitionFor(TransitionKind kind);

	uint32_t baseW = 1920, baseH = 1080;
	QString cfgDir;
	QString projPath;
	QVector<Input *> list;
	int nextId = 1;
	int pvw = 0, pgm = 0;
	int ov[5] = {0, 0, 0, 0, 0};
	int durationMs = 500;
	bool ftb = false;
	bool started = false;
	bool manual = false;

	OBSSourceAutoRelease cutTr, fadeTr, stingerTr;
	OBSSourceAutoRelease blackScene;
	obs_source_t *activeTr = nullptr;
};
