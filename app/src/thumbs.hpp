/* Miniaturas das entradas (o "multiview" da lista): desenhadas em resolução baixa, numa
 * passada só da placa de vídeo, e mostradas como imagem comum (fluidas, 30 quadros/s).
 * Antes cada miniatura era uma janela de vídeo própria (swap chain) a 60 quadros/s —
 * o que pesava era a quantidade, não a qualidade. Programa e Prévia continuam como sempre. */
#pragma once

#include <QImage>
#include <QWidget>

#include <obs.h>

class QTimer;

class ThumbView : public QWidget {
	Q_OBJECT

public:
	explicit ThumbView(QWidget *parent = nullptr);
	~ThumbView() override;
	void showSource(obs_source_t *source);
	void clear();
	void setImage(QImage img); /* chamado pelo hub (thread da interface) */

	/* quadros por segundo das miniaturas (padrão 30) */
	static void setFps(int fps);
	static int fps();
	/* resolução em % do tamanho na tela (padrão 50 = metade) */
	static void setScale(int pct);
	static int scale();
	static void shutdown(); /* antes de desligar o motor */
	static int resetDelivered(); /* teste: imagens entregues desde a última chamada */

signals:
	void clicked();
	void doubleClicked();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;

private:
	void updateActive();
	QImage img;
	QTimer *gate = nullptr;
	bool hasSource = false;
};
