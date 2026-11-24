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

	enum Tab { Video = 0, Recording = 1, Streaming = 2, Audio = 3 };

private:
	bool apply();

	QTabWidget *tabs = nullptr;
	QComboBox *resolution = nullptr;
	QComboBox *fps = nullptr;
	QLineEdit *recFolder = nullptr;
	QComboBox *recFormat = nullptr;
	QComboBox *recEncoder = nullptr;
	QSpinBox *recKbps = nullptr;
	QComboBox *service = nullptr;
	QLineEdit *server = nullptr;
	QLineEdit *key = nullptr;
	QComboBox *streamEncoder = nullptr;
	QSpinBox *streamKbps = nullptr;
	QSpinBox *audioKbps = nullptr;
	QComboBox *monitor = nullptr;
};
