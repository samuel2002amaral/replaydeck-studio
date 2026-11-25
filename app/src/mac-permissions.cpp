#include "mac-permissions.hpp"

#ifdef __APPLE__
#include "engine.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <obs.h>

int rdsMacMediaStatus(bool video);
void rdsMacMediaRequest(bool video);
bool rdsMacScreenAllowed();
bool rdsMacScreenRequest();
void rdsMacOpenPrivacy(const char *pane);

namespace {
enum Kind { Camera, Mic, Screen };

int status(Kind k)
{
	if (k == Screen)
		return rdsMacScreenAllowed() ? 2 : 0;
	return rdsMacMediaStatus(k == Camera);
}

bool allGranted()
{
	return status(Camera) == 2 && status(Mic) == 2 && status(Screen) == 2;
}

QSettings &store()
{
	static QSettings s(Engine::get().configDir() + "/permissoes-mac.ini", QSettings::IniFormat);
	return s;
}

void request(Kind k)
{
	switch (k) {
	case Camera:
	case Mic:
		if (rdsMacMediaStatus(k == Camera) == 0)
			rdsMacMediaRequest(k == Camera); /* o Mac mostra a janela de permitir */
		else
			rdsMacOpenPrivacy(k == Camera ? "Privacy_Camera" : "Privacy_Microphone");
		break;
	case Screen:
		/* primeira vez: o Mac mostra o aviso; depois, só nos Ajustes */
		if (!rdsMacScreenRequest() && store().value("pediu_tela").toBool())
			rdsMacOpenPrivacy("Privacy_ScreenCapture");
		store().setValue("pediu_tela", true);
		break;
	}
}

class PermDialog : public QDialog {
public:
	explicit PermDialog(QWidget *parent) : QDialog(parent)
	{
		setWindowTitle("Permissões do Mac");
		setMinimumWidth(560);
		auto *v = new QVBoxLayout(this);
		auto *intro = new QLabel("<b>O Mac pede autorização para o programa usar câmera, microfone e a tela.</b><br>"
					 "Clique em <b>Permitir tudo</b> e responda <b>Permitir</b> nas janelas do Mac. "
					 "É só uma vez.");
		intro->setWordWrap(true);
		v->addWidget(intro);
		auto *g = new QGridLayout;
		g->setHorizontalSpacing(12);
		const struct {
			Kind k;
			const char *name, *what;
		} rows[] = {{Camera, "Câmera", "câmeras USB, placas de captura, câmera do Mac"},
			    {Mic, "Microfone", "microfones e entradas de áudio"},
			    {Screen, "Gravação de Tela e Áudio do Sistema", "captura de tela, de janela e o som do Mac"}};
		int r = 0;
		for (const auto &row : rows) {
			auto *name = new QLabel(QString("<b>%1</b><br><span style='color:#9aa3b5'>%2</span>")
							.arg(QString::fromUtf8(row.name), QString::fromUtf8(row.what)));
			auto *st = new QLabel;
			auto *btn = new QPushButton("Permitir");
			const Kind k = row.k;
			connect(btn, &QPushButton::clicked, this, [k] { request(k); });
			g->addWidget(name, r, 0);
			g->addWidget(st, r, 1);
			g->addWidget(btn, r, 2);
			labels[k] = st;
			buttons[k] = btn;
			r++;
		}
		g->setColumnStretch(0, 1);
		v->addLayout(g);
		screenNote = new QLabel("Depois de permitir a <b>Gravação de Tela</b>, o Mac pede para fechar e abrir o "
					"programa de novo: clique em <b>Sair e Reabrir</b> (ou feche e abra).");
		screenNote->setWordWrap(true);
		screenNote->setObjectName("muted");
		v->addWidget(screenNote);
		dontShow = new QCheckBox("Não mostrar esta janela ao abrir");
		dontShow->setChecked(store().value("nao_mostrar").toBool());
		v->addWidget(dontShow);
		auto *bb = new QDialogButtonBox;
		auto *all = bb->addButton("Permitir tudo", QDialogButtonBox::AcceptRole);
		all->setDefault(true);
		connect(all, &QPushButton::clicked, this, [] {
			/* uma de cada vez: o Mac mostra as janelas em sequência */
			request(Camera);
			QTimer::singleShot(400, [] { request(Mic); });
			QTimer::singleShot(800, [] { request(Screen); });
		});
		auto *close = bb->addButton("Fechar", QDialogButtonBox::RejectRole);
		connect(close, &QPushButton::clicked, this, &QDialog::reject);
		v->addWidget(bb);
		connect(this, &QDialog::finished, this, [this] { store().setValue("nao_mostrar", dontShow->isChecked()); });
		auto *t = new QTimer(this);
		connect(t, &QTimer::timeout, this, [this] { refresh(); });
		t->start(700);
		refresh();
	}

private:
	void refresh()
	{
		for (Kind k : {Camera, Mic, Screen}) {
			const int s = status(k);
			labels[k]->setText(s == 2 ? "✅ Permitido" : s == 1 ? "❌ Negado" : "⚠️ Falta permitir");
			buttons[k]->setText(s == 2 ? "Permitido" : s == 1 ? "Abrir Ajustes" : "Permitir");
			buttons[k]->setEnabled(s != 2);
		}
	}
	QLabel *labels[3] = {};
	QPushButton *buttons[3] = {};
	QLabel *screenNote = nullptr;
	QCheckBox *dontShow = nullptr;
};
} // namespace

namespace macperm {
void showAtStartup(QWidget *parent)
{
	blog(LOG_INFO, "[permissões] câmera=%d microfone=%d tela=%d", status(Camera), status(Mic), status(Screen));
	if (allGranted() || store().value("nao_mostrar").toBool() || qEnvironmentVariableIsSet("RDS_SEM_PERMISSOES"))
		return;
	auto *d = new PermDialog(parent);
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->show();
}

void showDialog(QWidget *parent)
{
	auto *d = new PermDialog(parent);
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->show();
}

bool ensureScreen(QWidget *parent)
{
	if (rdsMacScreenAllowed())
		return true;
	PermDialog d(parent);
	d.setWindowTitle("Falta a permissão de Gravação de Tela");
	d.exec();
	return rdsMacScreenAllowed();
}
} // namespace macperm

#else
namespace macperm {
void showAtStartup(QWidget *) {}
void showDialog(QWidget *) {}
bool ensureScreen(QWidget *)
{
	return true;
}
} // namespace macperm
#endif
