#include "position-editor.hpp"
#include <cstring>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

/* ---------------- Posição ---------------- */

PositionEditor::PositionEditor(std::function<QSize()> size, QWidget *parent) : QWidget(parent), sourceSize(std::move(size))
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	auto *cols = new QHBoxLayout;
	cols->setSpacing(18);

	auto *left = new QGridLayout;
	left->setHorizontalSpacing(8);
	left->setVerticalSpacing(6);
	rows << addRow(left, 0, "Ampliação", &cur.zoom, 0, 5, 1, 3, 1000, "Tamanho da imagem (1 = tela cheia)");
	rows << addRow(left, 1, "Zoom X", &cur.zoomX, 0, 5, 1, 3, 1000, "Estica/encolhe só na largura");
	rows << addRow(left, 2, "Zoom Y", &cur.zoomY, 0, 5, 1, 3, 1000, "Estica/encolhe só na altura");
	rows << addRow(left, 3, "Pan X", &cur.panX, -2, 2, 0, 3, 1000,
		       "Para os lados, em meias telas: -0,5 = centro na metade esquerda, 0,5 = na direita");
	rows << addRow(left, 4, "Pan Y", &cur.panY, -2, 2, 0, 3, 1000,
		       "Para cima/baixo, em meias telas: 0,5 = metade de cima, -0,5 = metade de baixo");
	rows << addRow(left, 5, "Girar", &cur.rotation, -360, 360, 0, 1, 10, "Graus");
	{
		auto *rot = new QHBoxLayout;
		auto *l90 = new QPushButton("↺ 90°");
		auto *r90 = new QPushButton("↻ 90°");
		connect(l90, &QPushButton::clicked, this, [this] {
			cur.rotation = std::fmod(cur.rotation - 90.0, 360.0);
			refresh();
			emitChanged();
		});
		connect(r90, &QPushButton::clicked, this, [this] {
			cur.rotation = std::fmod(cur.rotation + 90.0, 360.0);
			refresh();
			emitChanged();
		});
		rot->addWidget(l90);
		rot->addWidget(r90);
		rot->addStretch(1);
		left->addLayout(rot, 6, 1, 1, 3);
	}
	cols->addLayout(left, 3);

	auto *right = new QGridLayout;
	right->setHorizontalSpacing(8);
	right->setVerticalSpacing(6);
	cropRows << addRow(right, 0, "Cortar X1", &cropPx[0], 0, 1, 0, 0, 1, "Corta da esquerda (pixels da imagem)");
	cropRows << addRow(right, 1, "Cortar Y1", &cropPx[1], 0, 1, 0, 0, 1, "Corta de cima");
	cropRows << addRow(right, 2, "Cortar X2", &cropPx[2], 0, 1, 1, 0, 1, "Onde termina na direita");
	cropRows << addRow(right, 3, "Cortar Y2", &cropPx[3], 0, 1, 1, 0, 1, "Onde termina embaixo");
	auto *resetAll = new QPushButton("Redefinir todas");
	resetAll->setToolTip("Volta tudo ao padrão: tela cheia, sem corte, sem giro");
	connect(resetAll, &QPushButton::clicked, this, [this] {
		cur = ItemPos();
		refresh();
		emitChanged();
	});
	right->addWidget(resetAll, 4, 0, 1, 4, Qt::AlignRight);
	right->setRowStretch(5, 1);
	cols->addLayout(right, 2);
	root->addLayout(cols);

	auto *presetBox = new QGroupBox("Rápido (ampliação e lugar; o corte e o giro continuam)");
	auto *presets = new QGridLayout(presetBox);
	presets->setSpacing(6);
	struct P {
		const char *label, *tip;
		double zoom, panX, panY;
	};
	static const P ps[] = {{"Tela cheia", "Ocupa a tela toda", 1, 0, 0},
			       {"Metade esq.", "Metade do tamanho, no meio da esquerda", 0.5, -0.5, 0},
			       {"Metade dir.", "Metade do tamanho, no meio da direita", 0.5, 0.5, 0},
			       {"Pequena ↘", "Pequena no canto de baixo à direita (imagem na imagem)", 0.3, 0.62, -0.62},
			       {"↖ Quarto", "Quarto de cima à esquerda", 0.5, -0.5, 0.5},
			       {"↗ Quarto", "Quarto de cima à direita", 0.5, 0.5, 0.5},
			       {"↙ Quarto", "Quarto de baixo à esquerda", 0.5, -0.5, -0.5},
			       {"↘ Quarto", "Quarto de baixo à direita", 0.5, 0.5, -0.5}};
	int k = 0;
	for (const P &p : ps) {
		auto *b = new QPushButton(QString::fromUtf8(p.label));
		b->setToolTip(QString::fromUtf8(p.tip));
		connect(b, &QPushButton::clicked, this, [this, p] { applyPreset(p.zoom, p.panX, p.panY); });
		presets->addWidget(b, k / 4, k % 4);
		k++;
	}
	root->addWidget(presetBox);
	root->addStretch(1);
	refresh();
}

PositionEditor::Row PositionEditor::addRow(QGridLayout *g, int r, const QString &label, double *value, double min,
					   double max, double def, int decimals, double sliderScale, const QString &tip)
{
	Row row;
	row.value = value;
	row.def = def;
	row.scale = sliderScale;
	auto *l = new QLabel(label);
	l->setToolTip(tip);
	row.slider = new QSlider(Qt::Horizontal);
	row.slider->setRange((int)std::lround(min * sliderScale), (int)std::lround(max * sliderScale));
	row.slider->setToolTip(tip);
	row.slider->setMinimumWidth(160);
	row.spin = new QDoubleSpinBox;
	row.spin->setDecimals(decimals);
	row.spin->setRange(min, max);
	row.spin->setSingleStep(decimals >= 3 ? 0.01 : 1.0);
	row.spin->setKeyboardTracking(false);
	row.spin->setMinimumWidth(80);
	auto *reset = new QToolButton;
	reset->setText("Redef.");
	reset->setToolTip("Volta ao padrão");
	g->addWidget(l, r, 0);
	g->addWidget(row.slider, r, 1);
	g->addWidget(row.spin, r, 2);
	g->addWidget(reset, r, 3);
	const bool isCrop = value >= cropPx && value < cropPx + 4;
	connect(row.slider, &QSlider::valueChanged, this, [this, row](int v) {
		if (updating)
			return;
		*row.value = v / row.scale;
		emitChanged();
		refresh();
	});
	connect(row.spin, &QDoubleSpinBox::valueChanged, this, [this, row](double v) {
		if (updating)
			return;
		*row.value = v;
		emitChanged();
		refresh();
	});
	connect(reset, &QToolButton::clicked, this, [this, value, isCrop] {
		if (isCrop) {
			const int i = int(value - cropPx);
			const QSize s = imageSize();
			*value = i == 2 ? s.width() : i == 3 ? s.height() : 0;
		} else {
			for (const Row &rw : rows)
				if (rw.value == value)
					*value = rw.def;
		}
		emitChanged();
		refresh();
	});
	return row;
}

QSize PositionEditor::imageSize() const
{
	QSize s = sourceSize ? sourceSize() : QSize();
	if (s.width() <= 0 || s.height() <= 0) {
		Engine &e = Engine::get();
		s = QSize((int)e.baseWidth(), (int)e.baseHeight());
	}
	return s;
}

void PositionEditor::emitChanged()
{
	/* corte em pixels -> frações (o que a entrada guarda) */
	const QSize s = imageSize();
	const bool fromCrop = std::abs(cropPx[0] - cur.cropX1 * s.width()) > 0.5 ||
			      std::abs(cropPx[1] - cur.cropY1 * s.height()) > 0.5 ||
			      std::abs(cropPx[2] - cur.cropX2 * s.width()) > 0.5 ||
			      std::abs(cropPx[3] - cur.cropY2 * s.height()) > 0.5;
	if (fromCrop) {
		cur.cropX1 = std::clamp(cropPx[0] / s.width(), 0.0, 1.0);
		cur.cropY1 = std::clamp(cropPx[1] / s.height(), 0.0, 1.0);
		cur.cropX2 = std::clamp(cropPx[2] / s.width(), 0.0, 1.0);
		cur.cropY2 = std::clamp(cropPx[3] / s.height(), 0.0, 1.0);
	}
	emit changed(cur);
}

void PositionEditor::applyPreset(double zoom, double panX, double panY)
{
	cur.zoom = zoom;
	cur.zoomX = cur.zoomY = 1.0;
	cur.panX = panX;
	cur.panY = panY;
	refresh();
	emitChanged();
}

void PositionEditor::setPos(const ItemPos &p)
{
	cur = p;
	refresh();
}

void PositionEditor::refresh()
{
	updating = true;
	const QSize s = imageSize();
	cropPx[0] = std::round(cur.cropX1 * s.width());
	cropPx[1] = std::round(cur.cropY1 * s.height());
	cropPx[2] = std::round(cur.cropX2 * s.width());
	cropPx[3] = std::round(cur.cropY2 * s.height());
	for (int i = 0; i < cropRows.size(); i++) {
		const int max = (i % 2 == 0) ? s.width() : s.height();
		cropRows[i].slider->setRange(0, max);
		cropRows[i].spin->setRange(0, max);
	}
	for (const Row &r : rows + cropRows) {
		r.slider->setValue((int)std::lround(*r.value * r.scale));
		r.spin->setValue(*r.value);
	}
	updating = false;
}

/* ---------------- Camadas ---------------- */

static QPixmap layoutIcon(int kind)
{
	QPixmap px(120, 68);
	px.fill(Qt::black);
	QPainter p(&px);
	p.setRenderHint(QPainter::Antialiasing);
	const QColor c1(0, 70, 255), c2(230, 20, 20), c3(20, 170, 60), c4(230, 170, 0);
	auto box = [&p](QRectF r, QColor c, int n) {
		p.fillRect(r, c);
		p.setPen(Qt::white);
		QFont f = p.font();
		f.setBold(true);
		f.setPixelSize(13);
		p.setFont(f);
		p.drawText(r.adjusted(4, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, QString::number(n));
	};
	switch (kind) {
	case 0:
		box(QRectF(0, 0, 120, 68), c1, 1);
		break;
	case 1:
		box(QRectF(0, 0, 60, 68), c1, 1);
		box(QRectF(60, 0, 60, 68), c2, 2);
		break;
	case 2:
		box(QRectF(0, 17, 60, 34), c1, 1);
		box(QRectF(60, 17, 60, 34), c2, 2);
		break;
	case 3:
		box(QRectF(0, 0, 120, 34), c1, 1);
		box(QRectF(0, 34, 120, 34), c2, 2);
		break;
	case 4:
		p.fillRect(QRectF(0, 0, 120, 68), QColor(60, 60, 60));
		box(QRectF(80, 44, 36, 20), c1, 1);
		break;
	case 5:
		box(QRectF(0, 0, 60, 34), c1, 1);
		box(QRectF(60, 0, 60, 34), c2, 2);
		box(QRectF(0, 34, 60, 34), c3, 3);
		box(QRectF(60, 34, 60, 34), c4, 4);
		break;
	}
	return px;
}

LayersEditor::LayersEditor(int inputId, QWidget *parent) : QWidget(parent), id(inputId)
{
	auto *root = new QHBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(14);

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(4);
	for (int i = 0; i < INPUT_LAYERS; i++) {
		auto *num = new QLabel(QString::number(i + 1));
		num->setAlignment(Qt::AlignCenter);
		num->setMinimumWidth(22);
		num->setStyleSheet("background:#000; color:#fff; font-weight:bold; border-radius:3px;");
		onBox[i] = new QCheckBox;
		onBox[i]->setToolTip("Mostrar esta camada");
		combo[i] = new QComboBox;
		combo[i]->setMinimumWidth(200);
		combo[i]->setToolTip("Entrada que aparece por cima (camada 1 embaixo, 10 em cima)");
		editBtn[i] = new QPushButton("Editar");
		editBtn[i]->setToolTip("Posição desta camada: tamanho, lugar, corte");
		behindBtn[i] = new QPushButton("Na frente");
		behindBtn[i]->setCheckable(true);
		behindBtn[i]->setMinimumWidth(84);
		behindBtn[i]->setToolTip("Na frente: por cima da imagem principal.\n"
					 "Atrás: embaixo da imagem principal — aparece pelas partes transparentes dela\n"
					 "(ex.: o replay atrás da arte do VAR, aparecendo pela janela da arte).");
		grid->addWidget(num, i, 0);
		grid->addWidget(onBox[i], i, 1);
		grid->addWidget(combo[i], i, 2);
		grid->addWidget(behindBtn[i], i, 3);
		grid->addWidget(editBtn[i], i, 4);
		connect(behindBtn[i], &QPushButton::toggled, this, [this, i](bool on) {
			behindBtn[i]->setText(on ? "Atrás" : "Na frente");
			if (!updating)
				Engine::get().setLayerBehind(id, i, on);
		});
		connect(onBox[i], &QCheckBox::toggled, this, [this, i](bool on) {
			if (!updating)
				Engine::get().setLayerOn(id, i, on);
		});
		connect(combo[i], &QComboBox::currentIndexChanged, this, [this, i] {
			if (updating)
				return;
			int pick = combo[i]->currentData().toInt();
			if (pick == -1) {
				/* "Programa (PGM)": cria a entrada que mostra o que está no ar e usa como camada */
				Input *pg = Engine::get().addInput("rds_program", "PROGRAMA");
				pick = pg ? pg->id : 0;
			}
			Engine::get().setLayer(id, i, pick);
		});
		connect(editBtn[i], &QPushButton::clicked, this, [this, i] { editLayer(i); });
	}
	grid->setRowStretch(INPUT_LAYERS, 1);
	root->addLayout(grid, 3);

	auto *side = new QVBoxLayout;
	auto *t = new QLabel("Arrumação pronta (só as posições das camadas; escolha as entradas ao lado):");
	t->setWordWrap(true);
	t->setObjectName("muted");
	side->addWidget(t);
	auto *g = new QGridLayout;
	g->setSpacing(6);
	static const char *names[] = {"Tela cheia", "Lado a lado", "Lado a lado (inteiras)", "Em cima / embaixo",
				      "Canto (imagem na imagem)", "4 telas"};
	for (int k = 0; k < 6; k++) {
		auto *b = new QToolButton;
		b->setIcon(QIcon(layoutIcon(k)));
		b->setIconSize(QSize(120, 68));
		b->setText(QString::fromUtf8(names[k]));
		b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		b->setToolTip(QString::fromUtf8(names[k]));
		connect(b, &QToolButton::clicked, this, [this, k] { applyLayout(k); });
		g->addWidget(b, k / 2, k % 2);
	}
	side->addLayout(g);
	auto *hint = new QLabel("Camada = outra entrada junto com esta (como fontes dentro de uma cena no OBS). "
				"Ela aparece com a posição e as camadas dela. \"Na frente\" fica por cima da imagem principal; "
				"\"Atrás\" fica embaixo dela e aparece pelas partes transparentes (a janela de uma arte, por "
				"exemplo).");
	hint->setWordWrap(true);
	hint->setObjectName("muted");
	side->addWidget(hint);
	side->addStretch(1);
	root->addLayout(side, 2);

	Engine &e = Engine::get();
	connect(&e, &Engine::inputsChanged, this, [this] {
		fillCombos();
		refresh();
	});
	connect(&e, &Engine::inputRenamed, this, [this] { fillCombos(); });
	connect(&e, &Engine::inputLayoutChanged, this, [this](int) {
		fillCombos(); /* uma camada mudou em outra entrada: a lista de proibidas muda */
		refresh();
	});
	fillCombos();
	refresh();
}

void LayersEditor::fillCombos()
{
	Engine &e = Engine::get();
	updating = true;
	for (int i = 0; i < INPUT_LAYERS; i++) {
		const int want = combo[i]->currentData().toInt();
		combo[i]->clear();
		combo[i]->addItem("Nenhuma", 0);
		int n = 0;
		bool hasPgm = false;
		for (Input *o : e.inputs())
			hasPgm = hasPgm || strcmp(obs_source_get_unversioned_id(o->source), "rds_program") == 0;
		if (!hasPgm)
			combo[i]->addItem("Programa (PGM) — o que está no ar", -1);
		for (Input *o : e.inputs()) {
			n++;
			if (o->id == id || e.layerWouldLoop(id, o->id))
				continue;
			combo[i]->addItem(QString("%1  %2").arg(n).arg(o->name), o->id);
		}
		combo[i]->setCurrentIndex(std::max(0, combo[i]->findData(want)));
	}
	updating = false;
}

void LayersEditor::refresh()
{
	Input *in = Engine::get().input(id);
	if (!in)
		return;
	updating = true;
	for (int i = 0; i < INPUT_LAYERS; i++) {
		const InputLayer &l = in->layers[i];
		int idx = combo[i]->findData(l.inputId);
		if (idx < 0 && l.inputId) {
			if (Input *li = Engine::get().input(l.inputId))
				combo[i]->addItem(li->name, li->id);
			idx = combo[i]->findData(l.inputId);
		}
		combo[i]->setCurrentIndex(std::max(0, idx));
		onBox[i]->setChecked(l.on);
		behindBtn[i]->setChecked(l.behind);
		behindBtn[i]->setText(l.behind ? "Atrás" : "Na frente");
		editBtn[i]->setEnabled(l.inputId != 0);
	}
	updating = false;
}

void LayersEditor::editLayer(int index)
{
	Input *in = Engine::get().input(id);
	if (!in)
		return;
	Input *li = Engine::get().input(in->layers[index].inputId);
	auto *d = new QDialog(window());
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle(QString("Camada %1 de %2: %3").arg(index + 1).arg(in->name, li ? li->name : QString("vazia")));
	auto *v = new QVBoxLayout(d);
	auto *note = new QLabel("Vale na hora: veja na imagem da entrada.");
	note->setObjectName("muted");
	v->addWidget(note);
	/* a camada é a entrada inteira (tamanho da tela) */
	auto *ed = new PositionEditor([] { return QSize(); }, d);
	ed->setPos(in->layers[index].pos);
	const int inputId = id;
	connect(ed, &PositionEditor::changed, d,
		[inputId, index](const ItemPos &p) { Engine::get().setLayerPosition(inputId, index, p); });
	connect(&Engine::get(), &Engine::inputsChanged, d, [d, inputId] {
		if (!Engine::get().input(inputId))
			d->close();
	});
	/* mexeu com o mouse na imagem (ou pela API): os números acompanham */
	connect(&Engine::get(), &Engine::inputLayoutChanged, ed, [ed, inputId, index](int changed) {
		Input *cur = Engine::get().input(inputId);
		if (changed == inputId && cur && cur->layers[index].pos != ed->pos())
			ed->setPos(cur->layers[index].pos);
	});
	v->addWidget(ed);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(bb, &QDialogButtonBox::rejected, d, &QDialog::close);
	v->addWidget(bb);
	d->resize(860, 380);
	d->show();
}

void LayersEditor::applyLayout(int kind)
{
	Engine &e = Engine::get();
	auto set = [&e, this](int i, double zoom, double panX, double panY, double cx1 = 0, double cy1 = 0,
			      double cx2 = 1, double cy2 = 1) {
		ItemPos p;
		p.zoom = zoom;
		p.panX = panX;
		p.panY = panY;
		p.cropX1 = cx1;
		p.cropY1 = cy1;
		p.cropX2 = cx2;
		p.cropY2 = cy2;
		e.setLayerPosition(id, i, p);
	};
	switch (kind) {
	case 0:
		set(0, 1, 0, 0);
		break;
	case 1: /* metade do meio de cada imagem, lado a lado, altura inteira */
		set(0, 1, -0.5, 0, 0.25, 0, 0.75, 1);
		set(1, 1, 0.5, 0, 0.25, 0, 0.75, 1);
		break;
	case 2:
		set(0, 0.5, -0.5, 0);
		set(1, 0.5, 0.5, 0);
		break;
	case 3:
		set(0, 1, 0, 0.5, 0, 0.25, 1, 0.75);
		set(1, 1, 0, -0.5, 0, 0.25, 1, 0.75);
		break;
	case 4:
		set(0, 0.3, 0.62, -0.62);
		break;
	case 5:
		set(0, 0.5, -0.5, 0.5);
		set(1, 0.5, 0.5, 0.5);
		set(2, 0.5, -0.5, -0.5);
		set(3, 0.5, 0.5, -0.5);
		break;
	}
}
