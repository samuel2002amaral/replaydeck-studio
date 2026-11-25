/* Pacote de configuração (.rdspacote): leva tudo deste computador para outro — projeto
 * (entradas, títulos GT, camadas, transições), configurações (vídeo, gravação, transmissão,
 * atalhos, replay, API do vMix, scripts, PTZ, playlist) e os arquivos usados (GTs, imagens,
 * vídeos, stingers). No outro computador: Arquivo > Importar pacote; o programa reinicia já
 * configurado, com os caminhos dos arquivos ajustados. */
#pragma once

class QWidget;
class QString;

namespace pack {
void exportDialog(QWidget *parent);
void importDialog(QWidget *parent);
/* na abertura, antes de ler qualquer configuração: aplica um pacote importado */
void applyPending(const QString &cfgDir);
/* importou: reabrir o programa depois de fechar */
bool restartRequested();
/* sem janelas (testes): parent nulo = sem barra de progresso */
bool writePack(const QString &out, bool withMedia, QWidget *parent, QString *err);
bool readPack(const QString &in, QWidget *parent, QString *err); /* deixa pronto para a próxima abertura */
} // namespace pack
