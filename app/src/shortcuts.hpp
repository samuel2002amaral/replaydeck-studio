/* Atalhos de teclado (como no vMix): tecla -> função (com entrada e valor).
 * Funcionam em qualquer janela do programa, menos enquanto se digita num campo de texto.
 * Salvos em atalhos.json. */
#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

struct Shortcut {
	QKeySequence key;
	QString function;
	QString input; /* número ou nome da entrada; vazio = Prévia */
	QString value;
};

class Shortcuts : public QObject {
	Q_OBJECT

public:
	static Shortcuts &get();
	void load();
	void save();
	QList<Shortcut> list() const { return items; }
	void setList(const QList<Shortcut> &l);
	static QList<Shortcut> suggested();
	void install(); /* passa a ouvir o teclado do programa todo */

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	Shortcuts() = default;
	QList<Shortcut> items;
};
