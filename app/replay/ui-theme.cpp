#include "ui-theme.hpp"
#include "studio-host.hpp"
#include <cstdlib>
#include <QFileInfo>
#include <obs-module.h>
#include <QBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QWidget>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QIcon>
#include <QPixmap>
#include <QPen>
#include <QPolygonF>
#include <cstring>
#include <algorithm>

#include <QSpacerItem>

namespace ui {

QString settingsFile()
{
	static QString file;
	if (!file.isEmpty())
		return file;
	if (const char *over = getenv("REPLAY_CONFIG_DIR")) {
		QDir().mkpath(QString::fromUtf8(over));
		file = QString::fromUtf8(over) + "/replay-dock.ini";
		return file;
	}
	{
		char *p = rdsReplayConfigPath("replay-dock.ini");
		if (p) {
			file = QString::fromUtf8(p);
			bfree(p);
			QDir().mkpath(QFileInfo(file).absolutePath());
			return file;
		}
	}
	file = QDir::tempPath() + "/obs-instant-replay-replay-dock.ini";
	return file;
}

FlowLayout::FlowLayout(QWidget *parent, int h, int v) : QLayout(parent), hs(h), vs(v)
{
	setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
	while (QLayoutItem *it = takeAt(0))
		delete it;
}

void FlowLayout::addGap(int px)
{
	addItem(new QSpacerItem(px, 1, QSizePolicy::Fixed, QSizePolicy::Fixed));
}

QSize FlowLayout::minimumSize() const
{
	/* nunca mais estreito que o maior grupo (e, na posição livre, que o grupo mais à direita) */
	QSize s;
	for (QLayoutItem *it : items) {
		QWidget *w = it->widget();
		if (w && (w->isHidden() || (w->property("irBreak").toBool() && !w->isVisible())))
			continue;
		QSize hint = it->sizeHint();
		if (freeOn && w && freePos.contains(w))
			hint.setWidth(freePos.value(w).x() + hint.width());
		s = s.expandedTo(QSize(hint.width(), 0));
	}
	const QMargins m = contentsMargins();
	return s + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::doFree(const QRect &rect, bool testOnly) const
{
	const QMargins m = contentsMargins();
	const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
	int bottom = 0;
	/* primeiro os que têm lugar; os sem lugar vão para baixo de tudo */
	for (QLayoutItem *it : items) {
		QWidget *w = it->widget();
		if (!w || w->isHidden() || w->property("irBreak").toBool() || !freePos.contains(w))
			continue;
		const QSize sz = it->sizeHint();
		QPoint p = freePos.value(w);
		p.setX(std::max(0, std::min(p.x(), r.width() - sz.width())));
		p.setY(std::max(0, p.y()));
		if (!testOnly)
			it->setGeometry(QRect(r.topLeft() + p, sz));
		bottom = std::max(bottom, p.y() + sz.height());
	}
	int x = 0, y = bottom ? bottom + vs : 0, lineH = 0;
	for (QLayoutItem *it : items) {
		QWidget *w = it->widget();
		if (!w || w->isHidden() || w->property("irBreak").toBool() || freePos.contains(w))
			continue;
		const QSize sz = it->sizeHint();
		if (x > 0 && x + sz.width() > r.width()) {
			x = 0;
			y += lineH + vs;
			lineH = 0;
		}
		if (!testOnly)
			it->setGeometry(QRect(r.topLeft() + QPoint(x, y), sz));
		x += sz.width() + hs;
		lineH = std::max(lineH, sz.height());
		bottom = std::max(bottom, y + sz.height());
	}
	return bottom + m.top() + m.bottom();
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
	if (freeOn)
		return doFree(rect, testOnly);
	const QMargins m = contentsMargins();
	const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
	int x = r.x(), y = r.y(), lineH = 0;
	for (QLayoutItem *it : items) {
		QWidget *bw = it->widget();
		if (bw && bw->property("irBreak").toBool()) {
			/* quebra de linha: visível só no modo organizar (aí ocupa lugar) */
			if (!bw->isHidden()) {
				const QSize bs = it->sizeHint();
				if (!testOnly)
					it->setGeometry(QRect(QPoint(x, y), bs));
				lineH = std::max(lineH, bs.height());
			}
			if (lineH > 0) {
				x = r.x();
				y += lineH + vs;
				lineH = 0;
			}
			continue;
		}
		if (it->widget() && it->widget()->isHidden())
			continue;
		const QSize sz = it->sizeHint();
		const bool gap = it->spacerItem() != nullptr;
		int next = x + sz.width();
		if (next > r.right() + 1 && lineH > 0) {
			if (gap)
				continue; /* espaço no começo da linha não serve */
			x = r.x();
			y += lineH + vs;
			next = x + sz.width();
			lineH = 0;
		}
		if (!testOnly)
			it->setGeometry(QRect(QPoint(x, y), sz));
		x = next + hs;
		lineH = std::max(lineH, sz.height());
	}
	return y + lineH - rect.y() + m.bottom();
}


/* ícones desenhados na hora (sem depender de plugins de imagem do Qt) */
static QString iconFile(const char *name)
{
	const QString dir = QDir::tempPath() + "/obs-instant-replay-ui";
	QDir().mkpath(dir);
	const QString file = dir + "/" + name + "-v1.png";
	if (QFile::exists(file))
		return file;
	const bool check = strcmp(name, "check") == 0;
	QImage img(check ? QSize(30, 30) : QSize(20, 12), QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	if (check) {
		p.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPolyline(QPolygonF({QPointF(7, 15.5), QPointF(12.5, 21), QPointF(23, 9)}));
	} else {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(MUTED));
		p.drawPolygon(QPolygonF({QPointF(2, 2), QPointF(18, 2), QPointF(10, 10)}));
	}
	p.end();
	img.save(file);
	return file;
}

static QString sheet()
{
	return QString(R"QSS(
#irRoot, #irRoot QWidget#irBody { background: %1; }
#irRoot QWidget { color: %5; font-size: 12px; }
#irRoot QLabel { background: transparent; }
#irRoot QLabel[role="muted"] { color: %6; font-size: 11px; }
#irRoot QLabel[role="caption"] { color: %6; font-size: 10px; font-weight: 700; letter-spacing: 1px; }
#irRoot QLabel[role="tc"] { color: #f4f6fa; font-family: "Consolas", "DejaVu Sans Mono", monospace; font-size: 22px; font-weight: 700; }
#irRoot QLabel[role="title"] { font-size: 13px; font-weight: 700; }

#irRoot QFrame[card="true"] { background: %2; border: 1px solid %4; border-radius: 10px; }
#irRoot QFrame[card="true"][live="true"] { border: 1px solid %8; }

#irRoot QPushButton, #irRoot QToolButton {
	background: %3; color: %5; border: 1px solid %4; border-radius: 7px;
	padding: 3px 9px; min-height: 22px; font-weight: 600;
}
#irRoot QPushButton:hover, #irRoot QToolButton:hover { background: #2d323e; border-color: #3b4250; }
#irRoot QPushButton:pressed, #irRoot QToolButton:pressed { background: #1c2028; }
#irRoot QPushButton:disabled { color: #555c6b; background: #1c1f26; }
#irRoot QPushButton:checked { background: #1d4ed8; border-color: %7; color: white; }
#irRoot QToolButton::menu-indicator { image: none; width: 0; }

#irRoot QPushButton[kind="primary"] { background: #1d4ed8; border-color: %7; color: white; }
#irRoot QPushButton[kind="primary"]:hover { background: #2563eb; }
#irRoot QPushButton[kind="go"] { background: #14532d; border-color: #166534; color: #dcfce7; }
#irRoot QPushButton[kind="go"]:hover { background: #166534; }
#irRoot QPushButton[kind="go"]:checked { background: %8; border-color: #4ade80; color: #052e16; }
#irRoot QPushButton[kind="out"] { background: #7f1d1d; border-color: #991b1b; color: #fee2e2; }
#irRoot QPushButton[kind="out"]:hover { background: #991b1b; }
#irRoot QPushButton[kind="rec"]:checked { background: %9; border-color: #f87171; color: white; }
#irRoot QPushButton[kind="warn"]:checked { background: %10; border-color: #fbbf24; color: #1f1300; }
#irRoot QFrame[irCard="true"] { background: #141821; border: 1px solid #262c38; border-radius: 7px; }
#irRoot QLabel[role="cap"] { color: #7d8696; font-size: 9px; font-weight: 700; letter-spacing: 1px; padding: 0 1px; background: transparent; border: none; }
#irRoot QPushButton[kind="move"] { background: #221c35; border-color: #45386e; color: #ddd6fe; }
#irRoot QPushButton[kind="move"]:hover { background: #2e2550; border-color: #8b5cf6; }
#irRoot QPushButton[kind="danger"] { background: #3a1517; border-color: #5b1d20; color: #fecaca; }
#irRoot QPushButton[kind="ghost"], #irRoot QToolButton[kind="ghost"] { background: transparent; border-color: transparent; color: %6; }
#irRoot QPushButton[kind="ghost"]:hover, #irRoot QToolButton[kind="ghost"]:hover { background: %3; color: %5; }

#irRoot QPushButton[size="big"] { min-height: 28px; font-size: 13px; padding: 3px 12px; }
#irRoot QPushButton[icon="true"] { font-size: 16px; padding: 1px 6px; }
#irRoot QPushButton[size="small"] { min-height: 20px; padding: 2px 6px; font-size: 11px; }

#irRoot QPushButton[seg="first"] { border-top-right-radius: 0; border-bottom-right-radius: 0; }
#irRoot QPushButton[seg="mid"] { border-radius: 0; border-left-width: 0; }
#irRoot QPushButton[seg="last"] { border-top-left-radius: 0; border-bottom-left-radius: 0; border-left-width: 0; }

#irRoot QLabel[chip="true"] { border-radius: 9px; padding: 2px 8px; font-size: 11px; font-weight: 700; background: %3; color: %6; }
#irRoot QLabel[chip="true"][tone="live"] { background: %9; color: white; }
#irRoot QLabel[chip="true"][tone="rec"] { background: #3a1517; color: #fca5a5; }
#irRoot QLabel[chip="true"][tone="warn"] { background: #3d2a05; color: #fcd34d; }
#irRoot QLabel[chip="true"][tone="ok"] { background: #0f2e1b; color: #86efac; }
#irRoot QLabel[chip="true"][tone="info"] { background: #172554; color: #93c5fd; }

#irRoot QTableWidget {
	background: #0f1115; alternate-background-color: #14171d; color: %5;
	gridline-color: #1f232b; border: 1px solid %4; border-radius: 8px;
	selection-background-color: #1e3a8a; selection-color: white;
}
#irRoot QTableWidget::item { padding: 2px 6px; }
#irRoot QHeaderView::section {
	background: %2; color: %6; border: none; border-bottom: 1px solid %4;
	padding: 5px 6px; font-size: 11px; font-weight: 700;
}
#irRoot QTableCornerButton::section { background: %2; border: none; }

#irRoot QSlider::groove:horizontal { height: 6px; background: #2a2f3a; border-radius: 3px; }
#irRoot QSlider::sub-page:horizontal { background: %7; border-radius: 3px; }
#irRoot QSlider::groove:vertical { width: 6px; background: #2a2f3a; border-radius: 3px; }
#irRoot QSlider::add-page:vertical { background: %7; border-radius: 3px; }
#irRoot QSlider::handle:vertical { width: 16px; height: 16px; margin: 0 -5px; border-radius: 8px; background: #f4f6fa; border: 2px solid %7; }
#irRoot QSlider::handle:horizontal { width: 16px; height: 16px; margin: -5px 0; border-radius: 8px; background: #f4f6fa; border: 2px solid %7; }

#irRoot QComboBox, #irRoot QLineEdit, #irRoot QSpinBox {
	background: #0f1115; border: 1px solid %4; border-radius: 6px; padding: 3px 8px; min-height: 22px; color: %5;
}
#irRoot QComboBox:hover, #irRoot QLineEdit:focus { border-color: #3b4250; }
#irRoot QComboBox::drop-down { border: none; width: 18px; }

#irRoot QScrollArea { background: transparent; border: none; }
#irRoot QPlainTextEdit, #irRoot QTextEdit, #irRoot QListWidget {
	background: #0f1115; border: 1px solid %4; border-radius: 8px; color: %5; selection-background-color: #1e3a8a;
}
#irRoot QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
#irRoot QScrollBar::handle:vertical { background: #3a404d; border-radius: 3px; min-height: 30px; }
#irRoot QScrollBar::add-line, #irRoot QScrollBar::sub-line { height: 0; width: 0; }
#irRoot QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }
#irRoot QScrollBar::handle:horizontal { background: #3a404d; border-radius: 3px; min-width: 30px; }

#irRoot QMenu { background: %2; border: 1px solid %4; padding: 4px; }
#irRoot QMenu::item { padding: 6px 18px; border-radius: 5px; }
#irRoot QMenu::item:selected { background: #1e3a8a; }
#irRoot QMenu::separator { height: 1px; background: %4; margin: 4px 6px; }

#irRoot QTabWidget::pane { border: 1px solid %4; border-radius: 8px; top: -1px; background: %2; }
#irRoot QTabBar::tab { background: transparent; color: %6; padding: 7px 14px; border: none; font-weight: 700; }
#irRoot QTabBar::tab:selected { color: %5; border-bottom: 2px solid %7; }
#irRoot QTabBar::tab:hover { color: %5; }
#irRoot QCheckBox { spacing: 8px; }
#irRoot QCheckBox::indicator, #irRoot QListView::indicator {
	width: 15px; height: 15px; border-radius: 4px; border: 1px solid #3b4250; background: #0f1115; image: none;
}
#irRoot QCheckBox::indicator:hover, #irRoot QListView::indicator:hover { border-color: %7; }
#irRoot QCheckBox::indicator:checked, #irRoot QListView::indicator:checked { background: %7; border-color: #60a5fa; image: url(@CHECK@); }
#irRoot QAbstractSpinBox::up-button, #irRoot QAbstractSpinBox::down-button { width: 0; border: none; }
#irRoot QAbstractSpinBox { min-width: 110px; }
#irRoot QComboBox::down-arrow { image: url(@ARROW@); width: 10px; height: 6px; margin-right: 6px; }
#irRoot QComboBox QAbstractItemView { background: %2; border: 1px solid %4; selection-background-color: #1e3a8a; outline: none; }
#irRoot QPushButton[tile="true"] { min-height: 34px; text-align: center; padding: 2px 4px; font-size: 11px; }
#irRoot QPushButton[tile="true"]:checked { background: %8; border-color: #4ade80; color: #052e16; }
#irRoot QPushButton[tile="true"][empty="true"] { color: #5b6272; border-style: dashed; background: transparent; }
#irRoot QToolTip { background: %2; color: %5; border: 1px solid %4; }
)QSS")
		.arg(BG, PANEL, RAISED, BORDER, TEXT, MUTED, BLUE, GREEN, RED)
		.arg(AMBER)
		.replace("@CHECK@", iconFile("check"))
		.replace("@ARROW@", iconFile("arrow"));
}

QIcon icon(const char *name, const char *color)
{
	const int S = 64;
	QPixmap pm(S, S);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	const QColor c(color);
	p.setPen(Qt::NoPen);
	p.setBrush(c);
	auto tri = [&](double x0, double x1, bool right) {
		/* triângulo de x0 a x1, centrado */
		if (right)
			p.drawPolygon(QPolygonF({QPointF(x0, 14), QPointF(x1, 32), QPointF(x0, 50)}));
		else
			p.drawPolygon(QPolygonF({QPointF(x1, 14), QPointF(x0, 32), QPointF(x1, 50)}));
	};
	const QString n = QString::fromLatin1(name);
	if (n == "play") {
		tri(18, 50, true);
	} else if (n == "pause") {
		p.drawRoundedRect(QRectF(16, 14, 11, 36), 2, 2);
		p.drawRoundedRect(QRectF(37, 14, 11, 36), 2, 2);
	} else if (n == "reverse") {
		tri(14, 46, false);
	} else if (n == "prev") {
		p.drawRect(QRectF(12, 16, 6, 32));
		tri(20, 52, false);
	} else if (n == "next") {
		tri(12, 44, true);
		p.drawRect(QRectF(46, 16, 6, 32));
	} else if (n == "ff") {
		tri(8, 34, true);
		tri(32, 58, true);
	} else if (n == "left" || n == "right" || n == "up" || n == "down") {
		QPolygonF t({QPointF(20, 14), QPointF(46, 32), QPointF(20, 50)}); /* para a direita */
		p.translate(32, 32);
		p.rotate(n == "right" ? 0 : n == "down" ? 90 : n == "left" ? 180 : 270);
		p.translate(-32, -32);
		p.drawPolygon(t);
	} else if (n == "zoomin" || n == "zoomout") {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(c, 6));
		p.drawEllipse(QPointF(27, 27), 16, 16);
		p.setPen(QPen(c, 8, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(39, 39), QPointF(54, 54));
		p.setPen(QPen(c, 5, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(QPointF(19, 27), QPointF(35, 27));
		if (n == "zoomin")
			p.drawLine(QPointF(27, 19), QPointF(27, 35));
	} else if (n == "export") {
		p.drawRect(QRectF(28, 10, 8, 24));
		p.drawPolygon(QPolygonF({QPointF(18, 30), QPointF(46, 30), QPointF(32, 44)}));
		p.drawRect(QRectF(14, 48, 36, 6));
	} else if (n == "gear") {
		p.save();
		p.translate(32, 32);
		for (int i = 0; i < 8; i++) {
			p.drawRoundedRect(QRectF(-5, -26, 10, 14), 2, 2);
			p.rotate(45);
		}
		p.restore();
		p.drawEllipse(QPointF(32, 32), 17, 17);
		p.setBrush(QColor(BG));
		p.drawEllipse(QPointF(32, 32), 7, 7);
	}
	p.end();
	return QIcon(pm);
}

void apply(QWidget *root)
{
	root->setObjectName("irRoot");
	root->setAttribute(Qt::WA_StyledBackground, true);
	root->setStyleSheet(sheet());
}

void applyDialog(QWidget *dlg)
{
	apply(dlg);
}

void setProp(QWidget *w, const char *name, const QVariant &value)
{
	if (w->property(name) == value)
		return;
	w->setProperty(name, value);
	w->style()->unpolish(w);
	w->style()->polish(w);
	w->update();
}

void setKind(QWidget *w, const char *kind)
{
	setProp(w, "kind", QString::fromLatin1(kind));
}

QPushButton *button(const QString &text, const QString &tip, QWidget *parent, const char *kind,
		    std::function<void()> onClick)
{
	auto *b = new QPushButton(text, parent);
	b->setToolTip(tip);
	b->setProperty("kind", QString::fromLatin1(kind));
	b->setCursor(Qt::PointingHandCursor);
	b->setFocusPolicy(Qt::NoFocus);
	if (onClick)
		QObject::connect(b, &QPushButton::clicked, parent, [onClick] { onClick(); });
	return b;
}

QPushButton *toggle(const QString &text, const QString &tip, QWidget *parent, const char *kind)
{
	auto *b = button(text, tip, parent, kind);
	b->setCheckable(true);
	return b;
}

void segment(const std::initializer_list<QPushButton *> &buttons)
{
	const size_t n = buttons.size();
	size_t i = 0;
	for (QPushButton *b : buttons) {
		b->setProperty("seg", n == 1 ? "" : i == 0 ? "first" : i + 1 == n ? "last" : "mid");
		i++;
	}
}

QFrame *card(QWidget *parent, QBoxLayout *layout, int margin, int spacing)
{
	auto *f = new QFrame(parent);
	f->setProperty("card", true);
	f->setLayout(layout);
	layout->setContentsMargins(margin, margin, margin, margin);
	layout->setSpacing(spacing);
	return f;
}

QLabel *caption(const QString &text, QWidget *parent)
{
	auto *l = new QLabel(text.toUpper(), parent);
	l->setProperty("role", "caption");
	return l;
}

QLabel *chip(const QString &text, QWidget *parent, const char *tone)
{
	auto *l = new QLabel(text, parent);
	l->setProperty("chip", true);
	l->setProperty("tone", QString::fromLatin1(tone));
	l->setAlignment(Qt::AlignCenter);
	return l;
}

void setChip(QLabel *c, const QString &text, const char *tone)
{
	if (c->text() != text)
		c->setText(text);
	setProp(c, "tone", QString::fromLatin1(tone));
	c->setVisible(!text.isEmpty());
}

} // namespace ui
