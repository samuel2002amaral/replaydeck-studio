#include "dialogs.hpp"
#include "engine.hpp"
#include "properties-view.hpp"
#include "video-display.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
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

InputWindow::InputWindow(int inputId, QWidget *parent) : QDialog(parent), id(inputId)
{
	Input *in = Engine::get().input(id);
	setWindowTitle(QString("%1 · %2").arg(Engine::get().indexOf(id) + 1).arg(in ? in->name : QString()));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(1100, 640);

	auto *root = new QHBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(10);

	auto *left = new QVBoxLayout;
	auto *note = new QLabel("Ao vivo nesta janela — a prévia e o programa não mudam.");
	note->setObjectName("muted");
	left->addWidget(note);
	auto *video = new VideoDisplay;
	video->setMinimumSize(640, 360);
	video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	if (in)
		video->showSource(in->scene);
	left->addWidget(video, 1);
	auto *btns = new QHBoxLayout;
	auto *toPvw = new QPushButton("Mandar para Prévia");
	auto *toPgm = new QPushButton("Cortar para Programa");
	auto *ov1 = new QPushButton("Overlay 1");
	connect(toPvw, &QPushButton::clicked, this, [this] { Engine::get().setPreview(id); });
	connect(toPgm, &QPushButton::clicked, this, [this] { Engine::get().cutTo(id); });
	connect(ov1, &QPushButton::clicked, this, [this] { Engine::get().toggleOverlay(1, id); });
	btns->addWidget(toPvw);
	btns->addWidget(toPgm);
	btns->addWidget(ov1);
	btns->addStretch(1);
	left->addLayout(btns);
	root->addLayout(left, 1);

	auto *right = new QVBoxLayout;
	auto *form = new QFormLayout;
	nameEdit = new QLineEdit(in ? in->name : QString());
	connect(nameEdit, &QLineEdit::editingFinished, this, [this] {
		if (!nameEdit->text().trimmed().isEmpty())
			Engine::get().renameInput(id, nameEdit->text().trimmed());
	});
	form->addRow("Nome", nameEdit);
	if (in)
		form->addRow("Tipo", new QLabel(Engine::typeDisplayName(obs_source_get_id(in->source))));
	right->addLayout(form);
	if (in) {
		auto *props = new PropertiesView(in->source, nullptr, obs_source_get_id(in->source));
		props->setMinimumWidth(340);
		right->addWidget(props, 1);
	}
	auto *closeBox = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::close);
	right->addWidget(closeBox);
	root->addLayout(right);

	connect(&Engine::get(), &Engine::inputsChanged, this, [this] {
		if (!Engine::get().input(id))
			close();
	});
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
	{"ffmpeg_source", "Vídeo, link ou celular (SRT/RTMP)"},
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
	setMinimumSize(900, 520);

	auto *root = new QVBoxLayout(this);
	auto *body = new QHBoxLayout;
	types = new QListWidget;
	types->setObjectName("typeList");
	types->setFixedWidth(240);
	types->setSpacing(1);
	for (const TypeDef &t : typeDefs) {
		const QString latest = latestType(t.id);
		if (latest.isEmpty())
			continue;
		typeIds << latest;
		types->addItem(QString::fromUtf8(t.label));
	}
	body->addWidget(types);

	auto *center = new QVBoxLayout;
	auto *form = new QFormLayout;
	nameEdit = new QLineEdit;
	form->addRow("Nome", nameEdit);
	center->addLayout(form);
	propsHolder = new QWidget;
	auto *ph = new QVBoxLayout(propsHolder);
	ph->setContentsMargins(0, 0, 0, 0);
	center->addWidget(propsHolder, 1);
	body->addLayout(center, 1);

	auto *right = new QVBoxLayout;
	auto *lbl = new QLabel("Prévia");
	lbl->setObjectName("muted");
	right->addWidget(lbl);
	video = new VideoDisplay;
	video->setFixedSize(352, 198);
	right->addWidget(video);
	right->addStretch(1);
	body->addLayout(right);
	root->addLayout(body, 1);

	auto *bb = new QDialogButtonBox;
	bb->addButton("Adicionar", QDialogButtonBox::AcceptRole);
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
	delete props;
	props = nullptr;
	if (temp) {
		obs_source_dec_showing(temp);
		obs_source_remove(temp);
		temp = nullptr;
	}
}

void AddInputDialog::selectType(int row)
{
	dropTemp();
	if (row < 0 || row >= typeIds.size())
		return;
	const QByteArray id = typeIds[row].toUtf8();
	temp = obs_source_create_private(id.constData(), "nova entrada", nullptr);
	if (!temp) {
		QMessageBox::warning(this, "Adicionar entrada", "Este tipo de entrada não está disponível neste computador.");
		return;
	}
	obs_source_inc_showing(temp);
	video->showSource(temp);
	props = new PropertiesView(temp, nullptr, id.constData());
	propsHolder->layout()->addWidget(props);
	QString base = types->item(row)->text().section(' ', 0, 0);
	base.remove(QRegularExpression("[^\\p{L}\\p{N}]"));
	nameEdit->setText(base.toUpper() + " " + QString::number(Engine::get().inputs().size() + 1));
}

void AddInputDialog::accept()
{
	if (!temp)
		return;
	const QString name = nameEdit->text().trimmed().isEmpty() ? QString("ENTRADA") : nameEdit->text().trimmed();
	video->clear();
	delete props;
	props = nullptr;
	obs_source_dec_showing(temp);
	Engine::get().adoptSource(temp.Get(), name);
	obs_source_get_ref(temp); /* a entrada ficou com uma referência; a nossa sai abaixo */
	temp = nullptr;
	QDialog::accept();
}

void AddInputDialog::reject()
{
	dropTemp();
	QDialog::reject();
}
