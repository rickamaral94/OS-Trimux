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
