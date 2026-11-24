/* API compatível com o vMix: controla o Studio pelo Stream Deck, Bitfocus Companion,
 * celular, outro PC... com as mesmas funções do vMix.
 *
 *  HTTP (porta 8088):  http://IP:8088/api/?Function=Cut&Input=2     /api = estado em XML
 *  TCP  (porta 8099):  FUNCTION Cut Input=2 · XML · TALLY · SUBSCRIBE TALLY · SUBSCRIBE ACTS · VERSION */
#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>

class QTcpServer;
class QTcpSocket;

class VmixApi : public QObject {
	Q_OBJECT

public:
	static VmixApi &get();
	void apply(bool enabled, int httpPort, int tcpPort); /* liga/desliga/troca a porta */
	void shutdown();
	QString status() const { return statusText; }
	static QString stateXml();
	static QString tallyString();

private:
	void onHttp();
	void onTcp();
	void handleHttp(QTcpSocket *s);
	void handleTcpLine(QTcpSocket *s, const QString &line);
	void pushState();
	QTcpServer *http = nullptr;
	QTcpServer *tcp = nullptr;
	QSet<QTcpSocket *> tallySubs, actsSubs;
	QString lastTally;
	QStringList lastActs;
	QString statusText;
	bool wired = false;
};
