/* Layout que põe os quadros das entradas lado a lado e quebra a linha quando acaba o espaço
 * (cabe quadro inteiro e barra minimizada na mesma linha, como no vMix). */
#pragma once

#include <QLayout>
#include <QList>
#include <QWidget>

class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent, int margin = 6, int spacing = 6) : QLayout(parent), gap(spacing)
	{
		setContentsMargins(margin, margin, margin, margin);
	}
	~FlowLayout() override
	{
		while (QLayoutItem *it = takeAt(0))
			delete it;
	}
	void addItem(QLayoutItem *item) override { items.append(item); }
	int count() const override { return items.size(); }
	QLayoutItem *itemAt(int i) const override { return items.value(i); }
	QLayoutItem *takeAt(int i) override { return i >= 0 && i < items.size() ? items.takeAt(i) : nullptr; }
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override { return place(QRect(0, 0, w, 0), false); }
	void setGeometry(const QRect &r) override
	{
		QLayout::setGeometry(r);
		place(r, true);
	}
	QSize sizeHint() const override { return minimumSize(); }
	QSize minimumSize() const override
	{
		QSize s;
		for (QLayoutItem *it : items)
			s = s.expandedTo(it->minimumSize());
		const QMargins m = contentsMargins();
		return s + QSize(m.left() + m.right(), m.top() + m.bottom());
	}

private:
	int place(const QRect &r, bool apply) const
	{
		const QMargins m = contentsMargins();
		const QRect area = r.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
		int x = area.x(), y = area.y(), lineH = 0;
		for (QLayoutItem *it : items) {
			if (it->widget() && it->widget()->isHidden())
				continue;
			const QSize sz = it->sizeHint();
			if (x > area.x() && x + sz.width() > area.right() + 1) {
				x = area.x();
				y += lineH + gap;
				lineH = 0;
			}
			if (apply)
				it->setGeometry(QRect(QPoint(x, y), sz));
			x += sz.width() + gap;
			lineH = qMax(lineH, sz.height());
		}
		return y + lineH - r.y() + m.bottom();
	}
	QList<QLayoutItem *> items;
	int gap;
};
