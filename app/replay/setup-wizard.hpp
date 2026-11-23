#pragma once
#include <QString>

class QWidget;

namespace setup {

/* Garante que exista uma cena com o replay (canal A) pronta para ir ao ar.
 * Se já existe alguma cena com a saída A, não mexe em nada e devolve vazio.
 * Se não existe, cria a cena "Replay" com a fonte "Replay A" ocupando a tela
 * toda e devolve o nome da cena criada. */
QString ensureReplayScene();

/* true: ainda não foi configurado (nenhuma câmera e o assistente nunca foi concluído/dispensado) */
bool needsSetup();

enum Result { Cancelled = 0, Done = 1, DoneStartRecording = 2 };

/* Cria uma câmera nova: fonte (captura de vídeo, NDI, DeckLink, câmera IP...) + uma cena
 * com ela ocupando a tela, e abre as propriedades do OBS para escolher o dispositivo.
 * Devolve o nome da fonte criada (vazio se cancelou). */
QString addCameraDialog(QWidget *parent);

/* Põe a fonte na primeira vaga livre de câmera do replay (false se não há vaga ou está gravando). */
bool assignToFreeSlot(const QString &source);

/* Configuração rápida: câmeras, pasta, qualidade e cena Replay numa tela só. */
Result showWizard(QWidget *parent);

} // namespace setup
