#include "gpu.hpp"

#include <util/base.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dxgi.h>
#endif

namespace gpu {

QString vendorName(unsigned vendor)
{
	switch (vendor) {
	case 0x10DE:
		return "NVIDIA";
	case 0x1002:
	case 0x1022:
		return "AMD";
	case 0x8086:
		return "Intel";
	default:
		return QString();
	}
}

QList<Adapter> adapters()
{
	QList<Adapter> out;
#ifdef _WIN32
	IDXGIFactory1 *factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)) || !factory)
		return out;
	IDXGIAdapter1 *a = nullptr;
	for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC1 d = {};
		if (SUCCEEDED(a->GetDesc1(&d))) {
			Adapter ad;
			ad.index = (int)i;
			ad.name = QString::fromWCharArray(d.Description);
			ad.vendor = d.VendorId;
			ad.dedicatedMB = d.DedicatedVideoMemory / (1024ull * 1024ull);
			ad.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || d.VendorId == 0x1414; /* Microsoft Basic Render */
			out << ad;
		}
		a->Release();
	}
	factory->Release();
#endif
	return out;
}

/* nota de uma placa para o automático: dedicada NVIDIA/AMD com mais memória ganha */
static long long score(const Adapter &a)
{
	if (a.software)
		return -1;
	long long s = (long long)a.dedicatedMB;
	if (a.vendor == 0x10DE)
		s += 1000000; /* NVIDIA: também libera o codificador NVENC direto da placa */
	else if ((a.vendor == 0x1002 || a.vendor == 0x1022) && a.dedicatedMB >= 1024)
		s += 500000;
	return s;
}

int pick(const QString &wanted, QString *chosenName)
{
	const QList<Adapter> list = adapters();
	for (const Adapter &a : list)
		blog(LOG_INFO, "placa de vídeo %d: %s (%s, %llu MB dedicados)%s", a.index, a.name.toUtf8().constData(),
		     vendorName(a.vendor).toUtf8().constData(), a.dedicatedMB, a.software ? " [software]" : "");
	if (list.isEmpty())
		return 0;
	if (!wanted.isEmpty() && wanted != "auto") {
		for (const Adapter &a : list)
			if (a.name == wanted) {
				if (chosenName)
					*chosenName = a.name;
				return a.index;
			}
		blog(LOG_WARNING, "a placa escolhida '%s' não está mais aqui: usando a automática",
		     wanted.toUtf8().constData());
	}
	const Adapter *best = &list.first();
	for (const Adapter &a : list)
		if (score(a) > score(*best))
			best = &a;
	if (chosenName)
		*chosenName = best->name;
	return best->index;
}

} // namespace gpu
