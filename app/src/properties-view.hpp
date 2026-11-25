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
	/* entrada nova: usa as opções já preparadas (aparece na hora); sem cache, busca em segundo plano */
	static PropertiesView *create(const char *sourceType, QWidget *parent = nullptr);
	void applyToSource(obs_source_t *source);
	bool hasChanges() const { return dirty; }
	void resetToDefaults(); /* botão Redefinir */
	~PropertiesView() override;

	obs_data_t *settings() const { return data; }
	bool isLoading() const { return loading; }
	static int reloadCount(); /* quantas vezes as opções foram buscadas na fonte (teste/diagnóstico) */

signals:
	void changed();
	void loaded(); /* as opções chegaram (a busca roda em segundo plano) */

private:
	PropertiesView(const char *sourceType, obs_properties_t *ready, obs_data_t *settings, QWidget *parent);
	void setupUi();
	void reload();  /* busca as opções na fonte (lento na câmera): só ao abrir e em botões */
	void refresh(); /* só redesenha os campos com as opções que já temos */
	void addProperty(QFormLayout *form, obs_property_t *p);
	void apply(obs_property_t *p);

	OBSWeakSourceAutoRelease weak;
	OBSSourceAutoRelease tempSrc; /* Mac: as capturas só montam as opções com uma fonte de verdade */
	obs_data_t *data = nullptr;
	QByteArray type;
	obs_properties_t *props = nullptr;
	QWidget *content = nullptr;
	bool rebuilding = false;
	bool dirty = false;
	bool loading = false;
	class QLabel *loadingLabel = nullptr;
	obs_property_t *pendingApply = nullptr;
	int autoPicks = 0;
};
