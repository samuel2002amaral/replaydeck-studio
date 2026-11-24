#include "multicorder.hpp"
#include "engine.hpp"
#include "outputs.hpp"

#include <util/base.h>

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

MultiCorder &MultiCorder::get()
{
	static MultiCorder m;
	return m;
}

static QString safeName(QString n)
{
	static const QString bad = "\\/:*?\"<>|";
	for (QChar &c : n)
		if (bad.contains(c))
			c = '_';
	return n.trimmed().isEmpty() ? QString("entrada") : n.trimmed();
}

bool MultiCorder::start(const QList<int> &inputIds, QString &error)
{
	if (active())
		return true;
	reap();
	if (inputIds.isEmpty()) {
		error = "Escolha pelo menos uma entrada.";
		return false;
	}
	const StudioSettings cfg = Outputs::get().settings();
	QString base = cfg.recFolder;
	if (base.isEmpty())
		base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ReplayDeck Studio";
	dir = base + "/MultiCorder " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss");
	if (!QDir().mkpath(dir)) {
		error = "Não foi possível criar a pasta:\n" + dir;
		return false;
	}
	const QString ext = cfg.recFormat.isEmpty() ? "mp4" : cfg.recFormat;

	/* um codificador de áudio para todos (o som do Master em cada arquivo, para sincronizar na edição) */
	OBSDataAutoRelease ast = obs_data_create();
	obs_data_set_int(ast, "bitrate", cfg.audioBitrate);
	audio = obs_audio_encoder_create("ffmpeg_aac", "multicorder-audio", ast, 0, nullptr);
	if (audio)
		obs_encoder_set_audio(audio, obs_get_audio());

	obs_video_info ovi;
	obs_get_video_info(&ovi);
	QStringList failed;
	for (int id : inputIds) {
		Input *in = Engine::get().input(id);
		if (!in)
			continue;
		auto *r = new Rec;
		r->inputId = id;
		r->file = QDir::toNativeSeparators(dir + "/" + safeName(in->name) + "." + ext);
		r->view = obs_view_create();
		obs_view_set_source(r->view, 0, in->scene); /* a entrada limpa, como está na lista */
		r->video = obs_view_add2(r->view, &ovi);
		if (!r->video) {
			failed << in->name + " (vídeo)";
			release(r);
			continue;
		}
		OBSDataAutoRelease st = obs_data_create();
		obs_data_set_string(st, "path", r->file.toUtf8().constData());
		if (ext == "mp4" || ext == "mov")
			obs_data_set_string(st, "muxer_settings", "movflags=frag_keyframe+empty_moov+delay_moov");
		const QByteArray oname = ("multicorder-" + in->name).toUtf8();
		/* o codificador escolhido em Gravação; se a placa não deixar abrir mais um, tenta o próximo */
		QStringList tried;
		QString choice = cfg.recEncoder;
		for (int attempt = 0; attempt < 6 && !r->started; attempt++) {
			QString used;
			r->venc = Outputs::get().makeVideoEncoder(choice, cfg.recBitrate, oname.constData(), used);
			if (!r->venc)
				break;
			obs_encoder_set_video(r->venc, r->video);
			r->out = obs_output_create("ffmpeg_muxer", oname.constData(), st, nullptr);
			obs_output_set_video_encoder(r->out, r->venc);
			if (audio)
				obs_output_set_audio_encoder(r->out, audio, 0);
			r->started = obs_output_start(r->out);
			if (r->started) {
				blog(LOG_INFO, "MultiCorder: %s -> %s (%s)", in->name.toUtf8().constData(),
				     r->file.toUtf8().constData(), used.toUtf8().constData());
				break;
			}
			blog(LOG_WARNING, "MultiCorder: %s não começou com %s", in->name.toUtf8().constData(),
			     used.toUtf8().constData());
			r->out = nullptr;
			r->venc = nullptr;
			tried << used;
			choice = "obs_x264";
			for (const auto &c : availableVideoEncoders())
				if (!tried.contains(c.first)) {
					choice = c.first;
					break;
				}
			if (used == "obs_x264")
				break;
		}
		if (!r->started) {
			failed << in->name;
			release(r);
			continue;
		}
		recs << r;
	}
	if (recs.isEmpty()) {
		error = "Nenhuma entrada começou a gravar: " + failed.join(", ");
		audio = nullptr;
		return false;
	}
	running = true;
	timer.start();
	if (!failed.isEmpty())
		error = "Não gravando: " + failed.join(", ");
	emit stateChanged();
	return true;
}

void MultiCorder::release(Rec *r)
{
	if (r->out && obs_output_active(r->out))
		return; /* ainda parando: solta depois */
	r->out = nullptr;
	r->venc = nullptr;
	if (r->view) {
		obs_view_remove(r->view);
		obs_view_set_source(r->view, 0, nullptr);
		obs_view_destroy(r->view);
		r->view = nullptr;
	}
	delete r;
}

void MultiCorder::reap()
{
	for (int i = recs.size() - 1; i >= 0; i--) {
		Rec *r = recs[i];
		if (!r->out || !obs_output_active(r->out)) {
			recs.removeAt(i);
			release(r);
		}
	}
	if (recs.isEmpty())
		audio = nullptr;
}

void MultiCorder::stop()
{
	if (!running)
		return;
	running = false;
	/* só para o que está ligado (parar uma saída que não ligou trava o motor) */
	for (Rec *r : recs)
		if (r->out && obs_output_active(r->out))
			obs_output_stop(r->out);
	blog(LOG_INFO, "MultiCorder parado: %s", dir.toUtf8().constData());
	emit stateChanged();
	/* os arquivos fecham em segundo plano; solta quando terminarem */
	auto *t = new QTimer(this);
	connect(t, &QTimer::timeout, this, [this, t] {
		reap();
		if (recs.isEmpty())
			t->deleteLater();
	});
	t->start(300);
}

void MultiCorder::shutdown()
{
	stop();
	for (Rec *r : recs)
		if (r->out && obs_output_active(r->out))
			obs_output_force_stop(r->out);
	for (Rec *r : recs) {
		r->out = nullptr;
		release(r);
	}
	recs.clear();
	audio = nullptr;
}

/* ---------- escolha das entradas ---------- */

MultiCorderDialog::MultiCorderDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("MultiCorder");
	resize(420, 460);
	auto *v = new QVBoxLayout(this);
	auto *hint = new QLabel("Cada entrada marcada grava num arquivo separado (sem overlays), com o som do Master. "
				"Formato, codificador e qualidade: os de Configurações → Gravação.");
	hint->setWordWrap(true);
	hint->setObjectName("muted");
	v->addWidget(hint);
	list = new QListWidget;
	const QSet<int> last = MultiCorder::get().lastSelection;
	for (Input *in : Engine::get().inputs()) {
		auto *it = new QListWidgetItem(in->name, list);
		it->setData(Qt::UserRole, in->id);
		const char *tid = obs_source_get_unversioned_id(in->source);
		const bool camera = tid && (strstr(tid, "dshow") || strstr(tid, "v4l2") || strstr(tid, "decklink") || strstr(tid, "avcapture") ||
					    strstr(tid, "av_capture") || strstr(tid, "ndi"));
		it->setCheckState((last.isEmpty() ? camera : last.contains(in->id)) ? Qt::Checked : Qt::Unchecked);
	}
	v->addWidget(list, 1);
	auto *bb = new QDialogButtonBox;
	auto *go = bb->addButton("● Gravar", QDialogButtonBox::AcceptRole);
	go->setObjectName("primaryButton");
	bb->addButton("Cancelar", QDialogButtonBox::RejectRole);
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(bb);
}

QList<int> MultiCorderDialog::selected() const
{
	QList<int> ids;
	for (int i = 0; i < list->count(); i++)
		if (list->item(i)->checkState() == Qt::Checked)
			ids << list->item(i)->data(Qt::UserRole).toInt();
	return ids;
}
