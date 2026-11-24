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

	QPaintEngine *paintEngine() const override { return nullptr; }

signals:
	void clicked();
	void doubleClicked();

protected:
	void paintEvent(QPaintEvent *) override;
	bool event(QEvent *e) override;
	void resizeEvent(QResizeEvent *) override;
	void showEvent(QShowEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;

private:
	static void draw(void *data, uint32_t cx, uint32_t cy);
	void createDisplay();
	void destroyDisplay();

	obs_display_t *display = nullptr;
	OBSWeakSourceAutoRelease weak;
	Mode mode = Mode::Source;
	bool hasContent = false;
	QSize forced;
	std::mutex m; /* o desenho roda na thread de vídeo */
};
