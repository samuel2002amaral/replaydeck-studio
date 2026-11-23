/* O que o replay pergunta ao Studio (implementado em src/replay-host.cpp). */
#pragma once

#include <obs.h>
#include <string>
#include <vector>

/* <config do Studio>/replay/<arquivo> (libere com bfree) */
char *rdsReplayConfigPath(const char *file);
/* arquivo de dados do replay (idiomas...), ou nullptr se não existe (libere com bfree) */
char *rdsReplayDataFile(const char *file);
/* fonte da entrada com esse nome (a câmera); referência, ou nullptr */
obs_source_t *rdsFindSource(const char *name);
/* cena da entrada com esse nome; referência, ou nullptr */
obs_source_t *rdsFindScene(const char *name);
/* nomes das entradas com imagem que podem ser câmeras do replay (sem as saídas do replay) */
std::vector<std::string> rdsCameraNames();

class QWidget;
#include <QString>
/* garante as entradas Replay A e Replay B; devolve o nome da que criou (vazio se já existiam) */
QString rdsEnsureReplayInputs();
/* abre "nova entrada" (câmera) e devolve o nome da entrada criada (vazio se cancelou) */
QString rdsAddCameraDialog(QWidget *parent);
/* já existem as entradas Replay A e B? */
bool rdsHasReplayInputs();
