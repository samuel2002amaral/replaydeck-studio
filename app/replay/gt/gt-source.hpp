#pragma once
#include <obs.h>
#include <QString>
#include <QStringList>
#include <QMap>
#include <memory>
#include <vector>

class GtTitle;
class QWidget;

#define GT_SOURCE_ID "instant_replay_gt"

void registerGtSource();

/* Controle de uma fonte GT (qualquer thread) */
bool gtIsSource(obs_source_t *src);
void gtPlayIn(obs_source_t *src);
void gtPlayOut(obs_source_t *src);
void gtToggle(obs_source_t *src);
void gtPlayPage(obs_source_t *src, const QString &page);
/* 0 fora do ar, 1 entrando, 2 no ar, 3 saindo */
int gtState(obs_source_t *src);
/* duração da animação de saída, em ms */
int gtOutLengthMs(obs_source_t *src);
int gtInLengthMs(obs_source_t *src);
QString gtTitleName(obs_source_t *src);
QString gtFile(obs_source_t *src);
QStringList gtPages(obs_source_t *src);
QMap<QString, QString> gtValues(obs_source_t *src);
void gtSetValues(obs_source_t *src, const QMap<QString, QString> &values);
std::shared_ptr<GtTitle> gtTitleOf(obs_source_t *src);     /* com os acréscimos do editor */
std::shared_ptr<GtTitle> gtBaseTitleOf(obs_source_t *src); /* como veio do arquivo */
/* Biblioteca de títulos (como no GT OBS): fontes internas, sem cena.
 * Ficam salvas junto com a coleção de cenas. */
void gtLibraryInit();
void gtLibraryShutdown();
obs_source_t *gtLibraryFind(const char *name); /* ref, ou nullptr */
obs_source_t *gtLibraryAdd(const QString &file);  /* ref */
/* título novo, em branco (do tamanho do vídeo), para montar no editor */
obs_source_t *gtLibraryNewBlank(const QString &name); /* ref */
void gtLibraryRemove(obs_source_t *src);
/* mesmo .gtzip com outros dados (ex.: escalação casa e fora): cópia na biblioteca */
obs_source_t *gtLibraryDuplicate(obs_source_t *src, const QString &name); /* ref */
/* troca o nome (único); devolve false se o nome já existe */
bool gtRename(obs_source_t *src, const QString &name);
std::vector<obs_source_t *> gtLibraryList();   /* refs: libere com obs_source_release */
/* nome mostrado na lista (no Studio: o nome da entrada) */
QString gtDisplayName(obs_source_t *src);
/* abre o editor de título (thread da interface) */
void gtOpenEditor(obs_source_t *src, QWidget *parent);
