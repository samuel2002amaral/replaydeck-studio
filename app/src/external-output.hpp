/* Saída externa: o Programa (ou a Prévia, o Replay, uma entrada) em tela cheia noutro monitor/TV.
 * Serve para telão, monitor de retorno e o monitor do VAR (Replay A/B para o árbitro). */
#pragma once

#include <QWidget>

class QMenu;
class QScreen;
class VideoDisplay;

class ExternalOutput : public QWidget {
	Q_OBJECT

public:
	enum class What { Program, Preview, Input };
	ExternalOutput(What what, int inputId, QScreen *screen);
	~ExternalOutput() override;
	QString description() const;

	/* menu do botão Externo (o que mostrar e em qual tela) */
	static void fillMenu(QMenu *menu, QWidget *parent);
	static void closeAll();
	static int openCount();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	void refresh();
	What what;
	int id;
	VideoDisplay *video = nullptr;
};
