## ReplayDeck Studio — fase 5 (títulos GT)

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
