"""Prepara a árvore do OBS para o ReplayDeck Studio.

Uso: python tools/patch-obs.py <pasta do obs> <pasta deste repositório> <pasta do replaydeck>

- copia os módulos embutidos (ReplayDeck e Placar de Futsal) para obs/plugins
- registra os dois como módulos do sistema (core), iguais aos módulos internos do OBS:
  ficam em core/<nome>, carregam sempre e não aparecem como plugin no Gerenciador de plugins
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
    + "\n# ReplayDeck Studio: módulos do sistema\nadd_core_module(replaydeck)\nadd_core_module(futsal-placar)\n"
    + text[idx:]
)
plugins_cmake.write_text(text, encoding="utf-8")
print("patch-obs: plugins/CMakeLists.txt ok")

# 2) ReplayDeck instalado como módulo do sistema também no Windows (no projeto dele isso só vale para o macOS)
replace_once(
    obs / "plugins" / "replaydeck" / "CMakeLists.txt",
    "if(APPLE AND COMMAND set_target_properties_obs)",
    "if(COMMAND set_target_properties_obs)",
)

# 3) textos do replay: "plugin" vira "sistema" (código, chaves de tradução e tradução em inglês)
TEXTOS = [
    ("o plugin responde", "o sistema responde"),
    ("funcionam com o OBS", "funcionam aqui"),
    ("temporários do plugin", "temporários do replay"),
    ("Idioma do plugin", "Idioma do replay"),
    ("the plugin answers", "the system answers"),
    ("work with OBS", "work here"),
    ("Plugin language", "Replay language"),
    ("plugin's temporary", "replay temporary"),
]
rd = obs / "plugins" / "replaydeck"
arquivos = [*rd.glob("src/**/*.cpp"), *rd.glob("src/**/*.hpp"), *rd.glob("src/**/*.h"),
            *rd.glob("data/lang/*.json"), *rd.glob("i18n/*.txt")]
trocas = 0
for f in arquivos:
    t = f.read_text(encoding="utf-8")
    novo = t
    for a, b in TEXTOS:
        novo = novo.replace(a, b)
    if novo != t:
        f.write_text(novo, encoding="utf-8")
        trocas += 1
print(f"patch-obs: textos do replay ajustados em {trocas} arquivos")

# 4) barra de título
replace_once(
    obs / "frontend" / "widgets" / "OBSBasic.cpp",
    'name << "OBS ";',
    f'name << "{STUDIO_NAME} - OBS ";',
)
