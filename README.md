# ReplayDeck Studio

Programa de transmissão ao vivo para esportes, com **interface própria no estilo vMix** e o **motor de vídeo do OBS (libobs)** por baixo.
Uma tela só; replay, títulos GT, áudio e saídas ficam dentro dela.

Desenho aprovado das telas: https://claude.ai/artifact/FEu9oqLV7BL5HjBqKL7Deo

## Fases

| Fase | Conteúdo | Situação |
|---|---|---|
| 2 | Base: entradas, prévia/programa, transições, overlays, janela da entrada, projeto | **v2.0.3** |
| 3 | Mixer de áudio, transmissão, gravação, configurações, saída externa | próxima |
| 4 | Replay instantâneo (código do ReplayDeck) dentro do programa | |
| 5 | Títulos GT e overlays | |
| 6 | MultiCorder, PTZ, lista, fone com o árbitro, atalhos, API vMix | |

## Código

- `app/` — o programa (C++/Qt 6): `engine` fala com o libobs; `main-window`, `input-tile`, `dialogs`, `properties-view`, `video-display` são a interface.
- `tools/patch-obs.py` — coloca `app/` dentro do código do OBS para compilar junto (mesmo libobs, Qt e dependências).
- `.github/workflows/build.yml` — compila no Windows e gera o zip portátil e o instalador (`installer/studio.iss`).

Teste local (Linux, contra o libobs instalado): `cmake -S app -B build && cmake --build build`;
`RDS_SELFTEST=/tmp/tela.png ./build/ReplayDeckStudio` roda um teste automático (cria entradas, transições, salva/abre projeto).

## Licença

GPL versão 2 ou posterior, como o OBS Studio.
