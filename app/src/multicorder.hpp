/* MultiCorder, como no vMix: grava cada entrada escolhida num arquivo separado
 * (câmeras "limpas", sem overlays), todos ao mesmo tempo e com o áudio do Master,
 * para editar depois. */
#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSet>

class QListWidget;

class MultiCorder : public QObject {
	Q_OBJECT

public:
	static MultiCorder &get();
	bool start(const QList<int> &inputIds, QString &error);
	void stop();
	void shutdown(); /* ao sair: para e solta tudo */
	bool active() const { return !recs.isEmpty() && running; }
	int count() const { return recs.size(); }
	qint64 elapsedMs() const { return active() ? timer.elapsed() : 0; }
	QString folder() const { return dir; }
	QSet<int> lastSelection;

signals:
	void stateChanged();

private:
	struct Rec {
		int inputId = 0;
		QString file;
		obs_view_t *view = nullptr;
		video_t *video = nullptr;
		OBSEncoderAutoRelease venc;
		OBSOutputAutoRelease out;
		bool started = false;
	};
	void release(Rec *r);
	void reap(); /* solta o que já parou */
	QList<Rec *> recs;
	OBSEncoderAutoRelease audio;
	bool running = false;
	QString dir;
	QElapsedTimer timer;
};

class MultiCorderDialog : public QDialog {
	Q_OBJECT

public:
	explicit MultiCorderDialog(QWidget *parent = nullptr);
	QList<int> selected() const;

private:
	QListWidget *list = nullptr;
};
