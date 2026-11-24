#include "outputs.hpp"
#include "engine.hpp"
#include "vertical-canvas.hpp"

#include <util/platform.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

const QList<StreamPreset> &streamPresets()
{
	static const QList<StreamPreset> list = {
		{"YouTube", "rtmp://a.rtmp.youtube.com/live2"},
		{"Facebook", "rtmps://live-api-s.facebook.com:443/rtmp/"},
		{"Twitch", "rtmp://live.twitch.tv/app"},
		{"Instagram", "rtmps://live-upload.instagram.com:443/rtmp/"},
		{"TikTok", ""},
		{"Kick", "rtmps://fa723fc1b171.global-contribute.live-video.net/app"},
		{"Personalizado", ""},
	};
	return list;
}

static bool encoderRegistered(const char *id)
{
	const char *t = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &t); i++)
		if (strcmp(t, id) == 0)
			return true;
	return false;
}

/* placa de vídeo primeiro (NVIDIA, AMD, Intel); x264 (processador) sempre funciona */
static const QList<QPair<QString, QString>> &encoderCandidates()
{
	static const QList<QPair<QString, QString>> list = {
		{"obs_nvenc_h264_tex", "NVIDIA NVENC (placa de vídeo)"},
		{"jim_nvenc", "NVIDIA NVENC (placa de vídeo)"},
		{"h264_texture_amf", "AMD (placa de vídeo)"},
		{"obs_qsv11_v2", "Intel Quick Sync (placa de vídeo)"},
		{"obs_qsv11", "Intel Quick Sync (placa de vídeo)"},
		{"obs_x264", "x264 (processador)"},
	};
	return list;
}

QList<QPair<QString, QString>> availableVideoEncoders()
{
	QList<QPair<QString, QString>> out;
	QStringList names;
	for (const auto &c : encoderCandidates()) {
		if (!encoderRegistered(c.first.toUtf8().constData()) || names.contains(c.second))
			continue;
		names << c.second;
		out << c;
	}
	return out;
}

Outputs &Outputs::get()
{
	static Outputs o;
	return o;
}

QString Outputs::settingsPath() const
{
	return Engine::get().configDir() + "/configuracoes.json";
}

void Outputs::load()
{
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(QFile::encodeName(settingsPath()).constData(), "bak");
	StudioSettings s;
	s.width = (int)Engine::get().baseWidth();
	s.height = (int)Engine::get().baseHeight();
	if (d) {
		auto str = [&](const char *k, QString &v) {
			if (obs_data_has_user_value(d, k))
				v = QString::fromUtf8(obs_data_get_string(d, k));
		};
		auto num = [&](const char *k, int &v) {
			if (obs_data_has_user_value(d, k))
				v = (int)obs_data_get_int(d, k);
		};
		num("largura", s.width);
		num("altura", s.height);
		num("fps_num", s.fpsNum);
		num("fps_den", s.fpsDen);
		str("gravar_pasta", s.recFolder);
		str("gravar_formato", s.recFormat);
		str("gravar_codificador", s.recEncoder);
		num("gravar_kbps", s.recBitrate);
		str("transmitir_codificador", s.streamEncoder);
		num("transmitir_kbps", s.streamBitrate);
		num("vertical_kbps", s.verticalBitrate);
		num("vertical_largura", s.verticalWidth);
		num("vertical_altura", s.verticalHeight);
		num("vertical_deslocamento", s.verticalOffset);
		s.verticalFit = obs_data_get_bool(d, "vertical_encaixar");
		OBSDataArrayAutoRelease arr = obs_data_get_array(d, "destinos");
		for (size_t i = 0; i < obs_data_array_count(arr); i++) {
			OBSDataAutoRelease o = obs_data_array_item(arr, i);
			StreamDestination dst;
			dst.enabled = obs_data_get_bool(o, "ativo");
			dst.name = QString::fromUtf8(obs_data_get_string(o, "nome"));
			dst.service = QString::fromUtf8(obs_data_get_string(o, "servico"));
			dst.server = QString::fromUtf8(obs_data_get_string(o, "servidor"));
			dst.key = QString::fromUtf8(obs_data_get_string(o, "chave"));
			dst.vertical = obs_data_get_bool(o, "vertical");
			s.destinations << dst;
		}
		/* versão anterior: um destino só */
		if (!arr && obs_data_has_user_value(d, "transmitir_servidor")) {
			StreamDestination dst;
			dst.service = QString::fromUtf8(obs_data_get_string(d, "transmitir_servico"));
			dst.name = dst.service;
			dst.server = QString::fromUtf8(obs_data_get_string(d, "transmitir_servidor"));
			dst.key = QString::fromUtf8(obs_data_get_string(d, "transmitir_chave"));
			s.destinations << dst;
		}
		num("audio_kbps", s.audioBitrate);
		str("fone_id", s.monitorDeviceId);
		str("fone_nome", s.monitorDeviceName);
	}
	cfg = s;
	applyMonitor();
}

void Outputs::save()
{
	OBSDataAutoRelease d = obs_data_create();
	obs_data_set_int(d, "largura", cfg.width);
	obs_data_set_int(d, "altura", cfg.height);
	obs_data_set_int(d, "fps_num", cfg.fpsNum);
	obs_data_set_int(d, "fps_den", cfg.fpsDen);
	obs_data_set_string(d, "gravar_pasta", cfg.recFolder.toUtf8().constData());
	obs_data_set_string(d, "gravar_formato", cfg.recFormat.toUtf8().constData());
	obs_data_set_string(d, "gravar_codificador", cfg.recEncoder.toUtf8().constData());
	obs_data_set_int(d, "gravar_kbps", cfg.recBitrate);
	obs_data_set_string(d, "transmitir_codificador", cfg.streamEncoder.toUtf8().constData());
	obs_data_set_int(d, "transmitir_kbps", cfg.streamBitrate);
	obs_data_set_int(d, "vertical_kbps", cfg.verticalBitrate);
	obs_data_set_int(d, "vertical_largura", cfg.verticalWidth);
	obs_data_set_int(d, "vertical_altura", cfg.verticalHeight);
	obs_data_set_int(d, "vertical_deslocamento", cfg.verticalOffset);
	obs_data_set_bool(d, "vertical_encaixar", cfg.verticalFit);
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const StreamDestination &dst : cfg.destinations) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_bool(o, "ativo", dst.enabled);
		obs_data_set_string(o, "nome", dst.name.toUtf8().constData());
		obs_data_set_string(o, "servico", dst.service.toUtf8().constData());
		obs_data_set_string(o, "servidor", dst.server.toUtf8().constData());
		obs_data_set_string(o, "chave", dst.key.toUtf8().constData());
		obs_data_set_bool(o, "vertical", dst.vertical);
		obs_data_array_push_back(arr, o);
	}
	obs_data_set_array(d, "destinos", arr);
	obs_data_set_int(d, "audio_kbps", cfg.audioBitrate);
	obs_data_set_string(d, "fone_id", cfg.monitorDeviceId.toUtf8().constData());
	obs_data_set_string(d, "fone_nome", cfg.monitorDeviceName.toUtf8().constData());
	obs_data_save_json_safe(d, QFile::encodeName(settingsPath()).constData(), "tmp", "bak");
}

bool Outputs::applyVideo(const StudioSettings &s, QString &error)
{
	return Engine::get().resetVideo((uint32_t)s.width, (uint32_t)s.height, (uint32_t)s.fpsNum, (uint32_t)s.fpsDen,
					error);
}

void Outputs::applyMonitor()
{
	const QByteArray id = cfg.monitorDeviceId.isEmpty() ? QByteArray("default") : cfg.monitorDeviceId.toUtf8();
	const QByteArray name = cfg.monitorDeviceName.toUtf8();
	obs_set_audio_monitoring_device(name.constData(), id.constData());
}

bool Outputs::setSettings(const StudioSettings &s, QString &error)
{
	if (s.width != cfg.width || s.height != cfg.height || s.fpsNum != cfg.fpsNum || s.fpsDen != cfg.fpsDen) {
		if (recording() || streaming()) {
			error = "Pare a gravação e a transmissão antes de mudar a resolução ou os quadros por segundo.";
			return false;
		}
		if (!applyVideo(s, error))
			return false;
	}
	cfg = s;
	applyMonitor();
	save();
	return true;
}

obs_encoder_t *Outputs::makeVideoEncoder(const QString &choice, int kbps, const char *name, QString &usedId)
{
	QStringList ids;
	if (choice != "auto" && !choice.isEmpty())
		ids << choice;
	for (const auto &c : availableVideoEncoders())
		ids << c.first;
	ids << "obs_x264";
	for (const QString &id : ids) {
		const QByteArray idb = id.toUtf8();
		if (!encoderRegistered(idb.constData()))
			continue;
		OBSDataAutoRelease st = obs_data_create();
		obs_data_set_string(st, "rate_control", "CBR");
		obs_data_set_int(st, "bitrate", kbps);
		obs_data_set_int(st, "keyint_sec", 2);
		if (id == "obs_x264")
			obs_data_set_string(st, "preset", "veryfast");
		obs_encoder_t *enc = obs_video_encoder_create(idb.constData(), name, st, nullptr);
		if (enc) {
			obs_encoder_set_video(enc, obs_get_video());
			usedId = id;
			return enc;
		}
	}
	return nullptr;
}

static obs_encoder_t *makeAudioEncoder(int kbps, const char *name)
{
	OBSDataAutoRelease st = obs_data_create();
	obs_data_set_int(st, "bitrate", kbps);
	obs_encoder_t *enc = obs_audio_encoder_create("ffmpeg_aac", name, st, 0, nullptr);
	if (enc)
		obs_encoder_set_audio(enc, obs_get_audio());
	return enc;
}

/* ---------------- gravação ---------------- */

bool Outputs::recording() const
{
	return recOut && obs_output_active(recOut);
}

bool Outputs::startRecording(QString &error)
{
	if (recording())
		return true;
	QString folder = cfg.recFolder;
	if (folder.isEmpty())
		folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ReplayDeck Studio";
	if (!QDir().mkpath(folder)) {
		error = "Não foi possível criar a pasta de gravação:\n" + folder;
		return false;
	}
	const QString ext = cfg.recFormat.isEmpty() ? "mp4" : cfg.recFormat;
	recFile = QDir::toNativeSeparators(folder + "/ReplayDeck " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss") +
					   "." + ext);

	/* um codificador; se ele não ligar (placa sem suporte), tenta o próximo até o x264 */
	QStringList tried;
	QString choice = cfg.recEncoder;
	for (int attempt = 0; attempt < 6; attempt++) {
		QString used;
		recVideo = makeVideoEncoder(choice, cfg.recBitrate, "gravar-video", used);
		if (!recVideo) {
			error = "Nenhum codificador de vídeo disponível.";
			return false;
		}
		recAudio = makeAudioEncoder(cfg.audioBitrate, "gravar-audio");
		OBSDataAutoRelease st = obs_data_create();
		obs_data_set_string(st, "path", recFile.toUtf8().constData());
		if (ext == "mp4" || ext == "mov") /* fragmentado: o arquivo abre mesmo se o PC desligar no meio */
			obs_data_set_string(st, "muxer_settings", "movflags=frag_keyframe+empty_moov+delay_moov");
		recOut = obs_output_create("ffmpeg_muxer", "gravar", st, nullptr);
		obs_output_set_video_encoder(recOut, recVideo);
		obs_output_set_audio_encoder(recOut, recAudio, 0);
		signal_handler_connect(obs_output_get_signal_handler(recOut), "stop", onRecStop, this);
		if (obs_output_start(recOut)) {
			recTimer.start();
			blog(LOG_INFO, "gravando: %s (%s, %d kbps)", recFile.toUtf8().constData(), used.toUtf8().constData(),
			     cfg.recBitrate);
			emit stateChanged();
			return true;
		}
		const char *last = obs_output_get_last_error(recOut);
		error = last ? QString::fromUtf8(last) : "A gravação não começou.";
		blog(LOG_WARNING, "gravação não começou com %s: %s", used.toUtf8().constData(), error.toUtf8().constData());
		signal_handler_disconnect(obs_output_get_signal_handler(recOut), "stop", onRecStop, this);
		recOut = nullptr;
		tried << used;
		if (used == "obs_x264")
			break;
		/* próximo da lista */
		const auto avail = availableVideoEncoders();
		choice = "obs_x264";
		for (const auto &c : avail)
			if (!tried.contains(c.first)) {
				choice = c.first;
				break;
			}
	}
	return false;
}

/* só para o que está ligado: parar uma saída que não ligou deixa o motor esperando
 * um fim que nunca vem, e soltar essa saída trava o programa */
void Outputs::stopRecording()
{
	if (recOut && obs_output_active(recOut))
		obs_output_stop(recOut);
}

void Outputs::onRecStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<Outputs *>(data);
	const int code = (int)calldata_int(cd, "code");
	QString msg;
	if (code != OBS_OUTPUT_SUCCESS) {
		const char *e = calldata_string(cd, "last_error");
		msg = e && *e ? QString::fromUtf8(e) : QString("código %1").arg(code);
	}
	QMetaObject::invokeMethod(self, [self, msg] {
		blog(LOG_INFO, "gravação parada: %s", self->recFile.toUtf8().constData());
		if (!msg.isEmpty())
			emit self->failed("Gravação", msg);
		emit self->stateChanged();
	}, Qt::QueuedConnection);
}

/* ---------------- transmissão (vários destinos) ---------------- */

bool Outputs::streaming() const
{
	return liveCount() > 0;
}

int Outputs::liveCount() const
{
	int n = 0;
	for (const Live *l : live)
		n += l->running ? 1 : 0;
	return n;
}

bool Outputs::streamReconnecting() const
{
	for (const Live *l : live)
		if (l->reconnecting)
			return true;
	return false;
}

int Outputs::streamDroppedFrames() const
{
	int n = 0;
	for (const Live *l : live)
		n += obs_output_get_frames_dropped(l->out);
	return n;
}

int Outputs::streamTotalFrames() const
{
	int n = 0;
	for (const Live *l : live)
		n += obs_output_get_total_frames(l->out);
	return n;
}

Outputs::Live *Outputs::liveFor(obs_output_t *out)
{
	for (Live *l : live)
		if (l->out.Get() == out)
			return l;
	return nullptr;
}

void Outputs::releaseStreams()
{
	for (Live *l : live) {
		signal_handler_t *sh = obs_output_get_signal_handler(l->out);
		signal_handler_disconnect(sh, "stop", onStreamStop, this);
		signal_handler_disconnect(sh, "reconnect", onStreamReconnect, this);
		signal_handler_disconnect(sh, "reconnect_success", onStreamReconnected, this);
		/* ligada (mesmo ainda conectando): para de vez; nunca ligada: não pode parar (trava) */
		if (l->running || obs_output_active(l->out))
			obs_output_force_stop(l->out);
		delete l;
	}
	live.clear();
	streamVideo = nullptr;
	streamVideoV = nullptr;
	streamAudio = nullptr;
	VerticalCanvas::get().stop();
}

bool Outputs::startStreaming(QString &error)
{
	if (streaming())
		return true;
	releaseStreams(); /* restos de uma transmissão anterior */
	QList<StreamDestination> todo;
	for (const StreamDestination &d : cfg.destinations)
		if (d.enabled && !d.server.trimmed().isEmpty() && !d.key.trimmed().isEmpty())
			todo << d;
	if (todo.isEmpty()) {
		error = "Nenhum destino ativo com servidor e chave (Configurações → Transmissão).";
		return false;
	}
	bool needH = false, needV = false;
	for (const StreamDestination &d : todo)
		(d.vertical ? needV : needH) = true;

	QString usedH, usedV;
	streamAudio = makeAudioEncoder(cfg.audioBitrate, "transmitir-audio");
	if (needH)
		streamVideo = makeVideoEncoder(cfg.streamEncoder, cfg.streamBitrate, "transmitir-video", usedH);
	if (needV) {
		if (!VerticalCanvas::get().start(cfg.verticalWidth, cfg.verticalHeight, cfg.verticalFit, cfg.verticalOffset,
						 error))
			return false;
		streamVideoV = makeVideoEncoder(cfg.streamEncoder, cfg.verticalBitrate, "transmitir-vertical", usedV);
		if (streamVideoV)
			obs_encoder_set_video(streamVideoV, VerticalCanvas::get().videoOutput());
	}
	if (!streamAudio || (needH && !streamVideo) || (needV && !streamVideoV)) {
		error = "Codificador indisponível.";
		releaseStreams();
		return false;
	}

	QStringList failed;
	for (const StreamDestination &d : todo) {
		auto *l = new Live;
		l->name = d.name.isEmpty() ? d.service : d.name;
		OBSDataAutoRelease ss = obs_data_create();
		obs_data_set_string(ss, "server", d.server.trimmed().toUtf8().constData());
		obs_data_set_string(ss, "key", d.key.trimmed().toUtf8().constData());
		const QByteArray outName = ("transmitir-" + l->name).toUtf8();
		l->service = obs_service_create("rtmp_custom", outName.constData(), ss, nullptr);
		OBSDataAutoRelease os = obs_data_create();
		l->out = obs_output_create("rtmp_output", outName.constData(), os, nullptr);
		obs_output_set_service(l->out, l->service);
		obs_output_set_video_encoder(l->out, d.vertical ? streamVideoV : streamVideo);
		obs_output_set_audio_encoder(l->out, streamAudio, 0);
		obs_output_set_reconnect_settings(l->out, 20, 3); /* caiu a internet: tenta de novo sozinho */
		signal_handler_t *sh = obs_output_get_signal_handler(l->out);
		signal_handler_connect(sh, "stop", onStreamStop, this);
		signal_handler_connect(sh, "reconnect", onStreamReconnect, this);
		signal_handler_connect(sh, "reconnect_success", onStreamReconnected, this);
		live << l;
		if (obs_output_start(l->out)) {
			l->running = true;
			blog(LOG_INFO, "transmitindo para %s (%s, %s)", l->name.toUtf8().constData(),
			     d.server.toUtf8().constData(), d.vertical ? "vertical" : "horizontal");
		} else {
			const char *last = obs_output_get_last_error(l->out);
			failed << l->name + (last ? ": " + QString::fromUtf8(last) : QString());
		}
	}
	if (!streaming()) {
		error = "Nenhum destino conectou.\n" + failed.join("\n");
		releaseStreams();
		return false;
	}
	if (!failed.isEmpty())
		emit this->failed("Transmissão", "Estes destinos não conectaram:\n" + failed.join("\n"));
	streamTimer.start();
	blog(LOG_INFO, "transmissão: %d destino(s) (%s%s%s)", liveCount(), usedH.toUtf8().constData(),
	     needH && needV ? " + " : "", usedV.toUtf8().constData());
	emit stateChanged();
	return true;
}

void Outputs::stopStreaming()
{
	for (Live *l : live)
		if (l->running)
			obs_output_stop(l->out);
}

void Outputs::onStreamStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<Outputs *>(data);
	auto *out = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	const int code = (int)calldata_int(cd, "code");
	QString msg;
	if (code != OBS_OUTPUT_SUCCESS) {
		const char *e = calldata_string(cd, "last_error");
		msg = e && *e ? QString::fromUtf8(e)
			      : code == OBS_OUTPUT_BAD_PATH        ? QString("Servidor inválido.")
			      : code == OBS_OUTPUT_CONNECT_FAILED  ? QString("Não conectou ao servidor (internet ou endereço).")
			      : code == OBS_OUTPUT_INVALID_STREAM  ? QString("A chave da transmissão foi recusada.")
			      : code == OBS_OUTPUT_DISCONNECTED    ? QString("A conexão caiu e não voltou.")
							      : QString("código %1").arg(code);
	}
	QMetaObject::invokeMethod(self, [self, out, msg] {
		Live *l = self->liveFor(out);
		const QString name = l ? l->name : QString("destino");
		if (l) {
			l->reconnecting = false;
			l->running = false;
		}
		blog(LOG_INFO, "transmissão parada: %s %s", name.toUtf8().constData(), msg.toUtf8().constData());
		if (!msg.isEmpty())
			emit self->failed("Transmissão", name + ": " + msg);
		if (!self->streaming())
			self->releaseStreams(); /* todos pararam: solta codificadores e o quadro vertical */
		emit self->stateChanged();
	}, Qt::QueuedConnection);
}

void Outputs::onStreamReconnect(void *data, calldata_t *cd)
{
	auto *self = static_cast<Outputs *>(data);
	auto *out = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	QMetaObject::invokeMethod(self, [self, out] {
		if (Live *l = self->liveFor(out))
			l->reconnecting = true;
		emit self->stateChanged();
	}, Qt::QueuedConnection);
}

void Outputs::onStreamReconnected(void *data, calldata_t *cd)
{
	auto *self = static_cast<Outputs *>(data);
	auto *out = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	QMetaObject::invokeMethod(self, [self, out] {
		if (Live *l = self->liveFor(out))
			l->reconnecting = false;
		emit self->stateChanged();
	}, Qt::QueuedConnection);
}

void Outputs::shutdown()
{
	if (recOut) {
		if (obs_output_active(recOut))
			obs_output_stop(recOut);
		signal_handler_disconnect(obs_output_get_signal_handler(recOut), "stop", onRecStop, this);
	}
	releaseStreams();
	/* espera a gravação fechar o arquivo */
	for (int i = 0; i < 100 && recOut && obs_output_active(recOut); i++)
		os_sleep_ms(50);
	recOut = nullptr;
	recVideo = nullptr;
	recAudio = nullptr;
}
