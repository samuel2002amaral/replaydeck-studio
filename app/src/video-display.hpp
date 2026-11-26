/* Widget que mostra vídeo do motor (prévia, programa, miniaturas das entradas).
 * Desenha direto na placa de vídeo, como o OBS e o vMix. */
#pragma once

#include <obs.hpp>

#include <QWidget>

#include <functional>
#include <mutex>

class VideoDisplay : public QWidget {
	Q_OBJECT

public:
	enum class Mode { Source, Program };

	explicit VideoDisplay(QWidget *parent = nullptr);
	~VideoDisplay() override;

	/* o que mostrar: uma fonte (prévia/miniatura) ou a saída do programa */
	void showSource(obs_source_t *src);
	/* desenhar a fonte como se tivesse este tamanho (cena vertical 9:16); vazio = o tamanho dela */
	void setCanvasSize(const QSize &s)
	{
		std::lock_guard<std::mutex> l(m);
		forced = s;
	}
	void showProgram();
	void clear();
	/* cor fora da imagem (0xAABBGGRR); padrão = a cor do painel */
	void setBackgroundColor(uint32_t abgr);
	/* desenho extra por cima da imagem (thread de vídeo), em coordenadas da imagem;
	 * pixel = quanto vale 1 pixel da tela nessas coordenadas */
	void setOverlay(std::function<void(float w, float h, float pixel)> fn)
	{
		std::lock_guard<std::mutex> l(m);
		overlay = std::move(fn);
	}
	/* ponto do widget -> coordenadas da imagem (com a tarja preta descontada) */
	QPointF toImage(const QPointF &p, float imgW, float imgH) const;
	float imageScale(float imgW, float imgH) const; /* pixels do widget por unidade da imagem */

	QPaintEngine *paintEngine() const override { return nullptr; }

signals:
	void clicked();
	void doubleClicked();

protected:
	void paintEvent(QPaintEvent *) override;
	bool event(QEvent *e) override;
	void resizeEvent(QResizeEvent *) override;
	void showEvent(QShowEvent *) override;
	void hideEvent(QHideEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;

private:
	static void draw(void *data, uint32_t cx, uint32_t cy);
	void createDisplay();
	void destroyDisplay();
	void updateEnabled(); /* só desenha quando aparece na tela */
	static void checkAll();

	obs_display_t *display = nullptr;
	uint32_t background = 0xFF2A211C;
	bool screenHooked = false;
	OBSWeakSourceAutoRelease weak;
	Mode mode = Mode::Source;
	bool hasContent = false;
	bool drawing = true;
	QSize forced;
	std::function<void(float, float, float)> overlay;
	std::mutex m; /* o desenho roda na thread de vídeo */
};
