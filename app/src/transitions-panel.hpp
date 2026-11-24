/* Aba "Transições": stingers (vídeo) e outras transições (deslizar, varrer, luma...).
 * Cada uma vira um botão no meio da tela, abaixo do Fade, e as funções Stinger1.. / Transition1.. */
#pragma once

#include <QWidget>

class QVBoxLayout;

class TransitionsPanel : public QWidget {
	Q_OBJECT

public:
	explicit TransitionsPanel(QWidget *parent = nullptr);

public slots:
	void addStinger();
	void addGtStinger();
	void addOther(const QString &type);

private:
	void rebuild();
	QWidget *card(int id);
	void openOptions(int id);
	QVBoxLayout *listLayout = nullptr;
};

/* duração de um vídeo em ms (0 se não deu para ler) */
int videoDurationMs(const QString &file);
