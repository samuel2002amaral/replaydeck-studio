/* Plugins do OBS: os mesmos arquivos (.dll) feitos para o OBS 30+ funcionam aqui —
 * fontes (entradas novas), filtros, transições, codificadores. O que depende da interface
 * do OBS (painéis, menus) não aparece.
 *
 * Onde procura: a pasta de plugins do OBS (C:\ProgramData\obs-studio\plugins), a pasta
 * antiga dentro do OBS instalado e a pasta própria (<configurações>\plugins).
 * Só carrega os que você ligou (Configurações → Plugins). Se um plugin derrubar o programa
 * ao abrir, ele é desligado sozinho na vez seguinte. */
#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QTreeWidget;

namespace plugins {

struct Plugin {
	QString name; /* nome do módulo (arquivo sem .dll) */
	QString bin;  /* caminho do .dll / .so */
	QString data; /* pasta de dados */
	QString where;
};

QList<Plugin> discover();
QStringList enabled();
void setEnabled(const QStringList &names);
QString ownFolder();
void loadEnabled();      /* no início do motor (antes do obs_post_load_modules) */
void startupFinished();  /* janela aberta: o carregamento deu certo */
QString crashedLastTime(); /* plugin que derrubou o programa na última abertura (já desligado) */

} // namespace plugins

class PluginsDialog : public QDialog {
	Q_OBJECT

public:
	explicit PluginsDialog(QWidget *parent = nullptr);

private:
	QTreeWidget *tree = nullptr;
};
