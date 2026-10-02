# Análise de viabilidade

Resumo das decisões, com foco em **boot/Rufus**, **frequência da CPU** e **PS2**.
Os fatos de hardware citados estão em [HARDWARE.md](HARDWARE.md).

## 1. Que tipo de projeto é este

| Opção | Viável? | Decisão |
|---|---|---|
| (1) Distribuição Linux completa que inicializa pelo microSD | Tecnicamente possível: a ROM de boot lê o SD antes da eMMC e a TrimUI publica um cartão de recuperação inicializável. Exigiria reaproveitar boot0/U-Boot/kernel 4.9 e os drivers PowerVR binários do firmware (sem licença de redistribuição) e só poderia ser validada no aparelho. Um erro aqui deixa o aparelho sem tela ou, pior, com um cartão que se comporta como cartão de recuperação. | **Não adotada nesta versão.** |
| (2) Imagem de atualização para o firmware oficial (`.awimg`) | Possível, mas regrava a eMMC. Contraria a prioridade nº 1 (não danificar o aparelho) e precisaria de redistribuir o firmware. | **Descartada.** |
| (3) Cartão de dados que o firmware oficial executa (combinação suportada) | O próprio `runtrimui.sh` oficial executa `trimui/app/MainUI` do cartão quando a pasta existe. Kernel, drivers, limites térmicos e recuperação continuam sendo os da TrimUI. Nada é escrito na eMMC. | **Adotada.** |

Consequências da opção (3):

* Retirar o cartão (ou apagar a pasta `trimui`) devolve o aparelho ao estado
  original: não há nada para "desinstalar" da memória interna.
* Os serviços oficiais (`trimui_inputd` para o controle, `keymon` para volume,
  brilho, suspensão e desligamento, `hardwareservice`) são reutilizados.
* Kernel 4.9, OpenGL ES 3.2 (PowerVR) e SDL2 2.30.8 do firmware são a base
  gráfica. Atualizações oficiais da TrimUI podem mudar esse ambiente; por isso
  o TriMux testa o modelo na inicialização e volta ao lançador oficial se algo
  não bater.

## 2. Imagem para o Rufus

**O que é entregue:** `TriMux-<versão>-brickpro.img.xz`, uma imagem de disco
com tabela MBR e **uma partição FAT32** (`TRIMUX`). O Rufus grava esse arquivo
diretamente (modo imagem/DD, aceita `.img.xz`). Ela **não é uma imagem de boot
do SoC**: o aparelho continua iniciando pelo firmware da eMMC e encontra o
TriMux no cartão. Isso é dito na documentação e nas notas de versão.

Detalhes que importam:

* Os setores 1–2047 da imagem são zerados de propósito. A ROM procura o
  cabeçalho `eGON.BT0` a 8 KiB; sem ele, o cartão nunca é tratado como cartão
  de boot/recuperação. Gravar o TriMux por cima de um antigo cartão de
  recuperação também neutraliza esse cartão.
* FAT32 foi escolhido porque é o único formato aceito pelo atualizador oficial
  (o arquivo `.awimg` precisa estar num cartão FAT32), então o mesmo cartão
  serve para atualizar o firmware oficial.
* A partição tem 1 GiB após a gravação. As tabelas FAT foram dimensionadas para
  1 TiB (256 MiB de FAT, que comprimem para quase nada), então a partição pode
  crescer até ocupar o cartão inteiro **sem mover dados**: o TriMux só reescreve
  o tamanho na MBR e no setor de boot (primeiro a MBR, depois a cópia de
  reserva e o setor principal), com o cartão remontado como somente leitura.
  O procedimento é testado automaticamente em imagens de 8 GiB e 1,5 TiB.
* Alternativa sem gravar imagem: copiar o conteúdo de
  `TriMux-<versão>-update.zip` para um cartão FAT32/exFAT já formatado.

## 3. Frequência da CPU, temperatura e "overclock"

* O firmware expõe o `cpufreq` padrão (`/sys/devices/system/cpu/cpufreq/policy0`)
  com os governadores `ondemand` (padrão), `conservative`, `schedutil`,
  `interactive`, `performance`, `powersave`, `userspace`.
* A tabela de frequências do device tree vai até **2,0 GHz** (com 1,25 V para o
  bin c0) e o próprio firmware oficial usa 2,0 GHz nos modos "performance".
  O fabricante anuncia **1,8 GHz**. O TriMux trata 1,8 GHz como limite rígido:
  nenhum perfil passa disso, e o código recusa um perfil se não houver uma
  frequência abaixo do limite (testado).
* O TriMux **não escreve** em pontos de disparo térmicos, dispositivos de
  resfriamento, tensões ou registradores. Escreve apenas `scaling_governor`,
  `scaling_min_freq` e `scaling_max_freq`, sempre lendo de volta.
* Perfis: **Economia** (até 1,2 GHz), **Equilibrado** (até 1,608 GHz; padrão no
  menu), **Desempenho seguro** (até 1,8 GHz, mínimo 1,008 GHz) e
  **Automático** (padrão), que usa o perfil recomendado de cada emulador.
  Não há "overclock" no TriMux.
* Os limites são aplicados **antes** de o emulador iniciar e o perfil padrão é
  restaurado ao sair do jogo, a cada inicialização e após travamentos.
* Proteção térmica adicional: durante o jogo, se a CPU passar de 75 °C por
  30 s seguidos, o TriMux cai para Economia e volta abaixo de 65 °C. É uma
  camada extra; os limites do kernel continuam ativos.
* O `keymon` oficial executa atalhos de FN configurados pelo usuário no
  sistema oficial (por exemplo, "CPU clock switcher", que vai até 2,0 GHz). O
  TriMux não altera a memória interna para desativá-los; em vez disso, confere
  o limite a cada 10 s (menu e jogo) e o devolve ao perfil, registrando no log.
* O serviço oficial `trimui_scened` **não é iniciado** pelo TriMux porque seus
  scripts de cena reescrevem os limites da CPU para 2,0 GHz a cada 5 s.
* Sem `zram` no kernel e com o cartão montado com `sync`, o TriMux **não cria
  swap** (evita desgaste do cartão e travamentos por E/S).

## 4. PlayStation 2

Conclusão: **não há emulador de PS2 compatível e utilizável neste aparelho**.
Detalhes e critérios em [PS2.md](PS2.md). Por isso o PS2 foi **removido**
do catálogo e do menu.

## 5. LEDs

O firmware tem o driver `led_anim` com zonas separadas. O TriMux detecta os
arquivos existentes e só mostra as zonas completas; por padrão **não mexe** nos
LEDs (o firmware continua controlando o aviso de bateria fraca). O controle só
é aplicado quando o usuário ativa "Controlar LEDs pelo TriMux".

## 6. Riscos remanescentes (exigem teste no aparelho)

1. O backend "mali" do SDL2 do firmware precisa abrir a janela/GLES do menu e
   do RetroArch. Se o RetroArch falhar em menos de 5 s, o TriMux tenta de novo
   com o renderizador SDL2 do RetroArch.
2. Numeração de botões e eixos (há tela de teste e mapeamento configurável).
3. Áudio do RetroArch via ALSA com as rotas `tinymix` do firmware.
4. Comportamento de suspensão com um emulador aberto (feita pelo `keymon`
   oficial).
