/* Filtros de uma entrada, como no OBS: chroma key, correção de cor, LUT, recorte, nitidez...
 * e de áudio: supressão de ruído, compressor, ganho, gate. Também os filtros de plugins. */
#pragma once

#include <obs.hpp>

#include <QWidget>

class QListWidget;
class QVBoxLayout;
class PropertiesView;

class FiltersWindow : public QWidget {
	Q_OBJECT

public:
	static void open(obs_source_t *source, const QString &title, QWidget *parent);

private:
	FiltersWindow(obs_source_t *source, const QString &title, QWidget *parent);
	void refresh(obs_source_t *select = nullptr);
	void showFilter(obs_source_t *f);
	obs_source_t *current() const;
	void addMenu();
	void move(obs_order_movement m);
	OBSWeakSourceAutoRelease weak;
	QListWidget *list = nullptr;
	QVBoxLayout *propsBox = nullptr;
	PropertiesView *props = nullptr;
};
