# Estrutura do cartão, formatos e BIOS

```
/                         partição FAT32 "TRIMUX" (ou seu cartão FAT32/exFAT)
├── LEIA-ME.txt
├── trimui/app/           ponto de entrada lido pelo firmware oficial (não apague)
│   ├── MainUI            inicia o TriMux (proteção contra reinícios em laço)
│   ├── preload.sh        decide entre TriMux e lançador oficial
│   └── premainui.sh
├── TriMux/               sistema — substituído nas atualizações
│   ├── bin/              trimux-ui (menu), trimuxctl (energia, LEDs, lançamento…)
│   ├── retroarch/        RetroArch, cores/, autoconfig/, retroarch.base.cfg
│   ├── scripts/          supervisor.sh, premenu.sh
│   ├── share/            systems.ini, emulators.ini, i18n/*.lang, fonts/
│   └── licenses/         licenças de cada componente
├── TriMuxData/           seus dados — nunca substituído
│   ├── config/           trimux.ini, overrides.ini, favorites.txt, recent.txt
│   ├── cache/            library.tsv (índice da biblioteca; pode ser apagado)
│   ├── logs/             trimux.log (máx. 256 KiB + 1 arquivo antigo)
│   ├── state/            contador de boot, marcador de jogo aberto
│   └── retroarch/        retroarch.cfg, config/<núcleo>/ (opções), remaps/
├── Roms/<PLATAFORMA>/    seus jogos (subpastas até 3 níveis)
├── Bios/                 suas BIOS
├── Saves/<PLATAFORMA>/   saves (SRAM) — criados ao jogar
├── States/<PLATAFORMA>/  estados salvos
└── Screenshots/
```

## Plataformas, pastas e formatos

A primeira pasta de cada linha é a criada pelo TriMux; as demais (sem diferença
entre maiúsculas e minúsculas) também são reconhecidas, assim como pastas no
formato `Nome Longo (TAG)` de cartões MinUI/NextUI.

| Plataforma | Pastas | Extensões | Emuladores (padrão primeiro) | BIOS em `/Bios` |
|---|---|---|---|---|
| NES / Famicom | FC, NES, famicom, FDS | nes unf unif fds zip 7z | FCEUmm, Nestopia UE | `disksys.rom` (só .fds) |
| Super Nintendo | SFC, SNES, SUPA | sfc smc swc fig zip 7z | Snes9x 2005 Plus, Supafaust | — |
| Game Boy | GB, gameboy | gb zip 7z | Gambatte, mGBA | — |
| Game Boy Color | GBC, gameboycolor | gbc zip 7z | Gambatte, mGBA | — |
| Game Boy Advance | GBA, MGBA, gameboyadvance | gba zip 7z | gpSP, mGBA | `gba_bios.bin` (opcional) |
| Mega Drive | MD, megadrive, genesis, GEN | md gen smd bin 68k sgd zip 7z | PicoDrive, Genesis Plus GX | — |
| Master System | MS, SMS, mastersystem | sms zip 7z | PicoDrive, Genesis Plus GX | — |
| Game Gear | GG, gamegear | gg zip 7z | Genesis Plus GX, PicoDrive | — |
| Sega CD | SEGACD, segacd, megacd | cue chd iso m3u | PicoDrive, Genesis Plus GX | `bios_CD_U.bin`/`_E`/`_J` (obrigatória) |
| 32X | 32X, sega32x | 32x zip 7z | PicoDrive | — |
| PC Engine / CD | PCE, pcengine, tg16, PCECD | pce cue chd ccd m3u zip 7z | Beetle PCE Fast | `syscard3.pce` (CD) |
| PlayStation | PS, PSX, PS1 | cue chd pbp m3u img iso | PCSX ReARMed | `scph5501.bin` etc. (opcional, HLE sem BIOS) |
| Arcade | ARCADE, FBNEO, FBN, MAME | zip 7z | FinalBurn Neo | conforme o jogo |
| Neo Geo | NEOGEO | zip 7z | FinalBurn Neo | `neogeo.zip` (obrigatória) |
| Neo Geo Pocket | NGP, NGPC | ngp ngc zip 7z | RACE | — |
| WonderSwan | WS, WSC, wswan, wswanc | ws wsc pc2 zip 7z | Beetle WonderSwan | — |
| Atari Lynx | LYNX | lnx o zip | Handy | `lynxboot.img` |
| Atari 2600 | A2600, atari2600 | a26 bin zip 7z | Stella 2014 | — |
| Atari 7800 | A7800, atari7800 | a78 bin zip 7z | ProSystem | `7800 BIOS (U).rom` (opcional) |
| DOOM | DOOM, PRBOOM | wad iwad pwad | PrBoom | `prboom.wad` (incluído, GPL) |
| Quake | QUAKE | pak | TyrQuake | — |
| Cave Story | CAVESTORY, NXENGINE | exe | NXEngine | — |
| Ports (scripts) | PORTS | sh (só 1º nível) | shell do firmware (experimental) | — ver [PORTS.md](PORTS.md) |
| PlayStation 2 (experimental) | PS2 | iso chd cso cue elf | nenhum incluído | `scph39001.bin` — veja [PS2.md](PS2.md) |

Notas:

* **Arcade:** use ROMs do conjunto correspondente à versão do FinalBurn Neo
  incluída (commit em `sources/sources.lock`). Mantenha os `.zip` como estão.
* **Vários discos (PS1, Sega CD, PCE CD):** crie `Jogo.m3u` listando os
  `.cue`/`.chd` (um por linha, na mesma pasta). O TriMux mostra só a entrada
  `.m3u` e esconde os discos individuais.
* **Nomes:** "Nomes limpos" remove `(USA)`, `[!]` etc. da exibição, mantendo
  `(Disc 1)`. Os arquivos não são renomeados.
* Para acrescentar plataformas ou extensões edite `TriMux/share/systems.ini`
  (sem recompilar). Para traduzir, copie `TriMux/share/i18n/pt_BR.lang` para
  `<código>.lang` e traduza os textos.

## Arquivos de configuração

* `TriMuxData/config/trimux.ini` — idioma, tema, perfil de energia,
  emulador padrão por plataforma (`[emulators]`), LEDs (`[leds.*]`),
  mapeamento de botões do menu (`[input]`: `btn_a`, `btn_b`, … `swap_ab`).
* `TriMuxData/config/overrides.ini` — emulador escolhido por jogo (`[games]`).
* Todos são gravados de forma atômica (arquivo temporário + `fsync` + troca).
  Um arquivo corrompido é ignorado (valores padrão) e não trava o menu.
