#include "appvmix.hpp"

#include "engine.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpSocket>

#include <obs.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#endif

AppVmix &AppVmix::get()
{
	static AppVmix a;
	return a;
}

AppVmix::AppVmix()
{
	load();
}

static QString iniPath()
{
	return Engine::get().configDir() + "/appvmix.ini";
}

void AppVmix::load()
{
	QSettings s(iniPath(), QSettings::IniFormat);
	dir = s.value("pasta").toString();
	autoOn = s.value("abrir_junto", false).toBool();
}

void AppVmix::save()
{
	QSettings s(iniPath(), QSettings::IniFormat);
	s.setValue("pasta", dir);
	s.setValue("abrir_junto", autoOn);
}

void AppVmix::setFolder(const QString &f)
{
	dir = QDir::cleanPath(f);
	save();
}

void AppVmix::setAutoStart(bool on)
{
	autoOn = on;
	save();
}

bool AppVmix::validFolder(const QString &f)
{
	return QFile::exists(f + "/package.json") && QFile::exists(f + "/server/src/index.ts");
}

bool AppVmix::running() const
{
	return proc && proc->state() != QProcess::NotRunning;
}

void AppVmix::setStatus(const QString &s)
{
	statusText = s;
	blog(LOG_INFO, "[APPVMIX] %s", s.toUtf8().constData());
	emit statusChanged();
}

static bool portInUse(int port)
{
	QTcpSocket t;
	t.connectToHost("127.0.0.1", port);
	return t.waitForConnected(300);
}

void AppVmix::startIfWanted()
{
	if (autoOn && validFolder(dir))
		start();
}

void AppVmix::start()
{
	if (running())
		return;
	if (!validFolder(dir)) {
		setStatus("Escolha a pasta do APPVMIX (a que tem o iniciar.bat)");
		return;
	}
	if (portInUse(3000)) {
		panelUrl = "http://127.0.0.1:3000";
		setStatus("Já está rodando (porta 3000 em uso: o iniciar.bat está aberto?)");
		return;
	}
	const QString node = QStandardPaths::findExecutable("node");
	if (node.isEmpty()) {
		setStatus("Falta o Node.js: instale a versão LTS em https://nodejs.org e ligue de novo");
		return;
	}
	/* o mesmo que o iniciar.bat: instala as dependências e gera o app web na primeira vez */
	QStringList steps;
	if (!QDir(dir + "/node_modules").exists())
		steps << "install";
	if (!QFile::exists(dir + "/web/dist/index.html"))
		steps << "build";
	steps << "start";
	launch(steps);
}

void AppVmix::launch(const QStringList &steps)
{
	if (steps.isEmpty())
		return;
	const QString step = steps.first();
	const QStringList rest = steps.mid(1);
	proc = new QProcess(this);
	proc->setWorkingDirectory(dir);
	proc->setProcessChannelMode(QProcess::MergedChannels);
#ifdef _WIN32
	/* sem a janela preta do prompt */
	proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
		a->flags |= CREATE_NO_WINDOW;
	});
#endif
	const QString logPath = Engine::get().configDir() + "/logs/appvmix.txt";
	connect(proc, &QProcess::readyRead, this, [this, logPath] {
		const QByteArray out = proc->readAll();
		QFile f(logPath);
		if (f.open(QIODevice::Append))
			f.write(out);
		/* "http://10.0.0.165:3000" = endereço para o celular */
		static const QRegularExpression re(R"(http://(\d+\.\d+\.\d+\.\d+):(\d+))");
		auto it = re.globalMatch(QString::fromUtf8(out));
		while (it.hasNext()) {
			const QRegularExpressionMatch m = it.next();
			if (m.captured(1).startsWith("127.") || m.captured(2) == "8088")
				continue; /* o endereço do vMix (o próprio Studio) */
			panelUrl = m.captured(0);
			setStatus("Rodando: no celular/tablet abra " + panelUrl);
		}
		if (panelUrl.isEmpty() && out.contains("Painel vMix rodando")) {
			panelUrl = "http://127.0.0.1:3000";
			setStatus("Rodando: neste PC abra " + panelUrl);
		}
	});
	connect(proc, &QProcess::finished, this, [this, step, rest](int code, QProcess::ExitStatus) {
		proc->deleteLater();
		proc = nullptr;
		if (stopping)
			return;
		if (step != "start" && code == 0) {
			launch(rest); /* próxima etapa */
			return;
		}
		setStatus(step == "install"  ? "Não conseguiu instalar as dependências (veja logs/appvmix.txt)"
			  : step == "build" ? "Não conseguiu gerar o app web (veja logs/appvmix.txt)"
					    : QString("O painel parou (código %1; veja logs/appvmix.txt)").arg(code));
	});

	if (step == "start") {
		/* direto pelo node (como o "npm start" faz, mas sem o npm no meio) */
		const QString tsx = dir + "/node_modules/tsx/dist/cli.mjs";
		const QString node = QStandardPaths::findExecutable("node");
		if (QFile::exists(tsx)) {
			proc->start(node, {tsx, "server/src/index.ts"});
		} else {
#ifdef _WIN32
			proc->start("cmd.exe", {"/c", "npm", "start"});
#else
			proc->start("npm", {"start"});
#endif
		}
		setStatus("Ligando o painel…");
	} else {
		const QStringList args = step == "install" ? QStringList{"install"} : QStringList{"run", "build"};
#ifdef _WIN32
		proc->start("cmd.exe", QStringList{"/c", "npm"} + args);
#else
		proc->start("npm", args);
#endif
		setStatus(step == "install" ? "Primeira vez: instalando as dependências do painel (alguns minutos)…"
					    : "Gerando o app web do painel…");
	}
}

void AppVmix::stop()
{
	if (!proc)
		return;
	stopping = true;
#ifdef _WIN32
	/* o node pode ter aberto outros processos: fecha a árvore inteira */
	QProcess::execute("taskkill", {"/PID", QString::number(proc->processId()), "/T", "/F"});
#else
	proc->terminate();
#endif
	proc->waitForFinished(3000);
	if (proc && proc->state() != QProcess::NotRunning)
		proc->kill();
	stopping = false;
	setStatus("Desligado");
}

void AppVmix::openFirewall()
{
#ifdef _WIN32
	/* uma vez só: pede administrador (janela do Windows) e cria a regra da porta 3000 */
	const wchar_t *args = L"/c netsh advfirewall firewall delete rule name=\"Painel vMix (3000)\" & "
			      L"netsh advfirewall firewall add rule name=\"Painel vMix (3000)\" dir=in action=allow "
			      L"protocol=TCP localport=3000";
	ShellExecuteW(nullptr, L"runas", L"cmd.exe", args, nullptr, SW_HIDE);
#endif
}
