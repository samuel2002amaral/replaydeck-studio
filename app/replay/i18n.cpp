#include "i18n.hpp"
#include "studio-host.hpp"
#include "ui-theme.hpp"

#include <obs-module.h>

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>

#include <mutex>
#include <string>
#include <unordered_map>

namespace i18n {

static const std::vector<Lang> kLangs = {
	{"pt-BR", "Português (Brasil)"}, {"en", "English"},   {"es", "Español"},     {"fr", "Français"},
	{"de", "Deutsch"},               {"it", "Italiano"},  {"ru", "Русский"},     {"zh-CN", "简体中文"},
	{"ja", "日本語"},                {"ko", "한국어"},
};

const std::vector<Lang> &languages()
{
	return kLangs;
}

static std::recursive_mutex g_mtx;
static bool g_loaded = false;
static QString g_code = "pt-BR";
static QHash<QString, QString> g_map;                         /* pt -> idioma atual */
static std::unordered_map<std::string, std::string> g_cstr;    /* cache para trc() */
static std::vector<QPointer<QWidget>> g_roots;
static std::vector<std::pair<QPointer<QWidget>, void (*)(QWidget *)>> g_hooks;

QString setting()
{
	QSettings st(ui::settingsFile(), QSettings::IniFormat);
	return st.value("language", "auto").toString();
}

static QString fromLocale(const char *loc)
{
	const QString l = QString::fromUtf8(loc ? loc : "").toLower();
	if (l.startsWith("pt"))
		return "pt-BR";
	if (l.startsWith("zh"))
		return "zh-CN";
	for (const Lang &g : kLangs)
		if (l.startsWith(QString::fromLatin1(g.code).left(2)))
			return QString::fromLatin1(g.code);
	return "en";
}

static QString effective(const QString &set)
{
	for (const Lang &g : kLangs)
		if (set == QString::fromLatin1(g.code))
			return set;
	if (const char *over = getenv("REPLAYDECK_LANG"))
		return fromLocale(over);
	return QStringLiteral("pt-BR"); /* o Studio é em português */
}

static QHash<QString, QString> loadFile(const QString &code)
{
	QHash<QString, QString> m;
	if (code == "pt-BR")
		return m;
	QString path;
	if (const char *dir = getenv("REPLAYDECK_LANG_DIR")) {
		path = QString::fromUtf8(dir) + "/" + code + ".json";
	} else {
		const QByteArray rel = ("lang/" + code + ".json").toUtf8();
		char *p = rdsReplayDataFile(rel.constData());
		if (p) {
			path = QString::fromUtf8(p);
			bfree(p);
		}
	}
	QFile f(path);
	if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) {
		blog(LOG_WARNING, "[ReplayDeck] idioma %s não encontrado", code.toUtf8().constData());
		return m;
	}
	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	for (auto it = o.begin(); it != o.end(); ++it) {
		const QString v = it.value().toString();
		if (!v.isEmpty())
			m.insert(it.key(), v);
	}
	return m;
}

static void ensure()
{
	std::lock_guard<std::recursive_mutex> lk(g_mtx);
	if (g_loaded)
		return;
	g_loaded = true;
	g_code = effective(setting());
	g_map = loadFile(g_code);
}

QString current()
{
	ensure();
	return g_code;
}

QString tr(const char *pt)
{
	if (!pt)
		return QString();
	ensure();
	const QString key = QString::fromUtf8(pt);
	std::lock_guard<std::recursive_mutex> lk(g_mtx);
	auto it = g_map.constFind(key);
	return it == g_map.constEnd() ? key : it.value();
}

const char *trc(const char *pt)
{
	if (!pt)
		return pt;
	ensure();
	std::lock_guard<std::recursive_mutex> lk(g_mtx);
	auto it = g_map.constFind(QString::fromUtf8(pt));
	if (it == g_map.constEnd())
		return pt;
	auto &s = g_cstr[std::string(pt)];
	s = it.value().toUtf8().toStdString();
	return s.c_str();
}

static QString fmt(const char *pt, const QStringList &args)
{
	QString t = tr(pt);
	for (const QString &a : args)
		t = t.arg(a);
	return t;
}

void setTip(QWidget *w, const char *pt, const QStringList &args)
{
	w->setProperty("i18nTip", QString::fromUtf8(pt));
	w->setProperty("i18nTipArgs", args);
	w->setToolTip(fmt(pt, args));
}

void track(QWidget *root)
{
	if (root)
		g_roots.emplace_back(root);
}

void onChanged(QWidget *owner, void (*fn)(QWidget *))
{
	g_hooks.emplace_back(owner, fn);
}

/* ---- retraduzir o que já está na tela ---- */

/* texto na tela -> texto-fonte (pt), usando o idioma que estava antes */
static QString keyOf(const QString &shown, const QHash<QString, QString> &rev)
{
	auto it = rev.constFind(shown);
	return it == rev.constEnd() ? shown : it.value();
}

static QString swapText(const QString &shown, const QHash<QString, QString> &rev, const QHash<QString, QString> &now,
			const QHash<QString, QString> &keys)
{
	if (shown.isEmpty())
		return shown;
	const QString k = keyOf(shown, rev);
	if (!keys.contains(k))
		return shown; /* não é texto do plugin (nome de câmera, cena...) */
	auto it = now.constFind(k);
	return it == now.constEnd() ? k : it.value();
}

static void retranslateWidget(QWidget *w, const QHash<QString, QString> &rev, const QHash<QString, QString> &now,
			      const QHash<QString, QString> &keys)
{
	auto sw = [&](const QString &s) { return swapText(s, rev, now, keys); };
	const QVariant tipKey = w->property("i18nTip");
	if (tipKey.isValid())
		w->setToolTip(fmt(tipKey.toString().toUtf8().constData(), w->property("i18nTipArgs").toStringList()));
	else if (!w->toolTip().isEmpty())
		w->setToolTip(sw(w->toolTip()));
	if (w->isWindow() || qobject_cast<QDockWidget *>(w))
		w->setWindowTitle(sw(w->windowTitle()));
	if (auto *l = qobject_cast<QLabel *>(w)) {
		if (!l->text().isEmpty())
			l->setText(sw(l->text()));
	} else if (auto *b = qobject_cast<QAbstractButton *>(w)) {
		if (!b->text().isEmpty())
			b->setText(sw(b->text()));
	} else if (auto *g = qobject_cast<QGroupBox *>(w)) {
		g->setTitle(sw(g->title()));
	} else if (auto *c = qobject_cast<QComboBox *>(w)) {
		for (int i = 0; i < c->count(); i++)
			c->setItemText(i, sw(c->itemText(i)));
	} else if (auto *e = qobject_cast<QLineEdit *>(w)) {
		e->setPlaceholderText(sw(e->placeholderText()));
	} else if (auto *pe = qobject_cast<QPlainTextEdit *>(w)) {
		pe->setPlaceholderText(sw(pe->placeholderText()));
	} else if (auto *t = qobject_cast<QTabWidget *>(w)) {
		for (int i = 0; i < t->count(); i++) {
			t->setTabText(i, sw(t->tabText(i)));
			t->setTabToolTip(i, sw(t->tabToolTip(i)));
		}
	} else if (auto *tw = qobject_cast<QTableWidget *>(w)) {
		for (int i = 0; i < tw->columnCount(); i++)
			if (QTableWidgetItem *h = tw->horizontalHeaderItem(i)) {
				h->setText(sw(h->text()));
				h->setToolTip(sw(h->toolTip()));
			}
	} else if (auto *tr = qobject_cast<QTreeWidget *>(w)) {
		if (QTreeWidgetItem *h = tr->headerItem())
			for (int i = 0; i < tr->columnCount(); i++)
				h->setText(i, sw(h->text(i)));
	} else if (auto *lw = qobject_cast<QListWidget *>(w)) {
		for (int i = 0; i < lw->count(); i++) {
			QListWidgetItem *it = lw->item(i);
			it->setText(sw(it->text()));
			it->setToolTip(sw(it->toolTip()));
		}
	}
	for (QAction *a : w->actions()) {
		a->setText(sw(a->text()));
		a->setToolTip(sw(a->toolTip()));
	}
}

static QHash<QString, QString> keySet()
{
	/* todos os idiomas têm as mesmas chaves: o inglês serve de lista */
	static QHash<QString, QString> keys;
	if (keys.isEmpty())
		keys = loadFile("en");
	return keys;
}

void setLanguage(const QString &code)
{
	{
		QSettings st(ui::settingsFile(), QSettings::IniFormat);
		st.setValue("language", code);
		st.sync();
	}
	ensure();
	QHash<QString, QString> rev, now;
	{
		std::lock_guard<std::recursive_mutex> lk(g_mtx);
		for (auto it = g_map.begin(); it != g_map.end(); ++it)
			rev.insert(it.value(), it.key());
		g_code = effective(code);
		g_map = loadFile(g_code);
		now = g_map;
	}
	const QHash<QString, QString> keys = keySet();
	for (auto &r : g_roots) {
		if (!r)
			continue;
		QWidget *root = r;
		if (auto *dw = qobject_cast<QDockWidget *>(root->parentWidget()))
			retranslateWidget(dw, rev, now, keys);
		retranslateWidget(root, rev, now, keys);
		for (QWidget *c : root->findChildren<QWidget *>())
			retranslateWidget(c, rev, now, keys);
		for (QAction *a : root->findChildren<QAction *>()) {
			a->setText(swapText(a->text(), rev, now, keys));
			a->setToolTip(swapText(a->toolTip(), rev, now, keys));
		}
	}
	for (auto &h : g_hooks)
		if (h.first)
			h.second(h.first);
}

} // namespace i18n
