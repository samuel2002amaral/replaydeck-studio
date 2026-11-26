/* O replay dentro do Studio: liga o código do replay (pasta replay/) ao programa.
 *
 * Ordem:
 *   registerTypes()  - antes de abrir o projeto (as entradas Replay A/B precisam do tipo)
 *   start(janela)    - depois da janela: "no ar", avisos e o painel do replay
 *   shutdown()       - ao fechar, antes de destruir a janela (para a gravação)
 *   finish()         - depois da janela, antes de desligar o motor */
#pragma once

class QMainWindow;
class QWidget;
class QString;

namespace replayhost {

void registerTypes();
QWidget *start(QMainWindow *window); /* devolve o painel do replay (para a aba do canto inferior direito) */
void shutdown();
void finish();
void selfTest(QMainWindow *window); /* RDS_SELFTEST_REPLAY */
/* menu Replay */
void openSettings();
void runWizard();
void toggleRecording();
/* rodapé: "Replay gravando · 0 perdidos" (alarm = perdeu quadro ou disco atrasado) */
QString statusText(bool &alarm, bool &recording);

} // namespace replayhost
