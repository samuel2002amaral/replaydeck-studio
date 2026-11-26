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
	enum class What { Program, Preview, Input, Vertical };
	ExternalOutput(What what, int inputId, QScreen *screen);
	~ExternalOutput() override;
	QString description() const;

	/* menu do botão Externo (o que mostrar e em qual tela) */
	static void fillMenu(QMenu *menu, QWidget *parent);
	/* "Tela 1 / Tela 2 / Numa janela" para mostrar isto (botão direito no Programa, na Prévia, na entrada) */
	static void addScreenActions(QMenu *menu, What what, int inputId, QWidget *parent);
	static void closeAll();
	static int openCount();
	/* API (StartStopExternal / Fullscreen): o Programa na outra tela (ou numa janela, se só há uma) */
	static bool programOpen();
	static void openProgram();
	static void closeProgram();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	void refresh();
	void goFullScreen(QScreen *screen, int attempt);
	What what;
	int id;
	VideoDisplay *video = nullptr;
};
