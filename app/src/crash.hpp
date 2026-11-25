/* Quando o programa fecha sozinho (Mac/Linux): grava onde caiu num arquivo e mostra na
 * próxima abertura, para mandar ao suporte. */
#pragma once

class QWidget;
class QString;

namespace crash {
void install(const QString &dir); /* cedo, logo depois de saber a pasta de configuração */
void showPrevious(QWidget *parent);
} // namespace crash
