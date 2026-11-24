#include "setup-wizard.hpp"
#include "i18n.hpp"
#include "engine.hpp"
#include "ui-theme.hpp"
#include "common.hpp"
#include "studio-host.hpp"
#include <obs-frontend-api.h>
#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QFormLayout>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVBoxLayout>
#include <cstring>
#include <algorithm>
#include <vector>

#define REPLAY_ID "instant_replay_output"

namespace setup {

/* ---------------- saída do replay ----------------
 * Studio: em vez de uma cena do OBS, o replay é uma ENTRADA (Replay A / Replay B), como no vMix. */
QString ensureReplayScene()
{
	return rdsEnsureReplayInputs();
}

/* Studio: a câmera nova é uma entrada nova (a mesma janela de "Adicionar entrada") */
QString addCameraDialog(QWidget *parent)
{
	return rdsAddCameraDialog(parent);
}

bool assignToFreeSlot(const QString &source)
{
	if (source.isEmpty() || engine().recording())
		return false;
	ReplayConfig cfg = engine().config();
	const std::string n = source.toUtf8().constData();
	for (int i = 0; i < MAX_CAMERAS; i++)
		if (cfg.cameras[i] == n)
			return true;
	for (int i = 0; i < MAX_CAMERAS; i++)
		if (cfg.cameras[i].empty()) {
			cfg.cameras[i] = n;
			engine().setConfig(cfg);
			return true;
		}
	return false;
}

/* ---------------- assistente ---------------- */


bool needsSetup()
{
	if (QSettings(ui::settingsFile(), QSettings::IniFormat).value("setupDone", false).toBool())
		return false;
	const ReplayConfig cfg = engine().config();
	for (int i = 0; i < MAX_CAMERAS; i++)
		if (!cfg.cameras[i].empty())
			return false;
	return true;
}

static bool looksLikeCamera(obs_source_t *s)
{
	const char *id = obs_source_get_id(s);
	static const char *ids[] = {"dshow_input",   "decklink-input",      "ndi_source", "v4l2_input", "macos-avcapture",
				    "macos-avcapture-fast",
				    "av_capture_input", "av_capture_input_v2", "aja_source", "teradek_source"};
	for (const char *c : ids)
		if (strcmp(id, c) == 0)
			return true;
	if (strcmp(id, "ffmpeg_source") == 0) { /* câmera IP / SRT / RTMP (não arquivo) */
		obs_data_t *st = obs_source_get_settings(s);
		const bool live = !obs_data_get_bool(st, "is_local_file");
		obs_data_release(st);
		return live;
	}
	return false;
}

struct Cand {
	QString name;
	bool camera;
	bool scene;
};

static QString freeText(const QString &dir)
{
	QString d = dir;
	while (!d.isEmpty() && !QDir(d).exists())
		d = QFileInfo(d).absolutePath() == d ? QString() : QFileInfo(d).absolutePath();
	if (d.isEmpty())
		return QString();
	const QStorageInfo si(d);
	if (!si.isValid())
		return QString();
	return tl("%1 GB livres neste disco").arg(si.bytesAvailable() / 1e9, 0, 'f', 0);
}

static bool g_wizardOpen = false;

static Result showWizardOnce(QWidget *parent, bool &reopen)
{
	reopen = false;
	if (g_wizardOpen)
		return Cancelled;
	if (engine().recording()) {
		QMessageBox::information(parent, "ReplayDeck", tl("Pare a gravação (●) antes de reconfigurar as câmeras."));
		return Cancelled;
	}
	ReplayConfig cfg = engine().config();
	g_wizardOpen = true;
	struct Reset {
		~Reset() { g_wizardOpen = false; }
	} reset;

	QDialog dlg(parent);
	dlg.setWindowTitle(tl("ReplayDeck — configuração rápida"));
	dlg.setMinimumSize(560, 600);
	ui::applyDialog(&dlg);
	auto *v = new QVBoxLayout(&dlg);
	v->setContentsMargins(18, 16, 18, 14);
	v->setSpacing(8);

	auto *title = new QLabel(tl("<span style='font-size:17px;font-weight:700'>Vamos deixar o replay pronto</span>"),
				 &dlg);
	{
		/* idioma logo no primeiro contato: troca e reabre a janela no idioma escolhido */
		auto *head = new QHBoxLayout();
		head->addWidget(title, 1);
		auto *langBox = new QComboBox(&dlg);
		langBox->setToolTip(tl("Idioma do plugin"));
		langBox->addItem(QString::fromUtf8("🌐  ") + tl("Automático (português)"), "auto");
		for (const i18n::Lang &lg : i18n::languages())
			langBox->addItem(QString::fromUtf8("🌐  ") + QString::fromUtf8(lg.name), QString::fromLatin1(lg.code));
		const int li = langBox->findData(i18n::setting());
		langBox->setCurrentIndex(li < 0 ? 0 : li);
		head->addWidget(langBox, 0, Qt::AlignTop);
		v->addLayout(head);
		QObject::connect(langBox, &QComboBox::currentIndexChanged, &dlg, [&, langBox](int) {
			i18n::setLanguage(langBox->currentData().toString());
			reopen = true;
			dlg.reject();
		});
	}
	auto *intro = new QLabel(tl("Três escolhas e pronto. Dá para mudar tudo depois em ⚙ Configurar."), &dlg);
	intro->setProperty("role", "muted");
	v->addWidget(intro);

	auto step = [&](const QString &n, const QString &t, const QString &hint) {
		auto *l = new QLabel(tl("<b>%1&nbsp;&nbsp;%2</b>").arg(n, t), &dlg);
		l->setContentsMargins(0, 10, 0, 0);
		v->addWidget(l);
		if (!hint.isEmpty()) {
			auto *h = new QLabel(hint, &dlg);
			h->setProperty("role", "muted");
			h->setWordWrap(true);
			v->addWidget(h);
		}
	};

	/* ---- 1. câmeras ---- */
	step("1", tl("Quais câmeras gravar?"),
	     tl("Marque as fontes que o replay deve gravar (até %1). "
			       "Arraste para mudar a ordem: a primeira marcada vira a câmera 1.")
		     .arg(MAX_CAMERAS));
	std::vector<Cand> cands; /* Studio: as entradas com imagem */
	for (const std::string &n : rdsCameraNames()) {
		obs_source_t *src = rdsFindSource(n.c_str());
		cands.push_back({QString::fromUtf8(n.c_str()), src && looksLikeCamera(src), false});
		obs_source_release(src);
	}
	bool anyConfigured = false;
	for (int i = 0; i < MAX_CAMERAS; i++)
		anyConfigured |= !cfg.cameras[i].empty();

	auto *list = new QListWidget(&dlg);
	list->setDragDropMode(QAbstractItemView::InternalMove);
	list->setDefaultDropAction(Qt::MoveAction);
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	list->setMinimumHeight(200);
	list->setStyleSheet("QListWidget::item { padding: 5px 4px; }");
	auto addItem = [&](const Cand &c, bool checked) {
		auto *it = new QListWidgetItem(list);
		it->setData(Qt::UserRole, c.name);
		it->setData(Qt::UserRole + 1, c.scene ? tl("  (cena)") : c.camera ? tl("  · câmera") : QString());
		it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
		it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
	};
	/* primeiro as já configuradas (na ordem), depois câmeras, fontes e cenas */
	QStringList used;
	for (int i = 0; i < MAX_CAMERAS; i++) {
		const QString n = QString::fromUtf8(cfg.cameras[i].c_str());
		if (n.isEmpty() || used.contains(n))
			continue;
		for (const Cand &c : cands)
			if (c.name == n) {
				addItem(c, true);
				used << n;
			}
	}
	int autoChecked = 0;
	for (int pass = 0; pass < 3; pass++)
		for (const Cand &c : cands) {
			if (used.contains(c.name))
				continue;
			const bool inPass = pass == 0 ? c.camera : pass == 1 ? (!c.camera && !c.scene) : c.scene;
			if (!inPass)
				continue;
			const bool check = !anyConfigured && c.camera && autoChecked < MAX_CAMERAS;
			autoChecked += check;
			addItem(c, check);
			used << c.name;
		}
	v->addWidget(list, 1);
	auto *camInfo = new QLabel(&dlg);
	camInfo->setProperty("role", "muted");
	auto *addCam = new QPushButton(tl("+ Adicionar câmera"), &dlg);
	addCam->setToolTip(tl("A câmera ainda não está no programa? Crie aqui a entrada dela"));
	auto *camRow = new QHBoxLayout();
	camRow->addWidget(camInfo, 1);
	camRow->addWidget(addCam);
	v->addLayout(camRow);
	if (cands.empty()) {
		auto *none = new QListWidgetItem(tl("Nenhuma fonte de vídeo ainda. Clique em \"+ Adicionar câmera\"."),
						 list);
		none->setFlags(Qt::NoItemFlags);
	}

	bool relabeling = false;
	auto relabel = [&] {
		if (relabeling)
			return;
		relabeling = true;
		int n = 0;
		for (int r = 0; r < list->count(); r++) {
			QListWidgetItem *it = list->item(r);
			const QString name = it->data(Qt::UserRole).toString();
			if (name.isEmpty())
				continue;
			if (it->checkState() == Qt::Checked && n >= MAX_CAMERAS)
				it->setCheckState(Qt::Unchecked);
			const bool on = it->checkState() == Qt::Checked;
			if (on)
				n++;
			it->setText((on ? tl("Câmera %1  —  ").arg(n) : QString()) + name +
				    it->data(Qt::UserRole + 1).toString());
		}
		camInfo->setText(n == 0 ? tl("Nenhuma câmera marcada.")
					: tl("%1 câmera(s) marcada(s).").arg(n));
		relabeling = false;
	};
	relabel();
	QObject::connect(addCam, &QPushButton::clicked, &dlg, [&] {
		const QString n = addCameraDialog(&dlg);
		if (n.isEmpty())
			return;
		/* tira o aviso de "nenhuma fonte" e põe a câmera nova marcada, logo após as marcadas */
		for (int r = list->count() - 1; r >= 0; r--)
			if (list->item(r)->data(Qt::UserRole).toString().isEmpty())
				delete list->takeItem(r);
		int pos = 0;
		while (pos < list->count() && list->item(pos)->checkState() == Qt::Checked)
			pos++;
		auto *it = new QListWidgetItem();
		it->setData(Qt::UserRole, n);
		it->setData(Qt::UserRole + 1, tl("  · câmera nova"));
		it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
		relabeling = true;
		list->insertItem(pos, it);
		it->setCheckState(Qt::Checked);
		relabeling = false;
		relabel();
		list->scrollToItem(it);
	});
	QObject::connect(list, &QListWidget::itemChanged, &dlg, [&](QListWidgetItem *) { relabel(); });
	QObject::connect(list->model(), &QAbstractItemModel::rowsMoved, &dlg, [&] { relabel(); });

	/* ---- 2. pasta ---- */
	step("2", tl("Onde salvar as gravações?"),
	     tl("Use o disco mais rápido e com mais espaço (de preferência SSD)."));
	QString dir = QString::fromUtf8(cfg.outputDir.c_str());
	if (dir.isEmpty())
		dir = QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath("ReplayDeck");
	auto *dirEdit = new QLineEdit(QDir::toNativeSeparators(dir), &dlg);
	auto *browse = new QPushButton(tl("Escolher…"), &dlg);
	auto *dirRow = new QHBoxLayout();
	dirRow->addWidget(dirEdit, 1);
	dirRow->addWidget(browse);
	v->addLayout(dirRow);
	auto *freeLbl = new QLabel(&dlg);
	freeLbl->setProperty("role", "muted");
	v->addWidget(freeLbl);
	auto updFree = [&] { freeLbl->setText(freeText(QDir::fromNativeSeparators(dirEdit->text().trimmed()))); };
	updFree();
	QObject::connect(dirEdit, &QLineEdit::textChanged, &dlg, [&] { updFree(); });
	QObject::connect(browse, &QPushButton::clicked, &dlg, [&] {
		const QString d = QFileDialog::getExistingDirectory(&dlg, tl("Pasta das gravações"), dirEdit->text());
		if (!d.isEmpty())
			dirEdit->setText(QDir::toNativeSeparators(d));
	});

	/* ---- 3. qualidade ---- */
	step("3", tl("Qualidade"), QString());
	auto *quality = new QComboBox(&dlg);
	quality->addItem(tl("Alta  ★ recomendada"), 18);
	quality->addItem(tl("Máxima  (usa mais disco)"), 14);
	quality->addItem(tl("Leve  (economiza disco, PC mais simples)"), 22);
	if (cfg.rec.cq > 0 && quality->findData(cfg.rec.cq) >= 0)
		quality->setCurrentIndex(quality->findData(cfg.rec.cq));
	v->addWidget(quality);

	/* ---- finalização ---- */
	auto *mkScene = new QCheckBox(tl("Criar as entradas \"Replay A\" e \"Replay B\""), &dlg);
	mkScene->setChecked(true);
	if (rdsHasReplayInputs()) {
		mkScene->setChecked(false);
		mkScene->setEnabled(false);
		mkScene->setText(tl("Entradas do replay: já existem ✓"));
	}
	auto *startRec = new QCheckBox(tl("Começar a gravar agora"), &dlg);
	startRec->setChecked(true);
	auto *opts = new QVBoxLayout();
	opts->setContentsMargins(0, 12, 0, 4);
	opts->addWidget(mkScene);
	opts->addWidget(startRec);
	v->addLayout(opts);

	auto *row = new QHBoxLayout();
	auto *later = new QPushButton(tl("Agora não"), &dlg);
	auto *done = new QPushButton(tl("Concluir ✓"), &dlg);
	ui::setKind(done, "primary");
	done->setDefault(true);
	row->addWidget(later);
	row->addStretch(1);
	row->addWidget(done);
	v->addLayout(row);
	QObject::connect(later, &QPushButton::clicked, &dlg, &QDialog::reject);
	QObject::connect(done, &QPushButton::clicked, &dlg, [&] {
		int n = 0;
		for (int r = 0; r < list->count(); r++)
			if (list->item(r)->checkState() == Qt::Checked)
				n++;
		if (n == 0) {
			QMessageBox::information(&dlg, "ReplayDeck", tl("Marque pelo menos uma câmera."));
			return;
		}
		const QString d = QDir::fromNativeSeparators(dirEdit->text().trimmed());
		if (d.isEmpty() || !QDir().mkpath(d)) {
			QMessageBox::warning(&dlg, "ReplayDeck",
					     tl("Não foi possível usar essa pasta. Escolha outra."));
			return;
		}
		dlg.accept();
	});

	/* não modal: deixa usar a janela de propriedades do OBS que abre ao criar câmera */
	dlg.setWindowModality(Qt::NonModal);
	dlg.show();
	{
		QEventLoop loop;
		QObject::connect(&dlg, &QDialog::finished, &loop, &QEventLoop::quit);
		loop.exec();
	}
	const int r = dlg.result();
	if (reopen)
		return Cancelled;
	QSettings(ui::settingsFile(), QSettings::IniFormat).setValue("setupDone", true); /* não pergunta de novo sozinho */
	if (r != QDialog::Accepted)
		return Cancelled;

	int n = 0;
	for (int i = 0; i < MAX_CAMERAS; i++)
		cfg.cameras[i].clear();
	for (int row_ = 0; row_ < list->count() && n < MAX_CAMERAS; row_++) {
		QListWidgetItem *it = list->item(row_);
		if (it->checkState() == Qt::Checked)
			cfg.cameras[n++] = it->data(Qt::UserRole).toString().toUtf8().constData();
	}
	cfg.outputDir = QDir::fromNativeSeparators(dirEdit->text().trimmed()).toUtf8().constData();
	cfg.rec.cq = quality->currentData().toInt();
	cfg.rec.bitrateMbps = cfg.rec.cq <= 14 ? 220 : cfg.rec.cq <= 18 ? 144 : 80;
	cfg.rec.mjpegQuality = cfg.rec.cq <= 14 ? 2 : cfg.rec.cq <= 18 ? 3 : 4;
	engine().setConfig(cfg);

	if (mkScene->isChecked())
		ensureReplayScene();
	return startRec->isChecked() ? DoneStartRecording : Done;
}

Result showWizard(QWidget *parent)
{
	bool reopen = true;
	Result r = Cancelled;
	for (int guard = 0; reopen && guard < 20; guard++)
		r = showWizardOnce(parent, reopen);
	return r;
}

} // namespace setup
