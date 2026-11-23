/* A única tela do programa. Tudo o mais abre a partir dela. */
#pragma once

#include <QMainWindow>

#include <util/platform.h>

class QGridLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;
class QWidget;
class InputTile;
class VideoDisplay;

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	MainWindow();
	~MainWindow() override;

protected:
	void closeEvent(QCloseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	QWidget *buildVideoArea();
	QWidget *buildTransitions();
	QWidget *buildInputsArea();
	QWidget *buildRightPanel();
	QWidget *buildBottomBar();
	void buildMenus();
	void rebuildTiles();
	void layoutTiles();
	void updateBus();
	void updateStatus();
	void openInput(int id);
	void addInput();
	void notYet(const QString &what, const QString &phase);

	VideoDisplay *preview = nullptr;
	VideoDisplay *program = nullptr;
	QLabel *pvwTitle = nullptr;
	QLabel *pgmTitle = nullptr;
	QLabel *pgmFooter = nullptr;
	QPushButton *ftbBtn = nullptr;
	QPushButton *ovBtn[5] = {};
	QSlider *tbar = nullptr;
	QSpinBox *duration = nullptr;
	QWidget *tilesHost = nullptr;
	QGridLayout *tilesGrid = nullptr;
	QLabel *emptyHint = nullptr;
	QList<InputTile *> tiles;
	QTabWidget *rightTabs = nullptr;
	QLabel *status[5] = {};
	QTimer *statusTimer = nullptr;
	os_cpu_usage_info_t *cpu = nullptr;
	int lastCols = 0;
};
