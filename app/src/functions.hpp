/* Funções do programa, com os mesmos nomes do vMix (Cut, Fade, OverlayInput1In, ReplayMarkIn...).
 * Atalhos de teclado, scripts e (depois) a API do celular chamam tudo por aqui. */
#pragma once

#include <QList>
#include <QString>

class QWidget;

struct FunctionDef {
	const char *name;     /* nome do vMix */
	const char *category; /* grupo na lista */
	const char *label;    /* o que faz, em português */
	bool input;           /* usa Input= (número ou nome da entrada) */
	bool value;           /* usa Value= */
	bool hold = false;    /* de segurar: age enquanto a tecla está apertada e para ao soltar */
};

const QList<FunctionDef> &functionList();
const FunctionDef *findFunction(const QString &name);

/* Executa na thread da interface. input: número (1, 2...) ou nome da entrada; vazio = Prévia.
 * Devolve false e explica em error se não deu. */
bool runFunction(const QString &name, const QString &input, const QString &value, QString *error = nullptr,
		 QWidget *parent = nullptr);

/* função de segurar: pressed = apertou (começa) / soltou (para) */
bool runHoldFunction(const QString &name, const QString &value, bool pressed);

/* "Função Input=3 Value=50" (forma das linhas de script e da API) */
bool runFunctionLine(const QString &line, QString *error = nullptr, QWidget *parent = nullptr);
