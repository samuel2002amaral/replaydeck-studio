#include "plugins.hpp"
#include "engine.hpp"

#include <obs-module.h>
#include <util/base.h>

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace plugins {

static QString g_crashed;

/* módulos do próprio OBS: não são plugins de terceiros */
static const QSet<QString> &coreNames()
{
	static const QSet<QString> s = {
		"win-capture",  "win-dshow",       "win-wasapi",      "obs-ffmpeg",        "obs-outputs",
		"obs-x264",     "obs-nvenc",       "obs-qsv11",       "image-source",      "obs-filters",
		"obs-transitions", "obs-text",     "text-freetype2",  "rtmp-services",     "obs-browser",
		"vlc-video",    "decklink",        "aja",             "nv-filters",        "obs-vst",
		"obs-webrtc",   "coreaudio-encoder", "frontend-tools", "obs-websocket",    "decklink-captions",
		"decklink-output-ui", "aja-output-ui", "win-mf",      "enc-amf",           "obs-libfdk",
		"linux-capture", "linux-v4l2",     "linux-pulseaudio", "linux-alsa",      "linux-pipewire",
		"linux-jack",   "obs-scripting",   "win-update",      "obs-qsv-test",      "obs-amf-test",
		"obs-nvenc-test"};
	return s;
}

QString ownFolder()
{
	return QDir(Engine::get().configDir()).filePath("plugins");
}

static QString listFile()
{
	return QDir(Engine::get().configDir()).filePath("plugins.json");
}

static QString markerFile()
{
	return QDir(Engine::get().configDir()).filePath("plugin-carregando.txt");
}

#ifdef _WIN32
static const char *kExt = "dll";
static const char *kArch = "64bit";
#else
static const char *kExt = "so";
static const char *kArch = "64bit";
#endif

/* layout novo do OBS: <pasta>/<nome>/bin/64bit/<nome>.dll e <pasta>/<nome>/data */
static void scanModern(const QString &root, const QString &where, QList<Plugin> &out)
{
	for (const QFileInfo &d : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		const QString name = d.fileName();
		const QString bin = QString("%1/bin/%2/%3.%4").arg(d.filePath(), kArch, name, kExt);
		if (QFileInfo::exists(bin))
			out << Plugin{name, bin, d.filePath() + "/data", where};
	}
}

QList<Plugin> discover()
{
	QList<Plugin> out;
	scanModern(ownFolder(), "pasta do ReplayDeck", out);
#ifdef _WIN32
	const QString pd = qEnvironmentVariable("ProgramData", "C:/ProgramData");
	scanModern(pd + "/obs-studio/plugins", "plugins do OBS", out);
	/* instalação antiga: dentro da pasta do OBS */
	for (const QString &pf : {qEnvironmentVariable("ProgramFiles", "C:/Program Files"),
				  qEnvironmentVariable("ProgramW6432", "C:/Program Files")}) {
		const QString obs = pf + "/obs-studio";
		for (const QFileInfo &f : QDir(obs + "/obs-plugins/64bit").entryInfoList({"*.dll"}, QDir::Files)) {
			const QString name = f.completeBaseName();
			out << Plugin{name, f.filePath(), obs + "/data/obs-plugins/" + name, "pasta do OBS instalado"};
		}
	}
#else
	scanModern(QDir::homePath() + "/.config/obs-studio/plugins", "plugins do OBS", out);
#endif
	/* tira os do próprio OBS e os repetidos (o primeiro achado vale) */
	QList<Plugin> clean;
	QSet<QString> seen;
	for (const Plugin &p : out) {
		if (coreNames().contains(p.name) || p.name.endsWith("-test") || seen.contains(p.name))
			continue;
		seen.insert(p.name);
		clean << p;
	}
	return clean;
}

QStringList enabled()
{
	QStringList l;
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(listFile().toUtf8().constData(), "bak");
	OBSDataArrayAutoRelease arr = d ? obs_data_get_array(d, "ligados") : nullptr;
	for (size_t i = 0, n = obs_data_array_count(arr); i < n; i++) {
		OBSDataAutoRelease o = obs_data_array_item(arr, i);
		l << QString::fromUtf8(obs_data_get_string(o, "nome"));
	}
	return l;
}

void setEnabled(const QStringList &names)
{
	OBSDataAutoRelease d = obs_data_create();
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const QString &n : names) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_string(o, "nome", n.toUtf8().constData());
		obs_data_array_push_back(arr, o);
	}
	obs_data_set_array(d, "ligados", arr);
	QDir().mkpath(Engine::get().configDir());
	obs_data_save_json_safe(d, listFile().toUtf8().constData(), "tmp", "bak");
}

void loadEnabled()
{
	QStringList on = enabled();
	/* da última vez o programa caiu carregando um plugin: esse fica desligado */
	QFile marker(markerFile());
	if (marker.open(QIODevice::ReadOnly)) {
		g_crashed = QString::fromUtf8(marker.readAll()).trimmed();
		marker.close();
		marker.remove();
		if (!g_crashed.isEmpty() && on.removeAll(g_crashed)) {
			setEnabled(on);
			blog(LOG_WARNING, "plugin '%s' derrubou o programa na última vez: desligado",
			     g_crashed.toUtf8().constData());
		}
	}
	if (on.isEmpty())
		return;
	for (const Plugin &p : discover()) {
		if (!on.contains(p.name))
			continue;
		if (obs_get_module(p.name.toUtf8().constData())) {
			blog(LOG_INFO, "plugin '%s' já carregado", p.name.toUtf8().constData());
			continue;
		}
		/* marca: se cair agora, a próxima abertura sabe quem foi */
		QFile m(markerFile());
		if (m.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			m.write(p.name.toUtf8());
			m.flush();
			m.close();
		}
		/* sem isso o motor ignora módulos fora da lista "segura" */
		obs_add_safe_module(p.name.toUtf8().constData());
		obs_module_t *mod = nullptr;
		const int code = obs_open_module(&mod, p.bin.toUtf8().constData(), p.data.toUtf8().constData());
		if (code != MODULE_SUCCESS) {
			blog(LOG_WARNING, "plugin '%s' não abriu (código %d: %s)", p.name.toUtf8().constData(), code,
			     code == MODULE_INCOMPATIBLE_VER ? "feito para outra versão do OBS"
			     : code == MODULE_MISSING_EXPORTS ? "não é um plugin do OBS"
							      : "falta algum arquivo dele ou do OBS");
			continue;
		}
		if (!obs_init_module(mod))
			blog(LOG_WARNING, "plugin '%s' não iniciou", p.name.toUtf8().constData());
		else
			blog(LOG_INFO, "plugin carregado: %s (%s)", p.name.toUtf8().constData(), p.bin.toUtf8().constData());
	}
	/* a marca fica até a janela abrir (startupFinished) */
}

void startupFinished()
{
	QFile::remove(markerFile());
}

QString crashedLastTime()
{
	return g_crashed;
}

} // namespace plugins

/* ---------- janela ---------- */

PluginsDialog::PluginsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Plugins do OBS");
	resize(760, 480);
	auto *v = new QVBoxLayout(this);
	auto *hint = new QLabel(
		"Plugins feitos para o OBS 30 ou mais novo funcionam aqui: entradas novas (Adicionar entrada → no fim da lista), "
		"filtros (⚙ da entrada → Filtros), transições e codificadores. Painéis e menus do OBS não aparecem. "
		"Instale o plugin para o OBS normalmente (ou copie para a pasta de plugins do ReplayDeck), marque aqui e abra o "
		"programa de novo. Se um plugin derrubar o programa, ele é desligado sozinho.");
	hint->setWordWrap(true);
	hint->setObjectName("muted");
	v->addWidget(hint);
	tree = new QTreeWidget;
	tree->setHeaderLabels({"Plugin", "Agora", "Onde"});
	tree->setRootIsDecorated(false);
	tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	const QStringList on = plugins::enabled();
	const QList<plugins::Plugin> found = plugins::discover();
	for (const plugins::Plugin &p : found) {
		auto *it = new QTreeWidgetItem(tree);
		it->setText(0, p.name);
		it->setCheckState(0, on.contains(p.name) ? Qt::Checked : Qt::Unchecked);
		it->setText(1, obs_get_module(p.name.toUtf8().constData()) ? "carregado" : "—");
		it->setText(2, p.where);
		it->setToolTip(2, p.bin);
	}
	if (found.isEmpty()) {
		auto *it = new QTreeWidgetItem(tree);
		it->setText(0, "Nenhum plugin encontrado");
		it->setDisabled(true);
	}
	v->addWidget(tree, 1);
	if (!plugins::crashedLastTime().isEmpty()) {
		auto *warn = new QLabel(QString("O plugin \"%1\" derrubou o programa da última vez e foi desligado.")
						.arg(plugins::crashedLastTime()));
		warn->setStyleSheet("color:#e8a33a;");
		v->addWidget(warn);
	}
	auto *bb = new QDialogButtonBox;
	auto *folder = bb->addButton("Abrir pasta de plugins", QDialogButtonBox::ActionRole);
	connect(folder, &QPushButton::clicked, this, [] {
		QDir().mkpath(plugins::ownFolder());
#ifdef _WIN32
		QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(plugins::ownFolder())});
#else
		QDesktopServices::openUrl(QUrl::fromLocalFile(plugins::ownFolder()));
#endif
	});
	auto *ok = bb->addButton("Salvar", QDialogButtonBox::AcceptRole);
	ok->setObjectName("primaryButton");
	bb->addButton("Cancelar", QDialogButtonBox::RejectRole);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(bb, &QDialogButtonBox::accepted, this, [this, on] {
		QStringList now;
		for (int i = 0; i < tree->topLevelItemCount(); i++)
			if (tree->topLevelItem(i)->checkState(0) == Qt::Checked)
				now << tree->topLevelItem(i)->text(0);
		plugins::setEnabled(now);
		if (QSet<QString>(now.begin(), now.end()) != QSet<QString>(on.begin(), on.end()))
			QMessageBox::information(this, "Plugins", "Vale quando o programa abrir de novo.");
		accept();
	});
	v->addWidget(bb);
}
