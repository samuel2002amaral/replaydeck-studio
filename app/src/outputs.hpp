/* Saídas do programa: gravar e transmitir (o que está no Programa, com o áudio do Master),
 * e as configurações gerais (vídeo, gravação, transmissão, fone), salvas em configuracoes.json. */
#pragma once

#include <obs.hpp>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

struct StudioSettings {
	/* vídeo */
	int width = 1920, height = 1080;
	int fpsNum = 30, fpsDen = 1;
	/* gravação */
	QString recFolder;          /* vazio = Vídeos/ReplayDeck Studio */
	QString recFormat = "mp4";  /* mp4 (fragmentado: não corrompe se cair a energia), mkv, mov */
	QString recEncoder = "auto";
	int recBitrate = 15000;     /* kbps */
	/* transmissão */
	QString streamService = "YouTube";
	QString streamServer = "rtmp://a.rtmp.youtube.com/live2";
	QString streamKey;
	QString streamEncoder = "auto";
	int streamBitrate = 6000;
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
	bool streamReconnecting() const { return reconnecting; }
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
	obs_encoder_t *makeVideoEncoder(const QString &choice, int kbps, const char *name, QString &usedId);
	static void onRecStop(void *data, calldata_t *cd);
	static void onStreamStop(void *data, calldata_t *cd);
	static void onStreamReconnect(void *data, calldata_t *cd);
	static void onStreamReconnected(void *data, calldata_t *cd);

	StudioSettings cfg;
	OBSOutputAutoRelease recOut, streamOut;
	OBSEncoderAutoRelease recVideo, recAudio, streamVideo, streamAudio;
	OBSServiceAutoRelease service;
	QElapsedTimer recTimer, streamTimer;
	QString recFile;
	bool reconnecting = false;
};
