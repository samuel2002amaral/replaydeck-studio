#include "transitions-panel.hpp"
#include "engine.hpp"
#include "properties-view.hpp"

#include "../replay/engine.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
}

#include <QDateTime>
#include <QHash>

static int probeDurationMs(const QString &file);

int videoDurationMs(const QString &file)
{
	/* abrir o vídeo custa: guarda pelo arquivo + data de modificação */
	static QHash<QString, QPair<qint64, int>> cache;
	const qint64 mtime = QFileInfo(file).lastModified().toMSecsSinceEpoch();
	auto it = cache.constFind(file);
	if (it != cache.constEnd() && it->first == mtime)
		return it->second;
	const int ms = probeDurationMs(file);
	cache.insert(file, qMakePair(mtime, ms));
	return ms;
}

static int probeDurationMs(const QString &file)
{
	AVFormatContext *fc = nullptr;
	const QByteArray p = file.toUtf8();
	if (avformat_open_input(&fc, p.constData(), nullptr, nullptr) < 0)
		return 0;
	int ms = 0;
	if (avformat_find_stream_info(fc, nullptr) >= 0 && fc->duration > 0)
		ms = (int)(fc->duration / (AV_TIME_BASE / 1000));
	avformat_close_input(&fc);
	return ms;
}

static QString uniqueTransitionName(const QString &wanted, int exceptId = 0)
{
	const QString base = wanted.trimmed().isEmpty() ? QStringLiteral("Transição") : wanted.trimmed();
	auto taken = [&](const QString &n) {
		if (n == "Corte" || n == "Fade")
			return true;
		for (StudioTransition *t : Engine::get().userTransitions())
			if (t->id != exceptId && t->name.compare(n, Qt::CaseInsensitive) == 0)
				return true;
		return false;
	};
	QString n = base;
	for (int i = 2; taken(n); i++)
		n = QString("%1 %2").arg(base).arg(i);
	return n;
}

static const struct {
	const char *type, *label, *tip;
} otherTypes[] = {
	{"slide_transition", "Deslizar", "A nova imagem empurra a antiga para o lado"},
	{"swipe_transition", "Varrer", "A nova imagem passa por cima da antiga"},
	{"wipe_transition", "Luma wipe", "Revela a nova imagem com um desenho (círculo, barras, relógio...)"},
	{"fade_to_color_transition", "Fade para cor", "Passa por uma cor (preto, branco...) no meio"},
	{"fade_transition", "Fade", "Fade com outra duração (ex.: um fade lento)"},
	{"rds_merge_transition", "Merge", "O que está nas duas entradas se move para o lugar novo (como no vMix)"},
};

TransitionsPanel::TransitionsPanel(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);

	auto *top = new QHBoxLayout;
	top->setSpacing(6);
	auto *addSt = new QPushButton("＋ Stinger (vídeo)…");
	addSt->setObjectName("primaryButton");
	addSt->setToolTip("Um vídeo com fundo transparente (.webm, .mov) ou com máscara (track matte).\n"
			  "No ponto de troca a imagem muda por baixo do vídeo.");
	connect(addSt, &QPushButton::clicked, this, &TransitionsPanel::addStinger);
	top->addWidget(addSt);
	auto *addGt = new QPushButton("＋ Stinger GT (título)…");
	addGt->setObjectName("primaryButton");
	addGt->setToolTip("Um título GT do vMix (.gtzip) como stinger: a animação de entrada cobre a tela,\n"
			  "a imagem troca por baixo e a animação de saída revela a entrada nova.");
	connect(addGt, &QPushButton::clicked, this, &TransitionsPanel::addGtStinger);
	top->addWidget(addGt);
	auto *other = new QToolButton;
	other->setText("＋ Outra transição");
	other->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(other);
	for (auto &o : otherTypes) {
		if (!obs_source_get_display_name(o.type))
			continue; /* tipo não existe neste motor */
		QAction *a = menu->addAction(QString::fromUtf8(o.label), this,
					     [this, type = QString::fromUtf8(o.type)] { addOther(type); });
		a->setToolTip(QString::fromUtf8(o.tip));
	}
	menu->setToolTipsVisible(true);
	other->setMenu(menu);
	top->addWidget(other);
	top->addStretch(1);
	root->addLayout(top);

	auto *hint = new QLabel("Cada transição vira um botão no meio da tela, abaixo do Fade. "
				"Atalhos e scripts: Stinger1…4 e Transition1…4, na ordem desta lista.");
	hint->setObjectName("muted");
	hint->setWordWrap(true);
	root->addWidget(hint);

	auto *cover = new QCheckBox("Stinger por cima dos overlays (placar, títulos GT)");
	cover->setToolTip("Desligado: o stinger passa por baixo dos overlays 1 a 4.");
	cover->setChecked(engineAlive() ? engine().config().stingerCoverOverlays : true);
	connect(cover, &QCheckBox::toggled, this, [](bool on) {
		if (!engineAlive())
			return;
		ReplayConfig c = engine().config();
		c.stingerCoverOverlays = on;
		engine().setConfig(c);
		engine().saveConfig();
	});
	root->addWidget(cover);

	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *inner = new QWidget;
	auto *iv = new QVBoxLayout(inner);
	iv->setContentsMargins(0, 0, 0, 0);
	iv->setSpacing(6);
	listLayout = new QVBoxLayout;
	listLayout->setSpacing(6);
	iv->addLayout(listLayout);
	iv->addStretch(1);
	scroll->setWidget(inner);
	root->addWidget(scroll, 1);

	connect(&Engine::get(), &Engine::transitionsChanged, this, &TransitionsPanel::rebuild, Qt::QueuedConnection);
	rebuild();
}

void TransitionsPanel::addStinger()
{
	const QString file = QFileDialog::getOpenFileName(
		this, "Escolha o vídeo do stinger", QString(),
		"Vídeos (*.webm *.mov *.mp4 *.mkv *.avi *.m4v *.gif);;Todos os arquivos (*)");
	if (file.isEmpty())
		return;
	const int dur = videoDurationMs(file);
	OBSDataAutoRelease st = obs_data_create();
	obs_data_set_string(st, "path", file.toUtf8().constData());
	obs_data_set_int(st, "tp_type", 0); /* ponto de troca em milissegundos */
	/* troca no meio do vídeo (o normal de um stinger); dá para ajustar no cartão */
	obs_data_set_int(st, "transition_point", dur > 0 ? dur / 2 : 500);
	obs_data_set_bool(st, "preload", true); /* fica na memória: começa na hora */
	int stingers = 0;
	for (StudioTransition *t : Engine::get().userTransitions())
		stingers += t->isStinger();
	const QString name = uniqueTransitionName(QString("Stinger %1").arg(stingers + 1));
	if (!Engine::get().addTransition("obs_stinger_transition", name, st))
		QMessageBox::warning(this, "Transições", "Não foi possível criar o stinger.");
}

void TransitionsPanel::addGtStinger()
{
	QStringList titles;
	for (Input *in : Engine::get().inputs())
		if (strcmp(obs_source_get_id(in->source), "instant_replay_gt") == 0)
			titles << in->name;
	const QString otherFile = QString::fromUtf8("Outro arquivo .gtzip…");
	QString choice = otherFile;
	if (!titles.isEmpty()) {
		bool ok = false;
		choice = QInputDialog::getItem(this, "Stinger GT",
					       "Qual título vira stinger?\n(os textos e imagens seguem a entrada do título)",
					       titles + QStringList{otherFile}, 0, false, &ok);
		if (!ok)
			return;
	}
	OBSDataAutoRelease st = obs_data_create();
	QString base;
	if (choice == otherFile) {
		const QString file =
			QFileDialog::getOpenFileName(this, "Título GT para o stinger", QString(), "Títulos GT (*.gtzip)");
		if (file.isEmpty())
			return;
		obs_data_set_string(st, "file", file.toUtf8().constData());
		base = QFileInfo(file).completeBaseName();
	} else {
		obs_data_set_string(st, "titulo", choice.toUtf8().constData());
		base = choice;
	}
	if (!Engine::get().addTransition("rds_gt_stinger", uniqueTransitionName("Stinger " + base), st))
		QMessageBox::warning(this, "Transições", "Não foi possível criar o stinger GT.");
}

void TransitionsPanel::addOther(const QString &type)
{
	QString label = Engine::transitionTypeName(type.toUtf8().constData());
	OBSDataAutoRelease st = obs_data_create();
	int ms = 800;
	if (type == "fade_transition")
		ms = 1500;
	Engine::get().addTransition(type, uniqueTransitionName(label), st, ms);
}

void TransitionsPanel::rebuild()
{
	while (QLayoutItem *it = listLayout->takeAt(0)) {
		delete it->widget();
		delete it;
	}
	const auto &list = Engine::get().userTransitions();
	if (list.isEmpty()) {
		auto *empty = new QLabel("Nenhuma transição ainda.\nClique em ＋ Stinger e escolha o vídeo da vinheta.");
		empty->setObjectName("muted");
		empty->setAlignment(Qt::AlignCenter);
		empty->setMinimumHeight(120);
		listLayout->addWidget(empty);
		return;
	}
	for (StudioTransition *t : list)
		listLayout->addWidget(card(t->id));
}

QWidget *TransitionsPanel::card(int id)
{
	Engine &e = Engine::get();
	StudioTransition *t = e.userTransition(id);
	auto *w = new QFrame;
	w->setObjectName("trCard");
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(10, 8, 10, 8);
	v->setSpacing(6);

	/* linha 1: tipo, nome, atalho, menu */
	auto *h1 = new QHBoxLayout;
	h1->setSpacing(8);
	auto *badge = new QLabel(Engine::transitionTypeName(obs_source_get_unversioned_id(t->source)));
	badge->setObjectName(t->isStinger() ? "trBadgeStinger" : "trBadge");
	h1->addWidget(badge);
	auto *name = new QLabel(t->name);
	name->setObjectName("trName");
	h1->addWidget(name, 1);
	int k = 0, ks = 0;
	for (StudioTransition *x : e.userTransitions()) {
		k++;
		ks += x->isStinger();
		if (x == t)
			break;
	}
	QStringList keys;
	if (t->isStinger() && ks <= 4)
		keys << QString("Stinger%1").arg(ks);
	if (k <= 4)
		keys << QString("Transition%1").arg(k);
	if (!keys.isEmpty()) {
		auto *fn = new QLabel(keys.join(" · "));
		fn->setObjectName("muted");
		fn->setToolTip("Nome da função para atalhos e scripts (Configurações → Atalhos)");
		h1->addWidget(fn);
	}
	auto *more = new QToolButton;
	more->setText(QString::fromUtf8("⋮"));
	more->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(more);
	menu->addAction("Renomear…", this, [this, id] {
		StudioTransition *x = Engine::get().userTransition(id);
		if (!x)
			return;
		bool ok = false;
		const QString n = QInputDialog::getText(this, "Renomear transição", "Nome:", QLineEdit::Normal, x->name, &ok);
		if (ok && !n.trimmed().isEmpty())
			Engine::get().renameTransition(id, uniqueTransitionName(n, id));
	});
	menu->addAction("Opções completas…", this, [this, id] { openOptions(id); });
	menu->addSeparator();
	menu->addAction("Subir", this, [id] { Engine::get().moveTransition(id, -1); });
	menu->addAction("Descer", this, [id] { Engine::get().moveTransition(id, +1); });
	menu->addSeparator();
	menu->addAction("Remover", this, [this, id] {
		StudioTransition *x = Engine::get().userTransition(id);
		if (x && QMessageBox::question(this, "Transições", QString("Remover \"%1\"?").arg(x->name)) ==
				 QMessageBox::Yes)
			Engine::get().removeTransition(id);
	});
	more->setMenu(menu);
	h1->addWidget(more);
	v->addLayout(h1);

	/* linha 2: o que mais se ajusta + usar */
	auto *h2 = new QHBoxLayout;
	h2->setSpacing(8);
	if (!t->isVideoStinger() && t->isStinger()) {
		/* stinger GT: título de origem e tempo cobrindo a tela */
		OBSDataAutoRelease st = obs_source_get_settings(t->source);
		const QString title = QString::fromUtf8(obs_data_get_string(st, "titulo"));
		const QString file = QString::fromUtf8(obs_data_get_string(st, "file"));
		const bool missing = !title.isEmpty() && !e.inputByName(title);
		auto *src = new QLabel(title.isEmpty() ? QFileInfo(file).fileName()
						       : QString::fromUtf8("título: ") + title +
								 (missing ? QString::fromUtf8("  (entrada não existe mais!)") : QString()));
		src->setObjectName("muted");
		src->setToolTip(title.isEmpty() ? file : QString("Usa a entrada de título GT \"%1\"").arg(title));
		src->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		h2->addWidget(src, 1);
		h2->addWidget(new QLabel("Cobrindo"));
		auto *hold = new QSpinBox;
		hold->setRange(0, 10000);
		hold->setSingleStep(50);
		hold->setSuffix(" ms");
		hold->setValue((int)obs_data_get_int(st, "segurar_ms"));
		hold->setToolTip("Quanto tempo o título fica cobrindo a tela entre a entrada e a saída\n"
				 "(a imagem troca no meio desse tempo)");
		connect(hold, &QSpinBox::valueChanged, this, [id](int val) {
			StudioTransition *x = Engine::get().userTransition(id);
			if (!x)
				return;
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_int(s, "segurar_ms", val);
			obs_source_update(x->source, s);
		});
		h2->addWidget(hold);
	} else if (t->isStinger()) {
		OBSDataAutoRelease st = obs_source_get_settings(t->source);
		const QString path = QString::fromUtf8(obs_data_get_string(st, "path"));
		const int dur = videoDurationMs(path);
		auto *file = new QLabel(QFileInfo(path).fileName() +
					(dur > 0 ? QString("  ·  %1 s").arg(dur / 1000.0, 0, 'f', 2) : QString()));
		file->setObjectName("muted");
		file->setToolTip(path);
		file->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		h2->addWidget(file, 1);
		auto *lbl = new QLabel("Troca em");
		h2->addWidget(lbl);
		auto *point = new QSpinBox;
		const bool frames = obs_data_get_int(st, "tp_type") == 1;
		point->setRange(0, dur > 0 ? dur : 60000);
		point->setSingleStep(frames ? 1 : 40);
		point->setSuffix(frames ? " quadros" : " ms");
		point->setValue((int)obs_data_get_int(st, "transition_point"));
		point->setToolTip("Momento do vídeo em que a imagem troca por baixo do stinger\n"
				  "(quando a vinheta cobre a tela toda)");
		connect(point, &QSpinBox::valueChanged, this, [id](int val) {
			StudioTransition *x = Engine::get().userTransition(id);
			if (!x)
				return;
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_int(s, "transition_point", val);
			obs_source_update(x->source, s);
		});
		h2->addWidget(point);
	} else {
		h2->addStretch(1);
		auto *lbl = new QLabel("Duração");
		h2->addWidget(lbl);
		auto *ms = new QSpinBox;
		ms->setRange(50, 20000);
		ms->setSingleStep(100);
		ms->setSuffix(" ms");
		ms->setValue(t->durationMs);
		connect(ms, &QSpinBox::editingFinished, this, [id, ms] { Engine::get().setUserTransitionMs(id, ms->value()); });
		h2->addWidget(ms);
	}
	auto *opts = new QPushButton("Opções…");
	opts->setToolTip(t->isStinger() ? "Arquivo, ponto de troca, máscara (track matte), áudio..."
					: "Direção, desenho do luma, cor...");
	connect(opts, &QPushButton::clicked, this, [this, id] { openOptions(id); });
	h2->addWidget(opts);
	auto *use = new QPushButton("▶ Usar");
	use->setObjectName("greenButton");
	use->setToolTip("Prévia para o Programa com esta transição");
	connect(use, &QPushButton::clicked, this, [id] { Engine::get().transitionWith(id); });
	h2->addWidget(use);
	v->addLayout(h2);
	return w;
}

void TransitionsPanel::openOptions(int id)
{
	StudioTransition *t = Engine::get().userTransition(id);
	if (!t)
		return;
	QDialog dlg(this);
	dlg.setWindowTitle(QString("Transição · %1").arg(t->name));
	dlg.resize(640, 560);
	auto *v = new QVBoxLayout(&dlg);
	OBSSourceAutoRelease src = obs_source_get_ref(t->source);
	auto *props = new PropertiesView(src, obs_source_get_id(src), &dlg);
	v->addWidget(props, 1);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	v->addWidget(bb);
	if (dlg.exec() == QDialog::Accepted && Engine::get().userTransition(id)) {
		props->applyToSource(src);
		emit Engine::get().transitionsChanged(); /* arquivo / ponto de troca podem ter mudado */
	}
}
