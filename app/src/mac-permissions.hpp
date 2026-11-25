/* Mac: janela que pede as permissões do sistema (câmera, microfone, gravação de tela)
 * de uma vez, na primeira vez que o programa abre. */
#pragma once

class QWidget;

namespace macperm {
/* mostra na abertura se falta alguma (e a pessoa não marcou "não mostrar de novo") */
void showAtStartup(QWidget *parent);
/* abre a janela (menu Ajuda) */
void showDialog(QWidget *parent);
/* antes de adicionar captura de tela/janela/áudio do sistema: true = pode seguir */
bool ensureScreen(QWidget *parent);
} // namespace macperm
