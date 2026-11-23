#include "dialogs.hpp"
#include "engine.hpp"
#include "properties-view.hpp"
#include "video-display.hpp"

#include <QDialogButtonBox>
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
	if (props && props->hasChanges())
		props->applyToSource(in->source);
}

/* ---------------- Adicionar entrada ---------------- */

struct TypeDef {
	const char *id;
	const char *label;
};

static const TypeDef typeDefs[] = {
	{"dshow_input", "Câmera / placa de captura"},
	{"decklink-input", "Blackmagic DeckLink"},
	{"aja_source", "AJA"},
	{"ffmpeg_source", "Vídeo, link ou celular"},
	{"vlc_source", "Lista de vídeos"},
	{"image_source", "Imagem"},
	{"slideshow", "Sequência de imagens"},
	{"browser_source", "Página web (navegador)"},
	{"text_gdiplus", "Texto"},
	{"color_source", "Cor"},
	{"monitor_capture", "Captura de tela"},
	{"window_capture", "Captura de janela"},
	{"game_capture", "Captura de jogo"},
	{"wasapi_input_capture", "Microfone / entrada de áudio"},
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

AddInputDialog::AddInputDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Adicionar entrada");
	resize(1180, 680);
	setMinimumSize(960, 540);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);
	auto *body = new QHBoxLayout;
	body->setSpacing(10);

	auto *typeBox = section("Tipo de entrada");
	typeBox->setFixedWidth(250);
	auto *tv = new QVBoxLayout(typeBox);
	types = new QListWidget;
	types->setObjectName("typeList");
	types->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	types->setTextElideMode(Qt::ElideRight);
	for (const TypeDef &t : typeDefs) {
		const QString latest = latestType(t.id);
		if (latest.isEmpty())
			continue;
		typeIds << latest;
		types->addItem(QString::fromUtf8(t.label));
	}
	tv->addWidget(types);
	body->addWidget(typeBox);

	auto *optBox = section("Opções");
	auto *ov = new QVBoxLayout(optBox);
	auto *form = new QFormLayout;
	nameEdit = new QLineEdit;
	form->addRow("Nome", nameEdit);
	ov->addLayout(form);
	propsHolder = new QWidget;
	auto *ph = new QVBoxLayout(propsHolder);
	ph->setContentsMargins(0, 0, 0, 0);
	ov->addWidget(propsHolder, 1);
	body->addWidget(optBox, 1);

	auto *pvBox = section("Prévia");
	pvBox->setFixedWidth(380);
	auto *pv = new QVBoxLayout(pvBox);
	video = new VideoDisplay;
	video->setFixedSize(352, 198);
	pv->addWidget(video);
	videoHint = new QLabel("Escolha as opções e clique em \"Testar imagem\" para ver antes de adicionar.");
	videoHint->setObjectName("muted");
	videoHint->setWordWrap(true);
	pv->addWidget(videoHint);
	testBtn = new QPushButton("Testar imagem");
	connect(testBtn, &QPushButton::clicked, this, &AddInputDialog::testImage);
	pv->addWidget(testBtn);
	pv->addStretch(1);
	body->addWidget(pvBox);
	root->addLayout(body, 1);

	auto *bb = new QDialogButtonBox;
	auto *addBtn = bb->addButton("Adicionar", QDialogButtonBox::AcceptRole);
	addBtn->setObjectName("primaryButton");
	bb->addButton("Cancelar", QDialogButtonBox::RejectRole);
	connect(bb, &QDialogButtonBox::accepted, this, &AddInputDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &AddInputDialog::reject);
	root->addWidget(bb);

	connect(types, &QListWidget::currentRowChanged, this, &AddInputDialog::selectType);
	if (types->count())
		types->setCurrentRow(0);
}

AddInputDialog::~AddInputDialog()
{
	dropTemp();
}

void AddInputDialog::dropTemp()
{
	video->clear();
	if (temp) {
		obs_source_dec_showing(temp);
		obs_source_remove(temp);
		temp = nullptr;
	}
	testBtn->setText("Testar imagem");
}

/* trocar o tipo só monta o formulário: nenhuma câmera é aberta até testar ou adicionar */
void AddInputDialog::selectType(int row)
{
	dropTemp();
	delete props;
	props = nullptr;
	if (row < 0 || row >= typeIds.size())
		return;
	currentType = typeIds[row];
	const QByteArray id = currentType.toUtf8();
	props = new PropertiesView(nullptr, id.constData());
	propsHolder->layout()->addWidget(props);
	QString base = types->item(row)->text().section(' ', 0, 0);
	base.remove(QRegularExpression("[^\\p{L}\\p{N}]"));
	nameEdit->setText(base.toUpper() + " " + QString::number(Engine::get().inputs().size() + 1));
}

void AddInputDialog::testImage()
{
	if (!props)
		return;
	if (temp) {
		/* já testando: aplica as opções atuais na fonte de teste */
		props->applyToSource(temp);
		return;
	}
	const QByteArray id = currentType.toUtf8();
	temp = obs_source_create_private(id.constData(), "teste", props->settings());
	if (!temp) {
		QMessageBox::warning(this, "Adicionar entrada", "Não foi possível abrir esta entrada com essas opções.");
		return;
	}
	props->applyToSource(nullptr);
	obs_source_inc_showing(temp);
	video->showSource(temp);
	testBtn->setText("Atualizar teste");
}

void AddInputDialog::accept()
{
	if (!props)
		return;
	const QString name = nameEdit->text().trimmed().isEmpty() ? QString("ENTRADA") : nameEdit->text().trimmed();
	video->clear();
	if (temp) {
		/* reaproveita a fonte de teste (a câmera já está aberta) com as opções finais */
		if (props->hasChanges())
			props->applyToSource(temp);
		obs_source_dec_showing(temp);
		Engine::get().adoptSource(temp.Get(), name);
		obs_source_get_ref(temp); /* a entrada ficou com uma referência; a nossa sai abaixo */
		temp = nullptr;
	} else {
		Engine::get().addInput(currentType, name, props->settings());
	}
	QDialog::accept();
}

void AddInputDialog::reject()
{
	dropTemp();
	QDialog::reject();
}
