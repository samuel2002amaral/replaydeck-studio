#pragma once
#include "gt-doc.hpp"

/* Aplica os valores do editor a um elemento (texto, cor, imagem). */
GtElement gtApplyValues(const GtElement &e, const GtValues &v);

/* Posição/visibilidade de uma camada com os valores do editor ("Layer:NOME.X"...) */
QRectF gtLayerRect(const GtLayer &L, const GtValues &v);
bool gtLayerVisible(const GtLayer &L, const GtValues &v);
double gtLayerRotation(const GtLayer &L, const GtValues &v);

/* Desenha um elemento no tamanho dele (pixels do título), alfa pré-multiplicado. */
QImage gtRenderElement(const GtTitle &t, const GtElement &e);

/* Desenha o título parado (estado final), para prévias. */
QImage gtRenderStill(const GtTitle &t, const GtValues &v);

/* curvas de aceleração do vMix: Linear, CubicEasingInOut, BackEasingOut... */
double gtEase(const QString &name, double p);
