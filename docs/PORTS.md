# Ports no TriMux

"Ports" são jogos de PC que rodam com um motor nativo em vez de um emulador
de console. O TriMux oferece dois tipos:

## 1. Motores incluídos (libretro)

| Plataforma | Motor | Pasta | Arquivos que você fornece |
|---|---|---|---|
| DOOM | PrBoom (GPL) | `Roms/DOOM` | `.wad`: DOOM/DOOM II/Final DOOM (seus) ou **Freedoom** (livre, freedoom.github.io) |
| Quake | TyrQuake (GPL) | `Roms/QUAKE/<jogo>/` | `pak0.pak` (shareware ou completo) e `pak1.pak` se tiver |
| Cave Story | NXEngine (GPL) | `Roms/CAVESTORY/<pasta>/` | `Doukutsu.exe` + pasta `data` (jogo freeware original) |

* O `prboom.wad` (arquivo de recursos do próprio PrBoom, GPL) já vai em `/Bios`.
* Nenhum dado de jogo é incluído.
* Os três núcleos são compilados nos commits de `sources/sources.lock` e
  verificados contra as bibliotecas do firmware (teste de fumaça em QEMU), mas
  **não foram testados no aparelho**.

## 2. Ports por script (`.sh`, estilo PortMaster)

Scripts `.sh` colocados **no primeiro nível** de `Roms/PORTS` aparecem na
plataforma **Ports** (subpastas, onde os ports guardam seus dados, não são
listadas). Ao abrir:

* o perfil de energia e a proteção térmica são aplicados como num jogo;
* o script roda com `/bin/sh` do firmware, a partir da própria pasta, com as
  variáveis `TRIMUX=1` e `TRIMUX_DEVICE=brickpro` e as bibliotecas do firmware
  no `LD_LIBRARY_PATH`;
* ao terminar, o TriMux volta ao menu e restaura o perfil padrão.

Situação: **experimental**. A plataforma é marcada assim no menu e pede
confirmação antes de abrir. Ports do [PortMaster](https://portmaster.games)
geralmente dependem do próprio PortMaster instalado (pasta de controle,
`gptokeyb`, runtimes) e de bibliotecas que o firmware pode não ter; o TriMux
não instala o PortMaster nem garante compatibilidade de nenhum port.

Segurança: um port é um programa com acesso total ao aparelho. Use apenas
ports de fontes confiáveis.
