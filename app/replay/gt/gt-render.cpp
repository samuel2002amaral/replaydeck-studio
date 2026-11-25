#include "gt-render.hpp"
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

GtElement gtApplyValues(const GtElement &src, const GtValues &v)
{
	GtElement e = src;
	if (e.name.isEmpty() || v.isEmpty())
		return e;
	auto val = [&](const QString &suffix, QString &out) {
		auto it = v.find(e.name + suffix);
		if (it == v.end())
			return false;
		out = it.value();
		return true;
	};
	QString s;
	if (e.kind == GtKind::Text && val(".Text", s))
		e.text = s;
	if (val(".Fill.Color", s)) {
		e.fill.color = gtColor(s);
		if (e.fill.kind != GtBrush::Gradient)
			e.fill.kind = GtBrush::Solid;
	}
	/* "res:" = imagem de dentro do .gtzip (cópia de um elemento); senão, arquivo */
	auto imgKey = [](const QString &s) {
		return s.isEmpty() ? QString() : s.startsWith("res:") ? s.mid(4) : s.startsWith("file:") ? s : "file:" + s;
	};
	if (e.kind == GtKind::Image && val(".Source", s))
		e.image = imgKey(s);
	if ((e.kind == GtKind::Rectangle || e.kind == GtKind::Ellipse) && val(".Fill.Bitmap", s)) {
		e.fill.kind = s.isEmpty() ? GtBrush::Solid : GtBrush::Bitmap;
		e.fill.bitmap = imgKey(s);
	}

	/* layout e estilo (aba Layout do editor) */
	auto num = [&](const QString &suffix, double &out) {
		QString t;
		if (!val(suffix, t))
			return;
		bool ok = false;
		const double d = t.toDouble(&ok);
		if (ok)
			out = d;
	};
	double x = e.rect.x(), y = e.rect.y(), w = e.rect.width(), h = e.rect.height();
	num(".X", x);
	num(".Y", y);
	num(".Width", w);
	num(".Height", h);
	e.rect = QRectF(x, y, std::max(1.0, w), std::max(1.0, h));
	num(".Opacity", e.opacity);
	num(".Rotation", e.rotation);
	if (val(".Visible", s))
		e.visible = s != "False";
	num(".StrokeThickness", e.strokeThickness);
	if (val(".Stroke.Color", s)) {
		e.stroke.color = gtColor(s);
		e.stroke.kind = GtBrush::Solid;
	}
	num(".RadiusX", e.radiusX);
	if (val(".RadiusX", s))
		e.radiusY = e.radiusX;
	if (e.kind == GtKind::Text) {
		if (val(".FontFamily", s) && !s.isEmpty())
			e.fontFamily = s;
		num(".FontSize", e.fontSize);
		if (val(".FontWeight", s))
			e.fontWeight = s;
		if (val(".FontStyle", s))
			e.fontStyle = s;
		if (val(".TextAlign", s))
			e.align = s;
		if (val(".VerticalAlign", s))
			e.valign = s;
		num(".LineSpacing", e.lineSpacing);
		if (e.lineSpacing <= 0)
			e.lineSpacing = 1;
	}
	if (e.kind == GtKind::Image && val(".Stretch", s))
		e.imageStretch = s;
	return e;
}

QRectF gtLayerRect(const GtLayer &L, const GtValues &v)
{
	const QString p = "Layer:" + L.name;
	auto num = [&](const QString &suffix, double def) {
		bool ok = false;
		const double d = v.value(p + suffix).toDouble(&ok);
		return v.contains(p + suffix) && ok ? d : def;
	};
	return QRectF(num(".X", L.rect.x()), num(".Y", L.rect.y()), std::max(1.0, num(".Width", L.rect.width())),
		      std::max(1.0, num(".Height", L.rect.height())));
}

double gtLayerRotation(const GtLayer &L, const GtValues &v)
{
	const QString k = "Layer:" + L.name + ".Rotation";
	bool ok = false;
	const double d = v.value(k).toDouble(&ok);
	return v.contains(k) && ok ? d : L.rotation;
}

bool gtLayerVisible(const GtLayer &L, const GtValues &v)
{
	const QString k = "Layer:" + L.name + ".Visible";
	return v.contains(k) ? v.value(k) != "False" : L.visible;
}

/* ---------- curvas ---------- */

static double easeIn(const QString &fam, double t)
{
	const double PI = 3.14159265358979323846;
	if (fam == "Quadratic")
		return t * t;
	if (fam == "Cubic")
		return t * t * t;
	if (fam == "Quartic")
		return t * t * t * t;
	if (fam == "Quintic")
		return t * t * t * t * t;
	if (fam == "Sine")
		return 1 - std::cos(t * PI / 2);
	if (fam == "Exponential")
		return t <= 0 ? 0 : std::pow(2, 10 * (t - 1));
	if (fam == "Circular")
		return 1 - std::sqrt(std::max(0.0, 1 - t * t));
	if (fam == "Back") {
		const double s = 1.70158;
		return t * t * ((s + 1) * t - s);
	}
	if (fam == "Elastic") {
		if (t <= 0 || t >= 1)
			return t;
		return -std::pow(2, 10 * (t - 1)) * std::sin((t - 1.075) * (2 * PI) / 0.3);
	}
	if (fam == "Bounce") {
		auto out = [](double x) {
			if (x < 1 / 2.75)
				return 7.5625 * x * x;
			if (x < 2 / 2.75) {
				x -= 1.5 / 2.75;
				return 7.5625 * x * x + 0.75;
			}
			if (x < 2.5 / 2.75) {
				x -= 2.25 / 2.75;
				return 7.5625 * x * x + 0.9375;
			}
			x -= 2.625 / 2.75;
			return 7.5625 * x * x + 0.984375;
		};
		return 1 - out(1 - t);
	}
	return t; /* Linear */
}

double gtEase(const QString &name, double p)
{
	p = std::clamp(p, 0.0, 1.0);
	static const QRegularExpression re("^(.*?)Easing(InOut|In|Out)$");
	const auto m = re.match(name);
	if (!m.hasMatch())
		return p;
	const QString fam = m.captured(1);
	const QString mode = m.captured(2);
	if (mode == "In")
		return easeIn(fam, p);
	if (mode == "Out")
		return 1 - easeIn(fam, 1 - p);
	return p < 0.5 ? easeIn(fam, p * 2) / 2 : 1 - easeIn(fam, (1 - p) * 2) / 2;
}

/* ---------- desenho ---------- */

static QBrush makeBrush(const GtTitle &t, const GtBrush &b, const QRectF &r)
{
	switch (b.kind) {
	case GtBrush::Solid:
		return QBrush(b.color);
	case GtBrush::Gradient: {
		QLinearGradient g(QPointF(r.left() + b.start.x() * r.width(), r.top() + b.start.y() * r.height()),
				  QPointF(r.left() + b.end.x() * r.width(), r.top() + b.end.y() * r.height()));
		for (auto &s : b.stops)
			g.setColorAt(std::clamp(s.offset, 0.0, 1.0), s.color);
		return QBrush(g);
	}
	case GtBrush::Bitmap: {
		QImage img = t.image(b.bitmap);
		if (img.isNull())
			return QBrush(b.color);
		/* preenchimento esticado no tamanho da forma */
		QImage scaled = img.scaled(std::max(1, (int)std::lround(r.width())), std::max(1, (int)std::lround(r.height())),
					   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		QBrush br(scaled);
		QTransform tr;
		tr.translate(r.left(), r.top());
		br.setTransform(tr);
		return br;
	}
	default:
		return QBrush(Qt::NoBrush);
	}
}

static int qtWeight(const QString &w)
{
	const QString s = w.toLower();
	if (s.contains("thin"))
		return QFont::Thin;
	if (s.contains("extralight") || s.contains("ultralight"))
		return QFont::ExtraLight;
	if (s.contains("light"))
		return QFont::Light;
	if (s.contains("medium"))
		return QFont::Medium;
	if (s.contains("semibold") || s.contains("demibold"))
		return QFont::DemiBold;
	if (s.contains("extrabold") || s.contains("ultrabold"))
		return QFont::ExtraBold;
	if (s.contains("black") || s.contains("heavy"))
		return QFont::Black;
	if (s.contains("bold"))
		return QFont::Bold;
	return QFont::Normal;
}

static QStringList textLines(QString text)
{
	/* o GT mistura \r\n, \n e \r sozinho como quebra de linha */
	text.replace("\r\n", "\n");
	text.replace('\r', '\n');
	while (text.endsWith('\n'))
		text.chop(1);
	return text.split('\n');
}

static void drawText(QPainter &p, const GtTitle &t, const GtElement &e, const QRectF &box)
{
	QFont f(e.fontFamily);
	f.setPixelSize(std::max(1, (int)std::lround(e.fontSize)));
	f.setWeight((QFont::Weight)qtWeight(e.fontWeight));
	f.setItalic(e.fontStyle.compare("Italic", Qt::CaseInsensitive) == 0);
	f.setHintingPreference(QFont::PreferNoHinting);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	/* algarismos de largura fixa: listas de números alinham como no vMix */
	f.setFeature(QFont::Tag("tnum"), 1);
#endif
	const QFontMetricsF fm(f);

	const QStringList lines = textLines(e.text);
	const double lineH = fm.height() * e.lineSpacing;
	const double total = lineH * lines.size() - (lineH - fm.height());

	double y = box.top();
	if (e.valign == "Center")
		y = box.top() + (box.height() - total) / 2;
	else if (e.valign == "Bottom")
		y = box.bottom() - total;

	/* TAB como no WPF/vMix: paradas a cada 4 × o tamanho da fonte
	 * (ex.: escalação "01<TAB>NOME" alinha os nomes numa coluna) */
	const double tabW = std::max(1.0, 4.0 * f.pixelSize());
	auto layoutLine = [&](const QString &ln, std::vector<std::pair<double, QString>> *segs) {
		double pos = 0;
		const QStringList parts = ln.split('\t');
		for (int i = 0; i < parts.size(); i++) {
			if (segs && !parts[i].isEmpty())
				segs->push_back({pos, parts[i]});
			pos += fm.horizontalAdvance(parts[i]);
			if (i + 1 < parts.size())
				pos = (std::floor(pos / tabW + 1e-6) + 1) * tabW;
		}
		return pos;
	};
	QPainterPath path;
	for (const QString &ln : lines) {
		std::vector<std::pair<double, QString>> segs;
		const double w = layoutLine(ln, &segs);
		double x = box.left();
		if (e.align == "Center")
			x = box.left() + (box.width() - w) / 2;
		else if (e.align == "Right")
			x = box.right() - w;
		for (const auto &sg : segs)
			path.addText(QPointF(x + sg.first, y + fm.ascent()), f, sg.second);
		y += lineH;
	}
	if (e.strokeThickness > 0 && e.stroke.kind != GtBrush::None) {
		QPen pen(makeBrush(t, e.stroke, box), e.strokeThickness * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		p.strokePath(path, pen);
	}
	p.fillPath(path, makeBrush(t, e.fill, box));
}

static void applyCrop(QImage &img, const GtElement &e)
{
	const QRectF c = e.crop;
	const bool full = c.left() <= 0 && c.top() <= 0 && c.right() >= 1 && c.bottom() >= 1;
	const bool feather = e.feather.left() > 0 || e.feather.top() > 0 || e.feather.right() > 0 || e.feather.bottom() > 0;
	if (full && !feather)
		return;
	const int W = img.width(), H = img.height();
	const double L = c.left() * W, T = c.top() * H, R = c.right() * W, B = c.bottom() * H;
	auto ramp = [](double d, double f) { return f <= 0 ? (d >= 0 ? 1.0 : 0.0) : std::clamp(d / f, 0.0, 1.0); };
	for (int y = 0; y < H; y++) {
		uchar *row = img.scanLine(y);
		const double ay = std::min(ramp(y + 0.5 - T, e.feather.top()), ramp(B - (y + 0.5), e.feather.bottom()));
		for (int x = 0; x < W; x++) {
			const double ax = std::min(ramp(x + 0.5 - L, e.feather.left()), ramp(R - (x + 0.5), e.feather.right()));
			const double a = std::min(ax, ay);
			if (a >= 1)
				continue;
			uchar *px = row + x * 4; /* RGBA pré-multiplicado */
			for (int k = 0; k < 4; k++)
				px[k] = (uchar)std::lround(px[k] * a);
		}
	}
}

QImage gtRenderElement(const GtTitle &t, const GtElement &e)
{
	const int w = std::max(1, (int)std::ceil(e.rect.width()));
	const int h = std::max(1, (int)std::ceil(e.rect.height()));
	QImage img(w, h, QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
	const QRectF box(0, 0, e.rect.width(), e.rect.height());

	switch (e.kind) {
	case GtKind::Rectangle:
	case GtKind::Ellipse: {
		QPainterPath shape;
		if (e.kind == GtKind::Ellipse)
			shape.addEllipse(box);
		else if (e.radiusX > 0 || e.radiusY > 0)
			shape.addRoundedRect(box, e.radiusX, e.radiusY > 0 ? e.radiusY : e.radiusX);
		else
			shape.addRect(box);
		p.fillPath(shape, makeBrush(t, e.fill, box));
		if (e.strokeThickness > 0 && e.stroke.kind != GtBrush::None) {
			QPen pen(makeBrush(t, e.stroke, box), e.strokeThickness);
			pen.setJoinStyle(Qt::MiterJoin);
			p.strokePath(shape, pen);
		}
		break;
	}
	case GtKind::Image: {
		const QImage src = t.image(e.image);
		if (!src.isNull()) {
			QSizeF s = src.size();
			if (e.imageStretch == "Fill")
				s = box.size();
			else if (e.imageStretch == "UniformToFill")
				s.scale(box.size(), Qt::KeepAspectRatioByExpanding);
			else if (e.imageStretch != "None")
				s.scale(box.size(), Qt::KeepAspectRatio);
			const QRectF dst((box.width() - s.width()) / 2, (box.height() - s.height()) / 2, s.width(), s.height());
			p.drawImage(dst, src);
		}
		break;
	}
	case GtKind::Text:
		drawText(p, t, e, box);
		break;
	default:
		break;
	}
	p.end();
	applyCrop(img, e);
	return img;
}

QImage gtRenderStill(const GtTitle &t, const GtValues &v)
{
	QImage canvas(t.width, t.height, QImage::Format_RGBA8888_Premultiplied);
	canvas.fill(Qt::transparent);
	QPainter p(&canvas);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	for (auto &L : t.layers) {
		if (!gtLayerVisible(L, v))
			continue;
		const QRectF lr = gtLayerRect(L, v);
		/* a camada é desenhada à parte (recorta o que sai dela) e depois girada */
		QImage layer((int)std::ceil(lr.width()), (int)std::ceil(lr.height()), QImage::Format_RGBA8888_Premultiplied);
		layer.fill(Qt::transparent);
		{
			QPainter lp(&layer);
			lp.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
			for (auto &src : L.elements) {
				const GtElement e = gtApplyValues(src, v);
				if (!e.visible)
					continue;
				lp.save();
				lp.setOpacity(e.opacity);
				const QPointF c = e.rect.center();
				lp.translate(c);
				lp.rotate(e.rotation);
				lp.translate(-e.rect.width() / 2, -e.rect.height() / 2);
				lp.drawImage(QPointF(0, 0), gtRenderElement(t, e));
				lp.restore();
			}
		}
		p.save();
		p.setRenderHint(QPainter::Antialiasing);
		p.translate(lr.center());
		p.rotate(gtLayerRotation(L, v));
		p.translate(-lr.width() / 2, -lr.height() / 2);
		p.drawImage(QPointF(0, 0), layer);
		p.restore();
	}
	return canvas;
}

/* ---------------- montar o título no editor ---------------- */

QString gtKindName(GtKind k)
{
	switch (k) {
	case GtKind::Text:
		return "Text";
	case GtKind::Image:
		return "Image";
	case GtKind::Rectangle:
		return "Rectangle";
	case GtKind::Ellipse:
		return "Ellipse";
	default:
		return "Other";
	}
}

GtKind gtKindFromName(const QString &s)
{
	if (s == "Text")
		return GtKind::Text;
	if (s == "Image")
		return GtKind::Image;
	if (s == "Rectangle")
		return GtKind::Rectangle;
	if (s == "Ellipse")
		return GtKind::Ellipse;
	return GtKind::Other;
}

GtElement gtNewElement(GtKind kind, const QString &name, int W, int H)
{
	GtElement e;
	e.kind = kind;
	e.name = name;
	switch (kind) {
	case GtKind::Text:
		e.tag = "TextBlock";
		e.rect = QRectF(W * 0.1, H * 0.42, W * 0.5, H * 0.12);
		e.text = "Texto";
		e.fontFamily = "Arial";
		e.fontSize = std::max(12.0, H * 0.06);
		e.fontWeight = "Bold";
		e.fill.kind = GtBrush::Solid;
		e.fill.color = Qt::white;
		e.align = "Left";
		e.valign = "Center";
		break;
	case GtKind::Image:
		e.tag = "Image";
		e.rect = QRectF(W * 0.1, H * 0.1, W * 0.25, H * 0.3);
		e.imageStretch = "Uniform";
		break;
	case GtKind::Ellipse:
		e.tag = "Ellipse";
		e.rect = QRectF(W * 0.45, H * 0.4, H * 0.2, H * 0.2);
		e.fill.kind = GtBrush::Solid;
		e.fill.color = QColor(245, 158, 11);
		break;
	default:
		e.kind = GtKind::Rectangle;
		e.tag = "Rectangle";
		e.rect = QRectF(W * 0.1, H * 0.72, W * 0.45, H * 0.12);
		e.fill.kind = GtBrush::Solid;
		e.fill.color = QColor(30, 58, 138);
		break;
	}
	return e;
}

QStringList gtAnimChoices()
{
	return {"None", "Fade", "FlyLeft", "FlyRight", "FlyTop", "FlyBottom", "Zoom"};
}

static bool isStructural(const QString &k)
{
	return k.startsWith("+L:") || k.startsWith("+E:") || k.endsWith(".Removed") || k.endsWith(".Order") ||
	       k.endsWith(".AnimIn") || k.endsWith(".AnimOut");
}

static void setAnim(GtTitle &t, const QString &sbType, const QString &object, const QString &choice, double dur,
		    double delay)
{
	GtStoryboard &sb = t.storyboards[sbType];
	sb.type = sbType;
	sb.anims.erase(std::remove_if(sb.anims.begin(), sb.anims.end(), [&](const GtAnim &a) { return a.object == object; }),
		       sb.anims.end());
	if (choice.isEmpty() || choice == "None")
		return;
	GtAnim a;
	a.object = object;
	a.duration = dur;
	a.delay = delay;
	a.interpolation = sbType == "TransitionIn" ? "CubicEasingOut" : "CubicEasingIn";
	if (choice.startsWith("Fly")) {
		a.type = "FlyFade";
		a.direction = choice.mid(3);
	} else if (choice == "Zoom") {
		a.type = "ZoomFade";
	} else {
		a.type = "Fade";
	}
	sb.anims.push_back(a);
}

std::shared_ptr<GtTitle> gtStructured(const std::shared_ptr<GtTitle> &base, const GtValues &v)
{
	if (!base)
		return base;
	bool any = false;
	for (auto it = v.begin(); it != v.end() && !any; ++it)
		any = isStructural(it.key());
	if (!any)
		return base;
	std::shared_ptr<GtTitle> t = base->clone();
	auto layerOf = [&t](const QString &n) -> GtLayer * {
		for (auto &L : t->layers)
			if (L.name == n)
				return &L;
		return nullptr;
	};
	auto hasElement = [&t](const QString &n) {
		for (auto &L : t->layers)
			for (auto &e : L.elements)
				if (e.name == n)
					return true;
		return false;
	};

	/* camadas novas, na ordem em que foram criadas */
	std::vector<std::pair<int, QString>> newLayers;
	std::vector<std::tuple<int, QString, QString, QString>> newElements; /* ordem, nome, tipo, camada */
	for (auto it = v.begin(); it != v.end(); ++it) {
		if (it.key().startsWith("+L:"))
			newLayers.push_back({it.value().toInt(), it.key().mid(3)});
		else if (it.key().startsWith("+E:")) {
			const QStringList p = it.value().split('|');
			newElements.push_back({p.value(2).toInt(), it.key().mid(3), p.value(0), p.value(1)});
		}
	}
	std::sort(newLayers.begin(), newLayers.end());
	for (auto &nl : newLayers) {
		if (nl.second.isEmpty() || layerOf(nl.second))
			continue;
		GtLayer L;
		L.name = nl.second;
		L.rect = QRectF(0, 0, t->width, t->height);
		t->layers.push_back(L);
	}
	std::sort(newElements.begin(), newElements.end());
	for (auto &ne : newElements) {
		const QString &name = std::get<1>(ne);
		if (name.isEmpty() || hasElement(name))
			continue;
		GtLayer *L = layerOf(std::get<3>(ne));
		if (!L) {
			if (t->layers.empty()) {
				GtLayer nl;
				nl.name = "Camada 1";
				nl.rect = QRectF(0, 0, t->width, t->height);
				t->layers.push_back(nl);
			}
			L = &t->layers.back();
		}
		L->elements.push_back(gtNewElement(gtKindFromName(std::get<2>(ne)), name, t->width, t->height));
	}

	/* apagados */
	t->layers.erase(std::remove_if(t->layers.begin(), t->layers.end(),
				       [&v](const GtLayer &L) { return v.value("Layer:" + L.name + ".Removed") == "True"; }),
			t->layers.end());
	for (auto &L : t->layers)
		L.elements.erase(std::remove_if(L.elements.begin(), L.elements.end(),
						[&v](const GtElement &e) {
							return !e.name.isEmpty() && v.value(e.name + ".Removed") == "True";
						}),
				 L.elements.end());

	/* ordem dentro da camada */
	for (auto &L : t->layers) {
		const QString ord = v.value("Layer:" + L.name + ".Order");
		if (ord.isEmpty())
			continue;
		const QStringList names = ord.split(',');
		std::stable_sort(L.elements.begin(), L.elements.end(), [&names](const GtElement &a, const GtElement &b) {
			const int ia = names.indexOf(a.name), ib = names.indexOf(b.name);
			return (ia < 0 ? 1 << 20 : ia) < (ib < 0 ? 1 << 20 : ib);
		});
	}

	/* animações escolhidas no editor */
	for (auto &L : t->layers)
		for (auto &e : L.elements) {
			if (e.name.isEmpty())
				continue;
			const double dur = v.value(e.name + ".AnimDuration", "0.5").toDouble();
			const double delay = v.value(e.name + ".AnimDelay", "0").toDouble();
			if (v.contains(e.name + ".AnimIn"))
				setAnim(*t, "TransitionIn", e.name, v.value(e.name + ".AnimIn"), dur, delay);
			if (v.contains(e.name + ".AnimOut"))
				setAnim(*t, "TransitionOut", e.name, v.value(e.name + ".AnimOut"), dur, 0);
		}
	return t;
}

QString gtFreeName(const GtTitle &t, const GtValues &v, const QString &prefix)
{
	for (int i = 1;; i++) {
		const QString n = prefix + QString::number(i);
		bool used = v.contains("+E:" + n) || v.contains("+L:" + n);
		for (auto &L : t.layers) {
			used = used || L.name == n;
			for (auto &e : L.elements)
				used = used || e.name == n;
		}
		if (!used)
			return n;
	}
}
