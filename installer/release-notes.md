## ReplayDeck Studio — fase 3 (áudio, gravar, transmitir, saída externa)

> 2.2.0:
> - **Mixer de áudio** (aba ao lado do replay): uma faixa por entrada com som — medidor, volume em dB, **On**, **Auto** (só toca quando a entrada está no ar; padrão das câmeras e vídeos) e **Fone**. Microfone e áudio do computador tocam sempre, mesmo fora do Programa. Faixa **MASTER** com o medidor do que sai.
> - **Gravar**: grava o Programa em MP4 (não corrompe se cair a energia), MKV ou MOV; usa a placa de vídeo (NVIDIA/AMD/Intel) e cai para o processador se precisar.
> - **Transmitir**: YouTube, Facebook, Twitch, Kick ou servidor próprio; reconecta sozinho se a internet cair; quadros perdidos na barra de baixo.
> - **Externo**: Programa, Prévia, Replay A/B (monitor do VAR) ou qualquer entrada em tela cheia noutro monitor/TV. Esc ou duplo clique fecha.
> - **Configurações**: resolução (1080p, 720p, 4K, vertical) e quadros por segundo, gravação, transmissão e o fone.
> - Inclui a 2.1.0: replay dentro do programa.

Programa próprio, com a tela aprovada (estilo vMix). Do OBS fica só o motor de vídeo por baixo.

**Já funciona nesta versão**
- Tela única: Prévia, Programa, transições (Quick Play, Cortar, Fade, FTB e T-bar) e overlays 1 a 4
- Entradas com imagem ao vivo: clique = prévia; botões 1–4 (overlay), GO, Cut, Áudio e Loop
- Engrenagem (ou duplo clique) abre a entrada ao vivo numa janela, com todas as opções, sem mexer na prévia nem no programa
- Adicionar entrada (escolhe o tipo, a imagem aparece sozinha, OK para colocar no projeto): câmera/placa de captura, DeckLink, vídeo/link/celular (SRT/RTMP), imagem, página web, texto, cor, captura de tela/janela/jogo, microfone
- Projeto salvo sozinho ao fechar; Arquivo › Abrir / Salvar como

**Ainda não (aparece no lugar, avisando a fase)**
- Fase 3: mixer de áudio, transmissão, gravação, configurações, saída externa (monitor do VAR)
- Fase 4: replay instantâneo dentro do painel da direita
- Fase 5: títulos GT · Fase 6: MultiCorder, PTZ, lista, fone com o árbitro

**Instalar:** baixe o `Instalador`, abra (Mais informações › Executar assim mesmo) e use o atalho *ReplayDeck Studio*.
Se algo der errado: Ajuda › Abrir pasta de logs e mande o arquivo mais recente.
