#include "settings-dialog.hpp"
#include "engine.hpp"
#include "outputs.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

static QWidget *page(QTabWidget *tabs, const QString &title, QFormLayout *&form, const QString &hint = QString())
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(14, 12, 14, 12);
	if (!hint.isEmpty()) {
		auto *h = new QLabel(hint);
		h->setObjectName("muted");
		h->setWordWrap(true);
		v->addWidget(h);
	}
	form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	form->setHorizontalSpacing(12);
	form->setVerticalSpacing(8);
	v->addLayout(form);
	v->addStretch(1);
	tabs->addTab(w, title);
	return w;
}

static void fillEncoders(QComboBox *box, const QString &current)
{
	box->addItem("Automático (placa de vídeo, se houver)", "auto");
	for (const auto &e : availableVideoEncoders())
		box->addItem(e.second, e.first);
	const int i = box->findData(current);
	box->setCurrentIndex(i < 0 ? 0 : i);
}

SettingsDialog::SettingsDialog(QWidget *parent, int startTab) : QDialog(parent)
{
	setWindowTitle("Configurações");
	setMinimumWidth(620);
	const StudioSettings s = Outputs::get().settings();
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	tabs = new QTabWidget;
	root->addWidget(tabs, 1);

	/* ---- vídeo ---- */
	QFormLayout *f;
	page(tabs, "Vídeo", f, "Tamanho e quadros por segundo do programa (o que vai para a gravação, a transmissão e o replay). "
			      "Só muda com tudo parado.");
	resolution = new QComboBox;
	for (const QSize &sz : {QSize(1920, 1080), QSize(1280, 720), QSize(3840, 2160), QSize(2560, 1440), QSize(1080, 1920)})
		resolution->addItem(QString("%1 × %2%3")
					    .arg(sz.width())
					    .arg(sz.height())
					    .arg(sz == QSize(1080, 1920) ? "  (vertical)" : sz == QSize(3840, 2160) ? "  (4K)" : ""),
				    sz);
	const QSize cur(s.width, s.height);
	if (resolution->findData(cur) < 0)
		resolution->addItem(QString("%1 × %2").arg(s.width).arg(s.height), cur);
	resolution->setCurrentIndex(resolution->findData(cur));
	f->addRow("Resolução", resolution);
	fps = new QComboBox;
	const QList<QPair<QString, QPoint>> rates = {{"25", {25, 1}},          {"29,97", {30000, 1001}}, {"30", {30, 1}},
						     {"50", {50, 1}},          {"59,94", {60000, 1001}}, {"60", {60, 1}}};
	for (const auto &r : rates)
		fps->addItem(r.first + " quadros/s", r.second);
	const int fi = fps->findData(QPoint(s.fpsNum, s.fpsDen));
	fps->setCurrentIndex(fi < 0 ? 2 : fi);
	f->addRow("Quadros por segundo", fps);

	/* ---- gravação ---- */
	page(tabs, "Gravação", f, "O botão Gravar grava o Programa (com o áudio do Master) num arquivo.");
	recFolder = new QLineEdit(s.recFolder);
	recFolder->setPlaceholderText(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ReplayDeck Studio");
	auto *browse = new QPushButton("Escolher…");
	connect(browse, &QPushButton::clicked, this, [this] {
		const QString d = QFileDialog::getExistingDirectory(this, "Pasta das gravações", recFolder->text());
		if (!d.isEmpty())
			recFolder->setText(d);
	});
	auto *row = new QHBoxLayout;
	row->addWidget(recFolder, 1);
	row->addWidget(browse);
	f->addRow("Pasta", row);
	recFormat = new QComboBox;
	recFormat->addItem("MP4 (abre em qualquer lugar; não corrompe se cair a energia)", "mp4");
	recFormat->addItem("MKV", "mkv");
	recFormat->addItem("MOV", "mov");
	recFormat->setCurrentIndex(qMax(0, recFormat->findData(s.recFormat)));
	f->addRow("Formato", recFormat);
	recEncoder = new QComboBox;
	fillEncoders(recEncoder, s.recEncoder);
	f->addRow("Codificador", recEncoder);
	recKbps = new QSpinBox;
	recKbps->setRange(1000, 150000);
	recKbps->setSingleStep(1000);
	recKbps->setSuffix(" kbps");
	recKbps->setValue(s.recBitrate);
	recKbps->setToolTip("15000 é ótimo para 1080p; 40000 ou mais para 4K");
	f->addRow("Qualidade", recKbps);

	/* ---- transmissão ---- */
	page(tabs, "Transmissão", f, "O botão Transmitir envia o Programa ao vivo. Pegue a chave no painel do YouTube/Facebook.");
	service = new QComboBox;
	for (const StreamPreset &p : streamPresets())
		service->addItem(QString::fromUtf8(p.name), QString::fromUtf8(p.server));
	service->setCurrentIndex(qMax(0, service->findText(s.streamService)));
	f->addRow("Serviço", service);
	server = new QLineEdit(s.streamServer);
	server->setPlaceholderText("rtmp://…");
	f->addRow("Servidor", server);
	connect(service, &QComboBox::currentIndexChanged, this, [this](int) {
		const QString url = service->currentData().toString();
		if (!url.isEmpty())
			server->setText(url);
	});
	key = new QLineEdit(s.streamKey);
	key->setEchoMode(QLineEdit::Password);
	key->setPlaceholderText("chave da transmissão");
	auto *show = new QCheckBox("mostrar");
	connect(show, &QCheckBox::toggled, this, [this](bool on) { key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
	auto *krow = new QHBoxLayout;
	krow->addWidget(key, 1);
	krow->addWidget(show);
	f->addRow("Chave", krow);
	streamEncoder = new QComboBox;
	fillEncoders(streamEncoder, s.streamEncoder);
	f->addRow("Codificador", streamEncoder);
	streamKbps = new QSpinBox;
	streamKbps->setRange(500, 50000);
	streamKbps->setSingleStep(500);
	streamKbps->setSuffix(" kbps");
	streamKbps->setValue(s.streamBitrate);
	streamKbps->setToolTip("YouTube 1080p30: 6000 a 9000. Deixe uma folga na sua internet de subida.");
	f->addRow("Qualidade", streamKbps);

	/* ---- áudio ---- */
	page(tabs, "Áudio", f, "Fone: onde tocam as entradas com o botão Fone ligado no mixer.");
	monitor = new QComboBox;
	monitor->addItem("Padrão do Windows", QStringList{"default", ""});
	obs_enum_audio_monitoring_devices(
		[](void *p, const char *name, const char *id) {
			auto *box = static_cast<QComboBox *>(p);
			if (strcmp(id, "default") != 0)
				box->addItem(QString::fromUtf8(name), QStringList{QString::fromUtf8(id), QString::fromUtf8(name)});
			return true;
		},
		monitor);
	for (int i = 0; i < monitor->count(); i++)
		if (monitor->itemData(i).toStringList().value(0) == s.monitorDeviceId)
			monitor->setCurrentIndex(i);
	f->addRow("Fone (monitoramento)", monitor);
	audioKbps = new QSpinBox;
	audioKbps->setRange(64, 320);
	audioKbps->setSingleStep(32);
	audioKbps->setSuffix(" kbps");
	audioKbps->setValue(s.audioBitrate);
	f->addRow("Qualidade do áudio", audioKbps);

	tabs->setCurrentIndex(startTab);

	auto *bb = new QDialogButtonBox;
	auto *ok = bb->addButton("OK", QDialogButtonBox::AcceptRole);
	ok->setObjectName("primaryButton");
	bb->addButton("Cancelar", QDialogButtonBox::RejectRole);
	connect(bb, &QDialogButtonBox::accepted, this, [this] {
		if (apply())
			accept();
	});
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(bb);
}

bool SettingsDialog::apply()
{
	StudioSettings s = Outputs::get().settings();
	const QSize sz = resolution->currentData().toSize();
	const QPoint r = fps->currentData().toPoint();
	s.width = sz.width();
	s.height = sz.height();
	s.fpsNum = r.x();
	s.fpsDen = r.y();
	s.recFolder = recFolder->text().trimmed();
	s.recFormat = recFormat->currentData().toString();
	s.recEncoder = recEncoder->currentData().toString();
	s.recBitrate = recKbps->value();
	s.streamService = service->currentText();
	s.streamServer = server->text().trimmed();
	s.streamKey = key->text().trimmed();
	s.streamEncoder = streamEncoder->currentData().toString();
	s.streamBitrate = streamKbps->value();
	s.audioBitrate = audioKbps->value();
	const QStringList m = monitor->currentData().toStringList();
	s.monitorDeviceId = m.value(0, "default");
	s.monitorDeviceName = m.value(1);
	QString err;
	if (!Outputs::get().setSettings(s, err)) {
		QMessageBox::warning(this, "Configurações", err);
		return false;
	}
	return true;
}
