"""Prepara a árvore do OBS para compilar o ReplayDeck Studio.

Uso: python tools/patch-obs.py <pasta do obs> <pasta deste repositório>

Do OBS usamos só o MOTOR (libobs e os módulos de captura, encoder e transmissão).
A interface é a nossa: a pasta app/ deste repositório é copiada para obs/rds-app
e entra no build do OBS, para usar o mesmo libobs, o mesmo Qt e as mesmas dependências.
"""
import pathlib
import shutil
import sys

obs = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(sys.argv[2]).resolve()

dst = obs / "rds-app"
if dst.exists():
    shutil.rmtree(dst)
shutil.copytree(repo / "app", dst)
print("patch-obs: app copiado para rds-app")

root_cmake = obs / "CMakeLists.txt"
text = root_cmake.read_text(encoding="utf-8")
marker = "add_subdirectory(frontend)\n"
if text.count(marker) != 1:
    sys.exit("patch-obs: não achei add_subdirectory(frontend) no CMakeLists.txt do OBS")
text = text.replace(marker, marker + "\n# ReplayDeck Studio (interface própria)\nadd_subdirectory(rds-app)\n")
root_cmake.write_text(text, encoding="utf-8")
print("patch-obs: CMakeLists.txt ok")

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
