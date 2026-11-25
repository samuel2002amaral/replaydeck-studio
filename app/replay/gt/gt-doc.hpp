#pragma once
/* Leitor de títulos do vMix GT (.gtzip): um zip com document.xml (camadas,
 * elementos, storyboards), resources.xml (caminho -> arquivo no zip) e as
 * imagens. */
#include <QColor>
#include <QImage>
#include <QMap>
#include <QMarginsF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

struct GtNode {
	QString tag;
	QMap<QString, QString> attrs;
	std::vector<std::shared_ptr<GtNode>> kids;

	QString attr(const QString &k, const QString &def = QString()) const { return attrs.value(k, def); }
	const GtNode *child(const QString &tag) const;
	/* caminho "A/B/C" de filhos */
	const GtNode *path(const QString &p) const;
};

struct GtGradientStop {
	double offset;
	QColor color;
};

struct GtBrush {
	enum Kind { None, Solid, Bitmap, Gradient } kind = None;
	QColor color = Qt::transparent;
	QString bitmap; /* chave do recurso (ou arquivo externo, se começar com "file:") */
	QPointF start{0.5, 0}, end{0.5, 1};
	std::vector<GtGradientStop> stops;
	QString stretch; /* Fill (padrão), Uniform, UniformToFill, None */
};

enum class GtKind { Rectangle, Ellipse, Text, Image, Other };

struct GtElement {
	GtKind kind = GtKind::Other;
	QString tag;
	QString name;
	QRectF rect;
	double opacity = 1;
	bool visible = true;
	GtBrush fill, stroke;
	double strokeThickness = 0;
	double radiusX = 0, radiusY = 0;
	double rotation = 0; /* graus, em torno do centro */
	/* texto */
	QString text;
	QString fontFamily = "Arial";
	double fontSize = 20;
	QString fontWeight = "Regular";
	QString fontStyle;
	QString align = "Left", valign = "Top";
	double lineSpacing = 1;
	/* imagem */
	QString image; /* chave do recurso */
	QString imageStretch = "Uniform";
	/* recorte (0..1) e borda suave (px) */
	QRectF crop{0, 0, 1, 1};
	QMarginsF feather;
	std::shared_ptr<GtNode> node;
};

struct GtLayer {
	QString name;
	QRectF rect;
	double rotation = 0;
	bool visible = true;
	std::vector<GtElement> elements;
};

struct GtAnim {
	QString type;   /* Fly, Zoom, ZoomFade, Fade, Wipe... */
	QString object; /* elemento ou camada */
	double delay = 0, duration = 0.5;
	QString interpolation = "Linear";
	QString direction;
	QMap<QString, QString> attrs;
};

struct GtStoryboard {
	QString type; /* TransitionIn, TransitionOut, Page1... */
	std::vector<GtAnim> anims;
	double length() const;
};

/* Campos editáveis, como no vMix: NOME.Text, NOME.Source, NOME.Fill.Bitmap, NOME.Fill.Color */
struct GtField {
	QString id;
	enum Kind { Text, Image, Color } kind;
	QString element;
	QString layer;
	QString value; /* valor original do arquivo */
};

class GtTitle {
public:
	bool load(const QString &path, QString &err);
	/* cópia (camadas, animações, arquivos do zip) para acrescentar elementos sem mexer no original */
	std::shared_ptr<GtTitle> clone() const;

	QString path;
	QString name;
	int width = 1920, height = 1080;
	std::vector<GtLayer> layers;
	std::map<QString, GtStoryboard> storyboards;
	std::shared_ptr<GtNode> root;

	std::vector<GtField> fields() const;
	/* storyboards além de entrada/saída (Page1, Page2...) */
	QStringList pages() const;

	/* imagem de um recurso do zip (ou de um arquivo externo) */
	QImage image(const QString &key) const;
	QByteArray resourceBytes(const QString &key) const;
	QString resourceFileName(const QString &key) const;

private:
	std::map<QString, QByteArray> files;   /* nome no zip -> bytes */
	std::map<QString, QString> resources;  /* caminho do recurso -> nome no zip */
	mutable std::map<QString, QImage> imageCache;
	mutable std::mutex cacheMtx;
};

/* Valores alterados no editor (campo -> valor) */
using GtValues = QMap<QString, QString>;

QColor gtColor(const QString &s);
QString gtColorString(const QColor &c);
