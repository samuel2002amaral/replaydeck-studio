/* Janelas que abrem a partir da tela principal. */
#pragma once

#include <obs.hpp>

#include <QDialog>

class QListWidget;
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
	int id;
	QLineEdit *nameEdit = nullptr;
};

/* Adicionar entrada: escolhe o tipo, preenche as opções e vê a imagem antes de criar. */
class AddInputDialog : public QDialog {
	Q_OBJECT

public:
	explicit AddInputDialog(QWidget *parent = nullptr);
	~AddInputDialog() override;

private:
	void selectType(int row);
	void accept() override;
	void reject() override;
	void dropTemp();

	QListWidget *types = nullptr;
	QLineEdit *nameEdit = nullptr;
	QWidget *propsHolder = nullptr;
	PropertiesView *props = nullptr;
	VideoDisplay *video = nullptr;
	OBSSourceAutoRelease temp; /* fonte de teste enquanto a janela está aberta */
	QStringList typeIds;
};
