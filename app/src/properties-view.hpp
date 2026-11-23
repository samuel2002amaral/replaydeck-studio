/* Formulário automático com as opções de uma fonte (dispositivo, arquivo, resolução...).
 * Lê as propriedades que o próprio motor descreve e monta os campos. */
#pragma once

#include <obs.hpp>

#include <QWidget>

class QFormLayout;

class PropertiesView : public QWidget {
	Q_OBJECT

public:
	/* source: altera a fonte ao vivo. Sem source (nova entrada): só preenche settings. */
	PropertiesView(obs_source_t *source, obs_data_t *settings, const char *sourceType, QWidget *parent = nullptr);
	~PropertiesView() override;

	obs_data_t *settings() const { return data; }

signals:
	void changed();

private:
	void rebuild();
	void addProperty(QFormLayout *form, obs_property_t *p);
	void apply(obs_property_t *p);

	OBSWeakSourceAutoRelease weak;
	obs_data_t *data = nullptr;
	QByteArray type;
	obs_properties_t *props = nullptr;
	QWidget *content = nullptr;
	bool rebuilding = false;
};
