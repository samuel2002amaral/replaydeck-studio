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
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
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
	/* projeto padrão do APPVMIX (FUTSALV2.vmix): funciona sem importar nada */
	static const struct {
		const char *key, *title;
	} defaults[] = {
		{"76a9fe91-6343-4f68-8324-9cbc732ffdea", "Offline - FEELWORLD USB3.0 Capture"},
		{"7cd17f30-1bc4-44b1-aede-0411a34143a0", "CELULAR"},
		{"8928a63f-d867-453b-87c3-886fa950aa10", "CELULAR 2"},
		{"0055c80d-9b55-4e0c-9713-cf7cde222063", "Browser vdo.ninja"},
		{"6c5149c4-35c8-4f36-ac0e-e6081c024aca", "PLACAR"},
		{"aa47b020-7924-4ff7-bee1-2465fd21b032", "GOL CASA"},
		{"59083fbc-c678-4f94-86e8-41cd5d0c8d9a", "GOL FORA"},
		{"b34b9585-6eac-4a9a-9224-d078c5fe0273", "PRÉ-JOGO"},
		{"1e3a390b-8c0f-4f72-b844-d8ee388f73ad", "NARRADOR"},
		{"74f76cde-0253-4df6-8ffc-7da4836760e9", "ARBITRAGEM"},
		{"afe738ad-8844-4e01-9f60-142f0a8186c2", "ESCALAÇÃO CASA"},
		{"8cdaccfc-d80c-4a02-9134-3c77f7fbaf1c", "ESCALACAO FORA"},
		{"fbb7b20d-4bfb-40cc-bb99-9662a015047b", "RESUMO PARTIDA"},
		{"7d3fe6d0-7dc4-489e-a451-18782f773734", "TEXTO RODAPE"},
		{"addd3219-97a6-4f39-9b02-369db9aaefe0", "BANNERYOUTUBE.png"},
		{"6f97d805-de72-4348-a040-ac6470b0249b", "TRANSIÇÃO - STINGER"},
		{"549e4e2b-aec3-4f66-9693-634d142e27bd", "ANUNCIO"},
		{"de625c8f-d86e-4c19-91d9-bb0ca0de792d", "List - ANUNCIANTES - NUTRI POWER.jpg"},
		{"15f96ffc-0eb2-46ab-a40c-8b75b05a5a0d", "LOGO IURY FILMS.png"},
		{"86ce7869-f993-4624-9a90-58306cc18c9c", "VAR"},
		{"850aa080-ea08-47f8-a4a3-7bd67cff0740", "REPLAY - A"},
		{"e2980cea-7f51-40ca-a746-0634a96678cd", "GOL CASA"},
		{"67542331-42ce-4987-a753-a9f9f7b47a2e", "GOL FORA"},
		{"d566889f-6db8-40d0-b555-09110d3e29ba", "AMARELO.gtzip"},
		{"778ea944-c2b4-4588-b595-53646b4eedb2", "AMARELO FORA"},
		{"49b11037-d9de-4fe7-8328-37ad56e43b84", "VERMELHO CASA"},
		{"a7fe658f-70d1-4857-8051-141912568a3e", "VERMELHO FORA"},
		{"c16d1173-6d70-42c0-95fb-865f01138b5f", "VAR.png"},
		{"47d59e5e-e5f9-42de-90c7-43f9c22bdb63", "CARDS PATROCINADORES - CARDS PATROCINADORES NUTRI POWER.jpg"},
		{"766c1a2a-b4f3-432d-b993-d092cc95da67", "Patrocinadores.mp4"},
		{"0c792abf-9289-40ca-9a72-2b948d789e16", "PENAL"},
		{"1e438fd3-a0bb-4990-a4d3-ab8aed7dee31", "REPLAY - B"},
	};
	for (const auto &d : defaults)
		g_titles.insert(QString::fromLatin1(d.key), QString::fromUtf8(d.title));
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

/* nome "igual" ao do vMix: sem acento, espaço, pontuação, maiúsculas e extensão (.gtzip, .png...) */
static QString normTitle(QString s)
{
	static const QRegularExpression ext(R"re(\.(gtzip|gtxml|png|jpe?g|mp4|mov|webm|mkv|avi|gif|xaml)$)re",
					    QRegularExpression::CaseInsensitiveOption);
	s.remove(ext);
	return norm(s);
}

Input *inputForVmixTitle(const QString &title)
{
	const QString t = normTitle(title);
	if (t.isEmpty())
		return nullptr;
	for (Input *in : Engine::get().inputs())
		if (normTitle(in->name) == t)
			return in;
	return nullptr;
}

int adoptKnownKeys()
{
	loadTitles();
	Engine &e = Engine::get();
	int n = 0;
	for (auto it = g_titles.begin(); it != g_titles.end(); ++it) {
		if (e.inputByKey(it.key()))
			continue; /* alguém já tem essa chave */
		Input *in = inputForVmixTitle(it.value());
		if (!in || g_titles.contains(in->key.toLower()))
			continue; /* não achou, ou a entrada já tem outra chave do vMix */
		if (e.setInputKey(in->id, it.key())) {
			n++;
			blog(LOG_INFO, "chave do vMix %s -> entrada \"%s\" (nome %s no vMix)", it.key().toUtf8().constData(),
			     in->name.toUtf8().constData(), it.value().toUtf8().constData());
		}
	}
	return n;
}

/* o APPVMIX que está mandando comandos: lê a config dele (nomes do vMix de cada chave) */
void learnFromApp(const QString &host)
{
	/* tenta de novo a cada 10 s enquanto não achar o app; achou: relê a cada minuto */
	static QMap<QString, qint64> next;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (host.isEmpty() || now < next.value(host, 0))
		return;
	next[host] = now + 10000;
	static QNetworkAccessManager *net = new QNetworkAccessManager(QCoreApplication::instance());
	const QString h = host.contains(':') ? "[" + host + "]" : host;
	QNetworkRequest req(QUrl(QString("http://%1:3000/app-api/backup").arg(h)));
	req.setTransferTimeout(3000);
	QNetworkReply *r = net->get(req);
	QObject::connect(r, &QNetworkReply::finished, r, [r, host] {
		r->deleteLater();
		if (r->error() != QNetworkReply::NoError)
			return; /* não é o APPVMIX (ou tem PIN): fica com o que já sabe */
		QList<QPair<QString, QString>> pairs;
		fromJson(QJsonDocument::fromJson(r->readAll()).object().value("config"), pairs);
		if (pairs.isEmpty())
			return;
		next[host] = QDateTime::currentMSecsSinceEpoch() + 60000;
		rememberTitles(pairs);
		const int n = adoptKnownKeys();
		blog(LOG_INFO, "APPVMIX em %s: %d nomes do vMix aprendidos, %d entradas ligadas", host.toUtf8().constData(),
		     (int)pairs.size(), n);
	});
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
