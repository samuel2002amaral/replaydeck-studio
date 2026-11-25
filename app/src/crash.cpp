#include "crash.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#ifndef _WIN32
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
QString g_dir;
#ifndef _WIN32
char g_path[1024] = {};

void handler(int sig)
{
	const int fd = ::open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		const char *name = sig == SIGSEGV ? "SIGSEGV (acesso inválido à memória)"
				   : sig == SIGBUS ? "SIGBUS"
				   : sig == SIGILL ? "SIGILL"
				   : sig == SIGFPE ? "SIGFPE"
				   : sig == SIGABRT ? "SIGABRT (abortou)"
						    : "sinal";
		const char head[] = "ReplayDeck Studio " RDS_VERSION " fechou sozinho: ";
		(void)!::write(fd, head, sizeof(head) - 1);
		(void)!::write(fd, name, strlen(name));
		(void)!::write(fd, "\n\nOnde caiu:\n", 13);
		void *frames[80];
		const int n = backtrace(frames, 80);
		backtrace_symbols_fd(frames, n, fd);
		::close(fd);
	}
	/* deixa o sistema terminar (e o Mac gerar o relatório dele também) */
	signal(sig, SIG_DFL);
	raise(sig);
}
#endif
} // namespace

namespace crash {
void install(const QString &dir)
{
	g_dir = dir + "/quedas";
	QDir().mkpath(g_dir);
#ifndef _WIN32
	const QByteArray p =
		QFile::encodeName(g_dir + "/queda-" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".txt");
	strncpy(g_path, p.constData(), sizeof(g_path) - 1);
	for (int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
		signal(s, handler);
#endif
}

void showPrevious(QWidget *parent)
{
	if (g_dir.isEmpty())
		return;
	QDir d(g_dir);
	const QStringList files = d.entryList({"queda-*.txt"}, QDir::Files, QDir::Time);
	if (files.isEmpty())
		return;
	QFile f(d.filePath(files.first()));
	QString text;
	if (f.open(QIODevice::ReadOnly))
		text = QString::fromUtf8(f.readAll());
	f.close();
	/* mostra uma vez só */
	for (const QString &n : files)
		QFile::rename(d.filePath(n), d.filePath(QString(n).replace("queda-", "vista-")));
	if (text.trimmed().isEmpty())
		return;
	auto *dlg = new QDialog(parent);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle("O programa fechou sozinho da última vez");
	dlg->resize(760, 460);
	auto *v = new QVBoxLayout(dlg);
	auto *l = new QLabel("Desculpe! O ReplayDeck Studio fechou sozinho na última vez. Clique em <b>Copiar</b> e mande "
			     "o texto abaixo para o suporte: ele mostra onde o programa caiu.");
	l->setWordWrap(true);
	v->addWidget(l);
	auto *t = new QPlainTextEdit(text);
	t->setReadOnly(true);
	t->setLineWrapMode(QPlainTextEdit::NoWrap);
	v->addWidget(t, 1);
	auto *bb = new QDialogButtonBox;
	auto *copy = bb->addButton("Copiar", QDialogButtonBox::ActionRole);
	QObject::connect(copy, &QPushButton::clicked, dlg, [text] { QApplication::clipboard()->setText(text); });
	auto *ok = bb->addButton("Fechar", QDialogButtonBox::AcceptRole);
	QObject::connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);
	v->addWidget(bb);
	dlg->show();
}
} // namespace crash
