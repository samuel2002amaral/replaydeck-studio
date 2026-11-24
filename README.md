# ReplayDeck Studio

Programa de transmissão ao vivo para esportes, com **interface própria no estilo vMix** e o **motor de vídeo do OBS (libobs)** por baixo.
Uma tela só; replay, títulos GT, áudio e saídas ficam dentro dela.

Desenho aprovado das telas: https://claude.ai/artifact/FEu9oqLV7BL5HjBqKL7Deo

## Fases

| Fase | Conteúdo | Situação |
|---|---|---|
| 2 | Base: entradas, prévia/programa, transições, overlays, janela da entrada, projeto | v2.0.11 |
| 3 | Mixer de áudio, transmissão, gravação, configurações, saída externa | **v2.2.0** (falta o fone com o árbitro) |
| 4 | Replay instantâneo (código do ReplayDeck) dentro do programa | v2.1.0 |
| 5 | Títulos GT e overlays | próxima |
| 6 | MultiCorder, PTZ, lista, fone com o árbitro, atalhos, API vMix | |

## Código

- `app/` — o programa (C++/Qt 6): `engine` fala com o libobs; `main-window`, `input-tile`, `dialogs`, `properties-view`, `video-display` são a interface.
- `app/replay/` — o replay, vindo do plugin ReplayDeck (gravação, câmera lenta, eventos, exportação e o painel).
  Ele conversava com a janela do OBS; aqui `replay/shim/obs-frontend-api.h` declara essas mesmas funções e
  `src/replay-host.cpp` responde com o modelo do Studio (cenas = entradas, Programa, Prévia, Corte/Fade).
- `tools/patch-obs.py` — ajustes no motor (código do OBS). O motor é compilado uma vez e guardado no cache do GitHub.
- `.github/workflows/build.yml` — usa o motor guardado e compila só `app/` (rápido); recompila o motor só quando `patch-obs.py`, a versão do OBS ou `ENGINE_REV` mudam. Gera o zip portátil e o instalador (`installer/studio.iss`).

Teste local (Linux, contra o libobs instalado): `cmake -S app -B build && cmake --build build`;
`RDS_SELFTEST=/tmp/tela.png ./build/ReplayDeckStudio` roda um teste automático (cria entradas, transições, salva/abre projeto);
`RDS_SELFTEST_REPLAY=/tmp/replay.png` grava duas entradas, marca um evento, põe o replay no ar e confere a volta;
`RDS_SELFTEST_OUTPUTS=<pasta> RDS_SELFTEST_RTMP=rtmp://… RDS_SELFTEST_TONE=<wav>` grava, transmite e muda para 720p.

## Licença

GPL versão 2 ou posterior, como o OBS Studio.
