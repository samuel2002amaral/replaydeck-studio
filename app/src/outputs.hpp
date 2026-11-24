/* Saídas do programa: gravar e transmitir (o que está no Programa, com o áudio do Master),
 * e as configurações gerais (vídeo, gravação, transmissão, fone), salvas em configuracoes.json. */
#pragma once

#include <obs.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

/* um destino da transmissão (YouTube, Facebook, Instagram...) */
struct StreamDestination {
	bool enabled = true;
	QString name;     /* como aparece para o operador */
	QString service;  /* nome do serviço (lista streamPresets) */
	QString server;
	QString key;
	bool vertical = false; /* quadro vertical 9:16 em vez do Programa 16:9 */
};

struct StudioSettings {
	/* vídeo */
	int width = 1920, height = 1080;
	int fpsNum = 30, fpsDen = 1;
	/* API igual a do vMix (Stream Deck, Companion, celular) */
	bool apiEnabled = true;
	int apiHttpPort = 8088, apiTcpPort = 8099;
	QString adapter = "auto"; /* placa de vídeo: "auto" (a dedicada) ou o nome; vale ao reabrir */
	/* gravação */
	QString recFolder;          /* vazio = Vídeos/ReplayDeck Studio */
	QString recFormat = "mp4";  /* mp4 (fragmentado: não corrompe se cair a energia), mkv, mov */
	QString recEncoder = "auto";
	int recBitrate = 15000;     /* kbps */
	/* transmissão: vários destinos ao mesmo tempo; um codificador por formato */
	QList<StreamDestination> destinations;
	QString streamEncoder = "auto";
	int streamBitrate = 6000;     /* horizontal */
	int verticalBitrate = 6000;
	int verticalWidth = 1080, verticalHeight = 1920;
	bool verticalFit = false;     /* encaixar com faixas (senão recorta o centro) */
	int verticalOffset = 0;       /* recorte: -100 esquerda .. 100 direita */
	bool hasStreamKey() const
	{
		for (const StreamDestination &d : destinations)
			if (d.enabled && !d.key.trimmed().isEmpty() && !d.server.trimmed().isEmpty())
				return true;
		return false;
	}
	/* áudio */
	int audioBitrate = 160;
	QString monitorDeviceId = "default";
	QString monitorDeviceName;
};

struct StreamPreset {
	const char *name;
	const char *server;
};
const QList<StreamPreset> &streamPresets();

/* codificadores de vídeo H.264 disponíveis neste PC (id, nome), placa de vídeo primeiro */
QList<QPair<QString, QString>> availableVideoEncoders();

class Outputs : public QObject {
	Q_OBJECT

public:
	static Outputs &get();

	StudioSettings settings() const { return cfg; }
	/* codificador de vídeo pela escolha ("auto" = o melhor da placa); usado também pelo MultiCorder */
	obs_encoder_t *makeVideoEncoder(const QString &choice, int kbps, const char *name, QString &usedId);
	/* aplica e salva; vídeo só muda sem nada gravando/transmitindo (error explica) */
	bool setSettings(const StudioSettings &s, QString &error);
	void load(); /* lê o arquivo e aplica (chamar depois do motor ligar) */

	bool startRecording(QString &error);
	void stopRecording();
	bool recording() const;
	QString recordingFile() const { return recFile; }
	qint64 recordingMs() const { return recording() ? recTimer.elapsed() : 0; }

	bool startStreaming(QString &error);
	void stopStreaming();
	bool streaming() const;
	int liveCount() const;         /* destinos no ar agora */
	bool streamReconnecting() const;
	qint64 streamingMs() const { return streaming() ? streamTimer.elapsed() : 0; }
	int streamDroppedFrames() const;
	int streamTotalFrames() const;

	void shutdown(); /* para tudo (ao fechar) */

signals:
	void stateChanged();
	void failed(const QString &what, const QString &message);

private:
	Outputs() = default;
	QString settingsPath() const;
	void save();
	bool applyVideo(const StudioSettings &s, QString &error);
	void applyMonitor();

	static void onRecStop(void *data, calldata_t *cd);
	static void onStreamStop(void *data, calldata_t *cd);
	static void onStreamReconnect(void *data, calldata_t *cd);
	static void onStreamReconnected(void *data, calldata_t *cd);

	StudioSettings cfg;
	struct Live {
		QString name;
		OBSOutputAutoRelease out;
		OBSServiceAutoRelease service;
		bool reconnecting = false;
		bool running = false; /* ligou (a conexão termina de abrir em segundo plano) até o aviso de parada */
	};
	Live *liveFor(obs_output_t *out);
	void releaseStreams();

	OBSOutputAutoRelease recOut;
	OBSEncoderAutoRelease recVideo, recAudio, streamVideo, streamVideoV, streamAudio;
	QList<Live *> live;
	QElapsedTimer recTimer, streamTimer;
	QString recFile;
};
