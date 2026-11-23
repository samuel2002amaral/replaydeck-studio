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
