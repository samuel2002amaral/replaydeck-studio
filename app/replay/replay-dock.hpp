#pragma once
#include <QWidget>
#include <QMap>
#include <QList>
#include <cstdint>
#include <vector>

class QPushButton;
class QToolButton;
class QLabel;
class QSlider;
class QTableWidget;
class QTableWidgetItem;
class QButtonGroup;

/* seleção de eventos do dock (usado pelo app/API); thread da interface */
int64_t replayDockSelectedEvent();
void replayDockSelect(int how); /* -1 anterior, +1 próximo, -2 primeiro, +2 último */

class ReplayDock : public QWidget {
	Q_OBJECT
public:
	explicit ReplayDock(QWidget *parent = nullptr);
	~ReplayDock() override;
	int64_t selectedEventId() const;
	void selectRelative(int delta);
	void runWizard();
	void maybeFirstRun();
	void setArranging(bool on);
	int idealWidth();
	void fitFloating(class QDockWidget *dw, bool center);
	void retranslated();

protected:
	void showEvent(QShowEvent *e) override;

public slots:
	/* Studio: também pelo menu Replay do programa */
	void toggleRecording();
	void openConfig();

private slots:
	void refresh();
	void eventItemChanged(QTableWidgetItem *item);

private:
	void rebuildEvents();
	void applyCameraLayout();
	void applyParts();
	void applyBlockOrder();
	void applyDockLayout();
	void buildZoom();
	QWidget *zoomBox = nullptr;
	QWidget *zoomBlk = nullptr; /* zoomBox ou a caixinha com nome em volta dele */
	QWidget *body = nullptr;
	QWidget *ctrlW = nullptr;
	QLayout *flowL = nullptr;
	QMap<QString, QWidget *> blocks;
	QMap<QString, QWidget *> blockInner;
	QMap<QString, QString> blockTitle;
	QWidget *wrapBlock(const char *id, QWidget *inner, const char *title);
	void rewrapBlocks();
	void updateCtrlMin();
	bool eventFilter(QObject *o, QEvent *e) override;
	bool arranging = false;
	bool zoomSide = false;
	class QFrame *arrangeBar = nullptr;
	class QFrame *dropMark = nullptr;
	QMap<QString, class BlockHandle *> handles;
	void syncHandles();
	bool freeLayoutOn() const;
	void setFreeLayout(bool on);
	void dropFree(class BlockHandle *hd, bool drop);
	QPushButton *freeBtn = nullptr;
	QPushButton *brBtn = nullptr;
	void dropAt(class BlockHandle *hd, QPoint p, bool drop);
	QMap<QString, QList<QWidget *>> parts;
	void previewChannel(int ch);

	QTableWidget *table;
	QPushButton *recBtn;
	QPushButton *playBtn;
	QPushButton *loopBtn;
	QPushButton *reverseBtn;
	QPushButton *lastBtn;
	QPushButton *eventsBtn;
	QPushButton *audioBtn;
	QPushButton *camBtns[2][8];
	QLabel *statusLbl;
	QLabel *tcLbl;
	QLabel *remainLbl;
	QLabel *modeChip;
	QLabel *zoomChip;
	class ZoomMap *zoomMap = nullptr;
	QSlider *zoomSlider = nullptr;
	bool zoomSliderSync = false;
	bool firstRunAsked = false;
	QPushButton *liveBtn;
	QPushButton *inBtn;
	int remainTick = 0;
	int diskTick = 0;
	void showMessage(const QString &m)
	{
		tempMsg = m;
		tempMsgTicks = 100;
	}
	QButtonGroup *selGroup;
	QButtonGroup *camGroup[2];
	std::vector<std::pair<int, QPushButton *>> speedBtns;
	QSlider *scrub;
	uint64_t shownEventsVersion = UINT64_MAX;
	bool rebuilding = false;
	QString tempMsg;
	int tempMsgTicks = 0;
};
