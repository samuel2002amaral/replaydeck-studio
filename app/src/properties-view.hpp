/* Formulário automático com as opções de uma fonte (dispositivo, arquivo, resolução...).
 * Lê as propriedades que o próprio motor descreve e monta os campos. */
#pragma once

#include <obs.hpp>

#include <QWidget>

class QFormLayout;

class PropertiesView : public QWidget {
	Q_OBJECT

public:
	/* As mudanças ficam guardadas aqui e só vão para a fonte em applyToSource()
	 * (quando o usuário confirma) — assim um clique não trava o programa reabrindo câmera.
	 * source: entrada existente (começa com as opções dela). Sem source: tipo novo, com os padrões. */
	PropertiesView(obs_source_t *source, const char *sourceType, QWidget *parent = nullptr);
	void applyToSource(obs_source_t *source);
	bool hasChanges() const { return dirty; }
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
	bool dirty = false;
	obs_property_t *pendingApply = nullptr;
	int autoPicks = 0;
};
