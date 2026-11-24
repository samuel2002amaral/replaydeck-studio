/* Scripts no jeito do vMix: uma lista de funções com esperas e repetições.
 *
 *   ' comentário
 *   API.Function("OverlayInput1In", Input:="Placar")      (forma do vMix)
 *   Fade Input=2 Value=800                                (forma curta)
 *   Sleep(3000)            ou   Esperar 3000
 *   Do ... Loop            ou   Repetir ... Fim           (até mandar parar)
 *   For i = 1 To 5 ... Next ou  Repetir 5 ... Fim
 *
 * Rodam sem travar a interface (as esperas não bloqueiam nada). */
#pragma once

#include <QDialog>
#include <QMap>
#include <QObject>
#include <QStringList>

class QTimer;
class QListWidget;
class QPlainTextEdit;
class QLabel;

struct ScriptOp {
	enum Kind { Call, Sleep, LoopStart, LoopEnd } kind = Call;
	QString text;    /* Call: a linha da função */
	int ms = 0;      /* Sleep */
	int count = -1;  /* LoopStart: vezes (-1 = até parar) */
	int jump = -1;   /* LoopEnd: índice do LoopStart; LoopStart: índice depois do LoopEnd */
	int line = 0;    /* linha no texto (para os erros) */
};

/* transforma o texto em passos; erro = "linha N: ..." */
QList<ScriptOp> compileScript(const QString &code, QString *error);

class ScriptRun;

class Scripts : public QObject {
	Q_OBJECT

public:
	static Scripts &get();
	void load();
	void save();
	QStringList names() const { return order; }
	QString code(const QString &name) const { return codes.value(name); }
	void setCode(const QString &name, const QString &code);
	bool add(const QString &name);
	void remove(const QString &name);
	bool rename(const QString &from, const QString &to);

	QString start(const QString &name); /* erro ou vazio */
	void stop(const QString &name);
	void stopAll();
	bool running(const QString &name) const { return runs.contains(name); }
	QString lastError(const QString &name) const { return errors.value(name); }

signals:
	void changed(); /* lista ou estado (rodando/parado/erro) */

private:
	friend class ScriptRun;
	void finished(const QString &name, const QString &error);
	QStringList order;
	QMap<QString, QString> codes;
	QMap<QString, ScriptRun *> runs;
	QMap<QString, QString> errors;
};

class ScriptsDialog : public QDialog {
	Q_OBJECT

public:
	explicit ScriptsDialog(QWidget *parent = nullptr);

private:
	void refreshList();
	void select(const QString &name);
	void updateState();
	QListWidget *list = nullptr;
	QPlainTextEdit *editor = nullptr;
	QLabel *state = nullptr;
	QString current;
	bool loading = false;
};
