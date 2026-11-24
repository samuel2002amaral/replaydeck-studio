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
