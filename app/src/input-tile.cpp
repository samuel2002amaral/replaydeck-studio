#include "input-tile.hpp"
#include "interact-window.hpp"
#include "filters-window.hpp"
#include "vmix-keys.hpp"
#include "engine.hpp"
#include "video-display.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <functional>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

/* entrada minimizada: barra estreita com o número e o nome na vertical (como no vMix) */
class CollapsedBar : public QWidget {
public:
	std::function<void()> onClick, onDoubleClick;
	QString number, text;
	QColor color;
	explicit CollapsedBar(QWidget *parent = nullptr) : QWidget(parent)
	{
		setFixedWidth(30);
		setCursor(Qt::PointingHandCursor);
		setToolTip("Clique: Prévia · duplo clique: abrir a barra (expandir)");
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.fillRect(rect(), color);
		p.setPen(Qt::white);
		QFont f = font();
		f.setBold(true);
		f.setPointSizeF(9.5);
		p.setFont(f);
		p.drawText(QRect(0, 4, width(), 18), Qt::AlignCenter, number);
		f.setBold(false);
		f.setPointSizeF(9);
		p.setFont(f);
		p.save();
		p.translate(width() / 2.0 + 5, height() - 6);
		p.rotate(-90);
		const QString elided = QFontMetrics(f).elidedText(text, Qt::ElideRight, height() - 34);
		p.drawText(QPoint(0, 0), elided);
		p.restore();
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button() == Qt::LeftButton && onClick)
			onClick();
		else
			QWidget::mousePressEvent(e);
	}
	void mouseDoubleClickEvent(QMouseEvent *e) override
	{
		if (e->button() == Qt::LeftButton && onDoubleClick)
			onDoubleClick();
	}
};

static QPushButton *tileButton(const QString &text, const QString &tip = QString())
{
	auto *b = new QPushButton(text);
	b->setObjectName("tileButton");
	b->setFixedHeight(20);
	b->setFocusPolicy(Qt::NoFocus);
	if (!tip.isEmpty())
		b->setToolTip(tip);
	return b;
}

InputTile::InputTile(int inputId, QWidget *parent) : QFrame(parent), id(inputId)
{
	setObjectName("inputTile");
	/* o quadro inteiro é uma janela nativa: assim a imagem (nativa) anda junto quando a lista
	 * muda de colunas; sem isso ela ficava parada no lugar antigo */
	setAttribute(Qt::WA_NativeWindow);
	auto *outer = new QHBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	bar = new CollapsedBar;
	bar->onClick = [this] { Engine::get().setPreview(id); };
	bar->onDoubleClick = [this] { Engine::get().setInputCollapsed(id, false); };
	outer->addWidget(bar);
	expanded = new QWidget;
	outer->addWidget(expanded);
	auto *v = new QVBoxLayout(expanded);
	v->setContentsMargins(1, 1, 1, 1);
	v->setSpacing(0);

	header = new QWidget;
	header->setObjectName("tileHeader");
	header->setFixedHeight(22);
	auto *h = new QHBoxLayout(header);
	h->setContentsMargins(6, 0, 2, 0);
	h->setSpacing(6);
	num = new QLabel;
	num->setObjectName("tileNum");
	name = new QLabel;
	name->setObjectName("tileName");
	auto *gear = new QToolButton;
	gear->setObjectName("tileGear");
	gear->setText("⚙");
	gear->setToolTip("Opções desta entrada (abrir, cor, renomear, remover)");
	gear->setAutoRaise(true);
	connect(gear, &QToolButton::clicked, this, [this, gear] { optionsMenu(gear->mapToGlobal(QPoint(0, gear->height()))); });
	h->addWidget(num);
	h->addWidget(name, 1);
	h->addWidget(gear);
	v->addWidget(header);

	Input *in0 = Engine::get().input(id);
	const bool hasVideo = in0 && (obs_source_get_output_flags(in0->source) & OBS_SOURCE_VIDEO);
	if (hasVideo) {
		video = new VideoDisplay;
		video->setFixedSize(214, 120);
		video->setToolTip("Clique: mandar para a Prévia · duplo clique: abrir");
		connect(video, &VideoDisplay::clicked, this, [this] { Engine::get().setPreview(id); });
		connect(video, &VideoDisplay::doubleClicked, this, [this] { emit openRequested(id); });
		v->addWidget(video);
	} else {
		/* entrada só de áudio (microfone): não tem imagem para mostrar */
		auto *ph = new QPushButton("Entrada de áudio\n(sem imagem)");
		ph->setObjectName("audioPlaceholder");
		ph->setFixedSize(214, 120);
		ph->setToolTip("Duplo clique ou engrenagem: abrir");
		connect(ph, &QPushButton::clicked, this, [this] { emit openRequested(id); });
		v->addWidget(ph);
	}

	auto *grid = new QGridLayout;
	grid->setContentsMargins(0, 1, 0, 0);
	grid->setSpacing(1);
	for (int ch = 1; ch <= 4; ch++) {
		ovBtn[ch] = tileButton(QString::number(ch), QString("Overlay %1 por cima do programa").arg(ch));
		connect(ovBtn[ch], &QPushButton::clicked, this, [this, ch] { Engine::get().toggleOverlay(ch, id); });
		grid->addWidget(ovBtn[ch], 0, ch - 1);
	}
	auto *go = tileButton("GO", "Para o programa com transição (Quick Play)");
	connect(go, &QPushButton::clicked, this, [this] { Engine::get().quickPlay(id); });
	auto *cut = tileButton("Cut", "Para o programa com corte seco");
	connect(cut, &QPushButton::clicked, this, [this] { Engine::get().cutTo(id); });
	grid->addWidget(go, 0, 4);
	grid->addWidget(cut, 0, 5);
	audio = tileButton("Áudio", "Ligar/desligar o áudio desta entrada");
	audio->setCheckable(true);
	connect(audio, &QPushButton::clicked, this, [this](bool on) { Engine::get().setInputAudio(id, on); });
	loop = tileButton("Loop", "Repetir o vídeo");
	loop->setCheckable(true);
	connect(loop, &QPushButton::clicked, this, [this](bool on) { Engine::get().setInputLoop(id, on); });
	grid->addWidget(audio, 1, 0, 1, 3);
	grid->addWidget(loop, 1, 3, 1, 3);
	v->addLayout(grid);

	connect(&Engine::get(), &Engine::busChanged, this, &InputTile::refresh);
	connect(&Engine::get(), &Engine::inputChanged, this, [this](int changed) {
		if (changed == id)
			refresh();
	});
	header->installEventFilter(this);
	bar->installEventFilter(this);
	header->setCursor(Qt::OpenHandCursor);
	header->setToolTip("Arraste para mudar a ordem");
	fullHeight = expanded->sizeHint().height() + 2;
	setFixedHeight(fullHeight);
	refresh();
}

bool InputTile::eventFilter(QObject *o, QEvent *ev)
{
	if (o != header && o != bar)
		return QFrame::eventFilter(o, ev);
	if (ev->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent *>(ev);
		if (me->button() == Qt::LeftButton)
			dragStart = me->globalPosition().toPoint();
	} else if (ev->type() == QEvent::MouseMove) {
		auto *me = static_cast<QMouseEvent *>(ev);
		if ((me->buttons() & Qt::LeftButton) && !dragStart.isNull() &&
		    (me->globalPosition().toPoint() - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
			dragStart = QPoint();
			auto *drag = new QDrag(this);
			auto *mime = new QMimeData;
			mime->setData(INPUT_MIME, QByteArray::number(id));
			drag->setMimeData(mime);
			/* o que segue o mouse: o cabeçalho (ou a barra) */
			QWidget *src = static_cast<QWidget *>(o);
			drag->setPixmap(src->grab());
			drag->setHotSpot(src->mapFromGlobal(QCursor::pos()));
			drag->exec(Qt::MoveAction);
			return true;
		}
	} else if (ev->type() == QEvent::MouseButtonRelease) {
		dragStart = QPoint();
	}
	return QFrame::eventFilter(o, ev);
}

void InputTile::refresh()
{
	Engine &e = Engine::get();
	Input *in = e.input(id);
	if (!in)
		return;
	/* minimizada: só a barra (e a imagem para de ser desenhada) */
	if ((in->collapsed ? 1 : 0) != lastCollapsed) { /* -1 na primeira vez: sempre aplica */
		lastCollapsed = in->collapsed ? 1 : 0;
		expanded->setVisible(!in->collapsed);
		bar->setVisible(in->collapsed);
		setFixedWidth(in->collapsed ? 30 : 216);
		if (video) {
			if (in->collapsed)
				video->clear();
			else
				video->showSource(in->scene);
		}
	}
	num->setText(QString::number(e.indexOf(id) + 1));
	name->setText(in->enabled ? in->name : in->name + "  · desativada");
	name->setToolTip(in->enabled ? in->name : in->name + " (desativada: botão direito → Ativar entrada)");
	/* só refaz o visual quando o estado muda de verdade (refazer estilo é caro) */
	const QString state = id == e.programId() ? "program" : id == e.previewId() ? "preview" : "";
	/* cabeçalho: vermelho no Programa, verde na Prévia; senão a cor escolhida para a entrada
	 * (no Programa/Prévia a cor da entrada continua numa faixa embaixo do cabeçalho) */
	const QString tag = inputColorHex(in->color);
	const QString head = state == "program" ? "#b0322a" : state == "preview" ? "#1f7a45" : tag;
	const QString look = state + "|" + tag;
	if (look != lastBus) {
		lastBus = look;
		setProperty("bus", state);
		style()->unpolish(this);
		style()->polish(this);
		header->setStyleSheet(QString("#tileHeader { background: %1; border-bottom: 3px solid %2; }")
					      .arg(head, state.isEmpty() ? head : tag));
		bar->color = QColor(head);
		bar->update();
	}
	bar->number = QString::number(e.indexOf(id) + 1);
	bar->text = in->name;
	bar->update();
	int ovMask = 0;
	for (int ch = 1; ch <= 4; ch++)
		if (e.overlay(ch) == id)
			ovMask |= 1 << ch;
	if (ovMask != lastOv) {
		lastOv = ovMask;
		for (int ch = 1; ch <= 4; ch++) {
			ovBtn[ch]->setProperty("on", (ovMask & (1 << ch)) != 0);
			ovBtn[ch]->style()->unpolish(ovBtn[ch]);
			ovBtn[ch]->style()->polish(ovBtn[ch]);
		}
	}
	audio->setChecked(in->audioOn);
	loop->setChecked(in->loop);
}

void InputTile::contextMenuEvent(QContextMenuEvent *e)
{
	/* como no vMix: botão direito minimiza (e na barra, expande) */
	if (Input *in = Engine::get().input(id))
		Engine::get().setInputCollapsed(id, !in->collapsed);
	e->accept();
}

void InputTile::optionsMenu(const QPoint &globalPos)
{
	QMenu menu(this);
	menu.addAction("Abrir (ver ao vivo e ajustar)…", this, [this] { emit openRequested(id); });
	menu.addAction("Mandar para a Prévia", this, [this] { Engine::get().setPreview(id); });
	menu.addAction("Chave do vMix (para apps externos)…", this, [this] { editVmixKey(window(), id); });
	menu.addAction("Filtros (chroma key, cor, LUT, ruído do microfone…)…", this, [this] {
		if (Input *in = Engine::get().input(id))
			FiltersWindow::open(in->source, in->name, window());
	});
	if (Input *me = Engine::get().input(id); me && InteractWindow::canInteract(me->source))
		menu.addAction("Interagir (usar a página com mouse e teclado)…", this, [this] {
			if (Input *in = Engine::get().input(id))
				InteractWindow::open(in->source, in->name, window());
		});
	if (Input *cur = Engine::get().input(id)) {
		menu.addAction(cur->collapsed ? "Expandir" : "Minimizar (ou botão direito)", this,
			       [this, on = !cur->collapsed] { Engine::get().setInputCollapsed(id, on); });
		QMenu *colors = menu.addMenu("Cor");
		for (const InputColor &c : inputColors()) {
			QPixmap px(14, 14);
			px.fill(QColor(QString::fromLatin1(c.hex)));
			QAction *a = colors->addAction(QIcon(px), QString::fromUtf8(c.label), this,
						       [this, key = QString::fromLatin1(c.key)] { Engine::get().setInputColor(id, key); });
			a->setCheckable(true);
			a->setChecked(cur->color == QLatin1String(c.key));
		}
	}
	menu.addSeparator();
	menu.addAction("Renomear…", this, [this] {
		Input *in = Engine::get().input(id);
		if (!in)
			return;
		bool ok = false;
		const QString n = QInputDialog::getText(this, "Renomear entrada", "Nome:", QLineEdit::Normal, in->name, &ok);
		if (ok && !n.trimmed().isEmpty())
			Engine::get().renameInput(id, n.trimmed());
	});
	menu.addSeparator();
	menu.addAction("Remover", this, [this] {
		Input *in = Engine::get().input(id);
		if (in && QMessageBox::question(this, "Remover entrada", "Remover \"" + in->name + "\"?") == QMessageBox::Yes)
			Engine::get().removeInput(id);
	});
	menu.exec(globalPos);
}
