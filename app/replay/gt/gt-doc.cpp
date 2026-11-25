#include "gt-doc.hpp"
#include "i18n.hpp"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>

extern "C" {
#include "miniz.h"
}

const GtNode *GtNode::child(const QString &t) const
{
	for (auto &k : kids)
		if (k->tag == t)
			return k.get();
	return nullptr;
}

const GtNode *GtNode::path(const QString &p) const
{
	const GtNode *n = this;
	for (const QString &part : p.split('/')) {
		n = n->child(part);
		if (!n)
			return nullptr;
	}
	return n;
}

double GtStoryboard::length() const
{
	double l = 0;
	for (auto &a : anims)
		l = std::max(l, a.delay + a.duration);
	return l;
}

QColor gtColor(const QString &s)
{
	QString h = s.trimmed();
	if (h.startsWith('#'))
		h = h.mid(1);
	bool ok = false;
	const uint v = h.toUInt(&ok, 16);
	if (!ok)
		return QColor(Qt::transparent);
	if (h.size() == 8)
		return QColor((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff, (v >> 24) & 0xff);
	if (h.size() == 6)
		return QColor((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
	return QColor(Qt::transparent);
}

QString gtColorString(const QColor &c)
{
	return QString("#%1%2%3%4")
		.arg(c.alpha(), 2, 16, QChar('0'))
		.arg(c.red(), 2, 16, QChar('0'))
		.arg(c.green(), 2, 16, QChar('0'))
		.arg(c.blue(), 2, 16, QChar('0'))
		.toUpper();
}

static QString g_xmlError;

/* O GT Designer às vezes grava encoding="utf-16" no cabeçalho de um arquivo que
 * na verdade está em UTF-8. Confere o conteúdo e corrige o cabeçalho. */
static QByteArray fixEncoding(const QByteArray &in)
{
	const bool bom16 = in.size() >= 2 && (((uchar)in[0] == 0xFF && (uchar)in[1] == 0xFE) ||
					       ((uchar)in[0] == 0xFE && (uchar)in[1] == 0xFF));
	const bool zeros = in.left(200).contains('\0');
	if (bom16 || zeros)
		return in; /* UTF-16 de verdade */
	const int end = in.indexOf("?>");
	if (end < 0 || end > 200)
		return in;
	QByteArray head = in.left(end);
	const QByteArray low = head.toLower();
	const int enc = low.indexOf("encoding");
	if (enc < 0 || !low.mid(enc).contains("utf-16"))
		return in;
	head = low.left(enc) + "encoding=\"utf-8\"";
	return head + in.mid(end);
}

static std::shared_ptr<GtNode> parseXml(const QByteArray &raw)
{
	g_xmlError.clear();
	const QByteArray data = fixEncoding(raw);
	QXmlStreamReader xr(data);
	std::vector<std::shared_ptr<GtNode>> stack;
	std::shared_ptr<GtNode> root;
	while (!xr.atEnd()) {
		xr.readNext();
		if (xr.isStartElement()) {
			auto n = std::make_shared<GtNode>();
			n->tag = xr.name().toString();
			for (const auto &a : xr.attributes())
				n->attrs.insert(a.name().toString(), a.value().toString());
			if (!stack.empty())
				stack.back()->kids.push_back(n);
			else
				root = n;
			stack.push_back(n);
		} else if (xr.isEndElement()) {
			if (!stack.empty())
				stack.pop_back();
		}
	}
	if (xr.hasError())
		g_xmlError = QString("%1 (linha %2)").arg(xr.errorString()).arg(xr.lineNumber());
	return root;
}

/* procura a <Composition> principal em qualquer nível do XML */
static std::shared_ptr<GtNode> findComposition(const std::shared_ptr<GtNode> &n)
{
	if (!n)
		return nullptr;
	if (n->tag == "Composition")
		return n;
	for (auto &k : n->kids)
		if (auto c = findComposition(k))
			return c;
	return nullptr;
}

static QStringList nums(const QString &s)
{
	return s.split(',', Qt::SkipEmptyParts);
}

static QRectF rectOf(const GtNode &n)
{
	const QStringList d = nums(n.attr("Dimensions", "0,0"));
	const QStringList l = nums(n.attr("Location", "0,0"));
	return QRectF(l.value(0).toDouble(), l.value(1).toDouble(), d.value(0).toDouble(), d.value(1).toDouble());
}

static QPointF pointOf(const QString &s, QPointF def)
{
	const QStringList p = nums(s);
	if (p.size() < 2)
		return def;
	return QPointF(p[0].toDouble(), p[1].toDouble());
}

/* "Rotation" pode vir como "45" ou "x,y,z" (o giro na tela é o z) */
static double rotationOf(const GtNode &n)
{
	const QStringList r = n.attr("Rotation", "0").split(',', Qt::SkipEmptyParts);
	if (r.isEmpty())
		return 0;
	return (r.size() >= 3 ? r[2] : r.last()).toDouble();
}

static GtBrush brushOf(const GtNode *holder)
{
	GtBrush b;
	const GtNode *br = holder ? holder->child("Brush") : nullptr;
	if (!br)
		return b;
	b.color = gtColor(br->attr("Color", "#00000000"));
	b.kind = GtBrush::Solid;
	b.start = pointOf(br->attr("StartPoint"), b.start);
	b.end = pointOf(br->attr("EndPoint"), b.end);
	b.stretch = br->attr("Stretch", "Fill");
	const QString type = br->attr("Type");
	if (type == "Bitmap") {
		if (const GtNode *bm = br->path("Brush.Bitmap/Bitmap")) {
			b.bitmap = bm->attr("Source");
			if (!b.bitmap.isEmpty())
				b.kind = GtBrush::Bitmap;
		}
	} else if (type.contains("Gradient")) {
		if (const GtNode *gs = br->child("Brush.GradientStops")) {
			for (auto &k : gs->kids)
				b.stops.push_back({k->attr("Offset", "0").toDouble(), gtColor(k->attr("Color"))});
		}
		/* sem paradas declaradas: cor -> transparente */
		if (b.stops.empty()) {
			const QColor c2 = gtColor(br->attr("Color2", br->attr("EndColor", "#00000000")));
			b.stops = {{0, b.color}, {1, c2}};
		}
		b.kind = GtBrush::Gradient;
	}
	return b;
}

static GtElement elementOf(const std::shared_ptr<GtNode> &n)
{
	GtElement e;
	e.node = n;
	e.tag = n->tag;
	e.name = n->attr("Name");
	e.rect = rectOf(*n);
	e.opacity = n->attr("Opacity", "1").toDouble();
	e.visible = n->attr("Visible", "True") != "False";
	e.strokeThickness = n->attr("StrokeThickness", "0").toDouble();
	e.radiusX = n->attr("RadiusX", n->attr("CornerRadius", "0")).toDouble();
	e.radiusY = n->attr("RadiusY", n->attr("CornerRadius", "0")).toDouble();
	e.rotation = rotationOf(*n);
	const QString t = n->tag;
	e.fill = brushOf(n->child(t + ".Fill"));
	e.stroke = brushOf(n->child(t + ".Stroke"));
	if (t == "Rectangle")
		e.kind = GtKind::Rectangle;
	else if (t == "Ellipse")
		e.kind = GtKind::Ellipse;
	else if (t == "TextBlock" || t == "Text")
		e.kind = GtKind::Text;
	else if (t == "Image")
		e.kind = GtKind::Image;

	if (e.kind == GtKind::Text) {
		e.text = n->attr("Text");
		e.fontFamily = n->attr("FontFamily", "Arial");
		e.fontSize = n->attr("FontSize", "20").toDouble();
		e.fontWeight = n->attr("FontWeight", "Regular");
		e.fontStyle = n->attr("FontStyle");
		e.align = n->attr("TextAlign", "Left");
		e.valign = n->attr("VerticalAlign", "Top");
		e.lineSpacing = n->attr("LineSpacing", "1").toDouble();
		if (e.lineSpacing <= 0)
			e.lineSpacing = 1;
	}
	if (e.kind == GtKind::Image) {
		if (const GtNode *bm = n->path("Image.Bitmap/Bitmap"))
			e.image = bm->attr("Source");
		e.imageStretch = n->attr("Stretch", "Uniform");
	}
	if (const GtNode *c = n->path(t + ".Crop/Crop")) {
		const QStringList r = nums(c->attr("Range", "0,0,1,1"));
		if (r.size() == 4) {
			const double l = r[0].toDouble(), tp = r[1].toDouble(), rr = r[2].toDouble(), b = r[3].toDouble();
			e.crop = QRectF(QPointF(l, tp), QPointF(rr, b));
		}
		const QStringList f = nums(c->attr("Feather", "0,0,0,0"));
		if (f.size() == 4)
			e.feather = QMarginsF(f[0].toDouble(), f[1].toDouble(), f[2].toDouble(), f[3].toDouble());
	}
	return e;
}

bool GtTitle::load(const QString &p, QString &err)
{
	path = p;
	name = QFileInfo(p).completeBaseName();
	files.clear();
	resources.clear();
	layers.clear();
	storyboards.clear();
	{
		std::lock_guard<std::mutex> lk(cacheMtx);
		imageCache.clear();
	}

	QFile f(p);
	if (!f.open(QIODevice::ReadOnly)) {
		err = tl("não foi possível abrir ") + p;
		return false;
	}
	const QByteArray zipData = f.readAll();
	mz_zip_archive zip;
	memset(&zip, 0, sizeof(zip));
	if (mz_zip_reader_init_mem(&zip, zipData.constData(), (size_t)zipData.size(), 0)) {
		const mz_uint count = mz_zip_reader_get_num_files(&zip);
		for (mz_uint i = 0; i < count; i++) {
			mz_zip_archive_file_stat st;
			if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
				continue;
			size_t size = 0;
			void *data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
			if (!data)
				continue;
			files[QString::fromUtf8(st.m_filename)] = QByteArray((const char *)data, (qsizetype)size);
			mz_free(data);
		}
		mz_zip_reader_end(&zip);
	} else if (!zipData.contains("<Composition") && !zipData.contains("<\0C\0o\0m\0p")) {
		err = tl("arquivo .gtzip inválido");
		return false;
	}

	/* .gtxml solto (sem zip) também vale */
	if (files.empty() && (zipData.contains("<Composition") || zipData.contains("<\0C\0o\0m\0p")))
		files["document.xml"] = zipData;

	auto docIt = files.find("document.xml");
	if (docIt == files.end()) {
		/* nome diferente: usa o primeiro .xml/.gtxml que tenha uma <Composition> */
		for (auto it = files.begin(); it != files.end(); ++it) {
			const QString n = it->first.toLower();
			if ((n.endsWith(".xml") || n.endsWith(".gtxml")) && n != "resources.xml" && n != "[content_types].xml" &&
			    findComposition(parseXml(it->second))) {
				docIt = it;
				break;
			}
		}
	}
	if (docIt == files.end()) {
		QStringList names;
		for (auto &kv : files)
			if (!kv.first.contains(QRegularExpression("^[0-9a-f]{8}-")))
				names << kv.first;
		err = tl("o .gtzip não tem document.xml (arquivos: %1)").arg(names.join(", "));
		return false;
	}

	if (auto resIt = files.find("resources.xml"); resIt != files.end()) {
		/* <resource><source guid="arquivo no zip">caminho</source></resource> */
		QXmlStreamReader xr(fixEncoding(resIt->second));
		QString guid;
		while (!xr.atEnd()) {
			xr.readNext();
			if (xr.isStartElement() && xr.name() == QLatin1String("source")) {
				guid = xr.attributes().value("guid").toString();
				const QString txt = xr.readElementText().trimmed();
				if (!guid.isEmpty() && !txt.isEmpty())
					resources[txt] = guid;
			}
		}
	}

	auto top = parseXml(docIt->second);
	root = findComposition(top);
	if (!root) {
		QString head = QString::fromUtf8(docIt->second.left(300)).simplified();
		err = tl("%1 sem <Composition> — raiz: <%2>%3 — início: %4")
			      .arg(docIt->first)
			      .arg(top ? top->tag : QString("nenhuma"))
			      .arg(g_xmlError.isEmpty() ? QString() : QString::fromUtf8(" — erro XML: ") + g_xmlError)
			      .arg(head.left(160));
		return false;
	}
	width = root->attr("Width", "1920").toInt();
	height = root->attr("Height", "1080").toInt();

	for (auto &k : root->kids) {
		if (k->tag == "Layer") {
			GtLayer L;
			L.name = k->attr("Name");
			L.rect = rectOf(*k);
			L.rotation = rotationOf(*k);
			L.visible = k->attr("Visible", "True") != "False";
			if (const GtNode *comp = k->path("Layer.Composition/Composition")) {
				for (auto &e : comp->kids)
					L.elements.push_back(elementOf(e));
			}
			layers.push_back(L);
		} else if (k->tag == "Storyboard") {
			GtStoryboard sb;
			sb.type = k->attr("Type", "TransitionIn");
			if (const GtNode *an = k->child("Storyboard.Animations")) {
				for (auto &a : an->kids) {
					GtAnim A;
					A.type = a->tag;
					A.object = a->attr("Object");
					A.delay = a->attr("Delay", "0").toDouble();
					A.duration = a->attr("Duration", "0.5").toDouble();
					A.interpolation = a->attr("Interpolation", "Linear");
					A.direction = a->attr("Direction");
					A.attrs = a->attrs;
					sb.anims.push_back(A);
				}
			}
			storyboards[sb.type] = sb;
		} else if (k->tag == "Rectangle" || k->tag == "TextBlock" || k->tag == "Image" || k->tag == "Ellipse") {
			/* elemento solto, fora de camada: vira uma camada do tamanho da tela */
			GtLayer L;
			L.name = k->attr("Name");
			L.rect = QRectF(0, 0, width, height);
			L.elements.push_back(elementOf(k));
			layers.push_back(L);
		}
	}
	return true;
}

std::shared_ptr<GtTitle> GtTitle::clone() const
{
	auto t = std::make_shared<GtTitle>();
	t->path = path;
	t->name = name;
	t->width = width;
	t->height = height;
	t->layers = layers;
	t->storyboards = storyboards;
	t->root = root;
	t->files = files; /* QByteArray: dados compartilhados, sem copiar */
	t->resources = resources;
	return t;
}

QByteArray GtTitle::resourceBytes(const QString &key) const
{
	if (key.startsWith("file:")) {
		QFile f(key.mid(5));
		if (f.open(QIODevice::ReadOnly))
			return f.readAll();
		return QByteArray();
	}
	auto r = resources.find(key);
	const QString fname = r != resources.end() ? r->second : key;
	auto it = files.find(fname);
	return it != files.end() ? it->second : QByteArray();
}

QString GtTitle::resourceFileName(const QString &key) const
{
	if (key.startsWith("file:"))
		return key.mid(5);
	QString n = key;
	n.replace('\\', '/');
	return n.section('/', -1);
}

QImage GtTitle::image(const QString &key) const
{
	if (key.isEmpty())
		return QImage();
	std::lock_guard<std::mutex> lk(cacheMtx);
	auto it = imageCache.find(key);
	if (it != imageCache.end())
		return it->second;
	QImage img;
	img.loadFromData(resourceBytes(key));
	if (!img.isNull())
		img = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
	imageCache[key] = img;
	return img;
}

std::vector<GtField> GtTitle::fields() const
{
	std::vector<GtField> out;
	for (auto &L : layers) {
		for (auto &e : L.elements) {
			if (e.name.isEmpty())
				continue;
			if (e.kind == GtKind::Text) {
				out.push_back({e.name + ".Text", GtField::Text, e.name, L.name, e.text});
				out.push_back({e.name + ".Fill.Color", GtField::Color, e.name, L.name, gtColorString(e.fill.color)});
			} else if (e.kind == GtKind::Image) {
				out.push_back({e.name + ".Source", GtField::Image, e.name, L.name, e.image});
			} else if (e.kind == GtKind::Rectangle || e.kind == GtKind::Ellipse) {
				if (e.fill.kind == GtBrush::Bitmap)
					out.push_back({e.name + ".Fill.Bitmap", GtField::Image, e.name, L.name, e.fill.bitmap});
				else
					out.push_back({e.name + ".Fill.Color", GtField::Color, e.name, L.name,
						       gtColorString(e.fill.color)});
			}
		}
	}
	return out;
}

QStringList GtTitle::pages() const
{
	QStringList p;
	for (auto &kv : storyboards)
		if (kv.first != "TransitionIn" && kv.first != "TransitionOut")
			p << kv.first;
	return p;
}
