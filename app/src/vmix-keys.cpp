#include "vmix-keys.hpp"
#include "engine.hpp"

#include <util/base.h>

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUuid>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

/* "PRÉ-JOGO", "pre jogo" e "Pre-Jogo" batem */
static QString norm(QString s)
{
	s = s.normalized(QString::NormalizationForm_D);
	QString out;
	for (QChar c : s)
		if (c.isLetterOrNumber())
			out += c.toLower();
	return out;
}

static void fromJson(const QJsonValue &v, QList<QPair<QString, QString>> &out)
{
	if (v.isArray()) {
		for (const QJsonValue &x : v.toArray())
			fromJson(x, out);
	} else if (v.isObject()) {
		const QJsonObject o = v.toObject();
		if (o.contains("inputKey") && o.value("inputKey").isString()) {
			const QString key = o.value("inputKey").toString();
			const QString title = o.value("hint").toString(o.value("title").toString());
			if (!key.isEmpty() && !title.isEmpty())
				out << qMakePair(title, key);
		}
		if (o.contains("key") && o.contains("title") && o.value("key").isString())
			out << qMakePair(o.value("title").toString(), o.value("key").toString());
		for (const QJsonValue &x : o)
			fromJson(x, out);
	}
}

/* <Input Key="..." Title="...">arquivo</Input> (.vmix) ou <input key="..." title="..."> (api.xml) */
static void fromXml(const QString &xml, QList<QPair<QString, QString>> &out)
{
	static const QRegularExpression tag(R"re(<input\b([^>]*)>([^<]*))re", QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression attr(R"re((\w+)\s*=\s*"([^"]*)")re");
	auto it = tag.globalMatch(xml);
	while (it.hasNext()) {
		const auto m = it.next();
		QString key, title, original;
		auto a = attr.globalMatch(m.captured(1));
		while (a.hasNext()) {
			const auto am = a.next();
			const QString k = am.captured(1).toLower();
			if (k == "key")
				key = am.captured(2);
			else if (k == "title")
				title = am.captured(2);
			else if (k == "originaltitle")
				original = am.captured(2);
		}
		if (title.isEmpty())
			title = original;
		if (title.isEmpty()) /* .vmix sem título: o nome do arquivo */
			title = QFileInfo(m.captured(2).trimmed()).completeBaseName();
		if (!key.isEmpty() && !title.isEmpty())
			out << qMakePair(title.toHtmlEscaped() == title ? title : QString(title).replace("&amp;", "&"), key);
	}
}

/* ---------- nomes do vMix por chave (para dizer o que falta) ---------- */

static QMap<QString, QString> g_titles; /* chave (minúscula) -> título no vMix */
static bool g_titlesLoaded = false;

static QString titlesFile()
{
	return QDir(Engine::get().configDir()).filePath("chaves-vmix.json");
}

static void loadTitles()
{
	if (g_titlesLoaded)
		return;
	g_titlesLoaded = true;
	QFile f(titlesFile());
	if (!f.open(QIODevice::ReadOnly))
		return;
	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	for (auto it = o.begin(); it != o.end(); ++it)
		g_titles.insert(it.key().toLower(), it.value().toString());
}

static void rememberTitles(const QList<QPair<QString, QString>> &pairs)
{
	loadTitles();
	for (const auto &p : pairs)
		if (!p.second.isEmpty() && !p.first.isEmpty())
			g_titles.insert(p.second.trimmed().toLower(), p.first);
	QJsonObject o;
	for (auto it = g_titles.begin(); it != g_titles.end(); ++it)
		o.insert(it.key(), it.value());
	QDir().mkpath(Engine::get().configDir());
	QFile f(titlesFile());
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(o).toJson());
}

QString vmixTitleOfKey(const QString &key)
{
	loadTitles();
	QString k = key.trimmed().toLower();
	if (k.startsWith('{') && k.endsWith('}'))
		k = k.mid(1, k.size() - 2);
	return g_titles.value(k);
}

struct Missing {
	QString key, title, reason;
	QSet<QString> functions, fields;
	int count = 0;
	QDateTime last;
};
static QMap<QString, Missing> g_missing; /* por título/chave */

void noteMissing(const QString &key, const QString &title, const QString &function, const QString &field,
		 const QString &reason)
{
	const QString id = (title.isEmpty() ? key : title) + "|" + reason;
	Missing &m = g_missing[id];
	m.key = key;
	m.title = title;
	m.reason = reason;
	m.functions.insert(function);
	if (!field.isEmpty())
		m.fields.insert(field);
	m.count++;
	m.last = QDateTime::currentDateTime();
	if (m.count == 1)
		blog(LOG_WARNING, "app externo pediu e não achou: %s %s (%s)", (title.isEmpty() ? key : title).toUtf8().constData(),
		     field.toUtf8().constData(), reason.toUtf8().constData());
}

void showMissing(QWidget *parent)
{
	QDialog dlg(parent);
	dlg.setWindowTitle("O que o app externo pediu e não achou");
	dlg.resize(900, 460);
	auto *v = new QVBoxLayout(&dlg);
	auto *hint = new QLabel(
		"Tudo que um app feito para o vMix (APPVMIX, Companion...) pediu e o Studio não tinha, desde que o programa abriu. "
		"Entrada faltando: crie/importe o título com o mesmo nome do vMix e use Entradas → Importar chaves do vMix. "
		"Campo faltando: o título do Studio não tem esse campo (nome diferente no .gtzip?).");
	hint->setWordWrap(true);
	hint->setObjectName("muted");
	v->addWidget(hint);
	auto *tree = new QTreeWidget;
	tree->setHeaderLabels({"Falta", "Nome no vMix", "Campos pedidos", "Funções", "Vezes", "Chave do vMix"});
	tree->setRootIsDecorated(false);
	for (const Missing &m : g_missing) {
		auto *it = new QTreeWidgetItem(tree);
		it->setText(0, m.reason);
		it->setText(1, m.title.isEmpty() ? QString("(desconhecido)") : m.title);
		QStringList fields(m.fields.begin(), m.fields.end());
		fields.sort();
		QStringList fns(m.functions.begin(), m.functions.end());
		fns.sort();
		it->setText(2, fields.join(", "));
		it->setText(3, fns.join(", "));
		it->setText(4, QString::number(m.count));
		it->setText(5, m.key);
		it->setToolTip(2, fields.join("\n"));
	}
	if (g_missing.isEmpty()) {
		auto *it = new QTreeWidgetItem(tree);
		it->setText(0, "Nada faltando até agora");
	}
	for (int c = 0; c < 5; c++)
		tree->resizeColumnToContents(c);
	v->addWidget(tree, 1);
	auto *row = new QHBoxLayout;
	auto *imp = new QPushButton("Importar chaves do vMix…");
	auto *copy = new QPushButton("Copiar lista");
	auto *clear = new QPushButton("Limpar");
	auto *close = new QPushButton("Fechar");
	row->addWidget(imp);
	row->addWidget(copy);
	row->addStretch(1);
	row->addWidget(clear);
	row->addWidget(close);
	v->addLayout(row);
	QObject::connect(imp, &QPushButton::clicked, &dlg, [&dlg] { importVmixKeys(&dlg); });
	QObject::connect(copy, &QPushButton::clicked, &dlg, [tree] {
		QStringList lines;
		for (int i = 0; i < tree->topLevelItemCount(); i++) {
			QStringList c;
			for (int k = 0; k < tree->columnCount(); k++)
				c << tree->topLevelItem(i)->text(k);
			lines << c.join(" | ");
		}
		QApplication::clipboard()->setText(lines.join("\n"));
	});
	QObject::connect(clear, &QPushButton::clicked, &dlg, [tree] {
		g_missing.clear();
		tree->clear();
	});
	QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
	dlg.exec();
}

bool applyVmixKeysFromFile(const QString &file, QStringList *done, QStringList *notFound, QString *err)
{
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) {
		*err = "Não foi possível abrir o arquivo.";
		return false;
	}
	const QByteArray data = f.readAll();
	QList<QPair<QString, QString>> pairs; /* título no vMix, chave */
	if (file.endsWith(".json", Qt::CaseInsensitive))
		fromJson(QJsonDocument::fromJson(data).object(), pairs);
	else
		fromXml(QString::fromUtf8(data), pairs);
	if (pairs.isEmpty()) {
		*err = "Nenhuma entrada com chave encontrada nesse arquivo.";
		return false;
	}
	rememberTitles(pairs);
	/* título -> chave (títulos repetidos no vMix ficam com a 1ª chave) */
	QMap<QString, QString> byTitle, shown;
	for (const auto &p : pairs) {
		const QString n = norm(p.first);
		if (!n.isEmpty() && !byTitle.contains(n)) {
			byTitle.insert(n, p.second.toLower());
			shown.insert(n, p.first);
		}
	}
	Engine &e = Engine::get();
	QSet<QString> used;
	for (Input *in : e.inputs()) {
		const QString n = norm(in->name);
		if (!byTitle.contains(n))
			continue;
		const QString key = byTitle.value(n);
		if (Input *other = e.inputByKey(key); other && other != in)
			other->key = QUuid::createUuid().toString(QUuid::WithoutBraces); /* a chave muda de dono */
		e.setInputKey(in->id, key);
		used.insert(n);
		*done << QString("%1  ←  %2").arg(in->name, key);
	}
	for (auto it = shown.begin(); it != shown.end(); ++it)
		if (!used.contains(it.key()))
			*notFound << it.value();
	blog(LOG_INFO, "chaves do vMix importadas de %s: %d entradas", file.toUtf8().constData(), (int)done->size());
	return true;
}

void importVmixKeys(QWidget *parent)
{
	const QString file = QFileDialog::getOpenFileName(
		parent, "Importar chaves do vMix", QString(),
		"Projeto do vMix, estado da API ou config do app (*.vmix *.xml *.json);;Todos os arquivos (*)");
	if (file.isEmpty())
		return;
	QStringList done, notFound;
	QString err;
	if (!applyVmixKeysFromFile(file, &done, &notFound, &err)) {
		QMessageBox::warning(parent, "Chaves do vMix", err);
		return;
	}
	QString msg = QString("%1 entrada(s) receberam a chave do vMix:\n\n%2").arg(done.size()).arg(done.join("\n"));
	if (!notFound.isEmpty())
		msg += QString("\n\nNo arquivo mas sem entrada com o mesmo nome aqui (%1):\n%2")
			       .arg(notFound.size())
			       .arg(notFound.mid(0, 40).join(", "));
	QMessageBox::information(parent, "Chaves do vMix", msg);
}

void editVmixKey(QWidget *parent, int inputId)
{
	Engine &e = Engine::get();
	Input *in = e.input(inputId);
	if (!in)
		return;
	bool ok = false;
	const QString k = QInputDialog::getText(parent, "Chave do vMix",
						QString("Chave (GUID) da entrada \"%1\" para apps feitos para o vMix.\n"
							"Cole a chave da entrada no vMix para o app achar esta aqui.")
							.arg(in->name),
						QLineEdit::Normal, in->key, &ok);
	if (!ok || k.trimmed() == in->key)
		return;
	if (!e.setInputKey(inputId, k))
		QMessageBox::warning(parent, "Chave do vMix", "Chave vazia ou já usada por outra entrada.");
}
