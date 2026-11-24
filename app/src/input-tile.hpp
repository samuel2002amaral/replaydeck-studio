/* Quadro de uma entrada na lista (estilo vMix): nome, imagem ao vivo e botões. */
#pragma once

#include <QFrame>

class QLabel;
class QPushButton;
class VideoDisplay;

class InputTile : public QFrame {
	Q_OBJECT

public:
	InputTile(int inputId, QWidget *parent = nullptr);
	int inputId() const { return id; }
	void refresh();

signals:
	void openRequested(int id);

protected:
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	int id;
	QWidget *header = nullptr;
	QWidget *expanded = nullptr;
	class CollapsedBar *bar = nullptr;
	int fullHeight = 0;
	int lastCollapsed = -1;
	QLabel *num = nullptr;
	QLabel *name = nullptr;
	VideoDisplay *video = nullptr;
	QPushButton *ovBtn[5] = {};
	QPushButton *audio = nullptr;
	QPushButton *loop = nullptr;
	QString lastBus;
	int lastOv = -1;
};
