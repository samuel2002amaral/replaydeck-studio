#pragma once
#include <QWidget>
#include <obs.h>
#include <vector>

class QResizeEvent;

class QVBoxLayout;
class QGridLayout;
class QPushButton;
class QComboBox;
class QLabel;

/* Painel "ReplayDeck GT": lista os títulos com ENTRADA/SAÍDA. */
class GtDock : public QWidget {
	Q_OBJECT
public:
	explicit GtDock(QWidget *parent = nullptr);
	~GtDock() override;

public slots:
	void newTitle(); /* título em branco, montado no editor */

private slots:
	void refresh();
	void addTitle();
	void convertTitle();

private:
	struct Row {
		obs_weak_source_t *src;
		QWidget *w;
		QLabel *name;
		QPushButton *go;
		QComboBox *ov;
		QLabel *thumb;
		QString thumbKey;
	};
	void rebuild(const std::vector<obs_source_t *> &sources);
	void clearRows();
	void setMode(bool grid);
	int columns() const;
	void resizeEvent(QResizeEvent *e) override;
	void removeTitle(obs_weak_source_t *weak);
	void renameTitle(obs_weak_source_t *weak);
	void duplicateTitle(obs_weak_source_t *weak);
	QGridLayout *listLayout;
	QPushButton *listBtn;
	QPushButton *gridBtn;
	bool gridMode = true;
	int shownColumns = 0;
	std::vector<Row> rows;
	QStringList shownNames;
	class QLineEdit *search = nullptr;
};
