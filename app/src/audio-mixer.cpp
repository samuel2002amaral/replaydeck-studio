#include "audio-mixer.hpp"
#include "engine.hpp"

#include <media-io/audio-io.h>
#include <obs-audio-controls.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

static const float METER_MIN_DB = -60.0f;

/* ---------------- medidor ---------------- */

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
	in[0] = in[1] = METER_MIN_DB;
	setFixedWidth(16);
	setMinimumHeight(80);
}

void LevelMeter::setLevels(float l, float r)
{
	/* guarda o maior até a próxima leitura da interface */
	float cur = in[0].load();
	while (l > cur && !in[0].compare_exchange_weak(cur, l)) {}
	cur = in[1].load();
	while (r > cur && !in[1].compare_exchange_weak(cur, r)) {}
}

void LevelMeter::tick()
{
	bool changed = false;
	for (int c = 0; c < 2; c++) {
		float v = in[c].exchange(METER_MIN_DB);
		if (!std::isfinite(v) || v < METER_MIN_DB)
			v = METER_MIN_DB;
		/* sobe na hora, desce devagar (como um medidor de mesa) */
		const float next = v > shown[c] ? v : std::max(v, shown[c] - 1.6f);
		if (next != shown[c]) {
			shown[c] = next;
			changed = true;
		}
		if (next >= hold[c]) {
			hold[c] = next;
			holdTicks[c] = 30;
		} else if (--holdTicks[c] <= 0 && hold[c] > METER_MIN_DB) {
			hold[c] = std::max(METER_MIN_DB, hold[c] - 1.0f);
			changed = true;
		}
	}
	if (changed)
		update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor("#0b0e13"));
	const int bw = (width() - 3) / 2;
	const int h = height();
	auto yOf = [h](float db) { return int(h * (1.0f - (std::clamp(db, METER_MIN_DB, 0.0f) - METER_MIN_DB) / -METER_MIN_DB)); };
	const int yYellow = yOf(-20.0f), yRed = yOf(-9.0f);
	for (int c = 0; c < 2; c++) {
		const int x = 1 + c * (bw + 1);
		const int top = yOf(shown[c]);
		/* verde até -20 dB, amarelo até -9 dB, vermelho acima */
		auto band = [&](int from, int to, const char *color) {
			const int a = std::max(top, from), b = to;
			if (b > a)
				p.fillRect(x, a, bw, b - a, QColor(color));
		};
		band(yYellow, h, "#22a55a");
		band(yRed, yYellow, "#d6b41f");
		band(0, yRed, "#d23b2f");
		if (hold[c] > METER_MIN_DB)
			p.fillRect(x, yOf(hold[c]), bw, 2, QColor("#e6eaf0"));
	}
	p.setPen(QColor(255, 255, 255, 40));
	for (float db : {-6.0f, -12.0f, -20.0f, -30.0f, -40.0f})
		p.drawLine(0, yOf(db), width(), yOf(db));
}

/* ---------------- faixa ---------------- */

static QPushButton *stripButton(const QString &text, const QString &tip)
{
	auto *b = new QPushButton(text);
	b->setObjectName("tileButton");
	b->setCheckable(true);
	b->setToolTip(tip);
	b->setFixedHeight(20);
	return b;
}

static void volmeterCb(void *param, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
		       const float[MAX_AUDIO_CHANNELS])
{
	(void)magnitude;
	static_cast<LevelMeter *>(param)->setLevels(peak[0], std::isfinite(peak[1]) ? peak[1] : peak[0]);
}

MixerStrip::MixerStrip(int inputId, QWidget *parent) : QWidget(parent), id(inputId)
{
	setObjectName("mixerStrip");
	setAttribute(Qt::WA_StyledBackground);
	setFixedWidth(84);
	conv = obs_fader_create(OBS_FADER_LOG);

	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(4, 4, 4, 4);
	v->setSpacing(4);
	title = new QLabel;
	title->setObjectName("mixerTitle");
	title->setAlignment(Qt::AlignCenter);
	title->setWordWrap(true);
	title->setFixedHeight(30);
	v->addWidget(title);

	auto *mid = new QHBoxLayout;
	mid->setSpacing(4);
	levels = new LevelMeter;
	fader = new QSlider(Qt::Vertical);
	fader->setRange(0, 1000);
	fader->setObjectName("mixerFader");
	fader->setToolTip("Volume (duplo clique no número: 0 dB)");
	mid->addStretch(1);
	mid->addWidget(levels);
	mid->addWidget(fader);
	mid->addStretch(1);
	v->addLayout(mid, 1);

	db = new QLabel;
	db->setObjectName("mixerDb");
	db->setAlignment(Qt::AlignCenter);
	v->addWidget(db);
	connect(fader, &QSlider::valueChanged, this, &MixerStrip::setFromSlider);

	if (id) {
		auto *row = new QHBoxLayout;
		row->setSpacing(1);
		onBtn = stripButton("On", "Entra na mistura (desligado = mudo)");
		autoBtn = stripButton("Auto", "Auto: só toca quando a entrada está no ar (Programa ou overlay).\n"
					      "Desligado: toca sempre (microfone, áudio do computador).");
		row->addWidget(onBtn);
		row->addWidget(autoBtn);
		v->addLayout(row);
		phonesBtn = stripButton("Fone", "Ouvir esta entrada no fone (saída de monitoramento)");
		v->addWidget(phonesBtn);
		connect(onBtn, &QPushButton::clicked, this, [this](bool on) { Engine::get().setInputAudio(id, on); });
		connect(autoBtn, &QPushButton::clicked, this, [this](bool on) { Engine::get().setInputAudioAuto(id, on); });
		connect(phonesBtn, &QPushButton::clicked, this, [this](bool on) { Engine::get().setInputMonitor(id, on); });

		if (Input *in = Engine::get().input(id)) {
			volmeter = obs_volmeter_create(OBS_FADER_LOG);
			obs_volmeter_attach_source(volmeter, in->source);
			obs_volmeter_add_callback(volmeter, volmeterCb, levels);
		}
	} else {
		auto *spacer = new QWidget;
		spacer->setFixedHeight(45);
		v->addWidget(spacer);
	}
	refresh();
}

MixerStrip::~MixerStrip()
{
	if (volmeter) {
		obs_volmeter_remove_callback(volmeter, volmeterCb, levels);
		obs_volmeter_destroy(volmeter);
	}
	obs_fader_destroy(conv);
}

void MixerStrip::setFromSlider(int pos)
{
	if (updating)
		return;
	obs_fader_set_deflection(conv, pos / 1000.0f);
	const float mul = pos <= 0 ? 0.0f : obs_fader_get_mul(conv);
	if (id)
		Engine::get().setInputVolume(id, mul);
	else
		Engine::get().setMasterVolume(mul);
}

void MixerStrip::refresh()
{
	Engine &e = Engine::get();
	float mul = e.masterVolume();
	if (id) {
		Input *in = e.input(id);
		if (!in)
			return;
		mul = in->volume;
		title->setText(QString("%1\n%2").arg(e.indexOf(id) + 1).arg(in->name));
		title->setToolTip(in->name);
		const QString bus = id == e.programId() ? "program" : id == e.previewId() ? "preview" : "";
		if (title->property("bus").toString() != bus) {
			title->setProperty("bus", bus);
			title->style()->unpolish(title);
			title->style()->polish(title);
		}
		onBtn->setChecked(in->audioOn);
		autoBtn->setChecked(in->audioAuto);
		phonesBtn->setChecked(in->monitor);
	} else {
		title->setText("MASTER");
	}
	const float dB = mul <= 0.0f ? -INFINITY : obs_mul_to_db(mul);
	obs_fader_set_db(conv, std::isfinite(dB) ? dB : -100.0f);
	updating = true;
	fader->setValue(mul <= 0.0f ? 0 : int(std::lround(obs_fader_get_deflection(conv) * 1000.0f)));
	updating = false;
	db->setText(std::isfinite(dB) ? QString("%1 dB").arg(dB, 0, 'f', 1) : QString("-∞"));
}

/* ---------------- mixer ---------------- */

AudioMixer::AudioMixer(QWidget *parent) : QWidget(parent)
{
	auto *h = new QHBoxLayout(this);
	h->setContentsMargins(6, 6, 6, 6);
	h->setSpacing(6);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *host = new QWidget;
	stripsLayout = new QHBoxLayout(host);
	stripsLayout->setContentsMargins(0, 0, 0, 0);
	stripsLayout->setSpacing(4);
	scroll->setWidget(host);
	h->addWidget(scroll, 1);
	master = new MixerStrip(0);
	master->setObjectName("masterStrip");
	h->addWidget(master);

	timer = new QTimer(this);
	timer->setInterval(33);
	connect(timer, &QTimer::timeout, this, [this] {
		if (!isVisible())
			return; /* aba escondida: nada a desenhar */
		for (MixerStrip *s : strips)
			s->meter()->tick();
		master->meter()->tick();
	});
	timer->start();

	Engine &e = Engine::get();
	connect(&e, &Engine::inputsChanged, this, &AudioMixer::rebuild);
	connect(&e, &Engine::inputChanged, this, [this](int id) {
		for (MixerStrip *s : strips)
			if (s->inputId() == id)
				s->refresh();
	});
	connect(&e, &Engine::busChanged, this, [this] {
		for (MixerStrip *s : strips)
			s->refresh();
	});
	connect(&e, &Engine::audioChanged, master, &MixerStrip::refresh);
	connected = audio_output_connect(obs_get_audio(), 0, nullptr, masterAudio, this);
	rebuild();
}

AudioMixer::~AudioMixer()
{
	if (connected)
		audio_output_disconnect(obs_get_audio(), 0, masterAudio, this);
}

/* mistura final (o que vai para a gravação e a transmissão): pico por canal */
void AudioMixer::masterAudio(void *param, size_t, struct audio_data *data)
{
	auto *self = static_cast<AudioMixer *>(param);
	float peak[2] = {0.0f, 0.0f};
	for (int c = 0; c < 2; c++) {
		const float *s = reinterpret_cast<const float *>(data->data[c]);
		if (!s)
			continue;
		for (uint32_t i = 0; i < data->frames; i++)
			peak[c] = std::max(peak[c], std::fabs(s[i]));
	}
	self->master->meter()->setLevels(obs_mul_to_db(peak[0]), obs_mul_to_db(peak[1]));
}

void AudioMixer::rebuild()
{
	while (QLayoutItem *it = stripsLayout->takeAt(0)) {
		delete it->widget(); /* faixas e o aviso */
		delete it;
	}
	strips.clear();
	for (Input *in : Engine::get().inputs()) {
		if (!Engine::hasAudio(in))
			continue;
		auto *s = new MixerStrip(in->id);
		strips << s;
		stripsLayout->addWidget(s);
	}
	if (strips.isEmpty()) {
		auto *hint = new QLabel("Nenhuma entrada com som.\nAdicione um microfone, o áudio do computador\nou uma câmera com áudio.");
		hint->setObjectName("muted");
		hint->setAlignment(Qt::AlignCenter);
		stripsLayout->addWidget(hint, 1);
	} else {
		stripsLayout->addStretch(1);
	}
}
