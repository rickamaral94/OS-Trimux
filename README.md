# TriMux — sistema de jogos para o TrimUI Brick Pro

TriMux é uma interface e um conjunto de emuladores para o **TrimUI Brick Pro
(TG4040)**, instalados em um cartão microSD gravável com o Rufus. Ele roda
**sobre o firmware oficial** da TrimUI, usando um ponto de entrada que o
próprio firmware já oferece: nada é gravado na memória interna, e tirar o
cartão devolve o aparelho ao estado original.

![Tela inicial](docs/img/02-inicio.png)

> **Estado: 0.3.0, pré-lançamento, ainda não testado em um aparelho físico.**
> Compilação, testes automáticos, testes em ambiente simulado e a verificação
> de compatibilidade com as bibliotecas do firmware oficial v1.1.1 (em QEMU)
> passaram. Inicialização real, imagem na tela, controles, som, suspensão e
> desempenho dependem de teste no Brick Pro — veja [docs/TESTES.md](docs/TESTES.md).

## Destaques

* Abre direto em um menu feito para os botões do aparelho, em **português do
  Brasil** (inglês incluído; novos idiomas são um arquivo de texto).
* Poucos passos até jogar: **Continuar**, Recentes, Favoritos, Todos os jogos,
  plataformas com contagem, busca com teclado na tela, filtros.
* **22 plataformas, 20 núcleos** (RetroArch 1.22.2 + núcleos libretro
  compilados para Cortex-A53), incluindo **ports**: DOOM (PrBoom), Quake
  (TyrQuake), Cave Story (NXEngine) e scripts `.sh` no estilo PortMaster. Troca de emulador **por jogo** ou **por
  plataforma** com o botão SELECT.
* Encontra jogos em pastas conhecidas (cartão oficial TrimUI, MinUI/NextUI,
  Batocera/Knulli) **sem mover ou renomear nada**.
* **Energia segura:** perfis Economia / Equilibrado / Desempenho seguro /
  Automático, até 1,8 GHz (máximo do fabricante), aplicados antes de
  cada jogo; proteção térmica adicional; limites térmicos do
  firmware sempre ativos.
  Opcional: mapear a chave lateral para liberar **2,0 GHz** (o máximo da
  tabela do firmware) só enquanto ela estiver ligada, com confirmação.
* **Rede:** Wi-Fi com busca e senha pelo teclado na tela (mesmas redes do
  sistema oficial), **RetroAchievements**, **transferência de arquivos por FTP**
  que só fica ligada com a janela aberta, serviço Bluetooth da TrimUI e o
  SSH/SFTP do firmware (desligado por padrão). Detalhes em
  [docs/REDE.md](docs/REDE.md).
* **Capas dos jogos**: baixa capas pelo Wi-Fi (libretro-thumbnails, sem
  conta), manualmente ou automaticamente para jogos novos, e mostra no painel
  da lista. Salva em `Imgs/`, a mesma pasta do sistema oficial.
  [docs/CAPAS.md](docs/CAPAS.md).
* **Atualização online**: procura e instala versões novas pelo Wi-Fi, sem
  gravar o cartão de novo; pacote conferido por SHA-256, versão anterior
  guardada e restaurada sozinha se a nova não abrir.
  [docs/ATUALIZACAO.md](docs/ATUALIZACAO.md).
* **Registros e desempenho** (opcional, desligado por padrão): grava
  temperatura, CPU e bateria em cada jogo e resume por perfil, para comparar
  no aparelho o que cada opção custa; FPS na tela e logs técnicos.
  [docs/DIAGNOSTICO.md](docs/DIAGNOSTICO.md).
* **LEDs** pelo driver oficial (barra superior, anéis dos analógicos, F1/F2,
  gatilhos), só se detectados e só se você ativar.
* Assistente inicial curto, que pode ser pulado e reaberto.
* Volta sozinho ao sistema oficial se algo der errado; **segure SELECT ao
  ligar** para abrir o sistema oficial.

| ![Plataforma](docs/img/03-plataforma.png) | ![Emulador por jogo](docs/img/04-emulador.png) |
|---|---|
| ![Energia](docs/img/06-energia.png) | ![Informações](docs/img/08-informacoes.png) |
| ![Ports](docs/img/11-ports.png) | ![Botões extras](docs/img/12-botoes.png) |
| ![Rede](docs/img/13-rede.png) | ![Senha do Wi-Fi](docs/img/15-senha-wifi.png) |
| ![Registros e desempenho](docs/img/16-registros.png) | ![Sessões gravadas](docs/img/17-sessoes.png) |
| ![Capa no painel](docs/img/18-capas.png) | ![Capas dos jogos](docs/img/19-capas-opcoes.png) |
| ![Atualização](docs/img/20-atualizacao.png) | ![Tela de início](docs/img/21-inicio.png) |

<sub>Capturas renderizadas no computador (SDL offscreen) com um Brick Pro
simulado; não são fotos do aparelho.</sub>

## Instalação rápida

1. Baixe `TriMux-<versão>-brickpro.img.xz` e o `.sha256` (Actions → artefato
   `trimux-image`, ou Releases quando houver uma versão publicada).
2. No **Rufus**: Dispositivo = seu microSD → SELECIONAR → o `.img.xz` →
   INICIAR. (Cartão de 2 GB ou mais; recomendado 16 GB+. **Apaga o cartão.**)
3. Coloque o cartão no Brick Pro desligado e ligue. No primeiro boot o TriMux
   passa a usar o cartão inteiro (a imagem tem 1 GiB) e reinicia uma vez
   sozinho. Depois, siga o assistente.
4. Copie seus jogos para `Roms/<PLATAFORMA>` e BIOS para `Bios/` (pelo leitor
   de cartão ou, com Wi-Fi, por *Rede e conexões › Transferir arquivos*).

Passo a passo completo, atualização sem perder saves e recuperação para o
firmware oficial: **[docs/INSTALACAO.md](docs/INSTALACAO.md)**.

## Atalhos

| Onde | Botões | Ação |
|---|---|---|
| Menu | A / B | abrir / voltar (pode trocar em Controles) |
| Menu | X | favoritar |
| Menu | Y | buscar |
| Menu | SELECT | trocar emulador (jogo ou plataforma) |
| Menu | START | opções do jogo |
| Menu | MENU | menu rápido (energia, LEDs, reindexar, sistema oficial, desligar) |
| Jogo | HOME (ou L3 + R3) | menu do jogo (RetroArch) |
| Jogo | MENU + START | fechar o jogo |
| Jogo | MENU + R1 / L1 | salvar / carregar estado |
| Jogo | MENU + R2 / L2 | trocar posição do estado |
| Jogo | MENU + X | avanço rápido |
| Menu | F1 / F2 | ação configurável (padrão: favoritar / jogo surpresa) |
| Sempre | chave lateral | configurável: modo economia, 2,0 GHz (opcional, com confirmação), apagar LEDs ou silenciar (vale também nos jogos) |
| Sempre | + / −, MENU + (+/−) | volume, brilho (firmware oficial) |
| Sempre | POWER / POWER 6 s | suspender / desligar à força (firmware oficial) |
| Ao ligar | segurar SELECT | abrir o sistema oficial |

## Compatibilidade

| Plataforma | Emulador padrão | Alternativa | Situação |
|---|---|---|---|
| NES/Famicom | FCEUmm | Nestopia UE | incluído, não medido no aparelho |
| SNES | Snes9x 2005 Plus | Supafaust | incluído, não medido |
| Game Boy / Color | Gambatte | mGBA | incluído, não medido |
| Game Boy Advance | gpSP | mGBA | incluído, não medido |
| Mega Drive, Master System, Game Gear, Sega CD, 32X | PicoDrive / Genesis Plus GX | a outra | incluído, não medido |
| PC Engine / CD | Beetle PCE Fast | — | incluído, não medido |
| PlayStation | PCSX ReARMed | — | incluído, não medido |
| Arcade / Neo Geo | FinalBurn Neo | — | incluído, não medido |
| Neo Geo Pocket, WonderSwan, Lynx, Atari 2600/7800 | RACE, Beetle WS, Handy, Stella 2014, ProSystem | — | incluído, não medido |
| Ports: DOOM, Quake, Cave Story | PrBoom, TyrQuake, NXEngine | — | incluído, não medido ([docs/PORTS.md](docs/PORTS.md)) |
| Ports por script (`.sh`, PortMaster) | shell do firmware | — | experimental, depende de cada port |
| **PlayStation 2** | — | — | **removido**: nenhum emulador viável neste hardware ([docs/PS2.md](docs/PS2.md)) |

Formatos, nomes de pastas e BIOS: [docs/ESTRUTURA_CARTAO.md](docs/ESTRUTURA_CARTAO.md).
Jogos, BIOS e firmware de consoles **não são incluídos**.

## Como funciona

O firmware oficial (`/usr/trimui/bin/runtrimui.sh`) executa
`/mnt/SDCARD/trimui/app/MainUI` quando essa pasta existe e decide, pelo
`trimui/app/preload.sh`, se abre o lançador oficial. O TriMux usa esse
mecanismo para iniciar o próprio menu, reaproveita os serviços oficiais de
controle, teclas e suspensão e entrega o controle de volta ao firmware em caso
de problema. A imagem do cartão **não** é uma imagem de boot do SoC; a análise
completa (incluindo por que não foi feita uma distribuição inicializável pelo
SD) está em [docs/VIABILIDADE.md](docs/VIABILIDADE.md) e o hardware auditado
em [docs/HARDWARE.md](docs/HARDWARE.md). Arquitetura do código:
[docs/ARQUITETURA.md](docs/ARQUITETURA.md).

## Compilar a partir do código

Requisitos: Linux x86-64 com Docker, `make`, `git`, `curl`, `7z` e Python 3.

```sh
make all-docker
```

Isso cria o contêiner de compilação fixado (Debian bullseye, snapshot
2026-08-24, GCC 10 / glibc 2.31 — compatível com glibc 2.33 do firmware),
baixa as fontes nos commits de `sources/sources.lock`, baixa o firmware oficial
(SHA-256 em `firmware/official.lock`, usado só nos testes), compila RetroArch,
núcleos, `trimux-ui` e `trimuxctl`, roda todos os testes e o teste de fumaça em
QEMU e gera em `build/out/`:

* `TriMux-<versão>-brickpro.img` e `.img.xz`
* `TriMux-<versão>-update.zip` (atualização manual) e
  `TriMux-<versão>-update.tar.gz` (atualização online)
* `TriMux-<versão>-brickpro.sha256`

Comandos individuais: `make test` (testes no computador), `make ui-native`
(menu no computador; rode com `TRIMUX_SDCARD=<pasta>`), `make docker-cross`,
`make docker-cores`, `make docker-image`. O CI (`.github/workflows/build.yml`)
executa o mesmo processo e publica a imagem como artefato. Para publicar uma
versão em *Releases*: envie uma tag `v*` ou, em *Actions › build › Run workflow*
na `main`, preencha `release_tag` (por exemplo `v0.1.0`); a imagem é gerada e
testada do zero antes de ser anexada.

## Testes

* 304 verificações unitárias em C (com AddressSanitizer/UBSan).
* Testes Python: `trimuxctl` contra um Brick Pro simulado (inclusive
  ferramentas de rede falsas do firmware), scripts de boot com
  comandos do firmware simulados, imagem/partição/expansão em arquivos de
  imagem, catálogo e traduções, e o menu com entradas roteirizadas.
* Teste de fumaça em QEMU com as bibliotecas reais do firmware v1.1.1.
* Testes no aparelho físico: **pendentes** (lista em
  [docs/TESTES.md](docs/TESTES.md)); medições de desempenho seguem
  [docs/DESEMPENHO.md](docs/DESEMPENHO.md).

## Licenças

Código do TriMux: MIT. Componentes de terceiros (RetroArch GPL-3.0, núcleos
GPL/MPL/zlib e alguns **não comerciais**): [docs/LICENCAS.md](docs/LICENCAS.md).
TriMux é um projeto independente, sem relação com a TrimUI.
