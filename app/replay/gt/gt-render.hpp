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

/* ---- montar o título no editor (como no GT Designer do vMix) ----
 * Elementos e camadas novos, apagados, a ordem e as animações ficam nos próprios valores:
 *   "+L:NOME" = ordem            camada nova (tela inteira)
 *   "+E:NOME" = Tipo|Camada|n    elemento novo (Text, Image, Rectangle, Ellipse)
 *   "NOME.Removed" = True        elemento apagado ("Layer:NOME.Removed" = camada apagada)
 *   "Layer:NOME.Order" = a,b,c   ordem dos elementos na camada (o último fica por cima)
 *   "NOME.AnimIn" / "NOME.AnimOut" = None, Fade, FlyLeft, FlyRight, FlyTop, FlyBottom, Zoom
 *   "NOME.AnimDuration" / "NOME.AnimDelay" (segundos)
 * Devolve o próprio base quando não há nada disso. */
std::shared_ptr<GtTitle> gtStructured(const std::shared_ptr<GtTitle> &base, const GtValues &v);
GtElement gtNewElement(GtKind kind, const QString &name, int titleW, int titleH);
QString gtKindName(GtKind k);
GtKind gtKindFromName(const QString &s);
/* nome livre: Texto1, Texto2... (olha o título montado e os valores) */
QString gtFreeName(const GtTitle &t, const GtValues &v, const QString &prefix);
/* animações que o editor oferece */
QStringList gtAnimChoices();
