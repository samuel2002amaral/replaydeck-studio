#include "settings-dialog.hpp"
#include "engine.hpp"
#include "outputs.hpp"
#include "functions.hpp"
#include "shortcuts.hpp"
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QSlider>
#include <algorithm>

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
	setMinimumWidth(820);
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
	{
		/* ---- transmissão: vários destinos ao mesmo tempo ---- */
		auto *w = new QWidget;
		auto *v = new QVBoxLayout(w);
		v->setContentsMargins(14, 12, 14, 12);
		auto *h = new QLabel("O botão Transmitir liga todos os destinos marcados ao mesmo tempo. Cada formato é "
				     "comprimido uma vez só, mesmo com vários destinos. Vertical = 9:16 (Reels, TikTok, Shorts).");
		h->setObjectName("muted");
		h->setWordWrap(true);
		v->addWidget(h);
		dests = new QTableWidget(0, 6);
		dests->setHorizontalHeaderLabels({"Ativo", "Nome", "Serviço", "Servidor", "Chave", "Formato"});
		dests->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
		dests->setColumnWidth(0, 46);
		dests->setColumnWidth(1, 120);
		dests->setColumnWidth(2, 110);
		dests->setColumnWidth(4, 150);
		dests->setColumnWidth(5, 120);
		dests->verticalHeader()->setVisible(false);
		dests->setSelectionBehavior(QAbstractItemView::SelectRows);
		dests->setMinimumHeight(150);
		v->addWidget(dests, 1);
		for (const StreamDestination &d : s.destinations)
			addDestinationRow(d);
		auto *row = new QHBoxLayout;
		auto *add = new QPushButton("+ Destino");
		connect(add, &QPushButton::clicked, this, [this] {
			StreamDestination d;
			d.name = "YouTube";
			d.service = "YouTube";
			d.server = streamPresets().first().server;
			addDestinationRow(d);
		});
		auto *del = new QPushButton("Remover");
		connect(del, &QPushButton::clicked, this, [this] {
			if (dests->currentRow() >= 0)
				dests->removeRow(dests->currentRow());
		});
		row->addWidget(add);
		row->addWidget(del);
		row->addStretch(1);
		v->addLayout(row);

		auto *ff = new QFormLayout;
		ff->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
		streamEncoder = new QComboBox;
		fillEncoders(streamEncoder, s.streamEncoder);
		ff->addRow("Codificador", streamEncoder);
		auto kbpsBox = [](int val) {
			auto *b = new QSpinBox;
			b->setRange(500, 50000);
			b->setSingleStep(500);
			b->setSuffix(" kbps");
			b->setValue(val);
			return b;
		};
		streamKbps = kbpsBox(s.streamBitrate);
		streamKbps->setToolTip("YouTube 1080p30: 6000 a 9000. Some os destinos e deixe folga na internet de subida.");
		ff->addRow("Qualidade horizontal", streamKbps);
		verticalKbps = kbpsBox(s.verticalBitrate);
		auto *vrow = new QHBoxLayout;
		verticalSize = new QComboBox;
		verticalSize->addItem("1080 × 1920", QSize(1080, 1920));
		verticalSize->addItem("720 × 1280 (mais leve)", QSize(720, 1280));
		verticalSize->setCurrentIndex(qMax(0, verticalSize->findData(QSize(s.verticalWidth, s.verticalHeight))));
		verticalFit = new QComboBox;
		verticalFit->addItem("Recortar o centro (tela cheia)", false);
		verticalFit->addItem("Encaixar inteiro (com faixas)", true);
		verticalFit->setCurrentIndex(s.verticalFit ? 1 : 0);
		verticalOffset = new QSlider(Qt::Horizontal);
		verticalOffset->setRange(-100, 100);
		verticalOffset->setValue(s.verticalOffset);
		verticalOffset->setToolTip("Recorte: puxe para a esquerda ou para a direita para escolher a parte que aparece");
		verticalOffset->setMinimumWidth(110);
		vrow->addWidget(verticalKbps);
		vrow->addWidget(verticalSize);
		vrow->addWidget(verticalFit);
		vrow->addWidget(new QLabel("posição"));
		vrow->addWidget(verticalOffset, 1);
		ff->addRow("Vertical", vrow);
		v->addLayout(ff);
		tabs->addTab(w, "Transmissão");
	}

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

	/* ---- atalhos ---- */
	{
		auto *w = new QWidget;
		auto *v = new QVBoxLayout(w);
		v->setContentsMargins(14, 12, 14, 12);
		auto *h = new QLabel("Tecla → função (os mesmos nomes do vMix). Entrada: número ou nome; vazio = a da Prévia. "
				     "Não disparam enquanto você digita num campo de texto.");
		h->setObjectName("muted");
		h->setWordWrap(true);
		v->addWidget(h);
		keys = new QTableWidget(0, 4);
		keys->setHorizontalHeaderLabels({"Tecla", "Função", "Entrada", "Value"});
		keys->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		keys->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		keys->setColumnWidth(2, 130);
		keys->setColumnWidth(3, 70);
		keys->verticalHeader()->setVisible(false);
		keys->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(keys, 1);
		for (const Shortcut &s : Shortcuts::get().list())
			addShortcutRow(s);
		auto *row = new QHBoxLayout;
		auto *add = new QPushButton("+ Adicionar");
		connect(add, &QPushButton::clicked, this, [this] {
			addShortcutRow(Shortcut{QKeySequence(), "Cut", "", ""});
			keys->scrollToBottom();
			if (auto *ed = qobject_cast<QKeySequenceEdit *>(keys->cellWidget(keys->rowCount() - 1, 0)))
				ed->setFocus();
		});
		auto *del = new QPushButton("Remover");
		connect(del, &QPushButton::clicked, this, [this] {
			const int r = keys->currentRow();
			if (r >= 0)
				keys->removeRow(r);
		});
		auto *sug = new QPushButton("Atalhos sugeridos");
		sug->setToolTip("Espaço = Corte, Enter = Fade, F1-F4 = overlays, 1-9 = Prévia, I/O = Mark In/Out, P = último lance...");
		connect(sug, &QPushButton::clicked, this, [this] {
			for (const Shortcut &s : Shortcuts::suggested()) {
				bool exists = false;
				for (int r = 0; r < keys->rowCount(); r++)
					if (auto *ed = qobject_cast<QKeySequenceEdit *>(keys->cellWidget(r, 0)); ed && ed->keySequence() == s.key)
						exists = true;
				if (!exists)
					addShortcutRow(s);
			}
		});
		row->addWidget(add);
		row->addWidget(del);
		row->addStretch(1);
		row->addWidget(sug);
		v->addLayout(row);
		tabs->addTab(w, "Atalhos");
	}

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

void SettingsDialog::addDestinationRow(const StreamDestination &d)
{
	const int r = dests->rowCount();
	dests->insertRow(r);
	auto *cellOn = new QWidget;
	auto *ch = new QHBoxLayout(cellOn);
	ch->setContentsMargins(0, 0, 0, 0);
	auto *on = new QCheckBox;
	on->setChecked(d.enabled);
	ch->addWidget(on, 0, Qt::AlignCenter);
	dests->setCellWidget(r, 0, cellOn);
	dests->setCellWidget(r, 1, new QLineEdit(d.name));
	auto *svc = new QComboBox;
	for (const StreamPreset &p : streamPresets())
		svc->addItem(QString::fromUtf8(p.name), QString::fromUtf8(p.server));
	svc->setCurrentIndex(qMax(0, svc->findText(d.service)));
	dests->setCellWidget(r, 2, svc);
	auto *srv = new QLineEdit(d.server);
	srv->setPlaceholderText("rtmp://…");
	dests->setCellWidget(r, 3, srv);
	auto *name = qobject_cast<QLineEdit *>(dests->cellWidget(r, 1));
	connect(svc, &QComboBox::currentIndexChanged, this, [svc, srv, name](int) {
		const QString url = svc->currentData().toString();
		srv->setText(url); /* TikTok e Personalizado: o servidor vem do painel do serviço */
		srv->setPlaceholderText(url.isEmpty() ? "cole o endereço do servidor (rtmp://…)" : "rtmp://…");
		if (name->text().isEmpty() || streamPresets().end() != std::find_if(streamPresets().begin(), streamPresets().end(),
										     [name](const StreamPreset &p) {
											     return name->text() == QString::fromUtf8(p.name);
										     }))
			name->setText(svc->currentText());
	});
	auto *key = new QLineEdit(d.key);
	key->setEchoMode(QLineEdit::PasswordEchoOnEdit);
	key->setPlaceholderText("chave");
	dests->setCellWidget(r, 4, key);
	auto *fmt = new QComboBox;
	fmt->addItem("Horizontal 16:9");
	fmt->addItem("Vertical 9:16");
	fmt->setCurrentIndex(d.vertical ? 1 : 0);
	dests->setCellWidget(r, 5, fmt);
}

void SettingsDialog::addShortcutRow(const Shortcut &s)
{
	const int r = keys->rowCount();
	keys->insertRow(r);
	auto *ed = new QKeySequenceEdit(s.key);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	ed->setMaximumSequenceLength(1); /* uma tecla (com Ctrl/Shift/Alt), não sequência */
#else
	connect(ed, &QKeySequenceEdit::editingFinished, ed, [ed] {
		if (ed->keySequence().count() > 1)
			ed->setKeySequence(QKeySequence(ed->keySequence()[0]));
	});
#endif
	ed->setClearButtonEnabled(true);
	keys->setCellWidget(r, 0, ed);
	auto *fn = new QComboBox;
	QString lastCat;
	for (const FunctionDef &f : functionList()) {
		if (lastCat != QLatin1String(f.category)) {
			lastCat = QString::fromLatin1(f.category);
			fn->addItem("— " + QString::fromUtf8(f.category) + " —");
			const int sep = fn->count() - 1;
			if (auto *m = qobject_cast<QStandardItemModel *>(fn->model()))
				m->item(sep)->setEnabled(false);
		}
		fn->addItem(QString("%1  ·  %2").arg(QString::fromLatin1(f.name), QString::fromUtf8(f.label)),
			    QString::fromLatin1(f.name));
	}
	fn->setCurrentIndex(qMax(0, fn->findData(s.function)));
	keys->setCellWidget(r, 1, fn);
	auto *in = new QComboBox;
	in->setEditable(true);
	in->addItem(QString());
	const auto &inputs = Engine::get().inputs();
	for (int i = 0; i < inputs.size(); i++)
		in->addItem(QString::number(i + 1) + "  " + inputs[i]->name, QString::number(i + 1));
	in->setCurrentText(s.input);
	in->setToolTip("Número ou nome da entrada; vazio = a que está na Prévia");
	keys->setCellWidget(r, 2, in);
	auto *val = new QLineEdit(s.value);
	keys->setCellWidget(r, 3, val);
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
	s.destinations.clear();
	for (int r = 0; r < dests->rowCount(); r++) {
		StreamDestination d;
		d.enabled = qobject_cast<QCheckBox *>(dests->cellWidget(r, 0)->findChild<QCheckBox *>())->isChecked();
		d.name = qobject_cast<QLineEdit *>(dests->cellWidget(r, 1))->text().trimmed();
		d.service = qobject_cast<QComboBox *>(dests->cellWidget(r, 2))->currentText();
		d.server = qobject_cast<QLineEdit *>(dests->cellWidget(r, 3))->text().trimmed();
		d.key = qobject_cast<QLineEdit *>(dests->cellWidget(r, 4))->text().trimmed();
		d.vertical = qobject_cast<QComboBox *>(dests->cellWidget(r, 5))->currentIndex() == 1;
		if (d.name.isEmpty())
			d.name = d.service;
		s.destinations << d;
	}
	s.streamEncoder = streamEncoder->currentData().toString();
	s.streamBitrate = streamKbps->value();
	s.verticalBitrate = verticalKbps->value();
	const QSize vs = verticalSize->currentData().toSize();
	s.verticalWidth = vs.width();
	s.verticalHeight = vs.height();
	s.verticalFit = verticalFit->currentData().toBool();
	s.verticalOffset = verticalOffset->value();
	s.audioBitrate = audioKbps->value();
	const QStringList m = monitor->currentData().toStringList();
	s.monitorDeviceId = m.value(0, "default");
	s.monitorDeviceName = m.value(1);
	QList<Shortcut> list;
	for (int r = 0; r < keys->rowCount(); r++) {
		auto *ed = qobject_cast<QKeySequenceEdit *>(keys->cellWidget(r, 0));
		auto *fn = qobject_cast<QComboBox *>(keys->cellWidget(r, 1));
		auto *in = qobject_cast<QComboBox *>(keys->cellWidget(r, 2));
		auto *val = qobject_cast<QLineEdit *>(keys->cellWidget(r, 3));
		if (!ed || ed->keySequence().isEmpty() || fn->currentData().toString().isEmpty())
			continue;
		/* "3  CAM 1" escolhido na lista vira "3" */
		QString input = in->currentText().trimmed();
		const int idx = in->findText(input);
		if (idx > 0)
			input = in->itemData(idx).toString();
		list << Shortcut{ed->keySequence(), fn->currentData().toString(), input, val->text().trimmed()};
	}
	Shortcuts::get().setList(list);
	QString err;
	if (!Outputs::get().setSettings(s, err)) {
		QMessageBox::warning(this, "Configurações", err);
		return false;
	}
	return true;
}
