#include "input-tile.hpp"
#include "engine.hpp"
#include "video-display.hpp"

#include <QContextMenuEvent>
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
	setFixedWidth(232);
	auto *v = new QVBoxLayout(this);
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
	gear->setToolTip("Abrir esta entrada (ver ao vivo e ajustar)");
	gear->setAutoRaise(true);
	connect(gear, &QToolButton::clicked, this, [this] { emit openRequested(id); });
	h->addWidget(num);
	h->addWidget(name, 1);
	h->addWidget(gear);
	v->addWidget(header);

	video = new VideoDisplay;
	video->setFixedSize(230, 129);
	video->setToolTip("Clique: mandar para a Prévia · duplo clique: abrir");
	connect(video, &VideoDisplay::clicked, this, [this] { Engine::get().setPreview(id); });
	connect(video, &VideoDisplay::doubleClicked, this, [this] { emit openRequested(id); });
	v->addWidget(video);

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
	if (Input *in = Engine::get().input(id))
		video->showSource(in->scene);
	refresh();
}

void InputTile::refresh()
{
	Engine &e = Engine::get();
	Input *in = e.input(id);
	if (!in)
		return;
	num->setText(QString::number(e.indexOf(id) + 1));
	name->setText(in->name);
	name->setToolTip(in->name);
	const char *state = id == e.programId() ? "program" : id == e.previewId() ? "preview" : "";
	setProperty("bus", state);
	header->setProperty("bus", state);
	for (QWidget *w : {static_cast<QWidget *>(this), header}) {
		w->style()->unpolish(w);
		w->style()->polish(w);
	}
	for (int ch = 1; ch <= 4; ch++) {
		ovBtn[ch]->setProperty("on", e.overlay(ch) == id);
		ovBtn[ch]->style()->unpolish(ovBtn[ch]);
		ovBtn[ch]->style()->polish(ovBtn[ch]);
	}
	audio->setChecked(in->audioOn);
	loop->setChecked(in->loop);
}

void InputTile::contextMenuEvent(QContextMenuEvent *e)
{
	QMenu menu(this);
	menu.addAction("Abrir…", this, [this] { emit openRequested(id); });
	menu.addAction("Mandar para a Prévia", this, [this] { Engine::get().setPreview(id); });
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
	menu.addAction("Mover para a esquerda", this, [this] { Engine::get().moveInput(id, Engine::get().indexOf(id) - 1); });
	menu.addAction("Mover para a direita", this, [this] { Engine::get().moveInput(id, Engine::get().indexOf(id) + 1); });
	menu.addSeparator();
	menu.addAction("Remover", this, [this] {
		Input *in = Engine::get().input(id);
		if (in && QMessageBox::question(this, "Remover entrada", "Remover \"" + in->name + "\"?") == QMessageBox::Yes)
			Engine::get().removeInput(id);
	});
	menu.exec(e->globalPos());
}
