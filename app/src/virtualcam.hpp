/* Câmera virtual: uma entrada (ou o Programa) vira uma webcam para outros programas
 * (Zoom, navegador, WhatsApp, Teams). No Windows aparece como "ReplayDeck Studio". */
#pragma once

#include <obs.hpp>

#include <QObject>
#include <QString>

class QMenu;
class QWidget;

class VirtualCam : public QObject {
	Q_OBJECT

public:
	static VirtualCam &get();

	bool available() const; /* o motor tem a saída da câmera virtual */
	bool active() const;
	int inputId() const { return id; } /* 0 = Programa */
	QString description() const;

	/* id 0 = Programa; muda de entrada sem desligar a câmera */
	bool start(int inputId, QWidget *parent, QString &error);
	void stop();
	void shutdown();

	/* menu do botão: o que compartilhar */
	void fillMenu(QMenu *menu, QWidget *parent);

signals:
	void stateChanged();

private:
	VirtualCam() = default;
	bool ensureInstalled(QWidget *parent, QString &error); /* Windows: registra uma vez (pede permissão) */
	void setViewSource();

	OBSOutputAutoRelease output;
	obs_view_t *view = nullptr;
	video_t *viewVideo = nullptr;
	int id = 0;
};
