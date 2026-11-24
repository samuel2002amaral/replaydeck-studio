/* Configurações do programa: vídeo, gravação, transmissão e fone. */
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QTabWidget;

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(QWidget *parent = nullptr, int startTab = 0);

	enum Tab { Video = 0, Recording = 1, Streaming = 2, Remote = 3, Audio = 4, Shortcuts = 5 };

private:
	bool apply();

	QTabWidget *tabs = nullptr;
	QComboBox *resolution = nullptr;
	QComboBox *fps = nullptr;
	QComboBox *adapter = nullptr;
	class QCheckBox *apiOn = nullptr;
	QSpinBox *apiHttp = nullptr, *apiTcp = nullptr;
	QLineEdit *recFolder = nullptr;
	QComboBox *recFormat = nullptr;
	QComboBox *recEncoder = nullptr;
	QSpinBox *recKbps = nullptr;
	class QTableWidget *dests = nullptr;
	class QCheckBox *globalKeys = nullptr;
	QComboBox *streamEncoder = nullptr;
	QSpinBox *streamKbps = nullptr;
	QSpinBox *verticalKbps = nullptr;
	QComboBox *verticalSize = nullptr;
	QComboBox *verticalFit = nullptr;
	class QSlider *verticalOffset = nullptr;
	void addDestinationRow(const struct StreamDestination &d);
	QSpinBox *audioKbps = nullptr;
	QComboBox *monitor = nullptr;
	class QTableWidget *keys = nullptr;
	void addShortcutRow(const struct Shortcut &s);
};
