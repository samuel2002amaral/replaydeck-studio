/* Janelas que abrem a partir da tela principal. */
#pragma once

#include <obs.hpp>

#include <QDialog>
#include <QIcon>

class QListWidget;
class QLabel;
class QPushButton;
class QLineEdit;
class QStackedWidget;
class PropertiesView;
class VideoDisplay;

/* Engrenagem da entrada: ela ao vivo, sem mexer na prévia nem no programa, com todas as opções. */
class InputWindow : public QDialog {
	Q_OBJECT

public:
	InputWindow(int inputId, QWidget *parent = nullptr);

private:
	void applyChanges();
	int id;
	QLineEdit *nameEdit = nullptr;
	PropertiesView *props = nullptr;
};

/* Passo 1 — Adicionar entrada: escolhe o tipo; mostra as entradas desse tipo que já existem. */
class AddInputDialog : public QDialog {
	Q_OBJECT

public:
	explicit AddInputDialog(QWidget *parent = nullptr);

private:
	void selectType(int row);
	void addNew();

	QListWidget *types = nullptr;
	QLabel *title = nullptr;
	QPushButton *newBtn = nullptr;
	QWidget *grid = nullptr;
	QPushButton *openBtn = nullptr;
	int selectedExisting = 0;
	QStringList typeIds;
	QStringList typeLabels;
};

/* Passo 2 — Nova entrada: a imagem aparece sozinha; as opções valem ao clicar em OK. */
class NewInputDialog : public QDialog {
	Q_OBJECT

public:
	NewInputDialog(const QString &typeId, const QString &label, QWidget *parent = nullptr);
	~NewInputDialog() override;

private:
	void accept() override;
	void reject() override;
	void drop();

	QString type;
	QLineEdit *nameEdit = nullptr;
	PropertiesView *props = nullptr;
	VideoDisplay *video = nullptr;
	QLabel *status = nullptr;
	class QTimer *waitTimer = nullptr;
	OBSSourceAutoRelease source;
};

QIcon inputTypeIcon(const QString &typeId);
