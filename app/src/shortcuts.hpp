/* Atalhos de teclado (como no vMix): TECLA FÍSICA -> função (com entrada e valor).
 *
 * A tecla é guardada pelo código físico (scan code), não pelo caractere: o 7 de cima e o 7 do
 * teclado numérico são teclas diferentes, e o teclado numérico funciona com o Num Lock ligado ou
 * desligado e em qualquer layout (ABNT, americano...). Ctrl/Shift/Alt fazem parte do atalho.
 *
 * Funcionam em qualquer janela do programa (menos enquanto se digita num campo de texto) e, com
 * a opção "global" (Windows), também com outro programa na frente. Salvos em atalhos.json. */
#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPushButton>
#include <QString>

struct Shortcut {
	quint32 scan = 0;   /* código físico da tecla (0 = atalho antigo, só pelo caractere) */
	int mods = 0;       /* Qt::ControlModifier | ShiftModifier | AltModifier | MetaModifier */
	QString label;      /* "Num 7", "Ctrl+F1"... */
	QKeySequence key;   /* atalhos antigos/sugeridos (sem código físico) */
	QString function;
	QString input; /* número ou nome da entrada; vazio = Prévia */
	QString value;
	QString display() const;
};

class Shortcuts : public QObject {
	Q_OBJECT

public:
	static Shortcuts &get();
	void load();
	void save();
	QList<Shortcut> list() const { return items; }
	void setList(const QList<Shortcut> &l);
	bool global() const { return globalOn; }
	void setGlobal(bool on); /* Windows: funcionam com outro programa na frente */
	static QList<Shortcut> suggested();
	void install(); /* passa a ouvir o teclado do programa todo */

	/* uma tecla apertada (programa ou gancho global): roda os atalhos dela */
	bool trigger(quint32 scan, int mods, int qtKey, bool keypad);
	/* nome legível de uma tecla física ("Num 7", "F1", "Ctrl+Espaço") */
	static QString keyName(quint32 scan, int mods, int qtKey, bool keypad);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	Shortcuts() = default;
	void applyGlobalHook();
	QList<Shortcut> items;
	bool globalOn = false;
};

/* botão que grava a tecla física: clique e aperte a tecla (com ou sem Ctrl/Shift/Alt) */
class KeyCapture : public QPushButton {
	Q_OBJECT
public:
	explicit KeyCapture(const Shortcut &s, QWidget *parent = nullptr);
	bool isEmpty() const { return sc.scan == 0 && sc.key.isEmpty(); }
	Shortcut shortcut() const { return sc; }
	bool sameKey(const Shortcut &o) const;
	void startCapture();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;

private:
	void paint();
	Shortcut sc;
	bool capturing = false;
};
