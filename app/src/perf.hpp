/* Ajustes do processo para rodar liso (Windows): sem "economia de energia" do Windows 11
 * nas threads de vídeo/áudio, prioridade acima do normal, relógio de 1 ms e sem suspender
 * a tela/PC enquanto o programa está aberto. Também o salvamento automático do projeto. */
#pragma once

class QObject;

namespace perf {
void tuneProcess(); /* antes de iniciar o motor */
void logState();    /* depois: escreve no log o que ficou valendo */
void restore();     /* ao sair */
void startAutosave(QObject *owner); /* salva o projeto 1x por minuto se algo mudou */
/* vigia: se a interface ficar parada mais de 2 s, escreve no log (para achar a causa de um travamento) */
void startHangWatch();
void stopHangWatch();
} // namespace perf
