# ReplayDeck Studio

Sistema de transmissão completo para esportes, baseado no **OBS Studio 33.0.1**, com tudo embutido — sem instalar plugin:

| Embutido | O que faz |
|---|---|
| **ReplayDeck** | Replay multicâmera, câmera lenta, zoom, canais A/B (VAR), eventos, títulos GT, API compatível com vMix |
| **Placar de Futsal** (`plugins/futsal-placar`) | Placar, cronômetro regressivo, faltas acumuladas, pedidos de tempo (1 min), prorrogação, pênaltis, atalhos e controle via obs-websocket |
| Move Transition | Transições avançadas |
| Source Record | Gravar cada câmera/fonte separada |
| Advanced Scene Switcher | Automação de cenas |
| Multiple RTMP outputs | Multistream (YouTube + Facebook + Twitch…) |

Roda em **modo portátil**: configurações ficam na pasta do programa, sem mexer no OBS normal.

## Download

Na página [Releases](../../releases/latest): `ReplayDeck-Studio-<versão>-Instalador.exe` ou `-Portatil.zip`.
Cada build também fica em **Actions → Build → Artifacts**.

## Como é montado

O GitHub Actions (`.github/workflows/build.yml`):

1. baixa o código oficial do OBS (`versions.env`) e do [ReplayDeck](https://github.com/samuel2002amaral/replaydeck);
2. `tools/patch-obs.py` coloca o ReplayDeck e o Placar dentro da árvore do OBS e troca o nome na barra de título;
3. compila o OBS inteiro junto com os módulos embutidos;
4. `tools/add-third-party.ps1` adiciona os plugins de terceiros listados em `third-party.json` (releases oficiais);
5. gera o zip portátil e o instalador (`installer/studio.iss`).

Publicar versão: commit com `[release]` na mensagem, ou *Run workflow* com "oficial".

## Controle remoto do placar (obs-websocket)

`CallVendorRequest` com `vendorName: "futsal-placar"`:

- `Acao` — `{"acao": "gol_casa"}`; ações: `gol_casa`, `gol_visitante`, `menos_gol_casa`, `menos_gol_visitante`, `falta_casa`, `falta_visitante`, `menos_falta_casa`, `menos_falta_visitante`, `tempo_casa`, `tempo_visitante`, `relogio`, `relogio_iniciar`, `relogio_pausar`, `relogio_menos_1s`, `relogio_mais_1s`, `proximo_periodo`, `zerar_periodo`, `inverter`
- `Definir` — `{"casa": {"nome": "...", "sigla": "...", "gols": 2, "faltas": 3}, "visitante": {...}, "periodo": 1, "relogio_ms": 600000, "competicao": "..."}`
- `Estado` — devolve placar, faltas, período e relógio

## Licença

GPL versão 2 ou posterior, como o OBS Studio. "OBS" e "OBS Studio" são marcas do projeto OBS; este é um build independente.
