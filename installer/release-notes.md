## ReplayDeck Studio — fase 6 (controle, gravação múltipla, PTZ, plugins)

> 2.15.0: **Posição e Camadas em cada entrada, como no vMix**. ⚙ da entrada (ou duplo clique) → abas novas:
> - **Posição**: Ampliação, Zoom X, Zoom Y, Pan X, Pan Y, Girar (com ↺/↻ 90°) e Cortar X1/Y1/X2/Y2 (em pixels da imagem; o que é cortado some e o resto fica no lugar). Cada linha tem Redefinir, e há Redefinir todas. Botões rápidos: tela cheia, metade esquerda/direita, quartos e pequena no canto. Vale na hora, em todo lugar onde a entrada aparece (prévia, programa, overlay, replay).
> - **Camadas**: até 10 outras entradas por cima desta (é o mesmo que fontes dentro de uma cena no OBS). Cada camada liga/desliga e tem **Editar** com a sua posição. Arrumações prontas: tela cheia, lado a lado, lado a lado inteiras, em cima/embaixo, imagem na imagem e 4 telas. Uma entrada não pode aparecer dentro dela mesma (o programa não deixa). O som das camadas segue a entrada no ar.
> - Tudo fica salvo no projeto. Pela API/atalhos (como no vMix): SetZoom, SetZoomX/Y, SetPanX/Y, SetRotation, SetCrop (X1,Y1,X2,Y2 de 0 a 1), SetCropX1/X2/Y1/Y2, ResetPosition, SetLayer e SetMultiViewOverlay (Value = 1,PLACAR), MultiViewOverlay/On/Off, SetLayerZoom/PanX/PanY. O XML da API mostra a posição e as camadas de cada entrada.
>
> 2.14.3: **versão para Mac** (Apple Silicon: M1, M2, M3, M4). Baixe o arquivo **ReplayDeck-Studio-2.14.3-Mac.dmg** abaixo, abra e arraste o programa para Aplicativos. Na primeira vez o Mac avisa que não pode verificar o programa: Ajustes do Sistema → Privacidade e Segurança → **Abrir Mesmo Assim** (só uma vez). Funciona igual ao Windows: entradas, replay, títulos GT, transições, overlays, API do vMix/APPVMIX, gravação e transmissão (codificador VideoToolbox do Mac). Ainda não tem no Mac: câmera virtual, entrada de página web e atalhos com outro programa na frente.
>
> 2.14.2: **o APPVMIX funciona sem importar nada**. O Studio já conhece as chaves do projeto padrão do APPVMIX (FUTSALV2: PLACAR, PENAL, RESUMO PARTIDA, GOL CASA...) e, quando o APPVMIX está rodando, lê a configuração dele sozinho (porta 3000) e aprende o nome de cada chave. Toda entrada com o mesmo nome do vMix ganha a chave na hora — o nome pode ter ou não acento e extensão ("PENAL.gtzip" = "PENAL"). Se o vMix tem duas entradas com o mesmo nome (ex.: duas "GOL CASA"), a mensagem diz isso e qual chave colar na segunda.
>
> 2.14.1: **o app externo diz o que está faltando**. Em vez de "entrada 6c5149c4-… não encontrada", a mensagem diz o nome: "falta a entrada PLACAR no Studio — crie ou importe um título com o nome PLACAR", ou "o título PLACAR não tem o campo escudofora.Source; campos dele: …", ou, se a chave nunca foi importada, como importar. Se a entrada com o mesmo nome do vMix existe, o Studio já adota a chave sozinho (sem erro). Entradas → **O que o app externo pediu e não achou** mostra a lista completa (entrada, campos, funções, quantas vezes), com botão para copiar.
>
> 2.14.0: **app externo (APPVMIX e outros feitos para o vMix) controla os overlays e os títulos GT**. Testado com o próprio APPVMIX: placar, gols, escudos, cores, cronômetro, gráficos entrando e saindo nos overlays, atalhos. (1) **Chaves do vMix**: cada entrada agora tem a chave (GUID) que os apps usam. Entradas → *Importar chaves do vMix* lê o projeto do vMix (.vmix), o estado da API (api.xml) ou a config do APPVMIX (data/config.json) e dá a cada entrada com o mesmo nome a chave dela no vMix: o app funciona sem remapear nada. Também ⚙ da entrada → Chave do vMix. (2) Funções novas dos títulos GT: SetImage (arquivo ou link), SetTextColour, SetColor, SetTextVisible/On/Off, SetImageVisible/On/Off, cronômetro (SetCountdown, StartCountdown, StopCountdown, PauseCountdown, AdjustCountdown); no XML da API aparecem os textos e as imagens de cada título. (3) Mais funções do vMix: Play, Pause, PlayPause, Restart, SetPosition, NextItem, PreviousItem, LoopOn/Off, Merge, SetTransitionDuration1–4, OverlayInput1–4Off, SetVolumeFade, MasterAudio/ON/OFF, StartStopExternal, Fullscreen/On/Off, Snapshot (foto do Programa), SetDynamicInput/Value, e do replay: ReplaySetSpeed no formato do vMix (0.5 = 50%), ReplayChangeSpeed, ReplayFastBackward, ReplayLiveToggle, ReplayRecorded, ReplayJumpToNow, ReplayMarkInLive, ReplayMarkInOutLive, ReplayStopEvents, ReplayStart/StopRecording, ReplayShowHide, Replay…ToOutput, ReplaySelect…Event, ReplaySelectEvents1–20, ReplayA/BCamera1–8, com Channel=1/2. (4) Página de atalhos do controlador web do vMix (http://IP:8088/): o app lista e dispara os atalhos do Studio. (5) **Replay: o zoom volta ao normal a cada marcação nova e ao tocar eventos** (dá para desligar nas configurações do replay).
>
> 2.13.0: **PTZ** (aba PTZ): câmeras robóticas por rede (VISCA sobre IP — Sony, PTZOptics e a maioria das outras): mover segurando os botões, zoom, velocidade, foco automático, posição inicial e 12 posições gravadas. Também por atalho e pela API: PTZPreset, PTZHome e as de segurar PTZHoldUp/Down/Left/Right/ZoomIn/ZoomOut.
>
> 2.12.0: **as funções que faltavam**. (1) **API do vMix**: Stream Deck, Bitfocus Companion, apps de celular e qualquer coisa feita para o vMix controlam o Studio (HTTP porta 8088 e TCP 8099, com tally); Configurações → Controle remoto. (2) **Scripts** (Configurações → Scripts): funções com esperas e repetições, também na forma do vMix (API.Function, Sleep, Do/Loop, For/Next); disparados por atalho (ScriptStart) ou pela API. (3) **Filtros** em cada entrada (⚙ → Filtros): chroma key, correção de cor, LUT, recorte, nitidez; e no som: supressão de ruído, compressor, ganho, gate. Salvos no projeto. (4) **Plugins do OBS** (Configurações → Plugins do OBS): os plugins instalados para o OBS 30+ (entradas, filtros, transições) funcionam aqui; o que derrubar o programa é desligado sozinho. (5) **Interagir** com a entrada web (⚙ → Interagir): mouse e teclado na página, como no OBS. (6) **MultiCorder**: grava cada câmera separada, limpa, com o som do Master. (7) **Playlist** (aba Lista): entradas ao ar em sequência, vídeos até o fim, com a transição escolhida. (8) Novas funções para atalhos/API: ActiveInput, SetFader, SetText e TitleBeginAnimation (títulos GT), ScriptStart/Stop, Start/StopMultiCorder, Start/StopPlayList, NextPlayListEntry.
>
> 2.11.0: **otimização**. (1) **Placa de vídeo dedicada sozinha**: em notebook com duas placas (Intel + NVIDIA/AMD) o Windows entregava a Intel; o programa todo rodava nela e o replay não conseguia usar o NVENC direto da placa (copiava cada quadro para a memória: CPU e disco altos). Agora escolhe a NVIDIA/AMD; dá para trocar em Configurações → Vídeo → Placa de vídeo. (2) O Windows 11 não "economiza energia" mais no programa (era o que engasgava com a janela atrás de outra ou no notebook), prioridade acima do normal, relógio de 1 ms e a tela/PC não dormem no meio do jogo. (3) Telas de vídeo que não aparecem (aba escondida, entrada rolada para fora, janela minimizada) param de desenhar; medidores e painéis escondidos param de atualizar. (4) Atalhos globais numa thread própria: o teclado do PC nunca atrasa. (5) Programa compilado com otimização completa. (6) O projeto se salva sozinho a cada minuto se algo mudou. (7) No log: uma linha de desempenho por minuto e aviso se a interface ficar parada (para achar a causa de qualquer travada). Barra de baixo mostra quadros perdidos pela codificação e a placa em uso.
>
> 2.10.0: **Stinger GT**: um título GT do vMix (.gtzip) funciona como stinger, igual a um vídeo. Aba Transições → ＋ Stinger GT (título): escolha um título já importado (os textos e imagens seguem a entrada) ou um .gtzip solto. A animação de **entrada** do título cobre a tela, a imagem troca por baixo e a animação de **saída** revela a entrada nova. "Cobrindo" ajusta quanto tempo o título fica na tela inteira; em Opções dá para fixar o momento da troca. Vale nos botões do meio, nos atalhos (Stinger1…4), no replay e por cima dos overlays.
>
> 2.9.0: **aba Transições, com stinger**. ＋ Stinger: escolha o vídeo da vinheta (.webm/.mov com transparência, ou com máscara/track matte); o ponto de troca já vem no meio do vídeo e se ajusta no cartão ("Troca em"). Também: Deslizar, Varrer, Luma wipe, Fade para cor e Fade com outra duração. Cada transição vira um **botão no meio da tela**, abaixo do Fade, e pode ir para atalhos e scripts (Stinger1…4, Transition1…4). O stinger passa **por cima dos overlays** (placar, títulos GT) — dá para desligar na aba. O replay também enxerga essas transições (entrar/sair do replay com stinger). Tudo fica salvo no projeto.
> **Atalhos de duas teclas**: segure Ctrl, Shift ou Alt e aperte a outra (Ctrl+7, Shift+F1, Ctrl+Num 7). Enquanto segura, o campo mostra "Ctrl + …". Ctrl+7 e o 7 sozinho são atalhos diferentes.
>
> 2.8.0: **títulos GT do vMix (.gtzip) dentro do programa**. Aba **Títulos GT** (ou Títulos → Importar): cada título importado vira uma **entrada**, salva no projeto. ▶ ENTRADA põe o título no overlay escolhido (O1 a O4, por cima do programa) com a animação de entrada; ■ SAÍDA (ou o botão do overlay, o atalho, a função) faz a **animação de saída** e só depois tira da tela. Com "Ent." o título só anima e você usa a entrada na Prévia/Programa como qualquer outra. Menu ⋮: editar o título (textos, imagens, cores, layout, listas CSV/TXT/JSON), renomear, duplicar com outros dados (ex.: escalação casa e fora), propriedades e páginas. Também em Adicionar entrada → "Título GT".
>
> 2.7.1: o instalador não pede mais para fechar o Chrome, o Claude, o Zoom etc. Eles deixam o arquivo da câmera virtual aberto (listam as câmeras do Windows); agora o instalador só olha o próprio programa e só troca o arquivo da câmera quando ele mudar de versão.
>
> 2.7.0: **atalhos de segurar** para o replay (como num controlador): enquanto a tecla está apertada a ação continua e para ao soltar — ré, play, avanço/ré rápidos, zoom e mover o zoom; **quadro a quadro**: um toque anda 1 quadro, segurando vai andando. Botão novo **"Atalhos do replay (teclado numérico)"**: Num 7 Mark In · Num 9 Mark Out · Num 4 ré (segure) · Num 6 play (segure) · Num 5 play/pausa · Num 1/Num 3 quadro a quadro · Num 8/Num 2 zoom (segure) · Num 0 último lance no ar · Num Enter põe no ar · Num , ao vivo · Num +/− velocidade · Num * evento de 10 s · Num / zoom inteiro · Shift+setas move o zoom.
>
> 2.6.1: **atalhos pela tecla física**. Clique na tecla e aperte a do teclado: vale a tecla em si, não o caractere — o 7 do teclado numérico (Num 7) é diferente do 7 de cima, e funciona com o Num Lock ligado ou desligado e em qualquer layout. Nova opção: **atalhos funcionando com outro programa na frente** (Configurações → Atalhos).
>
> 2.6.0: **replay muito mais leve**. No seu PC o replay não conseguia abrir a NVIDIA pelo FFmpeg e caía para MJPEG no processador (CPU alta e disco a 50 MB/s). Agora cada câmera do replay é comprimida pelo codificador NVIDIA do próprio motor, direto na placa de vídeo, sem copiar os quadros para a memória — o mesmo que a transmissão já usava. Se não der, tenta a NVIDIA pela memória e só depois o caminho antigo. Os erros do FFmpeg agora aparecem no log.
>
> 2.5.1: Ajuda → **Abrir pasta de logs** não fazia nada em alguns PCs (a associação de pastas do Windows quebrada). Agora abre o Explorer direto, já com o log mais novo selecionado. Novos: **Abrir o log atual** (Bloco de Notas) e **Copiar o log atual para a Área de Trabalho**.
>
> 2.5.0:
> - **Funções com os nomes do vMix** (Cut, Fade, QuickPlay, OverlayInput1In, SetVolume, ReplayMarkIn, ReplayPlayLastEvent… 60+) — base de atalhos, scripts e da API.
> - **Atalhos de teclado para tudo** (Configurações → Atalhos): tecla (com Ctrl/Shift/Alt) → função, entrada e valor. Botão "Atalhos sugeridos" (Espaço = Corte, Enter = Fade, F1-F4 = overlays, 1-9 = Prévia, I/O = Mark In/Out, P = último lance…). Não disparam enquanto você digita.
> - **Multitransmissão**: vários destinos ao mesmo tempo (YouTube, Facebook, Instagram, TikTok, Twitch, Kick, personalizado). Cada formato é comprimido uma vez só, mesmo com vários destinos.
> - **Transmissão vertical 9:16** (Reels, TikTok, Shorts): o Programa inteiro (com overlays) recortado ao centro — dá para puxar para os lados — ou encaixado com faixas; 1080×1920 ou 720×1280. Veja antes em Externo → Vertical 9:16. Só gasta processamento enquanto um destino vertical está no ar.
>
> 2.4.1:
> - **Câmera virtual**: o motor só criava a câmera se ela já estivesse instalada no Windows quando o programa abre, então aparecia "não tem câmera virtual". Agora ela existe sempre, e na primeira vez que você liga, o programa instala (64 e 32 bits, um pedido de permissão só).
> - **Externo do Programa no Windows**: a tela de vídeo agora se religa sozinha quando o Windows recria a janela (ao abrir, ao trocar entre janela e tela cheia).
> - Replay: parar a gravação depois de uma câmera falhar ao ligar podia travar o programa.
>
> 2.4.0:
> - **Câmera virtual** (botão "Câmera virtual ▾"): compartilha o Programa ou **uma entrada** como webcam para Zoom, navegador, WhatsApp, Teams. Aparece como **"ReplayDeck Studio"**. Na primeira vez o Windows pede permissão de administrador para instalar a câmera (uma vez só). Dá para trocar a entrada sem desligar.
> - **Externo em janela** agora abre numa janela normal (com título: mover, mudar o tamanho, fechar). Antes abria sem bordas e sem cursor e parecia que a tela tinha travado. Na tela cheia: Esc, duplo clique ou botão direito → Fechar; e o programa avisa antes de cobrir a própria tela.
> - Corrigido um travamento ao fechar o programa depois de encerrar uma transmissão.
>
> 2.3.1:
> - **Botão direito minimiza na hora** (como no vMix); na barra, botão direito (ou duplo clique) expande.
> - **Arrastar para mudar a ordem**: segure no cabeçalho (ou na barra) e solte onde quiser; a linha azul mostra onde ela entra.
> - As opções (abrir, cor, renomear, remover) ficam na engrenagem ⚙. "Desativar" saiu do menu: fica na janela da entrada.
>
> 2.3.0: visual mais claro e com um pouco de cor:
> - **Minimizar em barra** (botão direito → Minimizar): a entrada vira uma barra estreita com o nome na vertical, como no vMix. Clique = Prévia; duplo clique = expandir.
> - **Cor da entrada** (botão direito → Cor): azul, laranja, roxo, ciano, amarelo, rosa, marrom, cinza — para separar câmeras, gráficos, celulares.
> - Prévia e Programa com cabeçalho inteiro **verde** e **vermelho**; a entrada no ar mostra a sua cor numa faixa embaixo.
> - Overlay ligado em **âmbar** (não confunde com o vermelho do Programa).
> - Fundos um pouco mais claros, com mais separação entre painéis, quadros e botões.
>
> 2.2.1: nas telas de vídeo (Prévia, Programa, miniaturas, janela da entrada) a sobra ao redor da imagem agora tem a cor da página; preto só onde é a imagem que vai ao ar.
>
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
