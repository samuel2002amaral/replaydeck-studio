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

/* nome que a chave tinha no vMix (das importações), ou vazio */
QString vmixTitleOfKey(const QString &key);
/* o app pediu algo que não existe aqui: guarda para a janela "O que o app pediu e não achou" */
void noteMissing(const QString &key, const QString &title, const QString &function, const QString &field,
		 const QString &reason);
void showMissing(QWidget *parent);
