/* Mixer de áudio (aba do canto inferior direito), no estilo do vMix:
 * uma faixa por entrada com som (medidor, volume, On, Auto, fone) e a faixa MASTER. */
#pragma once

#include <obs.h>

#include <QWidget>

#include <atomic>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

/* barras de nível (dB), alimentadas por outra thread */
class LevelMeter : public QWidget {
	Q_OBJECT
public:
	explicit LevelMeter(QWidget *parent = nullptr);
	void setLevels(float leftDb, float rightDb); /* qualquer thread */
	void tick();                                 /* thread da interface: decai e redesenha */

protected:
	void paintEvent(QPaintEvent *) override;

private:
	std::atomic<float> in[2];
	float shown[2] = {-60.0f, -60.0f};
	float hold[2] = {-60.0f, -60.0f};
	int holdTicks[2] = {0, 0};
};

class MixerStrip : public QWidget {
	Q_OBJECT
public:
	MixerStrip(int inputId, QWidget *parent = nullptr); /* inputId 0 = MASTER */
	~MixerStrip() override;
	int inputId() const { return id; }
	void refresh();
	LevelMeter *meter() const { return levels; }

private:
	void setFromSlider(int pos);
	int id;
	obs_volmeter_t *volmeter = nullptr;
	obs_fader_t *conv = nullptr; /* só para converter posição <-> dB (escala do OBS) */
	QLabel *title = nullptr;
	QLabel *db = nullptr;
	QSlider *fader = nullptr;
	LevelMeter *levels = nullptr;
	QPushButton *onBtn = nullptr;
	QPushButton *autoBtn = nullptr;
	QPushButton *phonesBtn = nullptr;
	bool updating = false;
};

class AudioMixer : public QWidget {
	Q_OBJECT
public:
	explicit AudioMixer(QWidget *parent = nullptr);
	~AudioMixer() override;

private:
	void rebuild();
	static void masterAudio(void *param, size_t mix, struct audio_data *data);

	QHBoxLayout *stripsLayout = nullptr;
	QList<MixerStrip *> strips;
	MixerStrip *master = nullptr;
	QTimer *timer = nullptr;
	bool connected = false;
};
