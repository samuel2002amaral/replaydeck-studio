/* Playlist, como no vMix: uma sequência de entradas que vão ao ar sozinhas, uma depois da
 * outra (vídeos até o fim, imagens/câmeras pelo tempo escolhido), com a transição escolhida.
 * Ex.: intervalo com patrocinadores, vinhetas, replays de gols. */
#pragma once

#include <obs.hpp>

#include <QList>
#include <QObject>
#include <QTimer>
#include <QWidget>

class QTableWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

struct PlaylistItem {
	int inputId = 0;
	int seconds = 0; /* 0 = vídeo até o fim (outros tipos: 10 s) */
};

class Playlist : public QObject {
	Q_OBJECT

public:
	static Playlist &get();
	void load();
	void save();
	QList<PlaylistItem> items;
	bool loop = false;
	int transitionId = -1; /* -1 = Fade, 0 = Corte, >0 = da aba Transições */

	bool start();
	void stop();
	void next();
	bool active() const { return running; }
	int currentIndex() const { return running ? idx : -1; }

signals:
	void stateChanged();

private:
	void play(int i);
	static void mediaEnded(void *data, calldata_t *cd);
	void watch(obs_source_t *src);
	bool running = false;
	int idx = -1;
	QTimer timer;
	OBSWeakSourceAutoRelease watched;
};

class PlaylistPanel : public QWidget {
	Q_OBJECT

public:
	explicit PlaylistPanel(QWidget *parent = nullptr);

private:
	void rebuild();
	void pull(); /* tabela -> lista */
	QTableWidget *table = nullptr;
	QCheckBox *loop = nullptr;
	QComboBox *trans = nullptr;
	QPushButton *go = nullptr;
	QLabel *state = nullptr;
	bool filling = false;
};
