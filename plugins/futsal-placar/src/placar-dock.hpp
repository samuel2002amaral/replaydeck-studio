#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QTimer;

class PlacarDock : public QWidget {
	Q_OBJECT

public:
	explicit PlacarDock(QWidget *parent = nullptr);

private slots:
	void refresh();

private:
	QWidget *buildTeam(int t);
	void pickColor(int t, bool background);
	void applyNames();

	QLineEdit *name[2] = {};
	QLineEdit *sigla[2] = {};
	QPushButton *bgBtn[2] = {};
	QPushButton *fgBtn[2] = {};
	QLabel *score[2] = {};
	QLabel *fouls[2] = {};
	QLabel *timeouts[2] = {};
	QLabel *penalties[2] = {};
	QLabel *clock = nullptr;
	QLabel *status = nullptr;
	QPushButton *startStop = nullptr;
	QComboBox *period = nullptr;
	QLineEdit *setTime = nullptr;
	QSpinBox *periodMin = nullptr;
	QSpinBox *extraMin = nullptr;
	QCheckBox *countDown = nullptr;
	QLineEdit *competition = nullptr;
	QTimer *timer = nullptr;
	unsigned long long lastVersion = 0;
};
