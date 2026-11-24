#include "dialogs.hpp"
#include "engine.hpp"
#include "properties-view.hpp"
#include "video-display.hpp"

#include <QDialogButtonBox>
#include <QTimer>
#include <QListWidgetItem>
#include <QScrollArea>
#include <QPainter>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QVBoxLayout>

#include <cstring>

/* ---------------- Entrada aberta ---------------- */

static QGroupBox *section(const QString &title)
{
	auto *g = new QGroupBox(title.toUpper());
	g->setObjectName("section");
	return g;
}

InputWindow::InputWindow(int inputId, QWidget *parent) : QDialog(parent), id(inputId)
{
	Input *in = Engine::get().input(id);
	setWindowTitle(QString("%1 · %2").arg(Engine::get().indexOf(id) + 1).arg(in ? in->name : QString()));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(1180, 680);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);
	auto *body = new QHBoxLayout;
	body->setSpacing(10);

	auto *live = section("Ao vivo — a prévia e o programa não mudam");
	auto *lv = new QVBoxLayout(live);
	auto *video = new VideoDisplay;
	video->setMinimumSize(560, 315);
	video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	if (in)
		video->showSource(in->scene);
	lv->addWidget(video, 1);
	auto *btns = new QHBoxLayout;
	auto *toPvw = new QPushButton("Mandar para Prévia");
	toPvw->setObjectName("greenButton");
	auto *toPgm = new QPushButton("Cortar para Programa");
	toPgm->setObjectName("redButton");
	auto *ov1 = new QPushButton("Overlay 1");
	connect(toPvw, &QPushButton::clicked, this, [this] { Engine::get().setPreview(id); });
	connect(toPgm, &QPushButton::clicked, this, [this] { Engine::get().cutTo(id); });
	connect(ov1, &QPushButton::clicked, this, [this] { Engine::get().toggleOverlay(1, id); });
	btns->addWidget(toPvw);
	btns->addWidget(toPgm);
	btns->addWidget(ov1);
	btns->addStretch(1);
	auto *onOff = new QPushButton;
	onOff->setToolTip("Só esta entrada fica preta. A câmera continua ligada nas outras entradas que a usam;\n"
			  "ela só desliga de verdade quando todas estiverem desativadas.");
	auto paintOnOff = [this, onOff] {
		Input *cur = Engine::get().input(id);
		onOff->setText(cur && !cur->enabled ? "Ativar entrada" : "Desativar entrada");
	};
	paintOnOff();
	connect(onOff, &QPushButton::clicked, this, [this] {
		if (Input *cur = Engine::get().input(id))
			Engine::get().setInputEnabled(id, !cur->enabled);
	});
	connect(&Engine::get(), &Engine::inputChanged, onOff, [this, paintOnOff](int changed) {
		if (changed == id)
			paintOnOff();
	});
	btns->addWidget(onOff);
	lv->addLayout(btns);
	body->addWidget(live, 1);

	auto *opts = section("Opções");
	opts->setMinimumWidth(420);
	auto *ov = new QVBoxLayout(opts);
	auto *form = new QFormLayout;
	nameEdit = new QLineEdit(in ? in->name : QString());
	form->addRow("Nome", nameEdit);
	if (in)
		form->addRow("Tipo", new QLabel(Engine::typeDisplayName(obs_source_get_id(in->source))));
	ov->addLayout(form);
	if (in) {
		/* mesma câmera em outras entradas: as opções valem para todas (Desativar entrada não) */
		QStringList others;
		for (Input *o : Engine::get().inputs())
			if (o->id != id && o->source.Get() == in->source.Get())
				others << QString("%1 (%2)").arg(Engine::get().indexOf(o->id) + 1).arg(o->name);
		if (!others.isEmpty()) {
			auto *warn = new QLabel("Este aparelho também está em: " + others.join(", ") +
						".\nÉ a mesma câmera ligada uma vez só: as opções daqui valem para todas.\n"
						"\"Desativar entrada\" vale só para esta.");
			warn->setObjectName("muted");
			warn->setWordWrap(true);
			ov->addWidget(warn);
		}
		props = new PropertiesView(in->source, obs_source_get_id(in->source));
		ov->addWidget(props, 1);
	}
	body->addWidget(opts);
	root->addLayout(body, 1);

	auto *bb = new QDialogButtonBox;
	auto *ok = bb->addButton("OK", QDialogButtonBox::AcceptRole);
	ok->setObjectName("primaryButton");
	auto *applyBtn = bb->addButton("Aplicar", QDialogButtonBox::ApplyRole);
	bb->addButton("Cancelar", QDialogButtonBox::RejectRole);
	connect(bb, &QDialogButtonBox::accepted, this, [this] {
		applyChanges();
		close();
	});
	connect(applyBtn, &QPushButton::clicked, this, &InputWindow::applyChanges);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::close);
	root->addWidget(bb);

	connect(&Engine::get(), &Engine::inputsChanged, this, [this] {
		if (!Engine::get().input(id))
			close();
	});
}

void InputWindow::applyChanges()
{
	Input *in = Engine::get().input(id);
	if (!in)
		return;
	if (!nameEdit->text().trimmed().isEmpty() && nameEdit->text().trimmed() != in->name)
		Engine::get().renameInput(id, nameEdit->text().trimmed());
	if (props && props->hasChanges()) {
		props->applyToSource(in->source);
		Engine::get().mergeSameDevice(); /* trocou para um aparelho que já está em outra entrada? */
	}
}

/* ---------------- Adicionar entrada ---------------- */

struct TypeDef {
	const char *id;
	const char *label;
};

static const TypeDef typeDefs[] = {
	{"instant_replay_output", "Replay (saída A ou B)"},
	{"dshow_input", "Câmera / placa de captura"},
	{"decklink-input", "Blackmagic DeckLink"},
	{"aja_source", "AJA"},
	{"ffmpeg_source", "Vídeo, link ou celular"},
	{"vlc_source", "Lista de vídeos"},
	{"image_source", "Imagem"},
	{"slideshow", "Sequência de imagens"},
	{"browser_source", "Página web (navegador)"},
	{"instant_replay_gt", "Título GT (.gtzip do vMix)"},
	{"text_gdiplus", "Texto"},
	{"color_source", "Cor"},
	{"monitor_capture", "Captura de tela"},
	{"window_capture", "Captura de janela"},
	{"game_capture", "Captura de jogo"},
	{"wasapi_input_capture", "Microfone / entrada de áudio"},
	{"wasapi_output_capture", "Áudio do computador"},
	{"ndi_source", "NDI"},
	{"v4l2_input", "Câmera (Linux)"},
};

/* versão mais nova do tipo (ex.: color_source -> color_source_v3), ou vazio se não existir aqui */
static QString latestType(const char *unversioned)
{
	QString found;
	const char *id = nullptr, *unver = nullptr;
	for (size_t i = 0; obs_enum_input_types2(i, &id, &unver); i++) {
		if (unver && strcmp(unver, unversioned) == 0 && !(obs_get_source_output_flags(id) & OBS_SOURCE_DEPRECATED))
			found = QString::fromUtf8(id);
	}
	return found;
}

/* nome sem versão de um tipo (ex.: color_source_v3 -> color_source) */
static QByteArray unversionedOf(const QByteArray &typeId)
{
	const char *id = nullptr, *unver = nullptr;
	for (size_t i = 0; obs_enum_input_types2(i, &id, &unver); i++)
		if (id && typeId == id)
			return QByteArray(unver ? unver : id);
	return typeId;
}

/* ícones simples desenhados na hora (sem arquivos de imagem) */
QIcon inputTypeIcon(const QString &typeId)
{
	const QString t = QString::fromUtf8(unversionedOf(typeId.toUtf8()));
	QPixmap pm(32, 32);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	QPen pen(QColor(0xc9, 0xd2, 0xde), 2.4);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	if (t == "dshow_input" || t == "v4l2_input" || t == "decklink-input" || t == "aja_source") {
		p.drawRoundedRect(QRectF(4, 10, 17, 13), 3, 3);
		p.drawPolygon(QPolygonF({QPointF(21, 14), QPointF(28, 10), QPointF(28, 23), QPointF(21, 19)}));
	} else if (t == "ffmpeg_source" || t == "vlc_source") {
		p.setBrush(QColor(0xc9, 0xd2, 0xde));
		p.drawPolygon(QPolygonF({QPointF(10, 7), QPointF(25, 16), QPointF(10, 25)}));
	} else if (t == "image_source" || t == "slideshow") {
		p.drawRoundedRect(QRectF(5, 7, 22, 18), 3, 3);
		p.drawPolyline(QPolygonF({QPointF(7, 23), QPointF(13, 16), QPointF(18, 21), QPointF(21, 18), QPointF(26, 23)}));
		p.drawEllipse(QPointF(20, 12), 2, 2);
	} else if (t == "browser_source") {
		p.drawEllipse(QPointF(16, 16), 11, 11);
		p.drawEllipse(QRectF(11, 5, 10, 22));
		p.drawLine(QPointF(5, 16), QPointF(27, 16));
	} else if (t == "instant_replay_gt") {
		/* tarja de título (lower third) */
		p.drawLine(QPointF(5, 25), QPointF(27, 25));
		p.setBrush(QColor(0xc2, 0x74, 0x14));
		p.drawRoundedRect(QRectF(5, 15, 19, 7), 2, 2);
	} else if (t == "text_gdiplus" || t == "text_ft2_source") {
		p.drawLine(QPointF(7, 8), QPointF(25, 8));
		p.drawLine(QPointF(16, 8), QPointF(16, 25));
	} else if (t == "color_source") {
		p.setBrush(QColor(0x25, 0x63, 0xeb));
		p.drawRoundedRect(QRectF(6, 6, 20, 20), 4, 4);
	} else if (t == "monitor_capture" || t == "window_capture" || t == "game_capture") {
		p.drawRoundedRect(QRectF(4, 6, 24, 16), 2, 2);
		p.drawLine(QPointF(12, 26), QPointF(20, 26));
		p.drawLine(QPointF(16, 22), QPointF(16, 26));
	} else if (t.startsWith("wasapi")) {
		p.drawRoundedRect(QRectF(12, 4, 8, 14), 4, 4);
		p.drawArc(QRectF(8, 8, 16, 14), 180 * 16, 180 * 16);
		p.drawLine(QPointF(16, 22), QPointF(16, 27));
	} else {
		p.drawRoundedRect(QRectF(6, 6, 20, 20), 4, 4);
	}
	return QIcon(pm);
}

AddInputDialog::AddInputDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Adicionar entrada");
	resize(1040, 640);
	setMinimumSize(820, 520);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);
	auto *body = new QHBoxLayout;
	body->setSpacing(10);

	types = new QListWidget;
	types->setObjectName("typeList");
	types->setFixedWidth(280);
	types->setIconSize(QSize(18, 18));
	types->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	types->setTextElideMode(Qt::ElideRight);
	for (const TypeDef &t : typeDefs) {
		const QString latest = latestType(t.id);
		if (latest.isEmpty())
			continue;
		typeIds << latest;
		typeLabels << QString::fromUtf8(t.label);
		types->addItem(new QListWidgetItem(inputTypeIcon(latest), QString::fromUtf8(t.label)));
	}
	/* os outros tipos do motor (plugins do OBS, por exemplo) no fim da lista */
	{
		QSet<QByteArray> known;
		for (const TypeDef &t : typeDefs)
			known.insert(QByteArray(t.id));
		static const QSet<QByteArray> internal = {"scene", "group", "instant_replay_source",
							  "rds_program_mirror", "audio_line"};
		QList<QPair<QString, QString>> extra; /* rótulo, id */
		const char *id = nullptr, *unver = nullptr;
		for (size_t i = 0; obs_enum_input_types2(i, &id, &unver); i++) {
			const uint32_t fl = obs_get_source_output_flags(id);
			const QByteArray u(unver ? unver : id);
			if (known.contains(u) || internal.contains(u) || u.startsWith("instant_replay") || u.startsWith("rds_") ||
			    (fl & (OBS_SOURCE_DEPRECATED | OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_CAP_OBSOLETE)))
				continue;
			if (latestType(u.constData()) != QString::fromUtf8(id))
				continue; /* só a versão mais nova */
			const char *dn = obs_source_get_display_name(id);
			if (dn)
				extra << qMakePair(QString::fromUtf8(dn), QString::fromUtf8(id));
		}
		std::sort(extra.begin(), extra.end());
		for (const auto &x : extra) {
			typeIds << x.second;
			typeLabels << x.first;
			auto *it = new QListWidgetItem(inputTypeIcon(x.second), x.first);
			it->setToolTip("Tipo do motor ou de plugin: " + x.second);
			types->addItem(it);
		}
	}
	body->addWidget(types);

	auto *right = new QWidget;
	right->setObjectName("panel");
	auto *rv = new QVBoxLayout(right);
	rv->setContentsMargins(14, 12, 14, 12);
	rv->setSpacing(8);
	title = new QLabel;
	title->setObjectName("dialogTitle");
	rv->addWidget(title);
	auto *sub = new QLabel("Crie uma entrada nova ou abra uma deste tipo que já está no projeto.");
	sub->setObjectName("muted");
	rv->addWidget(sub);
	newBtn = new QPushButton;
	newBtn->setObjectName("addNewButton");
	newBtn->setMinimumHeight(54);
	connect(newBtn, &QPushButton::clicked, this, &AddInputDialog::addNew);
	rv->addWidget(newBtn);

	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	grid = new QWidget;
	auto *gl = new QGridLayout(grid);
	gl->setContentsMargins(0, 4, 0, 0);
	gl->setSpacing(8);
	gl->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	scroll->setWidget(grid);
	rv->addWidget(scroll, 1);

	openBtn = new QPushButton("Abrir existente");
	openBtn->setEnabled(false);
	connect(openBtn, &QPushButton::clicked, this, [this] {
		if (!Engine::get().input(selectedExisting))
			return;
		auto *w = new InputWindow(selectedExisting, parentWidget());
		w->show();
		QDialog::accept();
	});
	auto *row = new QHBoxLayout;
	row->addWidget(openBtn);
	row->addStretch(1);
	rv->addLayout(row);
	body->addWidget(right, 1);
	root->addLayout(body, 1);

	auto *bb = new QDialogButtonBox;
	bb->addButton("Fechar", QDialogButtonBox::RejectRole);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(bb);

	connect(types, &QListWidget::currentRowChanged, this, &AddInputDialog::selectType);
	if (types->count())
		types->setCurrentRow(0);
}

/* trocar o tipo só monta a lista: nada é aberto aqui */
void AddInputDialog::selectType(int row)
{
	auto *gl = static_cast<QGridLayout *>(grid->layout());
	while (QLayoutItem *it = gl->takeAt(0)) {
		delete it->widget();
		delete it;
	}
	selectedExisting = 0;
	openBtn->setEnabled(false);
	if (row < 0 || row >= typeIds.size())
		return;
	title->setText(typeLabels[row]);
	newBtn->setText("+  Adicionar nova entrada: " + typeLabels[row]);

	const QByteArray type = typeIds[row].toUtf8();
	const QByteArray unver = unversionedOf(type);
	int n = 0;
	for (Input *in : Engine::get().inputs()) {
		if (unver != obs_source_get_unversioned_id(in->source))
			continue;
		auto *card = new QPushButton;
		card->setObjectName("existingCard");
		card->setCheckable(true);
		card->setAutoExclusive(true);
		card->setFixedSize(180, 128);
		auto *cv = new QVBoxLayout(card);
		cv->setContentsMargins(5, 5, 5, 5);
		cv->setSpacing(4);
		auto *thumb = new VideoDisplay;
		thumb->setFixedSize(168, 94);
		thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
		thumb->showSource(in->scene);
		cv->addWidget(thumb);
		auto *name = new QLabel(in->name);
		name->setAttribute(Qt::WA_TransparentForMouseEvents);
		cv->addWidget(name);
		const int id = in->id;
		connect(card, &QPushButton::clicked, this, [this, id] {
			selectedExisting = id;
			openBtn->setEnabled(true);
		});
		gl->addWidget(card, n / 3, n % 3);
		n++;
	}
	if (n == 0) {
		auto *none = new QLabel("Nenhuma entrada deste tipo no projeto ainda.");
		none->setObjectName("muted");
		gl->addWidget(none, 0, 0);
	}
}

void AddInputDialog::addNew()
{
	const int row = types->currentRow();
	if (row < 0)
		return;
	QWidget *owner = parentWidget();
	const QString id = typeIds[row], label = typeLabels[row];
	QDialog::accept();
	NewInputDialog d(id, label, owner);
	d.exec();
}

/* ---------------- Nova entrada ---------------- */

NewInputDialog::NewInputDialog(const QString &typeId, const QString &label, QWidget *parent)
	: QDialog(parent),
	  type(typeId)
{
	setWindowTitle("Nova entrada · " + label);
	resize(760, 760);
	setMinimumSize(620, 560);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);

	video = new VideoDisplay;
	video->setMinimumHeight(300);
	video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	root->addWidget(video, 3);
	status = new QLabel("Ligando…");
	status->setObjectName("muted");
	root->addWidget(status);
	/* a câmera leva um instante para mandar a primeira imagem: avisa até ela chegar */
	waitTimer = new QTimer(this);
	waitTimer->setInterval(200);
	connect(waitTimer, &QTimer::timeout, this, [this] {
		if (source && obs_source_get_width(source) > 0) {
			status->setText("Imagem ao vivo. As opções valem para o projeto quando você clicar em OK.");
			waitTimer->stop();
		} else if (source && !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) {
			status->setText("Entrada só de áudio.");
			waitTimer->stop();
		}
	});

	auto *form = new QFormLayout;
	nameEdit = new QLineEdit;
	QString base = label.section(' ', 0, 0);
	base.remove(QRegularExpression("[^\\p{L}\\p{N}]"));
	nameEdit->setText(base.toUpper() + " " + QString::number(Engine::get().inputs().size() + 1));
	form->addRow("Nome", nameEdit);
	root->addLayout(form);

	/* opções já preparadas em segundo plano desde que o programa abriu: aparecem na hora */
	const QByteArray id = type.toUtf8();
	props = PropertiesView::create(id.constData());
	root->addWidget(props, 4);

	if (props->isLoading())
		connect(props, &PropertiesView::loaded, this, &NewInputDialog::syncPreview);
	else
		syncPreview();

	/* a prévia acompanha as opções (com um pequeno atraso para não reabrir a cada tecla).
	 * Nada entra no projeto antes do OK; Cancelar descarta tudo. */
	auto *debounce = new QTimer(this);
	debounce->setSingleShot(true);
	debounce->setInterval(400);
	connect(debounce, &QTimer::timeout, this, [this] {
		if (!props->isLoading())
			syncPreview();
	});
	connect(props, &PropertiesView::changed, debounce, qOverload<>(&QTimer::start));

	auto *bb = new QDialogButtonBox;
	auto *reset = bb->addButton("Redefinir", QDialogButtonBox::ResetRole);
	auto *ok = bb->addButton("OK", QDialogButtonBox::AcceptRole);
	ok->setObjectName("primaryButton");
	bb->addButton("Cancelar", QDialogButtonBox::RejectRole);
	connect(reset, &QPushButton::clicked, this, [this, debounce] {
		props->resetToDefaults();
		debounce->start();
	});
	connect(bb, &QDialogButtonBox::accepted, this, &NewInputDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &NewInputDialog::reject);
	root->addWidget(bb);
}

NewInputDialog::~NewInputDialog()
{
	drop();
}

/* Mostra a imagem: se o aparelho escolhido já está em outra entrada, usa a imagem que já está
 * rodando (o Windows só deixa abrir uma câmera uma vez); senão abre a entrada nova. */
void NewInputDialog::syncPreview()
{
	const QByteArray unver = unversionedOf(type.toUtf8());
	const QString key = Engine::deviceKey(unver.constData(), props->settings());
	const int used = Engine::get().inputUsingDevice(unver.constData(), key);
	if (used) {
		drop();
		sharedId = used;
		Input *in = Engine::get().input(used);
		video->showSource(in->scene);
		waitTimer->stop();
		status->setText(QString("Este aparelho já está na entrada %1 (%2): a nova entrada vai usar a mesma imagem, "
					"sem abrir de novo.")
					.arg(Engine::get().indexOf(used) + 1)
					.arg(in->name));
		props->applyToSource(nullptr);
		return;
	}
	sharedId = 0;
	if (!source) {
		const QByteArray tid = type.toUtf8();
		source = obs_source_create_private(tid.constData(), "nova entrada", props->settings());
		if (!source)
			return;
		props->applyToSource(nullptr); /* já abriu com estas opções */
		obs_source_inc_showing(source);
		video->showSource(source);
		status->setText("Ligando…");
		waitTimer->start();
	} else if (props->hasChanges()) {
		props->applyToSource(source);
	}
}

void NewInputDialog::drop()
{
	video->clear();
	if (source) {
		obs_source_dec_showing(source);
		obs_source_remove(source);
		source = nullptr;
	}
}

void NewInputDialog::accept()
{
	const QString name = nameEdit->text().trimmed().isEmpty() ? QString("ENTRADA") : nameEdit->text().trimmed();
	video->clear();
	if (sharedId && Engine::get().input(sharedId)) {
		Engine::get().addSharedInput(sharedId, name);
	} else if (source) {
		if (props->hasChanges())
			props->applyToSource(source);
		obs_source_dec_showing(source);
		Engine::get().adoptSource(source.Get(), name);
		obs_source_get_ref(source); /* a entrada ficou com uma referência; a nossa sai abaixo */
		source = nullptr;
	} else {
		Engine::get().addInput(type, name, props->settings()); /* OK antes de terminar a busca */
	}
	QDialog::accept();
}

void NewInputDialog::reject()
{
	drop();
	QDialog::reject();
}
