/* "Interagir", como no OBS: usar a página de uma entrada web (navegador) com o mouse e o
 * teclado — clicar em botões, rolar, digitar num placar online, fazer login... */
#pragma once

#include <obs.hpp>

#include <QWidget>

class VideoDisplay;

class InteractWindow : public QWidget {
	Q_OBJECT

public:
	explicit InteractWindow(obs_source_t *source, const QString &title, QWidget *parent = nullptr);
	static bool canInteract(obs_source_t *source);
	static void open(obs_source_t *source, const QString &title, QWidget *parent);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	bool toSource(const QPointF &p, int *x, int *y) const;
	OBSWeakSourceAutoRelease weak;
	VideoDisplay *view = nullptr;
};
