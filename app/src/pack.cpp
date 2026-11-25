#include "pack.hpp"

#include "engine.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDataStream>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <functional>
#include <memory>
#include <obs.h>

namespace {

const char *MAGIC = "RDSPACOTE";
const qint32 VERSION = 1;

/* configurações que viajam no pacote (relativas à pasta de configuração) */
QStringList configFiles(const QString &cfg)
{
	QStringList out;
	for (const char *f : {"configuracoes.json", "atalhos.json", "chaves-vmix.json", "scripts.json", "ptz.json",
			      "playlist.json", "plugins.json", "replay/config.json", "replay/replay-dock.ini"})
		if (QFile::exists(cfg + "/" + f))
			out << f;
	/* presets do GT e títulos criados no editor */
	for (const char *dir : {"replay/gt-presets", "titulos"}) {
		QDirIterator it(cfg + "/" + dir, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
			out << QDir(cfg).relativeFilePath(it.next());
	}
	return out;
}

bool isJson(const QString &rel)
{
	return rel.endsWith(".json") || rel.endsWith(".rdsproj");
}

/* passa por todos os textos de um JSON (e permite trocar) */
QJsonValue walk(const QJsonValue &v, const std::function<QString(const QString &)> &fn)
{
	if (v.isString())
		return fn(v.toString());
	if (v.isArray()) {
		QJsonArray a = v.toArray();
		for (int i = 0; i < a.size(); i++)
			a[i] = walk(a[i], fn);
		return a;
	}
	if (v.isObject()) {
		QJsonObject o = v.toObject();
		for (auto it = o.begin(); it != o.end(); ++it)
			it.value() = walk(it.value(), fn);
		return o;
	}
	return v;
}

QJsonValue rootOf(const QByteArray &bytes)
{
	const QJsonDocument d = QJsonDocument::fromJson(bytes);
	if (d.isObject())
		return d.object();
	if (d.isArray())
		return d.array();
	return QJsonValue();
}

QByteArray toBytes(const QJsonValue &v)
{
	return v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented)
			   : QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented);
}

/* "file:C:/x.png" -> "C:/x.png" */
QString stripFile(const QString &s)
{
	return s.startsWith("file:") ? s.mid(5) : s;
}

/* um texto que é o caminho de um arquivo que existe neste computador */
bool isLocalFile(const QString &s)
{
	const QString p = stripFile(s);
	if (p.size() < 4 || p.size() > 1024 || p.contains('\n'))
		return false;
	if (!QDir::isAbsolutePath(p))
		return false;
	const QFileInfo fi(p);
	return fi.isFile();
}

QString human(qint64 bytes)
{
	if (bytes > 1024LL * 1024 * 1024)
		return QString::number(bytes / 1073741824.0, 'f', 1) + " GB";
	if (bytes > 1024 * 1024)
		return QString::number(bytes / 1048576.0, 'f', 1) + " MB";
	return QString::number(qMax<qint64>(1, bytes / 1024)) + " KB";
}

bool writeFileEntry(QDataStream &out, const QString &name, const QString &path, QProgressDialog *prog, qint64 &done)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	out << name << (qint64)f.size();
	QByteArray buf;
	while (!f.atEnd()) {
		buf = f.read(4 * 1024 * 1024);
		out.writeRawData(buf.constData(), (int)buf.size());
		done += buf.size();
		if (prog) {
			prog->setValue((int)(done / 1048576));
			QApplication::processEvents();
			if (prog->wasCanceled())
				return false;
		}
	}
	return out.status() == QDataStream::Ok;
}

/* arquivos usados (GTs, imagens, vídeos, stingers, scripts): procura nos textos */
QMap<QString, QString> usedFiles(const QString &proj, const QString &cfg, const QStringList &cfgs)
{
	QMap<QString, QString> refs;
	auto scan = [&refs](const QByteArray &bytes) {
		walk(rootOf(bytes), [&refs](const QString &s) {
			if (isLocalFile(s))
				refs.insert(stripFile(s), stripFile(s));
			return s;
		});
	};
	{
		QFile f(proj);
		if (f.open(QIODevice::ReadOnly))
			scan(f.readAll());
	}
	for (const QString &rel : cfgs)
		if (isJson(rel)) {
			QFile f(cfg + "/" + rel);
			if (f.open(QIODevice::ReadOnly))
				scan(f.readAll());
		}
	return refs;
}

} // namespace

namespace pack {

bool writePack(const QString &out, bool withMedia, QWidget *parent, QString *err)
{
	Engine &e = Engine::get();
	const QString cfg = e.configDir();
	const QString proj = e.projectPath().isEmpty() ? e.defaultProjectPath() : e.projectPath();
	e.saveProject(proj);
	const QStringList cfgs = configFiles(cfg);
	const QMap<QString, QString> refs = usedFiles(proj, cfg, cfgs);
	qint64 mediaBytes = 0;
	for (const QString &p : refs)
		mediaBytes += QFileInfo(p).size();
	/* manifesto: que arquivo do pacote era qual caminho */
	QJsonArray files;
	QMap<QString, QString> packName; /* original -> nome no pacote */
	if (withMedia) {
		int n = 0;
		for (const QString &p : refs.keys()) {
			const QString name = QString("arquivos/%1_%2").arg(++n).arg(QFileInfo(p).fileName());
			packName.insert(p, name);
			files.append(QJsonObject{{"original", p}, {"pacote", name}});
		}
	}
	QJsonObject manifest{{"programa", QString("ReplayDeck Studio %1").arg(RDS_VERSION)},
			     {"data", QDateTime::currentDateTime().toString(Qt::ISODate)},
			     {"config", QJsonArray::fromStringList(cfgs)},
			     {"arquivos", files}};

	QFile f(out + ".parcial");
	if (!f.open(QIODevice::WriteOnly)) {
		*err = "Não consegui criar o arquivo:\n" + out;
		return false;
	}
	QDataStream s(&f);
	s.setVersion(QDataStream::Qt_6_0);
	s << QString(MAGIC) << VERSION << QJsonDocument(manifest).toJson(QJsonDocument::Compact);
	std::unique_ptr<QProgressDialog> prog;
	if (parent) {
		prog = std::make_unique<QProgressDialog>("Montando o pacote…", "Cancelar", 0,
							 (int)((mediaBytes + 1) / 1048576) + 1, parent);
		prog->setWindowModality(Qt::WindowModal);
		prog->setMinimumDuration(300);
	}
	qint64 done = 0;
	bool ok = writeFileEntry(s, "projeto.rdsproj", proj, nullptr, done);
	for (const QString &rel : cfgs)
		ok = ok && writeFileEntry(s, "config/" + rel, cfg + "/" + rel, nullptr, done);
	for (auto it = packName.begin(); ok && it != packName.end(); ++it)
		ok = writeFileEntry(s, it.value(), it.key(), prog.get(), done);
	s << QString("FIM");
	f.close();
	const bool canceled = prog && prog->wasCanceled();
	prog.reset();
	if (!ok) {
		QFile::remove(out + ".parcial");
		if (!canceled)
			*err = "Não consegui montar o pacote (falta espaço ou um arquivo sumiu).";
		return false;
	}
	QFile::remove(out);
	QFile::rename(out + ".parcial", out);
	blog(LOG_INFO, "[pacote] exportado: %s (%d config, %d arquivos)", out.toUtf8().constData(), (int)cfgs.size() + 1,
	     (int)packName.size());
	return true;
}

void exportDialog(QWidget *parent)
{
	Engine &e = Engine::get();
	const QString cfg = e.configDir();
	/* o projeto como está agora */
	const QString proj = e.projectPath().isEmpty() ? e.defaultProjectPath() : e.projectPath();
	e.saveProject(proj);

	QStringList cfgs = configFiles(cfg);
	const QMap<QString, QString> refs = usedFiles(proj, cfg, cfgs);
	qint64 mediaBytes = 0;
	QStringList big;
	for (const QString &p : refs) {
		const qint64 sz = QFileInfo(p).size();
		mediaBytes += sz;
		if (sz > 50LL * 1024 * 1024)
			big << QString("%1 (%2)").arg(QFileInfo(p).fileName(), human(sz));
	}

	/* confirmação */
	QDialog d(parent);
	d.setWindowTitle("Exportar configuração para outro computador");
	d.setMinimumWidth(560);
	auto *v = new QVBoxLayout(&d);
	auto *l = new QLabel(QString("<b>O pacote leva:</b><br>"
				     "• o projeto: %1 entradas, títulos GT, camadas, posições, transições<br>"
				     "• as configurações: vídeo, gravação, transmissão, atalhos, replay, API do vMix, "
				     "scripts, PTZ, playlist (%2 arquivos)<br>"
				     "• os arquivos usados: %3 arquivo(s), %4 (GTs, imagens, vídeos, stingers)")
				     .arg(e.inputs().size())
				     .arg(cfgs.size() + 1)
				     .arg(refs.size())
				     .arg(human(mediaBytes)));
	l->setWordWrap(true);
	v->addWidget(l);
	auto *withMedia = new QCheckBox(QString("Incluir os arquivos usados (%1)").arg(human(mediaBytes)));
	withMedia->setChecked(true);
	withMedia->setEnabled(!refs.isEmpty());
	v->addWidget(withMedia);
	if (!big.isEmpty()) {
		auto *b = new QLabel("Arquivos grandes: " + big.join(", "));
		b->setWordWrap(true);
		b->setObjectName("muted");
		v->addWidget(b);
	}
	auto *note = new QLabel("Câmeras, placas de captura e microfones são de cada computador: no outro, escolha os "
				"aparelhos de novo nas entradas (⚙). Chaves de transmissão também vão no pacote: "
				"envie só para quem pode usar.");
	note->setWordWrap(true);
	note->setObjectName("muted");
	v->addWidget(note);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	bb->button(QDialogButtonBox::Ok)->setText("Exportar…");
	QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
	QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
	v->addWidget(bb);
	if (d.exec() != QDialog::Accepted)
		return;

	const QString suggested = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/ReplayDeck-" +
				  QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".rdspacote";
	QString out = QFileDialog::getSaveFileName(parent, "Salvar pacote", suggested, "Pacote do ReplayDeck (*.rdspacote)");
	if (out.isEmpty())
		return;
	if (!out.endsWith(".rdspacote"))
		out += ".rdspacote";

	QString err;
	if (!writePack(out, withMedia->isChecked(), parent, &err)) {
		if (!err.isEmpty())
			QMessageBox::warning(parent, "Exportar", err);
		return;
	}
	QMessageBox::information(parent, "Exportar",
				 QString("Pronto! Pacote salvo em:\n%1\n(%2)\n\nNo outro computador: instale o ReplayDeck "
					 "Studio, abra e use Arquivo > Importar pacote…")
					 .arg(out, human(QFileInfo(out).size())));
}

bool readPack(const QString &in, QWidget *parent, QString *err)
{
	QFile f(in);
	if (!f.open(QIODevice::ReadOnly)) {
		*err = "Não consegui abrir o pacote.";
		return false;
	}
	QDataStream s(&f);
	s.setVersion(QDataStream::Qt_6_0);
	QString magic;
	qint32 ver = 0;
	QByteArray man;
	s >> magic >> ver >> man;
	if (magic != MAGIC || ver > VERSION) {
		*err = "Este arquivo não é um pacote do ReplayDeck (ou é de uma versão mais nova).";
		return false;
	}
	const QString pending = Engine::get().configDir() + "/importar-pendente";
	QDir(pending).removeRecursively();
	QDir().mkpath(pending);
	std::unique_ptr<QProgressDialog> prog;
	if (parent) {
		prog = std::make_unique<QProgressDialog>("Abrindo o pacote…", QString(), 0, (int)(f.size() / 1048576) + 1,
							 parent);
		prog->setWindowModality(Qt::WindowModal);
		prog->setMinimumDuration(300);
	}
	bool ok = true;
	while (ok) {
		QString name;
		s >> name;
		if (name == "FIM" || s.status() != QDataStream::Ok)
			break;
		qint64 size = 0;
		s >> size;
		if (name.contains("..") || name.startsWith('/') || name.contains(':') || size < 0) {
			ok = false;
			break;
		}
		const QString dst = pending + "/" + name;
		QDir().mkpath(QFileInfo(dst).absolutePath());
		QFile o(dst);
		if (!o.open(QIODevice::WriteOnly)) {
			ok = false;
			break;
		}
		QByteArray buf;
		while (size > 0 && ok) {
			buf.resize((int)qMin<qint64>(size, 4 * 1024 * 1024));
			const int got = s.readRawData(buf.data(), (int)buf.size());
			if (got <= 0) {
				ok = false;
				break;
			}
			o.write(buf.constData(), got);
			size -= got;
			if (prog) {
				prog->setValue((int)(f.pos() / 1048576));
				QApplication::processEvents();
			}
		}
	}
	if (!ok) {
		QDir(pending).removeRecursively();
		*err = "O pacote está incompleto ou corrompido.";
		return false;
	}
	QFile mf(pending + "/manifesto.json");
	if (mf.open(QIODevice::WriteOnly))
		mf.write(man);
	return true;
}

static bool g_restart = false;

void importDialog(QWidget *parent)
{
	const QString in = QFileDialog::getOpenFileName(parent, "Importar pacote", QString(),
							"Pacote do ReplayDeck (*.rdspacote)");
	if (in.isEmpty())
		return;
	QFile f(in);
	if (!f.open(QIODevice::ReadOnly))
		return;
	QDataStream s(&f);
	s.setVersion(QDataStream::Qt_6_0);
	QString magic;
	qint32 ver = 0;
	QByteArray man;
	s >> magic >> ver >> man;
	if (magic != MAGIC || ver > VERSION) {
		QMessageBox::warning(parent, "Importar", "Este arquivo não é um pacote do ReplayDeck (ou é de uma versão mais nova).");
		return;
	}
	const QJsonObject manifest = QJsonDocument::fromJson(man).object();
	const int nFiles = manifest.value("arquivos").toArray().size();
	if (QMessageBox::question(parent, "Importar pacote",
				  QString("Pacote de %1 (%2).\n\nVai TROCAR o projeto e as configurações deste "
					  "computador pelos do pacote (os atuais ficam guardados numa cópia de segurança) "
					  "e trazer %3 arquivo(s).\n\nO programa reinicia em seguida. Continuar?")
					  .arg(manifest.value("programa").toString(),
					       QDateTime::fromString(manifest.value("data").toString(), Qt::ISODate)
						       .toString("dd/MM/yyyy HH:mm"))
					  .arg(nFiles)) != QMessageBox::Yes)
		return;

	f.close();
	QString err;
	if (!readPack(in, parent, &err)) {
		QMessageBox::warning(parent, "Importar", err);
		return;
	}
	blog(LOG_INFO, "[pacote] importado para aplicar na próxima abertura: %s", in.toUtf8().constData());
	g_restart = true;
	if (QWidget *w = parent ? parent->window() : nullptr)
		w->close();
	else
		qApp->quit();
}

bool restartRequested()
{
	return g_restart;
}

void applyPending(const QString &cfg)
{
	const QString pending = cfg + "/importar-pendente";
	QFile mf(pending + "/manifesto.json");
	if (!mf.open(QIODevice::ReadOnly))
		return;
	const QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
	mf.close();

	/* cópia de segurança do que existe agora */
	const QString backup = cfg + "/backup-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
	QStringList current = configFiles(cfg);
	current << "projeto.rdsproj";
	for (const QString &rel : current) {
		if (!QFile::exists(cfg + "/" + rel))
			continue;
		QDir().mkpath(QFileInfo(backup + "/" + rel).absolutePath());
		QFile::copy(cfg + "/" + rel, backup + "/" + rel);
	}

	/* arquivos usados: ficam em <config>/arquivos-importados */
	const QString media = cfg + "/arquivos-importados";
	QDir().mkpath(media);
	QMap<QString, QString> map; /* caminho antigo -> caminho novo */
	for (const QJsonValue &v : manifest.value("arquivos").toArray()) {
		const QJsonObject o = v.toObject();
		const QString from = pending + "/" + o.value("pacote").toString();
		QString to = media + "/" + QFileInfo(o.value("pacote").toString()).fileName();
		QFile::remove(to);
		if (QFile::rename(from, to) || QFile::copy(from, to))
			map.insert(o.value("original").toString(), QDir::cleanPath(to));
	}
	auto fix = [&map](const QString &s) -> QString {
		const bool pre = s.startsWith("file:");
		const QString p = pre ? s.mid(5) : s;
		auto it = map.find(p);
		if (it == map.end()) {
			/* mesmo caminho com a outra barra (C:\x e C:/x) */
			QString alt = p;
			alt.replace('\\', '/');
			for (auto m = map.begin(); m != map.end(); ++m)
				if (QString(m.key()).replace('\\', '/') == alt)
					return (pre ? "file:" : "") + m.value();
			return s;
		}
		return (pre ? "file:" : "") + it.value();
	};
	/* pastas de gravação que não existem neste computador: voltam ao padrão */
	const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ReplayDeck";
	auto fixDirs = [&videos](QJsonObject o) {
		for (const char *k : {"output_dir", "gravar_pasta"}) {
			const QString d = o.value(k).toString();
			if (!d.isEmpty() && !QDir(d).exists() && !QDir().mkpath(d))
				o.insert(k, QString(k) == QLatin1String("output_dir") ? videos : QString());
		}
		return o;
	};

	auto install = [&](const QString &src, const QString &rel) {
		QFile in(src);
		if (!in.open(QIODevice::ReadOnly))
			return;
		QByteArray bytes = in.readAll();
		in.close();
		if (isJson(rel)) {
			QJsonValue root = walk(rootOf(bytes), fix);
			if (root.isObject())
				root = fixDirs(root.toObject());
			if (!root.isNull())
				bytes = toBytes(root);
		}
		const QString dst = cfg + "/" + rel;
		QDir().mkpath(QFileInfo(dst).absolutePath());
		QFile out(dst);
		if (out.open(QIODevice::WriteOnly))
			out.write(bytes);
	};
	for (const QJsonValue &v : manifest.value("config").toArray())
		install(pending + "/config/" + v.toString(), v.toString());
	install(pending + "/projeto.rdsproj", "projeto.rdsproj");

	QDir(pending).removeRecursively();
	blog(LOG_INFO, "[pacote] aplicado: %d configurações, %d arquivos (cópia de segurança em %s)",
	     (int)manifest.value("config").toArray().size() + 1, (int)map.size(), backup.toUtf8().constData());
}

} // namespace pack
