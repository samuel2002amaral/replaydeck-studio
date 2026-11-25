/* Imagem da entrada com molduras, como no OBS: clique para escolher a imagem principal ou uma
 * camada, arraste para mover, puxe os cantos/bordas para mudar o tamanho, Alt + borda corta.
 * A moldura aparece mesmo quando a camada está vazia (replay parado, câmera desligada). */
#pragma once

#include "engine.hpp"
#include "video-display.hpp"

#include <QTimer>

class LayoutView : public VideoDisplay {
	Q_OBJECT

public:
	explicit LayoutView(int inputId, QWidget *parent = nullptr);
	~LayoutView() override;
	int selected() const { return sel; } /* -1 = imagem principal; 0..9 = camada; -2 = nada */
	void select(int which);

	struct Box {
		int which = -1; /* -1 principal, 0..9 camada */
		QString name;
		ItemPos pos;
		uint32_t imgW = 0, imgH = 0;
		bool empty = false; /* sem imagem agora */
		ItemFrame f;
	};
	QList<Box> boxes() const;

signals:
	void selectionChanged(int which);

protected:
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	enum Handle { None, Move, L, R, T, B, TL, TR, BL, BR };
	void rebuild();                    /* molduras a partir da entrada (thread da interface) */
	Handle hit(const Box &b, const QPointF &img) const;
	void setPos(int which, const ItemPos &p);
	void dragTo(const QPointF &img, Qt::KeyboardModifiers mods);
	float canvasW() const { return (float)Engine::get().baseWidth(); }
	float canvasH() const { return (float)Engine::get().baseHeight(); }

	int id;
	int sel = -2;
	QList<Box> cache; /* de baixo para cima */
	/* arrasto */
	Handle drag = None;
	int dragWhich = -2;
	QPointF dragStart;
	ItemPos startPos;
	ItemFrame startFrame;
	uint32_t dragImgW = 0, dragImgH = 0;
	QTimer *timer = nullptr;
	struct LayoutViewShared *shared = nullptr; /* molduras para a thread de vídeo */
};
