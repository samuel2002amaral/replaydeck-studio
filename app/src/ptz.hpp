/* PTZ: câmeras robóticas por rede (VISCA sobre IP: Sony, PTZOptics, Avonic, Tenveo e a
 * maioria das chinesas). Mover segurando, zoom, posições gravadas (presets), casa, foco. */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

class QComboBox;
class QSlider;
class QPushButton;
class QUdpSocket;
class QTcpSocket;

struct PtzCamera {
	QString name, host;
	int port = 52381;
	int protocol = 0; /* 0 = VISCA UDP Sony (52381), 1 = VISCA TCP (5678), 2 = VISCA UDP puro (1259) */
};

class Ptz : public QObject {
	Q_OBJECT

public:
	static Ptz &get();
	void load();
	void save();
	QList<PtzCamera> cams;
	int current = 0; /* câmera escolhida no painel */
	int speed = 12;  /* 1..24 */

	/* dx: -1 esquerda, 1 direita, 0 parado; dy: -1 baixo, 1 cima */
	void move(int cam, int dx, int dy);
	void stop(int cam) { move(cam, 0, 0); }
	void zoom(int cam, int dir); /* 1 aproxima, -1 afasta, 0 para */
	void home(int cam);
	void preset(int cam, int n, bool store);
	void autofocus(int cam);
	int find(const QString &nameOrNumber) const; /* "" = a escolhida */

private:
	void send(int cam, const QByteArray &visca);
	QUdpSocket *udp = nullptr;
	QList<QTcpSocket *> tcp;
	quint32 seq = 0;
};

class PtzPanel : public QWidget {
	Q_OBJECT

public:
	explicit PtzPanel(QWidget *parent = nullptr);

private:
	void refresh();
	bool editCamera(PtzCamera &c);
	QComboBox *camBox = nullptr;
	QSlider *speed = nullptr;
	QPushButton *storeBtn = nullptr;
};
