#pragma once
/* Visual único dos painéis do plugin (replay, overlays, títulos GT e janelas).
 * Escuro, compacto, com cores de estado de mesa de corte:
 *  azul = selecionado, verde = no ar / rodando, vermelho = gravando / sair, âmbar = atenção. */
#include <QString>
#include <QVariant>
#include <initializer_list>
#include <functional>

class QWidget;
class QPushButton;
class QLabel;
class QFrame;
class QBoxLayout;

#include <QLayout>
#include <QHash>
#include <QIcon>
#include <QList>

namespace ui {

/* arquivo das preferências do painel, dentro da pasta de configuração do próprio OBS
 * (cada OBS, instalado ou portátil, tem a sua) */
QString settingsFile();

/* layout que quebra linha sozinho quando o painel fica estreito */
class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent = nullptr, int hSpacing = 4, int vSpacing = 4);
	~FlowLayout() override;
	void addItem(QLayoutItem *item) override { items.append(item); }
	/* espaço extra antes do próximo item (separa grupos) */
	void addGap(int px);
	int count() const override { return (int)items.size(); }
	QLayoutItem *itemAt(int i) const override { return items.value(i); }
	QLayoutItem *takeAt(int i) override { return i >= 0 && i < items.size() ? items.takeAt(i) : nullptr; }
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
	void setGeometry(const QRect &r) override
	{
		QLayout::setGeometry(r);
		doLayout(r, false);
	}
	QSize sizeHint() const override { return minimumSize(); }
	QSize minimumSize() const override;

	/* posição livre: cada widget no ponto que a pessoa escolheu (x, y em px) */
	void setFreeMode(bool on) { freeOn = on; invalidate(); }
	bool freeMode() const { return freeOn; }
	void setFreePos(QWidget *w, const QPoint &p) { freePos[w] = p; invalidate(); }
	void clearFreePos() { freePos.clear(); invalidate(); }

private:
	int doLayout(const QRect &r, bool testOnly) const;
	int doFree(const QRect &r, bool testOnly) const;
	QList<QLayoutItem *> items;
	int hs, vs;
	bool freeOn = false;
	QHash<QWidget *, QPoint> freePos;
};


/* cores */
inline constexpr const char *BG = "#121419";
inline constexpr const char *PANEL = "#1a1d24";
inline constexpr const char *RAISED = "#242832";
inline constexpr const char *BORDER = "#2d323d";
inline constexpr const char *TEXT = "#e8ebf1";
inline constexpr const char *MUTED = "#8b93a5";
inline constexpr const char *BLUE = "#3b82f6";
inline constexpr const char *GREEN = "#22c55e";
inline constexpr const char *RED = "#ef4444";
inline constexpr const char *AMBER = "#f59e0b";

/* aplica o visual a um painel (e a tudo dentro dele) */
void apply(QWidget *root);
/* aplica a uma janela de configuração */
void applyDialog(QWidget *dlg);

/* tipos de botão: "" normal, "primary" azul, "go" verde (rodando = aceso),
 * "rec" vermelho quando ligado, "danger", "ghost" (sem fundo), "seg" (grupo) */
QPushButton *button(const QString &text, const QString &tip, QWidget *parent, const char *kind = "",
		    std::function<void()> onClick = nullptr);
QPushButton *toggle(const QString &text, const QString &tip, QWidget *parent, const char *kind = "");
/* muda o tipo/tamanho e redesenha */
void setKind(QWidget *w, const char *kind);
void setProp(QWidget *w, const char *name, const QVariant &value);
/* botões encostados (segmentado): marca primeiro/meio/último */
void segment(const std::initializer_list<QPushButton *> &buttons);

/* cartão (fundo levemente elevado, cantos arredondados) */
QFrame *card(QWidget *parent, QBoxLayout *layout, int margin = 8, int spacing = 6);
/* rótulo pequeno em maiúsculas */
QLabel *caption(const QString &text, QWidget *parent);
/* etiqueta colorida ("live", "rec", "warn", "ok", "info", "" neutra) */
QLabel *chip(const QString &text, QWidget *parent, const char *tone = "");
/* ícones desenhados (nítidos em qualquer tema/fonte): play, pause, prev, next, reverse, ff, export, gear */
QIcon icon(const char *name, const char *color = "#e8ebf1");
void setChip(QLabel *chip, const QString &text, const char *tone);

} // namespace ui
