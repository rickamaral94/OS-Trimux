# TriMux 0.5.2 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidade:

* **Grade de capas:** a lista de jogos pode ser vista como uma grade de 10
  capas por tela (5 × 2). Mude em *START › Ver jogos como* na lista de jogos,
  ou em *Configurações › Aparência*.
  * As setas andam pela grade, e L1/R1 trocam de página.
  * O jogo selecionado fica em destaque e as outras capas um pouco
    escurecidas. O nome e a plataforma dele aparecem embaixo.
  * Jogos sem capa aparecem como um cartão com a cor e a sigla da
    plataforma.
  * As capas carregam aos poucos (duas por quadro), para a navegação não
    travar enquanto as imagens abrem.
  * A lista continua sendo o padrão.
* **Acabamento:** opções Ligado/Desligado viram chaves deslizantes, as barras
  de rolagem ficaram arredondadas (e aparecem também nos menus), os títulos
  de seção ganharam uma linha de destaque e a bateria do cabeçalho tem cantos
  arredondados.
* **Informações do aparelho** em cartões, com barras de bateria, temperatura
  (verde, amarela ou vermelha), memória e espaço no cartão.
* **Teclado na tela:** a tecla escolhida "levanta" com sombra e brilho, e a
  caixa de texto ganhou borda de destaque.
* O ranking de popularidade aparece como uma etiqueta no painel do jogo.

# TriMux 0.5.1 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidade: **visual
renovado**, com o mesmo jeito de usar:

* **Fundo com a cor da plataforma:** ao escolher uma plataforma ou um jogo,
  o fundo ganha um tom suave da cor dela.
* **Seleção animada:** o destaque desliza entre os itens, com sombra e
  brilho. Pode ser desligado em *Configurações › Aparência › Animações*.
* **Painéis com faixa colorida e sombra.**
  * Na tela inicial, a plataforma mostra o **último jogo jogado com a
    capa**.
  * *Continuar* mostra a capa do jogo.
  * *Recentes* e *Todos os jogos* mostram as capas dos últimos jogos
    abertos.
* **Cabeçalho:** o nome TriMux em dois tons e um **ícone do Wi-Fi** (aceso
  quando está conectado).
* Diálogos, avisos e listas vazias com acabamento novo (sombras, cantos
  arredondados, mensagem centralizada).
* **Ports do sistema oficial da TrimUI:** pastas com `config.json` e
  `launch.sh`, copiadas do cartão original, agora aparecem em Ports. Funciona
  tanto na pasta `Ports` da raiz do cartão (onde o sistema oficial guarda os
  ports) quanto em `Roms/PORTS/<jogo>/`. O ícone da pasta vira a capa
  ([PORTS.md](PORTS.md)).
* Nenhuma imagem nova vai para o cartão: tudo é desenhado pelo menu. As
  capturas da documentação foram refeitas no computador; não são fotos do
  aparelho.

# TriMux 0.5.0 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidades:

* **PlayStation Portable** (pasta `Roms/PSP`): emulador PPSSPP 1.19.3
  (núcleo libretro, GLES), compilado dos fontes oficiais.
  * **Não precisa de BIOS.** As fontes e os arquivos de sistema do PSP vêm do
    próprio PPSSPP e ficam em `TriMux/retroarch/system/PPSSPP`, atualizados
    junto com o TriMux.
  * Formatos `.iso`, `.cso` (ocupa menos espaço), `.chd` e `.pbp`.
  * Em *Configurações › Emuladores › PlayStation Portable* há a **Resolução
    interna**: Original (480×272) ou 2x (960×544, mais nítida e mais pesada).
  * Ordem por popularidade com os 24 PSP mais vendidos (Wikipedia).
  * Os saves do PSP ficam em `Saves/PSP`.
* **Capas:**
  * **Escolha de plataformas:** em *Capas dos jogos › Plataformas para
    baixar*, desligue as plataformas que não quer agora. O download passa por
    cima delas.
  * **Depois de suspender:** ao acordar o aparelho, o download espera o
    Wi-Fi voltar (até 15 min), liga o Wi-Fi de novo se o firmware tiver
    desligado e continua da mesma capa, em vez de parar com erro de rede.
    Enquanto o aparelho está suspenso nada roda, então o download só anda com
    ele acordado.
* Lista de popularidade do Mega Drive corrigida: 4 jogos que se perdiam na
  leitura da tabela voltaram (23 no total).

**Ferramentas próprias** na aba Aplicativos ([FERRAMENTAS.md](FERRAMENTAS.md)):

* **Estatísticas de jogo:** tempo total, sessões, último jogo, os 10 mais
  jogados e o tempo por plataforma, a partir do tempo de jogo que o TriMux já
  conta desde a 0.4.7.
* **Jogo surpresa:** sorteia um jogo, de preferência um que você nunca abriu,
  e mostra ele na lista, pronto para jogar.
* **Gerenciador de arquivos:** navega pelas pastas do cartão com tamanho e
  data e apaga arquivos com confirmação. As pastas do TriMux e do sistema
  oficial são somente leitura.
* **Arquivos pelo navegador:** com o Wi-Fi conectado, os arquivos do cartão
  aparecem no navegador do computador ou do celular, em
  `http://<endereço do aparelho>:8080`. Dá para baixar, enviar jogos e BIOS
  (arrastando para a página, com barra de progresso), criar pastas e apagar
  com confirmação, sem tirar o cartão.
  * Usa a BusyBox httpd do próprio firmware: nada é instalado.
  * Só funciona com a janela aberta no aparelho, e não tem senha, como o FTP.
  * Envios incompletos nunca ficam no cartão.
* **Limpeza do cartão:** acha e apaga, depois de confirmar, os arquivos que
  o macOS e o Windows criam ao copiar (`._arquivo`, `.DS_Store`,
  `Thumbs.db`...), registros antigos e restos de instalações interrompidas.
  Jogos, saves, BIOS, capas e configurações nunca são tocados.
* Enquanto a janela do FTP ou dos arquivos pelo navegador estiver aberta, o
  aparelho não desliga mais sozinho por inatividade no meio de uma cópia.
* Por SSH: `trimuxctl stats`, `trimuxctl clean scan|run` e
  `trimuxctl web start|stop|status`.

# TriMux 0.4.9 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidades:

* **Nintendo 64** (pasta `Roms/N64`): emulador Mupen64Plus-Next, com
  recompilador para ARM64 e vídeo GLideN64 em GLES 3.
  * Jogos leves devem rodar bem; os mais pesados podem ficar lentos neste
    processador.
  * Em *Configurações › Emuladores › Nintendo 64* há a **Resolução interna**:
    Padrão 640×480, Original (mais leve) ou 2x.
* **Dreamcast** (pasta `Roms/DC`): emulador Flycast, em GLES 3.
  * Funciona sem BIOS, mas `Bios/dc/dc_boot.bin` e `Bios/dc/dc_flash.bin`
    (do seu console) melhoram a compatibilidade.
  * Formatos `.chd`, `.cdi`, `.gdi`, `.cue` e `.m3u`.
  * **Resolução interna**: 640×480, 1,5x ou 2x.
* **Ordem por popularidade** também no N64 (53 jogos, da lista de vendas da
  Wikipedia). O Dreamcast não tem lista publicada e fica sem ranking.
* Os dois emuladores são compilados pelo TriMux a partir dos fontes
  oficiais (GPL), nos commits de `sources/sources.lock`. Nada do sistema
  oficial é copiado.
* **Capas: sem ciclo de reinício.** Se o aparelho reiniciar no meio do
  download de capas, o download automático não recomeça sozinho no boot
  seguinte (antes, com capas automáticas ligadas, ele recomeçava a cada boot
  e podia reiniciar de novo). O estado mostra *Não terminou: o aparelho
  reiniciou*; *Baixar capas agora* continua de onde parou
  ([CAPAS.md](CAPAS.md)).
  * A cada 50 capas o registro grava o andamento, a memória livre e a
    temperatura, direto no cartão, para mostrar onde e em que condições o
    reinício aconteceu.
  * Pausa curta entre downloads, para um ritmo mais leve no Wi-Fi e no
    cartão.
  * A causa dos reinícios ainda não foi confirmada no aparelho. Nenhum
    vazamento de memória foi encontrado no TriMux; a suspeita é o driver de
    Wi-Fi ou o kernel sob tráfego contínuo, ou temperatura.

# TriMux 0.4.8 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidades:

* **PortMaster integrado aos Ports** ([PORTS.md](PORTS.md)):
  * Instale em *Aplicativos › Loja de aplicativos › PortMaster*.
  * Em **Ports** aparece a entrada **Portmaster**, que abre a loja de ports
    (Stardew Valley, Celeste, Half-Life e centenas de outros).
  * Os ports instalados entram na mesma lista de Ports.
  * Usa o pacote MinUI PortMaster (MIT), que traz bash, Python e as
    bibliotecas que o firmware não tem. Ele é baixado quando você pede e não
    vai na imagem.
* **Loja de aplicativos** ([LOJA.md](LOJA.md)): na aba Aplicativos, uma lista
  escolhida a dedo com instalação em um toque pelo Wi-Fi. Começa com
  PortMaster e **Grout** (cliente do RomM).
  * Cada app tem versão fixa e SHA-256 conferido antes de gravar.
  * A tela mostra a licença, a origem e se o app ajusta algo do sistema
    enquanto está aberto.
  * Só grava em `Apps/`, `Emus/` e `Roms/PORTS/` no cartão. Remover apaga só
    os arquivos do app.
* A aba **Aplicativos** aparece sempre na tela inicial, por causa da loja.
* Painel de descrição dos menus: textos longos não são mais cortados.

# TriMux 0.4.7 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidade:

* **Ordem dos jogos:** em *Configurações › Biblioteca › Ordem dos jogos* ou
  pelo **START** na lista. Três opções, que valem para todas as plataformas:
  * **Nome (A–Z):** como antes.
  * **Popularidade:** os jogos mais vendidos da plataforma primeiro. Os que
    não estão no ranking vêm depois, em ordem alfabética. O painel mostra a
    posição, por exemplo "Nº 3 entre os mais vendidos do NES". Há ranking
    para NES, SNES, Game Boy e Game Boy Color, GBA, Mega Drive e
    PlayStation. As listas vêm das tabelas de vendas da Wikipedia (CC BY-SA
    4.0, com a fonte no topo de cada arquivo em
    `TriMux/share/popular/`). O jogo é reconhecido pelo nome do arquivo, no
    padrão No-Intro. Arquivos com outros nomes ficam sem ranking.
  * **Mais jogados:** os jogos em que você passou mais tempo primeiro. O
    TriMux passou a contar, no aparelho, quantas vezes e por quanto tempo
    cada jogo foi jogado (sessões acima de 10 s), em
    `TriMuxData/state/plays.ini`. O painel mostra, por exemplo, "Jogado 5
    vezes · 3 h 20 min".

# TriMux 0.4.6 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidade:

* **Imagem dos jogos** ([IMAGEM.md](IMAGEM.md)): em *Configurações ›
  Emuladores › <plataforma>* (ou **START › Imagem da plataforma** num jogo),
  cada plataforma ganhou opções explicadas na própria tela:
  * **Formato da tela:** Original, Pixels perfeitos ou Tela cheia.
  * **Visual:** Nítido, Suave, Pixel suave, Tela de LCD ou TV antiga. São
    três shaders leves, feitos para GPU de celular, e a tela sugere o melhor
    para cada plataforma.
  * **Resolução interna:** 2x no PlayStation (modelos 3D mais nítidos, mais
    pesado), até 960×600 no DOOM e até 1024×768 no Quake. Nos consoles 2D a
    tela explica que quem melhora a imagem é o Visual.
  * **Cores do aparelho original** e **Rastro de tela LCD** no Game Boy, GBC
    e GBA. O rastro também está no Game Gear e no Lynx.
  * **Texturas HD no NES:** o FCEUmm lê pacotes Mesen HD Pack em
    `Bios/HdPacks/<jogo>/`. A tela mostra quantos pacotes foram encontrados.
    Os pacotes não vêm com o TriMux.
  * **Imagem de todas as plataformas** muda formato e visual de todas de uma
    vez.
  * Enquanto você não escolhe nada, nada muda: vale o que já estava no
    RetroArch.

# TriMux 0.4.5 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Correção da 0.4.4:

* **Nomes em japonês, chinês e coreano apareciam como `????`:** a fonte do
  menu (DejaVu Sans) não tem esses caracteres. Agora, quando falta um
  caractere, o TriMux usa a fonte do próprio firmware (`full.ttf`, Source Han
  Sans, com japonês, chinês e coreano), no mesmo tamanho do resto do texto.
  Ela só é aberta quando aparece o primeiro nome desses e é lida direto da
  memória interna, sem cópia e sem gravar nada lá. Os arquivos dos jogos não
  mudam.

# TriMux 0.4.4 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Correção da 0.4.3 e um ajuste novo nas luzes:

* **Capas sumindo depois de reiniciar:** o firmware da TrimUI verifica o
  cartão a cada boot (`fsck.fat -p`). Arquivos que ainda não estavam
  gravados por completo no cartão podem ser apagados nessa verificação, e o
  reinício da atualização costumava ser o primeiro depois de baixar as capas.
  Agora cada capa é gravada até o fim no cartão antes de valer (`fsync` e
  renomeação). Antes de reiniciar, desligar ou abrir o sistema oficial, o
  TriMux para o download de capas e descarrega tudo para o cartão. A
  atualização pelo menu nunca apagou capas, saves ou jogos; agora um teste
  automático garante isso a cada versão.
* Capas perdidas antes desta versão precisam ser baixadas de novo:
  *Configurações › Biblioteca › Capas dos jogos › Baixar capas agora*.
* **Todas as luzes de uma vez:** em *Configurações › LEDs*, a nova entrada
  *Todas as luzes* muda cor, brilho, efeito ou liga/desliga em todas as zonas
  juntas. Depois dá para ajustar cada zona separadamente, como antes.

# TriMux 0.4.3 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Novidades e correções da
0.4.2:

* **LEDs com controle total no menu:** *Configurações › LEDs* ganhou
  **Luzes: Acesas/Apagadas**, que apaga ou acende tudo na hora e mantém assim
  no menu e nos jogos, igual ao atalho do F1/F2. A antiga opção "Controlar
  LEDs" virou **Cores e efeitos: TriMux / Padrão da TrimUI** e explica quando
  o firmware reaplica o padrão dele. A mesma chave também está no menu rápido.
* **Indicador de volume e brilho:** no menu, ao usar os botões de volume ou
  MENU + volume, aparece na tela o nível atual (escala de 0 a 20 no volume) e
  se está subindo ou descendo. Nos jogos ainda não há indicador.
* **Data e hora** (*Configurações › Sistema › Data e hora*): fuso horário
  (Brasil e outros), ajuste automático pela internet ao ligar, botão para
  acertar na hora e ajuste manual de dia, mês, ano, hora e minuto. O relógio
  do aparelho é gravado como o próprio firmware faz (`date` e `hwclock -w -u`).
  O fuso vale para o TriMux e não é gravado na memória interna.
* **Aplicativos:** nova entrada na tela inicial, separada dos jogos. Ela lista
  apps no formato da TrimUI (pasta com `config.json` e `launch.sh`) da pasta
  `Apps/` do cartão, os instalados na memória interna e os do próprio sistema
  (Música, Fotos, Leitor, Player, Moonlight). Ficam de fora o formatador do
  cartão, o modo USB e o editor de teclas FN.

# TriMux 0.4.2 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Correções da 0.4.1:

* **LEDs voltavam ao padrão da TrimUI:** o serviço `keymon` do firmware, que o
  TriMux mantém para volume, brilho e botão POWER, reaplica as opções de LED
  do sistema oficial sempre que o arquivo de configurações dele muda. Ele
  próprio regrava esse arquivo a cada ajuste de volume. Agora, com
  "Controlar LEDs" ligado, o TriMux confere os LEDs a cada 2 s e põe de volta
  a sua escolha, tanto no menu quanto nos jogos. Só reescreve o que mudou, sem
  reiniciar efeitos. Apagar os LEDs também desliga a chave geral do firmware,
  e os LEDs ficam escuros de verdade. Com bateria abaixo de 10% e sem
  carregador, o aviso vermelho do firmware tem prioridade.
* **2,0 GHz pela chave lateral:** com a chave ligada, o perfil de energia
  aparece como "Desempenho máximo (2,0 GHz)" e fica travado no menu, no menu
  rápido e no atalho F1/F2. Ao desligar a chave, a escolha volta a ficar
  livre.

# TriMux 0.4.1 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Correções da 0.4.0:

* **Cartão inteiro no primeiro boot:** quem grava a imagem não precisa mais
  expandir a partição à mão. No primeiro boot o TriMux passa a usar o cartão
  inteiro (até 1 TiB) e reinicia uma vez; depois, o menu mostra o espaço
  total. Cartões preparados pelo computador nunca são alterados.
* **"Expandir partição" indisponível:** a opção recusava todo cartão em uso,
  porque o Linux marca o cartão montado como "em uso" no setor de boot
  principal e o TriMux comparava esse setor com a cópia de reserva. Corrigido.
  O pedido pelo menu também falhava ao deixar o cartão somente leitura e,
  agora, volta ao menu com o motivo no log quando não dá para expandir.
* **Tela de início do TriMux** com logotipo, versão e situação, no lugar do
  texto "Carregando…". Ela aparece logo que o menu abre e enquanto os jogos
  são indexados. O logotipo da TrimUI ao ligar continua: ele vem da memória
  interna, que o TriMux não altera.
* **Controle dos LEDs:** as cores não acendiam ou apagavam depois de um
  segundo. O firmware deixa no boot uma repetição só para cada efeito, e a
  chave geral de LEDs pode estar desligada pela opção do sistema oficial. O
  TriMux agora liga a chave geral e desliga as animações por quadros quando
  você acende uma zona. Também usa 30000 repetições, o mesmo número que o
  firmware usa, e aplica suas escolhas de novo 5 s depois do boot, depois
  que o serviço da TrimUI aplicou as dele. Falhas de escrita vão para o log.
* O `update.zip` traz as pastas `Roms/<plataforma>` vazias e os arquivos do
  TriMux em `Bios/` (inclusive o `prboom.wad` do DOOM), para quem prepara o
  cartão pelo computador.

Arquivos desta versão: `TriMux-0.4.1-brickpro.img.xz` (Rufus/balenaEtcher),
`TriMux-0.4.1-update.zip`, `TriMux-0.4.1-update.tar.gz` (atualização online)
e `TriMux-0.4.1-brickpro.sha256`.

# TriMux 0.4.0 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Tudo das versões anteriores
continua valendo (listas abaixo), mais:

* Atualização online ([ATUALIZACAO.md](ATUALIZACAO.md)): em *Configurações ›
  Sistema › Atualização*, procura no GitHub e instala versões novas pelo
  Wi-Fi, sem gravar o cartão de novo. O pacote é conferido por SHA-256 antes
  de qualquer mudança e só as pastas `TriMux` e `trimui` são trocadas. A
  versão anterior fica guardada: dá para voltar pelo menu, e ela volta
  sozinha se a nova não chegar ao menu em 3 tentativas. A busca ao ligar é
  opcional, e nada é instalado sem confirmação.
* Esta é a primeira versão com o atualizador. Para chegar nela vindo da 0.3.0
  ou anterior, use uma vez o `update.zip` ([INSTALACAO.md](INSTALACAO.md),
  seção 4).

Arquivos desta versão: `TriMux-0.4.0-brickpro.img.xz` (Rufus/balenaEtcher),
`TriMux-0.4.0-update.zip` (atualizar pelo computador, sem perder saves),
`TriMux-0.4.0-update.tar.gz` (usado pela atualização online) e
`TriMux-0.4.0-brickpro.sha256`.

# TriMux 0.3.0 (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Tudo das versões anteriores
continua valendo (listas abaixo), mais:

* Capas dos jogos ([CAPAS.md](CAPAS.md)): download pelo Wi-Fi a partir do
  libretro-thumbnails (gratuito, sem conta), manual ou automático para jogos
  novos, em segundo plano e pausando durante os jogos; capa no painel da
  lista; arcade reconhecido pelos títulos do FinalBurn Neo; imagens em
  `Imgs/`, a mesma pasta do sistema oficial.

Arquivos desta versão: `TriMux-0.3.0-brickpro.img.xz` (Rufus/balenaEtcher),
`TriMux-0.3.0-update.zip` (atualizar um cartão com TriMux, sem perder saves) e
`TriMux-0.3.0-brickpro.sha256`.

# TriMux 0.2.0 (pré-lançamento)

**Estado: não testado em um Brick Pro físico**, como a 0.1.0. Tudo da 0.1.0
continua valendo (lista abaixo), mais:

* Registros e desempenho ([DIAGNOSTICO.md](DIAGNOSTICO.md)): registro opcional
  de temperatura, CPU e bateria em cada jogo com resumo por perfil, FPS na
  tela, log detalhado do TriMux e log do RetroArch; tudo desligado por padrão.
* Perguntas frequentes sobre instalação na memória interna
  ([INSTALACAO.md](INSTALACAO.md)).

Arquivos desta versão: `TriMux-0.2.0-brickpro.img.xz` (Rufus/balenaEtcher),
`TriMux-0.2.0-update.zip` (atualizar um cartão com TriMux, sem perder saves) e
`TriMux-0.2.0-brickpro.sha256`.

# TriMux 0.1.0 — primeira versão (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Todo o código foi compilado,
testado automaticamente e verificado contra as bibliotecas do firmware oficial
v1.1.1 em QEMU, mas inicialização, imagem na tela, controles, áudio, suspensão
e desempenho reais ainda precisam ser confirmados no aparelho
(veja [TESTES.md](TESTES.md)).

## O que tem

* Menu próprio em português do Brasil (inglês incluído), feito para os botões
  do aparelho, com assistente inicial, continuar, recentes, favoritos, busca,
  filtros e informações por plataforma.
* 22 plataformas com 20 núcleos libretro compilados para Cortex-A53 e RetroArch
  1.22.2 (GLES via SDL2 do firmware, ALSA, controle via `/dev/input/js*`).
* Ports: DOOM (PrBoom), Quake (TyrQuake), Cave Story (NXEngine) e scripts `.sh`
  no estilo PortMaster (experimental).
* Botões extras configuráveis: F1/F2 (favoritar, jogo surpresa, busca,
  recentes, menu rápido, trocar perfil, LEDs) e chave lateral (modo economia,
  LEDs apagados, alto-falante mudo ou 2,0 GHz, também durante o jogo).
* Proteção do limite de CPU: se um atalho de FN do firmware oficial subir a
  CPU (até 2,0 GHz), o TriMux devolve ao limite do perfil em até 10 s.
* Troca de emulador por jogo ou por plataforma; restauração da configuração
  recomendada (arquiva, não apaga).
* Perfis de energia com teto de 1,8 GHz, aplicados antes de cada jogo,
  proteção térmica adicional e restauração automática do padrão.
* Opcional: chave lateral libera 2,0 GHz (máximo da tabela do firmware) só
  enquanto estiver ligada; exige confirmação; desligada, o teto é 1,8 GHz.
* Controle de LEDs pelo driver oficial (opcional, desligado por padrão).
* Rede ([REDE.md](REDE.md)): Wi-Fi (ligar/desligar, procurar, conectar com
  senha, redes salvas compartilhadas com o sistema oficial), serviço Bluetooth
  da TrimUI, RetroAchievements (RetroArch compilado com rede, HTTPS e
  conquistas; sem atualizador online), transferência de arquivos por FTP só
  enquanto a janela está aberta e SSH/SFTP do firmware desligado por padrão.
* Teclado na tela com minúsculas, maiúsculas e símbolos para senhas.
* Retorno automático ao sistema oficial em caso de falhas repetidas; SELECT ao
  ligar abre o sistema oficial.
* Imagem `.img.xz` para Rufus (MBR + FAT32) com expansão segura da partição no
  aparelho; pacote `-update.zip` para atualizar sem perder dados.

## Limitações conhecidas

* Funciona **sobre** o firmware oficial (não é uma distribuição que inicializa
  sozinha pelo cartão). Mudanças futuras no firmware da TrimUI podem exigir
  ajustes.
* F1/F2 não têm ação dentro dos jogos (no RetroArch todo atalho exige segurar
  MENU); a chave lateral funciona também nos jogos.
* Numeração de botões/eixos baseada em documentação comunitária da família
  TG5040; se algo estiver trocado, use *Controles › Testar controles* e ajuste
  `[input]` em `trimux.ini` (menu) ou remapeie no menu do RetroArch (jogos).
* Volume e brilho são controlados pelas teclas do firmware (sem indicador na
  tela dentro do TriMux, porque o serviço de OSD oficial não é iniciado).
* Bluetooth: parear continua sendo no sistema oficial; o TriMux só mantém o
  serviço da TrimUI ativo. Áudio Bluetooth nos jogos não é garantido.
* Wi-Fi: só redes abertas e WPA/WPA2 com senha (sem EAP, WEP ou só WPA3).
* FTP sem senha (limite do BusyBox do firmware); a senha do RetroAchievements
  fica no cartão sem criptografia.
* Funções de rede testadas só em ambiente simulado.
* PlayStation 2: removido (nenhum emulador viável; análise em [PS2.md](PS2.md)).
* N64, PSP, Dreamcast e outros sistemas mais pesados não estão incluídos.
* Sem swap: jogos de arcade muito grandes podem não caber na memória.
* Snes9x 2005, PicoDrive, Genesis Plus GX e FinalBurn Neo têm licenças não
  comerciais.

## Arquivos

* `TriMux-0.1.0-brickpro.img.xz` — gravar com Rufus/balenaEtcher.
* `TriMux-0.1.0-update.zip` — atualizar/instalar em cartão existente.
* `TriMux-0.1.0-brickpro.sha256` — hashes SHA-256.
