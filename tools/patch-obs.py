"""Prepara a árvore do OBS para o ReplayDeck Studio.

Uso: python tools/patch-obs.py <pasta do obs> <pasta deste repositório> <pasta do replaydeck>

- copia os módulos embutidos (ReplayDeck e Placar de Futsal) para obs/plugins
- coloca os dois na lista de compilação (não como módulos "core": se um falhar, o programa abre)
- troca o nome na barra de título
Cada troca confere se o texto original existe: se o OBS mudar, o build para aqui com um erro claro.
"""
import pathlib
import shutil
import sys

obs = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(sys.argv[2]).resolve()
replaydeck = pathlib.Path(sys.argv[3]).resolve()

STUDIO_NAME = "ReplayDeck Studio"


def replace_once(path: pathlib.Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        sys.exit(f"patch-obs: esperado 1 ocorrência de {old!r} em {path}, achei {text.count(old)}")
    path.write_text(text.replace(old, new), encoding="utf-8")
    print(f"patch-obs: {path.relative_to(obs)} ok")


def copy_plugin(src: pathlib.Path, name: str) -> None:
    dst = obs / "plugins" / name
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".git", ".github", "app", "build*"))
    print(f"patch-obs: plugin {name} copiado")


copy_plugin(replaydeck, "replaydeck")
copy_plugin(repo / "plugins" / "futsal-placar", "futsal-placar")

# 1) compilar junto com o OBS (antes da lista final de módulos core)
plugins_cmake = obs / "plugins" / "CMakeLists.txt"
text = plugins_cmake.read_text(encoding="utf-8")
marker = "\nset_obs_core_modules()\n"
idx = text.rfind(marker)
if idx < 0:
    sys.exit("patch-obs: não achei set_obs_core_modules() em plugins/CMakeLists.txt")
text = (
    text[:idx]
    + "\n# ReplayDeck Studio: módulos embutidos\nadd_subdirectory(replaydeck)\nadd_subdirectory(futsal-placar)\n"
    + text[idx:]
)
plugins_cmake.write_text(text, encoding="utf-8")
print("patch-obs: plugins/CMakeLists.txt ok")

# 2) barra de título
replace_once(
    obs / "frontend" / "widgets" / "OBSBasic.cpp",
    'name << "OBS ";',
    f'name << "{STUDIO_NAME} - OBS ";',
)
