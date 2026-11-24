#include "vmix-api.hpp"
#include "engine.hpp"
#include "functions.hpp"
#include "outputs.hpp"
#include "virtualcam.hpp"
#include "shortcuts.hpp"
#include "vmix-keys.hpp"

#include <QCryptographicHash>
#include "multicorder.hpp"
#include "playlist.hpp"

#include "../replay/gt/gt-source.hpp"
#include "../replay/gt/gt-doc.hpp"

#include <util/base.h>

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>
#include <cstring>

VmixApi &VmixApi::get()
{
	static VmixApi a;
	return a;
}

static QString esc(const QString &s)
{
	return s.toHtmlEscaped();
}

/* id estável de um atalho (formato GUID, como no vMix) */
static QString shortcutId(const Shortcut &sc)
{
	const QByteArray h = QCryptographicHash::hash(
		(sc.display() + "|" + sc.function + "|" + sc.input + "|" + sc.value).toUtf8(), QCryptographicHash::Sha1).toHex();
	return QString::fromLatin1(h.left(8) + "-" + h.mid(8, 4) + "-" + h.mid(12, 4) + "-" + h.mid(16, 4) + "-" + h.mid(20, 12));
}

/* tipo do vMix para o tipo de fonte */
static QString vmixType(obs_source_t *src)
{
	const char *id = src ? obs_source_get_unversioned_id(src) : "";
	if (!id)
		return "Unknown";
	if (strstr(id, "dshow") || strstr(id, "v4l2") || strstr(id, "decklink") || strstr(id, "av_capture"))
		return "Capture";
	if (strstr(id, "ffmpeg") || strstr(id, "vlc"))
		return "Video";
	if (strstr(id, "image"))
		return "Image";
	if (strstr(id, "browser"))
		return "Browser";
	if (strstr(id, "color"))
		return "Colour";
	if (strstr(id, "text"))
		return "Xaml";
	if (strcmp(id, GT_SOURCE_ID) == 0)
		return "GT";
	if (strstr(id, "instant_replay"))
		return "Replay";
	if (strstr(id, "capture"))
		return "DesktopCapture";
	return "Unknown";
}

/* campos de um título GT (Nome.Text, Escudo.Source...) e o valor atual */
static QList<QPair<QString, QString>> gtFields(obs_source_t *src, GtField::Kind kind)
{
	QList<QPair<QString, QString>> out;
	std::shared_ptr<GtTitle> t = gtTitleOf(src);
	if (!t)
		return out;
	const QMap<QString, QString> vals = gtValues(src);
	for (const GtField &f : t->fields())
		if (f.kind == kind)
			out << qMakePair(f.id, vals.contains(f.id) ? vals.value(f.id) : f.value);
	return out;
}

QString VmixApi::stateXml()
{
	Engine &e = Engine::get();
	QString x;
	x.reserve(4096);
	x += "<vmix><version>27.0.0.49</version><edition>ReplayDeck Studio</edition><preset></preset><inputs>";
	int num = 0;
	for (Input *in : e.inputs()) {
		num++;
		x += QString("<input key=\"%1\" number=\"%2\" type=\"%3\" title=\"%4\" shortTitle=\"%4\" state=\"Running\" "
			     "position=\"0\" duration=\"0\" loop=\"%5\" muted=\"%6\" volume=\"%7\" balance=\"0\" solo=\"False\" "
			     "audiobusses=\"M\" meterF1=\"0\" meterF2=\"0\">%4")
			     .arg(inputKey(in->id))
			     .arg(num)
			     .arg(vmixType(in->source), esc(in->name), in->loop ? "True" : "False",
				  in->audioOn ? "False" : "True")
			     .arg(qRound(std::cbrt(in->volume) * 100.0));
		if (gtIsSource(in->source)) {
			int idx = 0;
			for (const auto &t : gtFields(in->source, GtField::Text))
				x += QString("<text index=\"%1\" name=\"%2\">%3</text>").arg(idx++).arg(esc(t.first), esc(t.second));
			idx = 0;
			for (const auto &t : gtFields(in->source, GtField::Image))
				x += QString("<image index=\"%1\" name=\"%2\">%3</image>").arg(idx++).arg(esc(t.first), esc(t.second));
		}
		x += "</input>";
	}
	x += "</inputs><overlays>";
	for (int i = 1; i <= 6; i++) {
		int n = 0;
		if (i <= 4)
			n = e.inputs().indexOf(e.input(e.overlay(i))) + 1;
		x += n > 0 ? QString("<overlay number=\"%1\">%2</overlay>").arg(i).arg(n)
			   : QString("<overlay number=\"%1\" />").arg(i);
	}
	const int pvw = e.inputs().indexOf(e.input(e.previewId())) + 1;
	const int pgm = e.inputs().indexOf(e.input(e.programId())) + 1;
	x += QString("</overlays><preview>%1</preview><active>%2</active><fadeToBlack>%3</fadeToBlack><transitions>")
		     .arg(pvw)
		     .arg(pgm)
		     .arg(e.fadedToBlack() ? "True" : "False");
	/* 4 botões como no vMix: os da aba Transições, na ordem; os que faltam são fade */
	for (int tn = 1; tn <= 4; tn++) {
		StudioTransition *t = e.nthTransition(tn, false);
		x += QString("<transition number=\"%1\" effect=\"%2\" duration=\"%3\" />")
			     .arg(tn)
			     .arg(t ? esc(t->name) : QString("Fade"))
			     .arg(t ? t->durationMs : e.transitionMs());
	}
	Outputs &o = Outputs::get();
	x += QString("</transitions><recording>%1</recording><external>False</external><streaming>%2</streaming>"
		     "<playList>%5</playList><multiCorder>%4</multiCorder><fullscreen>False</fullscreen>"
		     "<audio><master volume=\"%3\" muted=\"False\" meterF1=\"0\" meterF2=\"0\" headphonesVolume=\"100\" /></audio>"
		     "</vmix>")
		     .arg(o.recording() ? "True" : "False", o.streaming() ? "True" : "False")
		     .arg(qRound(std::cbrt(e.masterVolume()) * 100.0))
		     .arg(MultiCorder::get().active() ? "True" : "False")
		     .arg(Playlist::get().active() ? "True" : "False");
	return x;
}

/* um dígito por entrada: 0 fora, 1 no ar (programa ou overlay), 2 na prévia */
QString VmixApi::tallyString()
{
	Engine &e = Engine::get();
	QString t;
	for (Input *in : e.inputs()) {
		bool air = in->id == e.programId();
		for (int i = 1; i <= 4; i++)
			air = air || e.overlay(i) == in->id;
		t += air ? '1' : (in->id == e.previewId() ? '2' : '0');
	}
	return t;
}

void VmixApi::apply(bool enabled, int httpPort, int tcpPort)
{
	shutdown();
	if (!wired) {
		wired = true;
		Engine &e = Engine::get();
		connect(&e, &Engine::busChanged, this, &VmixApi::pushState);
		connect(&e, &Engine::inputsChanged, this, &VmixApi::pushState);
		connect(&Outputs::get(), &Outputs::stateChanged, this, &VmixApi::pushState);
		/* entrada nova/renomeada com o nome de uma entrada do vMix: ganha a chave dela */
		connect(&e, &Engine::inputsChanged, this, [] { adoptKnownKeys(); }, Qt::QueuedConnection);
		connect(&e, &Engine::inputRenamed, this, [] { adoptKnownKeys(); }, Qt::QueuedConnection);
		adoptKnownKeys();
	}
	if (!enabled) {
		statusText = "desligada";
		return;
	}
	QStringList st;
	http = new QTcpServer(this);
	if (http->listen(QHostAddress::Any, (quint16)httpPort)) {
		connect(http, &QTcpServer::newConnection, this, &VmixApi::onHttp);
		st << QString("HTTP na porta %1").arg(httpPort);
	} else {
		st << QString("HTTP: porta %1 ocupada (o vMix está aberto?)").arg(httpPort);
		delete http;
		http = nullptr;
	}
	tcp = new QTcpServer(this);
	if (tcp->listen(QHostAddress::Any, (quint16)tcpPort)) {
		connect(tcp, &QTcpServer::newConnection, this, &VmixApi::onTcp);
		st << QString("TCP na porta %1").arg(tcpPort);
	} else {
		st << QString("TCP: porta %1 ocupada").arg(tcpPort);
		delete tcp;
		tcp = nullptr;
	}
	statusText = st.join(" · ");
	blog(LOG_INFO, "API do vMix: %s", statusText.toUtf8().constData());
}

void VmixApi::shutdown()
{
	for (QTcpServer *s : {http, tcp})
		if (s) {
			s->close();
			s->deleteLater();
		}
	http = tcp = nullptr;
	for (QTcpSocket *s : tallySubs + actsSubs)
		s->disconnectFromHost();
	tallySubs.clear();
	actsSubs.clear();
}

/* ---------- HTTP ---------- */

void VmixApi::onHttp()
{
	while (QTcpSocket *s = http->nextPendingConnection()) {
		connect(s, &QTcpSocket::readyRead, this, [this, s] { handleHttp(s); });
		connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
		/* conexão aberta sem pedido: fecha depois de um tempo */
		QTimer::singleShot(10000, s, [s] { s->abort(); });
	}
}

void VmixApi::handleHttp(QTcpSocket *s)
{
	/* quem manda comandos pode ser o APPVMIX: aprende os nomes do vMix das chaves dele */
	learnFromApp(s->peerAddress().toString().remove("::ffff:"));
	QByteArray req = s->property("req").toByteArray() + s->readAll();
	if (!req.contains("\r\n\r\n") && req.size() < 16384) {
		s->setProperty("req", req);
		return;
	}
	const QList<QByteArray> first = req.left(req.indexOf("\r\n")).split(' ');
	const QString target = first.size() >= 2 ? QString::fromUtf8(first[1]) : QString("/");
	const QUrl url("http://localhost" + target);
	const QString path = url.path().toLower();
	QByteArray body, type = "text/xml; charset=utf-8";
	int code = 200;
	if (path == "/api" || path == "/api/") {
		const QUrlQuery q(url);
		QMap<QString, QString> params;
		QString fn;
		for (const auto &kv : q.queryItems(QUrl::FullyDecoded)) {
			if (kv.first.compare("Function", Qt::CaseInsensitive) == 0)
				fn = kv.second;
			else
				params.insert(kv.first, kv.second);
		}
		if (fn.isEmpty()) {
			body = stateXml().toUtf8();
		} else {
			QString err;
			type = "text/plain; charset=utf-8";
			if (runFunctionParams(fn, params, &err)) {
				body = "Function completed successfully.";
			} else {
				code = 500;
				body = err.toUtf8();
				blog(LOG_INFO, "API: %s falhou: %s", fn.toUtf8().constData(), err.toUtf8().constData());
			}
		}
	} else if (path == "/" && url.hasQuery() && QUrlQuery(url).hasQueryItem("shortcut")) {
		/* atalho pelo controlador web do vMix: /?shortcut=ID */
		const QString id = QUrlQuery(url).queryItemValue("shortcut");
		type = "text/plain; charset=utf-8";
		bool found = false;
		for (const Shortcut &sc : Shortcuts::get().list()) {
			if (shortcutId(sc) != id)
				continue;
			found = true;
			QMap<QString, QString> params;
			params.insert("Input", sc.input);
			params.insert("Value", sc.value);
			QString err;
			if (!runFunctionParams(sc.function, params, &err)) {
				code = 500;
				body = err.toUtf8();
			}
			break;
		}
		if (!found) {
			code = 404;
			body = "Shortcut not found";
		}
	} else if (path == "/") {
		/* página de atalhos do controlador web do vMix (apps como o APPVMIX leem esta lista) */
		type = "text/html; charset=utf-8";
		QString h = "<!doctype html><html><head><meta charset='utf-8'><title>ReplayDeck Studio - Shortcuts</title>"
			    "<style>body{font-family:sans-serif;background:#1c212a;color:#eef}.shortcutButton{display:inline-block;"
			    "margin:4px;padding:10px;background:#2b3549;border-radius:4px;cursor:pointer}</style>"
			    "<script>function run(id){fetch('?shortcut='+id)}</script></head><body>"
			    "<h3>ReplayDeck Studio</h3><p>API igual a do vMix: <a href='/api'>/api</a></p>";
		for (const Shortcut &sc : Shortcuts::get().list()) {
			const QString id = shortcutId(sc);
			h += QString("<div onclick=\"javascript:run('%1');$.ajax('?shortcut=%1');\" class=\"shortcutButton\">\n"
				     "    <div class=\"shortcutButtonText\">\n        <strong>%2</strong><br />%3<br />%4\n    </div>\n    </div>\n")
				     .arg(id, esc(sc.display()), esc(sc.function), esc(sc.input.isEmpty() ? QString("Preview") : sc.input));
		}
		h += "</body></html>";
		body = h.toUtf8();
	} else if (path == "/tally" || path == "/tally/") {
		type = "text/plain; charset=utf-8";
		body = tallyString().toUtf8();
	} else {
		code = 404;
		type = "text/plain; charset=utf-8";
		body = "not found";
	}
	QByteArray resp = "HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error") +
			  "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
			  "\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body;
	s->write(resp);
	s->disconnectFromHost();
}

/* ---------- TCP ---------- */

void VmixApi::onTcp()
{
	while (QTcpSocket *s = tcp->nextPendingConnection()) {
		s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		connect(s, &QTcpSocket::readyRead, this, [this, s] {
			QByteArray buf = s->property("buf").toByteArray() + s->readAll();
			int nl;
			while ((nl = buf.indexOf('\n')) >= 0) {
				const QString line = QString::fromUtf8(buf.left(nl)).trimmed();
				buf.remove(0, nl + 1);
				if (!line.isEmpty())
					handleTcpLine(s, line);
			}
			if (buf.size() > 65536)
				buf.clear();
			s->setProperty("buf", buf);
		});
		connect(s, &QTcpSocket::disconnected, this, [this, s] {
			tallySubs.remove(s);
			actsSubs.remove(s);
			s->deleteLater();
		});
	}
}

static QStringList actsLines()
{
	Engine &e = Engine::get();
	QStringList l;
	auto num = [&e](int id) { return e.inputs().indexOf(e.input(id)) + 1; };
	l << QString("ACTS OK Input %1 1").arg(num(e.programId()));
	l << QString("ACTS OK InputPreview %1 1").arg(num(e.previewId()));
	for (int i = 1; i <= 4; i++)
		l << QString("ACTS OK Overlay%1 %2 %3").arg(i).arg(num(e.overlay(i))).arg(e.overlay(i) ? 1 : 0);
	l << QString("ACTS OK FadeToBlack %1").arg(e.fadedToBlack() ? 1 : 0);
	l << QString("ACTS OK Recording %1").arg(Outputs::get().recording() ? 1 : 0);
	l << QString("ACTS OK Streaming %1").arg(Outputs::get().streaming() ? 1 : 0);
	return l;
}

void VmixApi::handleTcpLine(QTcpSocket *s, const QString &line)
{
	learnFromApp(s->peerAddress().toString().remove("::ffff:"));
	const QString cmd = line.section(' ', 0, 0).toUpper();
	const QString rest = line.section(' ', 1);
	auto send = [s](const QString &t) { s->write((t + "\r\n").toUtf8()); };
	if (cmd == "FUNCTION") {
		const QString fn = rest.section(' ', 0, 0);
		QMap<QString, QString> params;
		const QUrlQuery q(rest.section(' ', 1).trimmed());
		for (const auto &kv : q.queryItems(QUrl::FullyDecoded))
			params.insert(kv.first, kv.second);
		QString err;
		if (runFunctionParams(fn, params, &err))
			send("FUNCTION OK Completed");
		else
			send("FUNCTION ER " + err);
	} else if (cmd == "XML") {
		const QByteArray x = stateXml().toUtf8() + "\r\n";
		s->write("XML " + QByteArray::number(x.size()) + "\r\n" + x);
	} else if (cmd == "TALLY") {
		send("TALLY OK " + tallyString());
	} else if (cmd == "SUBSCRIBE" || cmd == "UNSUBSCRIBE") {
		const QString what = rest.trimmed().toUpper();
		QSet<QTcpSocket *> *set = what == "TALLY" ? &tallySubs : what == "ACTS" ? &actsSubs : nullptr;
		if (!set) {
			send(cmd + " ER " + what);
			return;
		}
		if (cmd == "SUBSCRIBE") {
			set->insert(s);
			send("SUBSCRIBE OK " + what);
			if (set == &tallySubs)
				send("TALLY OK " + tallyString());
			else
				for (const QString &a : actsLines())
					send(a);
		} else {
			set->remove(s);
			send("UNSUBSCRIBE OK " + what);
		}
	} else if (cmd == "VERSION") {
		send("VERSION OK 27.0.0.49");
	} else if (cmd == "QUIT") {
		s->disconnectFromHost();
	} else {
		send(cmd + " ER comando desconhecido");
	}
}

void VmixApi::pushState()
{
	if (!tallySubs.isEmpty()) {
		const QString t = tallyString();
		if (t != lastTally) {
			lastTally = t;
			for (QTcpSocket *s : tallySubs)
				s->write(("TALLY OK " + t + "\r\n").toUtf8());
		}
	}
	if (!actsSubs.isEmpty()) {
		const QStringList a = actsLines();
		for (int i = 0; i < a.size(); i++)
			if (i >= lastActs.size() || lastActs[i] != a[i])
				for (QTcpSocket *s : actsSubs)
					s->write((a[i] + "\r\n").toUtf8());
		lastActs = a;
	}
}
