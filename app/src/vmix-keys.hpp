/* Chaves do vMix nas entradas: um app feito para o vMix (APPVMIX, Companion, Stream Deck)
 * guarda as entradas pela chave (GUID). Importando as chaves do projeto do vMix (.vmix), do
 * estado da API (api.xml) ou da configuração do app (config.json), o app funciona aqui sem
 * remapear nada: cada entrada do Studio com o mesmo nome recebe a chave da entrada do vMix. */
#pragma once

class QWidget;

#include <QStringList>
void importVmixKeys(QWidget *parent);
bool applyVmixKeysFromFile(const QString &file, QStringList *done, QStringList *notFound, QString *err);
void editVmixKey(QWidget *parent, int inputId);
