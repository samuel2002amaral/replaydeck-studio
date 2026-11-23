"""Ajustes no MOTOR (código do OBS) usado pelo ReplayDeck Studio.

Uso: python tools/patch-obs.py <pasta do obs> <pasta deste repositório>

O motor é compilado uma vez e guardado; qualquer mudança neste arquivo faz o GitHub recompilá-lo.
O programa em si (pasta app/) é compilado à parte, contra o motor guardado.
"""
import pathlib
import sys

obs = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(sys.argv[2]).resolve()


# Câmera (win-dshow): a procura de dispositivos do DirectShow leva segundos quando há muitas câmeras
# (virtuais, placas de captura). O plugin original procura tudo de novo ao montar as opções E de novo
# ao ligar a câmera. Aqui todo mundo recebe a última lista na hora e ela é atualizada em segundo plano.
dshow = obs / "plugins" / "win-dshow" / "win-dshow.cpp"
if dshow.exists():
    t = dshow.read_text(encoding="utf-8")
    helper = """#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
/* ReplayDeck Studio: lista de dispositivos guardada. Quem pede recebe a última lista NA HORA;
 * se ela tem mais de 30 s, uma nova procura roda em segundo plano para a próxima vez.
 * Só a primeiríssima procura (feita em segundo plano pelo programa ao abrir) espera. */
struct RdsEnumState {
	std::mutex m;
	std::vector<VideoDevice> devices;
	uint64_t time = 0;
	std::atomic<bool> busy{false};
	~RdsEnumState()
	{
		/* ao fechar, espera uma procura em andamento terminar (no máximo 15 s) */
		for (int i = 0; busy && i < 150; i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
};
static RdsEnumState rdsEnum;

static void RdsEnumNow(std::vector<VideoDevice> &out)
{
	const uint64_t t0 = os_gettime_ns();
	Device::EnumVideoDevices(out);
	blog(LOG_INFO, "[rds] procura de câmeras: %zu dispositivos em %llu ms", out.size(),
	     (unsigned long long)((os_gettime_ns() - t0) / 1000000));
}

static void RdsEnumVideoDevicesCached(std::vector<VideoDevice> &out)
{
	std::unique_lock<std::mutex> lock(rdsEnum.m);
	if (!rdsEnum.time) {
		std::vector<VideoDevice> fresh;
		lock.unlock();
		RdsEnumNow(fresh);
		lock.lock();
		rdsEnum.devices = fresh;
		rdsEnum.time = os_gettime_ns();
	} else if (os_gettime_ns() - rdsEnum.time > 30000000000ULL && !rdsEnum.busy) {
		rdsEnum.busy = true;
		std::thread([] {
			const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			std::vector<VideoDevice> fresh;
			RdsEnumNow(fresh);
			if (SUCCEEDED(com))
				CoUninitialize();
			{
				std::lock_guard<std::mutex> l(rdsEnum.m);
				rdsEnum.devices = fresh;
				rdsEnum.time = os_gettime_ns();
			}
			rdsEnum.busy = false;
		}).detach();
	}
	out = rdsEnum.devices;
}

"""
    anchor = "bool DShowInput::UpdateVideoConfig(obs_data_t *settings)"
    if t.count(anchor) != 1:
        sys.exit("patch-obs: win-dshow mudou (UpdateVideoConfig)")
    t = t.replace(anchor, helper + anchor)
    for old, new in (
        ("Device::EnumVideoDevices(data.devices);", "RdsEnumVideoDevicesCached(data.devices);"),
        ("Device::EnumVideoDevices(data->devices);", "RdsEnumVideoDevicesCached(data->devices);"),
    ):
        if t.count(old) != 1:
            sys.exit(f"patch-obs: win-dshow mudou ({old})")
        t = t.replace(old, new)
    dshow.write_text(t, encoding="utf-8")
    print("patch-obs: win-dshow com procura de dispositivos reaproveitada")
