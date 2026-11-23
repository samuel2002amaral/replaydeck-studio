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
# ao ligar a câmera. Aqui as duas reaproveitam a última procura feita nos últimos 30 segundos, então
# a câmera liga logo depois que o programa preparou as opções.
dshow = obs / "plugins" / "win-dshow" / "win-dshow.cpp"
if dshow.exists():
    t = dshow.read_text(encoding="utf-8")
    helper = (
        "#include <mutex>\n"
        "static std::mutex rdsEnumMutex;\n"
        "static std::vector<VideoDevice> rdsEnumCache;\n"
        "static uint64_t rdsEnumTime = 0;\n"
        "static void RdsEnumVideoDevicesCached(std::vector<VideoDevice> &out)\n"
        "{\n"
        "\tstd::lock_guard<std::mutex> lock(rdsEnumMutex);\n"
        "\tconst uint64_t now = os_gettime_ns();\n"
        "\tif (!rdsEnumTime || now - rdsEnumTime > 30000000000ULL) {\n"
        "\t\trdsEnumCache.clear();\n"
        "\t\tDevice::EnumVideoDevices(rdsEnumCache);\n"
        "\t\trdsEnumTime = now;\n"
        "\t}\n"
        "\tout = rdsEnumCache;\n"
        "}\n\n"
    )
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
