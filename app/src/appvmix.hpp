/* Painel APPVMIX (app de GT/placar feito para o vMix): o Studio liga e desliga sozinho,
 * sem o iniciar.bat. Roda o servidor do painel (Node) escondido, a partir da pasta escolhida. */
#pragma once

#include <QObject>
#include <QString>

class QProcess;

class AppVmix : public QObject {
	Q_OBJECT

public:
	static AppVmix &get();
	QString folder() const { return dir; }
	bool autoStart() const { return autoOn; }
	void setFolder(const QString &f);
	void setAutoStart(bool on);
	void startIfWanted(); /* na abertura do programa */
	void start();
	void stop();
	bool running() const;
	QString url() const { return panelUrl; }
	QString status() const { return statusText; }
	/* Windows: libera a porta 3000 no Firewall para celulares/tablets (pede administrador uma vez) */
	static void openFirewall();
	static bool validFolder(const QString &f);

signals:
	void statusChanged();

private:
	AppVmix();
	void load();
	void save();
	void setStatus(const QString &s);
	void launch(const QStringList &steps);

	QString dir;
	bool autoOn = false;
	QProcess *proc = nullptr;
	QString panelUrl;
	QString statusText = "Desligado";
	bool stopping = false;
};
