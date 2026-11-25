/* Abas Posição e Camadas da entrada, como no vMix. Tudo vale na hora (ao vivo). */
#pragma once

#include "engine.hpp"

#include <QSize>
#include <QWidget>
#include <functional>

class QDoubleSpinBox;
class QSlider;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QPushButton;

/* Ampliação, Zoom X/Y, Pan X/Y, giro e corte. Emite changed() a cada mexida. */
class PositionEditor : public QWidget {
	Q_OBJECT

public:
	/* sourceSize: tamanho da imagem (para o corte em pixels); vazio = tamanho da tela */
	explicit PositionEditor(std::function<QSize()> sourceSize, QWidget *parent = nullptr);
	void setPos(const ItemPos &p); /* sem emitir changed */
	ItemPos pos() const { return cur; }

signals:
	void changed(const ItemPos &p);

private:
	struct Row {
		QSlider *slider = nullptr;
		QDoubleSpinBox *spin = nullptr;
		double *value = nullptr;
		double def = 0;
		double scale = 1; /* slider é inteiro: valor * scale */
	};
	Row addRow(QGridLayout *g, int r, const QString &label, double *value, double min, double max, double def,
		   int decimals, double sliderScale, const QString &tip = QString());
	void refresh();
	void emitChanged();
	void applyPreset(double zoom, double panX, double panY);
	QSize imageSize() const;

	std::function<QSize()> sourceSize;
	ItemPos cur;
	QList<Row> rows;
	/* corte: mostrado em pixels da imagem, guardado em frações */
	double cropPx[4] = {0, 0, 0, 0};
	QList<Row> cropRows;
	bool updating = false;
};

/* 10 camadas: outra entrada por cima desta, cada uma com a sua posição (Editar). */
class LayersEditor : public QWidget {
	Q_OBJECT

public:
	explicit LayersEditor(int inputId, QWidget *parent = nullptr);

private:
	void fillCombos();
	void refresh();
	void editLayer(int index);
	void applyLayout(int kind);

	int id;
	QCheckBox *onBox[INPUT_LAYERS] = {};
	QComboBox *combo[INPUT_LAYERS] = {};
	QPushButton *editBtn[INPUT_LAYERS] = {};
	QPushButton *behindBtn[INPUT_LAYERS] = {};
	bool updating = false;
};
