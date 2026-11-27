#include "gpu-monitor.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

/* só o necessário da nvml.h (a dll vem com o driver da NVIDIA) */
namespace {
typedef int nvmlReturn_t;
typedef struct nvmlDevice_st *nvmlDevice_t;
struct nvmlUtilization_t {
	unsigned int gpu;
	unsigned int memory;
};
typedef nvmlReturn_t (*InitFn)(void);
typedef nvmlReturn_t (*HandleFn)(unsigned int, nvmlDevice_t *);
typedef nvmlReturn_t (*TempFn)(nvmlDevice_t, int, unsigned int *);
typedef nvmlReturn_t (*UtilFn)(nvmlDevice_t, nvmlUtilization_t *);
typedef nvmlReturn_t (*EncFn)(nvmlDevice_t, unsigned int *, unsigned int *);
typedef nvmlReturn_t (*ReasonsFn)(nvmlDevice_t, unsigned long long *);

constexpr unsigned long long kSwPowerCap = 0x4ull;
constexpr unsigned long long kHwSlowdown = 0x8ull;
constexpr unsigned long long kSwThermal = 0x20ull;
constexpr unsigned long long kHwThermal = 0x40ull;
constexpr unsigned long long kHwPowerBrake = 0x80ull;

struct Nvml {
	bool tried = false, ready = false;
	nvmlDevice_t dev = nullptr;
	TempFn temp = nullptr;
	UtilFn util = nullptr;
	EncFn enc = nullptr;
	ReasonsFn reasons = nullptr;

	void init()
	{
		tried = true;
		HMODULE m = LoadLibraryW(L"nvml.dll");
		if (!m) {
			wchar_t path[MAX_PATH];
			if (ExpandEnvironmentStringsW(L"%ProgramFiles%\\NVIDIA Corporation\\NVSMI\\nvml.dll", path, MAX_PATH))
				m = LoadLibraryW(path);
		}
		if (!m)
			return;
		auto initFn = (InitFn)GetProcAddress(m, "nvmlInit_v2");
		auto handle = (HandleFn)GetProcAddress(m, "nvmlDeviceGetHandleByIndex_v2");
		temp = (TempFn)GetProcAddress(m, "nvmlDeviceGetTemperature");
		util = (UtilFn)GetProcAddress(m, "nvmlDeviceGetUtilizationRates");
		enc = (EncFn)GetProcAddress(m, "nvmlDeviceGetEncoderUtilization");
		reasons = (ReasonsFn)GetProcAddress(m, "nvmlDeviceGetCurrentClocksEventReasons");
		if (!reasons)
			reasons = (ReasonsFn)GetProcAddress(m, "nvmlDeviceGetCurrentClocksThrottleReasons");
		if (!initFn || !handle || initFn() != 0 || handle(0, &dev) != 0)
			return;
		ready = true;
	}
};
Nvml g;
} // namespace

GpuStatus gpumon::read()
{
	GpuStatus s;
	if (!g.tried)
		g.init();
	if (!g.ready)
		return s;
	s.ok = true;
	nvmlUtilization_t u{};
	if (g.util && g.util(g.dev, &u) == 0)
		s.gpu = (int)u.gpu;
	unsigned int e = 0, period = 0;
	if (g.enc && g.enc(g.dev, &e, &period) == 0)
		s.encoder = (int)e;
	unsigned int t = 0;
	if (g.temp && g.temp(g.dev, 0 /* NVML_TEMPERATURE_GPU */, &t) == 0)
		s.tempC = (int)t;
	unsigned long long r = 0;
	if (g.reasons && g.reasons(g.dev, &r) == 0) {
		s.hot = (r & (kSwThermal | kHwThermal | kHwSlowdown)) != 0;
		s.power = (r & (kSwPowerCap | kHwPowerBrake)) != 0;
	}
	return s;
}

#else

GpuStatus gpumon::read()
{
	return GpuStatus{};
}

#endif
