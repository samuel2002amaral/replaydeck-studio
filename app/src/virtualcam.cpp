#include "virtualcam.hpp"
#include "engine.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

#ifdef _WIN32
/* o mesmo GUID passado ao motor no workflow (-DVIRTUALCAM_GUID) */
static const wchar_t *VCAM_CLSID_KEY = L"CLSID\\{80797F4E-1E09-4520-9573-CAC17D1569CF}\\InprocServer32";
#endif

VirtualCam &VirtualCam::get()
{
	static VirtualCam v;
	static bool wired = false;
	if (!wired) {
		wired = true;
		/* a entrada compartilhada foi removida: desliga a câmera */
		QObject::connect(&Engine::get(), &Engine::inputsChanged, &v, [] {
			VirtualCam &c = VirtualCam::get();
			if (c.active() && c.id && !Engine::get().input(c.id))
				c.stop();
		});
	}
	return v;
}

bool VirtualCam::available() const
{
	return obs_get_output_flags("virtualcam_output") != 0;
}

bool VirtualCam::active() const
{
	return output && obs_output_active(output);
}

QString VirtualCam::description() const
{
	if (!id)
		return "Programa";
	Input *in = Engine::get().input(id);
	return in ? in->name : QString("Entrada");
}

#ifdef _WIN32
static QString registeredModule()
{
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CLASSES_ROOT, VCAM_CLSID_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
		return QString();
	wchar_t path[MAX_PATH] = {};
	DWORD size = sizeof(path);
	const LSTATUS r = RegQueryValueExW(key, nullptr, nullptr, nullptr, reinterpret_cast<BYTE *>(path), &size);
	RegCloseKey(key);
	return r == ERROR_SUCCESS ? QString::fromWCharArray(path) : QString();
}
#endif

bool VirtualCam::ensureInstalled(QWidget *parent, QString &error)
{
#ifdef _WIN32
	char *mod = obs_find_module_file(obs_get_module("win-dshow"), "obs-virtualcam-module64.dll");
	const QString module = mod ? QDir::toNativeSeparators(QString::fromUtf8(mod)) : QString();
	bfree(mod);
	if (module.isEmpty()) {
		error = "O arquivo da câmera virtual não está na pasta do programa. Reinstale o ReplayDeck Studio.";
		return false;
	}
	const QString current = registeredModule();
	if (!current.isEmpty() && QFileInfo::exists(current))
		return true; /* já instalada (por esta ou outra cópia do programa) */

	if (QMessageBox::question(parent, "Câmera virtual",
				  "Para os outros programas enxergarem a câmera \"ReplayDeck Studio\", ela precisa ser "
				  "instalada no Windows uma única vez.\n\nO Windows vai pedir permissão de administrador. "
				  "Instalar agora?") != QMessageBox::Yes) {
		error = "Câmera virtual não instalada.";
		return false;
	}
	/* regsvr32 com permissão de administrador (registra para todos os programas do PC):
	 * a versão de 64 bits (Zoom, navegador, Teams) e a de 32 bits (programas antigos), num pedido só */
	QString cmd = QString("/c \"\"%1\\System32\\regsvr32.exe\" /s \"%2\"")
			      .arg(QString::fromLocal8Bit(qgetenv("SystemRoot")), module);
	const QString module32 = QFileInfo(module).absolutePath() + "/obs-virtualcam-module32.dll";
	if (QFileInfo::exists(module32))
		cmd += QString(" & \"%1\\SysWOW64\\regsvr32.exe\" /s \"%2\"")
			       .arg(QString::fromLocal8Bit(qgetenv("SystemRoot")), QDir::toNativeSeparators(module32));
	cmd += "\"";
	const std::wstring params = cmd.toStdWString();
	SHELLEXECUTEINFOW sei = {};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"runas";
	sei.lpFile = L"cmd.exe";
	sei.lpParameters = params.c_str();
	sei.nShow = SW_HIDE;
	if (!ShellExecuteExW(&sei)) {
		error = "A instalação foi cancelada (permissão negada).";
		return false;
	}
	if (sei.hProcess) {
		WaitForSingleObject(sei.hProcess, 30000);
		CloseHandle(sei.hProcess);
	}
	if (registeredModule().isEmpty()) {
		error = "O Windows não registrou a câmera virtual.";
		return false;
	}
	blog(LOG_INFO, "câmera virtual instalada: %s", module.toUtf8().constData());
	return true;
#else
	(void)parent;
	(void)error;
	return true; /* Linux: precisa do v4l2loopback carregado */
#endif
}

void VirtualCam::setViewSource()
{
	if (!view)
		return;
	Input *in = Engine::get().input(id);
	obs_view_set_source(view, 0, in ? in->scene.Get() : nullptr);
}

bool VirtualCam::start(int inputId, QWidget *parent, QString &error)
{
	if (inputId && !Engine::get().input(inputId)) {
		error = "Entrada não encontrada.";
		return false;
	}
	if (active()) {
		/* já ligada: só troca o que está sendo compartilhado */
		const bool wasProgram = id == 0;
		id = inputId;
		if (wasProgram != (inputId == 0)) {
			stop();
			return start(inputId, parent, error);
		}
		setViewSource();
		emit stateChanged();
		return true;
	}
	if (!available()) {
		error = "Este motor não tem câmera virtual.";
		return false;
	}
	if (!ensureInstalled(parent, error))
		return false;

	id = inputId;
#ifdef __APPLE__
	/* no Mac a câmera virtual é uma "extensão de sistema" que só funciona assinada pela Apple */
	error = "No Mac a câmera virtual ainda não está disponível (ela precisa ser assinada pela Apple). "
		"Use a saída Externo ou um app de captura de tela.";
	return false;
#endif
	output = obs_output_create("virtualcam_output", "camera-virtual", nullptr, nullptr);
	if (!output) {
		error = "Não foi possível criar a câmera virtual.";
		return false;
	}
	if (id) {
		/* uma entrada só: uma "visão" própria do motor, no tamanho do programa */
		view = obs_view_create();
		setViewSource();
		obs_video_info ovi;
		obs_get_video_info(&ovi);
		viewVideo = obs_view_add2(view, &ovi);
		obs_output_set_media(output, viewVideo, obs_get_audio());
	}
	if (!obs_output_start(output)) {
		const char *e = obs_output_get_last_error(output);
		error = e ? QString::fromUtf8(e)
			  : QString("A câmera virtual não ligou (no Linux precisa do v4l2loopback).");
		stop();
		return false;
	}
	blog(LOG_INFO, "câmera virtual ligada: %s", description().toUtf8().constData());
	emit stateChanged();
	return true;
}

void VirtualCam::stop()
{
	if (output) {
		/* para de verdade antes de soltar a visão que alimenta a câmera */
		if (obs_output_active(output))
			obs_output_force_stop(output);
		output = nullptr;
	}
	if (view) {
		obs_view_remove(view);
		obs_view_set_source(view, 0, nullptr);
		obs_view_destroy(view);
		view = nullptr;
		viewVideo = nullptr;
	}
	emit stateChanged();
}

void VirtualCam::shutdown()
{
	if (output || view)
		stop();
}

void VirtualCam::fillMenu(QMenu *menu, QWidget *parent)
{
	menu->clear();
	menu->addSection(active() ? "Compartilhando: " + description() : "Compartilhar como webcam");
	auto add = [&](const QString &label, int inputId) {
		QAction *a = menu->addAction(label, parent, [this, inputId, parent] {
			QString err;
			if (!start(inputId, parent, err))
				QMessageBox::warning(parent, "Câmera virtual", err);
		});
		a->setCheckable(true);
		a->setChecked(active() && id == inputId);
	};
	add("Programa", 0);
	for (Input *in : Engine::get().inputs()) {
		if (!(obs_source_get_output_flags(in->source) & OBS_SOURCE_VIDEO))
			continue;
		add(QString("%1  %2").arg(Engine::get().indexOf(in->id) + 1).arg(in->name), in->id);
	}
	if (active()) {
		menu->addSeparator();
		menu->addAction("Desligar a câmera virtual", parent, [this] { stop(); });
	}
}
