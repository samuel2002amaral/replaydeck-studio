#include "layout-view.hpp"

#include <QKeyEvent>
#include <QMouseEvent>

#include <graphics/matrix4.h>
#include <obs.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
struct DrawBox {
	float cx, cy, hw, hh, rot;
	bool selected, empty;
};
} // namespace

/* o que a thread de vídeo desenha (cópia feita na thread da interface) */
struct LayoutViewShared {
	std::mutex m;
	QList<DrawBox> boxes;
};

static void drawRect(float x, float y, float w, float h)
{
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(w, h, 1.0f);
	gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();
}

static double rad(double deg)
{
	return deg * 3.14159265358979323846 / 180.0;
}

LayoutView::LayoutView(int inputId, QWidget *parent) : VideoDisplay(parent), id(inputId)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::ClickFocus);
	shared = new LayoutViewShared;
	setOverlay([shared = shared](float, float, float pixel) {
		std::lock_guard<std::mutex> l(shared->m);
		if (shared->boxes.isEmpty())
			return;
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
		const float t = 2.0f * pixel;  /* linha de 2 pixels da tela */
		const float hs = 9.0f * pixel; /* alça */
		for (const DrawBox &b : shared->boxes) {
			struct vec4 c;
			if (b.selected)
				vec4_set(&c, 1.0f, 0.18f, 0.18f, 1.0f);
			else if (b.empty)
				vec4_set(&c, 1.0f, 0.78f, 0.1f, 0.95f);
			else
				vec4_set(&c, 0.35f, 0.62f, 1.0f, 0.85f);
			gs_effect_set_vec4(color, &c);
			gs_matrix_push();
			gs_matrix_translate3f(b.cx, b.cy, 0.0f);
			gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, (float)rad(b.rot));
			while (gs_effect_loop(solid, "Solid")) {
				drawRect(-b.hw, -b.hh, 2 * b.hw, t);
				drawRect(-b.hw, b.hh - t, 2 * b.hw, t);
				drawRect(-b.hw, -b.hh, t, 2 * b.hh);
				drawRect(b.hw - t, -b.hh, t, 2 * b.hh);
				if (b.empty) {
					/* vazia agora (replay parado...): um X mostra onde ela vai aparecer */
					gs_render_start(true);
					gs_vertex2f(-b.hw, -b.hh);
					gs_vertex2f(b.hw, b.hh);
					gs_vertex2f(b.hw, -b.hh);
					gs_vertex2f(-b.hw, b.hh);
					gs_render_stop(GS_LINES);
				}
				if (b.selected) {
					const float xs[3] = {-b.hw, 0.0f, b.hw}, ys[3] = {-b.hh, 0.0f, b.hh};
					for (float x : xs)
						for (float y : ys)
							if (x != 0.0f || y != 0.0f)
								drawRect(x - hs / 2, y - hs / 2, hs, hs);
				}
			}
			gs_matrix_pop();
		}
	});
	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &LayoutView::rebuild);
	timer->start(150); /* tamanho das imagens muda sozinho (replay começa, câmera abre) */
	connect(&Engine::get(), &Engine::inputLayoutChanged, this, [this](int changed) {
		if (changed == id)
			rebuild();
	});
	if (Input *in = Engine::get().input(id))
		showSource(in->scene);
	rebuild();
}

LayoutView::~LayoutView()
{
	setOverlay(nullptr); /* espera o desenho em andamento acabar */
	delete shared;
}

QList<LayoutView::Box> LayoutView::boxes() const
{
	QList<Box> out;
	Engine &e = Engine::get();
	Input *in = e.input(id);
	if (!in)
		return out;
	const uint32_t W = e.baseWidth(), H = e.baseHeight();
	Box m;
	m.which = -1;
	m.name = "Imagem principal";
	m.pos = in->pos;
	m.imgW = obs_source_get_width(in->source);
	m.imgH = obs_source_get_height(in->source);
	m.empty = !m.imgW || !m.imgH || !in->enabled;
	m.f = itemFrame(m.pos, m.imgW, m.imgH, W, H);
	/* de baixo para cima: camadas de trás, a principal, camadas da frente */
	QList<Box> front;
	for (int i = 0; i < INPUT_LAYERS; i++) {
		const InputLayer &l = in->layers[i];
		Input *li = e.input(l.inputId);
		if (!li || !l.on)
			continue;
		Box b;
		b.which = i;
		b.name = QString("Camada %1: %2").arg(i + 1).arg(li->name);
		b.pos = l.pos;
		b.imgW = W; /* a camada é a entrada inteira (tamanho da tela) */
		b.imgH = H;
		b.empty = !obs_source_get_width(li->source);
		b.f = itemFrame(b.pos, W, H, W, H);
		if (l.behind)
			b.name += " (atrás)";
		(l.behind ? out : front) << b;
	}
	out << m;
	out << front;
	return out;
}

void LayoutView::rebuild()
{
	if (!isVisible())
		return;
	cache = boxes();
	bool selExists = false;
	for (const Box &b : cache)
		selExists = selExists || b.which == sel;
	if (sel != -2 && !selExists) {
		sel = -2;
		emit selectionChanged(sel);
	}
	QList<DrawBox> draw;
	for (const Box &b : cache) {
		/* a principal só ganha moldura quando escolhida ou quando não ocupa a tela toda */
		if (b.which == -1 && b.which != sel && b.pos.isDefault())
			continue;
		draw << DrawBox{(float)b.f.visCx, (float)b.f.visCy, (float)b.f.visW / 2, (float)b.f.visH / 2, (float)b.pos.rotation,
				b.which == sel, b.empty};
	}
	/* a escolhida por último (por cima das outras) */
	std::stable_sort(draw.begin(), draw.end(), [](const DrawBox &a, const DrawBox &b) { return !a.selected && b.selected; });
	{
		std::lock_guard<std::mutex> l(shared->m);
		shared->boxes = draw;
	}
}

void LayoutView::select(int which)
{
	if (sel == which)
		return;
	sel = which;
	rebuild();
	emit selectionChanged(sel);
}

/* ponto da tela -> coordenadas da moldura (sem o giro), com o centro da parte visível em 0,0 */
static QPointF toLocal(const ItemFrame &f, double rotDeg, const QPointF &p)
{
	const double dx = p.x() - f.visCx, dy = p.y() - f.visCy;
	const double c = std::cos(rad(rotDeg)), s = std::sin(rad(rotDeg));
	return QPointF(c * dx + s * dy, -s * dx + c * dy);
}

LayoutView::Handle LayoutView::hit(const Box &b, const QPointF &img) const
{
	const QPointF q = toLocal(b.f, b.pos.rotation, img);
	const double hw = b.f.visW / 2, hh = b.f.visH / 2;
	const double tol = 8.0 / std::max(0.01f, imageScale(canvasW(), canvasH())); /* 8 pixels da tela */
	const bool nl = std::abs(q.x() + hw) <= tol, nr = std::abs(q.x() - hw) <= tol;
	const bool nt = std::abs(q.y() + hh) <= tol, nb = std::abs(q.y() - hh) <= tol;
	const bool inX = q.x() >= -hw - tol && q.x() <= hw + tol, inY = q.y() >= -hh - tol && q.y() <= hh + tol;
	if (!inX || !inY)
		return None;
	if (b.which == sel) {
		if (nl && nt)
			return TL;
		if (nr && nt)
			return TR;
		if (nl && nb)
			return BL;
		if (nr && nb)
			return BR;
		if (nl)
			return L;
		if (nr)
			return R;
		if (nt)
			return T;
		if (nb)
			return B;
	}
	return Move;
}

void LayoutView::mousePressEvent(QMouseEvent *e)
{
	if (e->button() != Qt::LeftButton) {
		VideoDisplay::mousePressEvent(e);
		return;
	}
	setFocus();
	cache = boxes();
	const QPointF img = toImage(e->position(), canvasW(), canvasH());
	Handle h = None;
	int which = -2;
	/* a escolhida primeiro (dá para mexer na principal mesmo embaixo de uma camada) */
	for (const Box &b : cache)
		if (b.which == sel && (h = hit(b, img)) != None) {
			which = b.which;
			break;
		}
	if (which == -2) {
		for (int i = cache.size() - 1; i >= 0; i--) {
			if ((h = hit(cache[i], img)) != None) {
				which = cache[i].which;
				h = Move; /* acabou de escolher: arrastar move */
				break;
			}
		}
	}
	select(which);
	if (which == -2) {
		drag = None;
		return;
	}
	for (const Box &b : cache)
		if (b.which == which) {
			startPos = b.pos;
			startFrame = b.f;
			dragImgW = b.imgW;
			dragImgH = b.imgH;
		}
	drag = h;
	dragWhich = which;
	dragStart = img;
}

void LayoutView::mouseMoveEvent(QMouseEvent *e)
{
	const QPointF img = toImage(e->position(), canvasW(), canvasH());
	if (drag != None && (e->buttons() & Qt::LeftButton)) {
		dragTo(img, e->modifiers());
		return;
	}
	/* cursor conforme o que está embaixo */
	Handle h = None;
	for (const Box &b : cache)
		if (b.which == sel && (h = hit(b, img)) != None)
			break;
	if (h == None)
		for (int i = cache.size() - 1; i >= 0 && h == None; i--)
			if (hit(cache[i], img) != None)
				h = Move;
	switch (h) {
	case TL:
	case BR:
		setCursor(Qt::SizeFDiagCursor);
		break;
	case TR:
	case BL:
		setCursor(Qt::SizeBDiagCursor);
		break;
	case L:
	case R:
		setCursor(Qt::SizeHorCursor);
		break;
	case T:
	case B:
		setCursor(Qt::SizeVerCursor);
		break;
	case Move:
		setCursor(Qt::SizeAllCursor);
		break;
	default:
		unsetCursor();
	}
}

void LayoutView::mouseReleaseEvent(QMouseEvent *e)
{
	drag = None;
	VideoDisplay::mouseReleaseEvent(e);
}

void LayoutView::leaveEvent(QEvent *e)
{
	unsetCursor();
	VideoDisplay::leaveEvent(e);
}

void LayoutView::setPos(int which, const ItemPos &p)
{
	if (which == -1)
		Engine::get().setInputPosition(id, p);
	else if (which >= 0)
		Engine::get().setLayerPosition(id, which, p);
}

void LayoutView::dragTo(const QPointF &img, Qt::KeyboardModifiers mods)
{
	const uint32_t W = (uint32_t)canvasW(), H = (uint32_t)canvasH();
	const ItemFrame &f = startFrame;
	ItemPos p = startPos;
	const double pxl = 1.0 / std::max(0.01f, imageScale(canvasW(), canvasH()));
	const double rot = startPos.rotation;
	const double c = std::cos(rad(rot)), s = std::sin(rad(rot));

	if (drag == Move) {
		double dx = img.x() - dragStart.x(), dy = img.y() - dragStart.y();
		if (!(mods & Qt::ControlModifier)) {
			/* gruda nas bordas e no meio da tela (Ctrl solta) */
			const double snap = 10.0 * pxl;
			auto snapAxis = [snap](double center, double half, double size, double &d) {
				const double c = center + d;
				const double cands[3][2] = {{c - half, 0.0}, {c + half, size}, {c, size / 2.0}};
				for (const auto &k : cands)
					if (std::abs(k[0] - k[1]) < snap) {
						d += k[1] - k[0];
						return;
					}
			};
			snapAxis(f.visCx, f.visW / 2, W, dx);
			snapAxis(f.visCy, f.visH / 2, H, dy);
		}
		setPanFromCenter(p, f.fullCx + dx, f.fullCy + dy, W, H);
		setPos(dragWhich, p);
		return;
	}

	const int sx = (drag == L || drag == TL || drag == BL) ? -1 : (drag == R || drag == TR || drag == BR) ? 1 : 0;
	const int sy = (drag == T || drag == TL || drag == TR) ? -1 : (drag == B || drag == BL || drag == BR) ? 1 : 0;
	const QPointF q = toLocal(f, rot, img); /* mouse na moldura de antes do arrasto */
	const double hw = f.visW / 2, hh = f.visH / 2;

	if (mods & Qt::AltModifier) {
		/* Alt: corta (como no OBS); a imagem inteira fica parada */
		const double fl = (f.fullCx - f.visCx) - f.fullW / 2, ft = (f.fullCy - f.visCy) - f.fullH / 2;
		const double minF = 0.01;
		if (sx < 0 && f.fullW > 0)
			p.cropX1 = std::clamp((q.x() - fl) / f.fullW, 0.0, f.x2 - minF);
		if (sx > 0 && f.fullW > 0)
			p.cropX2 = std::clamp((q.x() - fl) / f.fullW, f.x1 + minF, 1.0);
		if (sy < 0 && f.fullH > 0)
			p.cropY1 = std::clamp((q.y() - ft) / f.fullH, 0.0, f.y2 - minF);
		if (sy > 0 && f.fullH > 0)
			p.cropY2 = std::clamp((q.y() - ft) / f.fullH, f.y1 + minF, 1.0);
		setPos(dragWhich, p);
		return;
	}

	/* tamanho: o canto/borda oposto fica parado */
	const double ax = -sx * hw, ay = -sy * hh;
	const double minS = 8.0;
	double nw = sx ? std::max(minS, sx * (q.x() - ax)) : 2 * hw;
	double nh = sy ? std::max(minS, sy * (q.y() - ay)) : 2 * hh;
	double kx = hw > 0 ? nw / (2 * hw) : 1.0, ky = hh > 0 ? nh / (2 * hh) : 1.0;
	const bool corner = sx && sy;
	if (corner && !(mods & Qt::ShiftModifier)) {
		/* canto: mantém a proporção (Shift solta) */
		const double k = std::max(kx, ky);
		kx = ky = k;
		p.zoom = startPos.zoom * k;
	} else {
		p.zoomX = startPos.zoomX * kx;
		p.zoomY = startPos.zoomY * ky;
	}
	p.zoom = std::clamp(p.zoom, 0.0, 5.0);
	p.zoomX = std::clamp(p.zoomX, 0.0, 5.0);
	p.zoomY = std::clamp(p.zoomY, 0.0, 5.0);
	nw = 2 * hw * kx;
	nh = 2 * hh * ky;
	/* centro novo da parte visível, na moldura e depois na tela */
	const double lcx = sx ? ax + sx * nw / 2 : 0.0, lcy = sy ? ay + sy * nh / 2 : 0.0;
	const double vcx = f.visCx + c * lcx - s * lcy, vcy = f.visCy + s * lcx + c * lcy;
	const double fw = f.fullW * kx, fh = f.fullH * ky;
	const double fcx = vcx - fw * ((f.x1 + f.x2) / 2 - 0.5), fcy = vcy - fh * ((f.y1 + f.y2) / 2 - 0.5);
	setPanFromCenter(p, fcx, fcy, W, H);
	setPos(dragWhich, p);
}

void LayoutView::keyPressEvent(QKeyEvent *e)
{
	if (sel == -2) {
		VideoDisplay::keyPressEvent(e);
		return;
	}
	const double step = (e->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
	double dx = 0, dy = 0;
	switch (e->key()) {
	case Qt::Key_Left:
		dx = -step;
		break;
	case Qt::Key_Right:
		dx = step;
		break;
	case Qt::Key_Up:
		dy = -step;
		break;
	case Qt::Key_Down:
		dy = step;
		break;
	default:
		VideoDisplay::keyPressEvent(e);
		return;
	}
	cache = boxes();
	for (const Box &b : cache)
		if (b.which == sel) {
			ItemPos p = b.pos;
			setPanFromCenter(p, b.f.fullCx + dx, b.f.fullCy + dy, (uint32_t)canvasW(), (uint32_t)canvasH());
			setPos(sel, p);
		}
}
