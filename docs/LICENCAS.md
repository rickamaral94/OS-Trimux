# Licenças e origem dos componentes

O código do TriMux neste repositório está sob a licença **MIT** (`LICENSE`).
A imagem do cartão também contém os componentes abaixo, cada um com sua
licença. Os textos de licença vão no cartão em `TriMux/licenses/`. Os commits
exatos estão em [`sources/sources.lock`](../sources/sources.lock) e são
verificados pelo `scripts/fetch_sources.sh`.

## Incluídos na imagem

| Componente | Uso | Licença | Origem |
|---|---|---|---|
| RetroArch v1.22.2 | frontend dos emuladores | GPL-3.0-or-later | github.com/libretro/RetroArch |
| FCEUmm | NES | GPL-2.0-or-later | libretro/libretro-fceumm |
| Nestopia UE | NES | GPL-2.0-or-later | libretro/nestopia |
| Snes9x 2005 Plus | SNES | **Licença Snes9x (não comercial)** | libretro/snes9x2005 |
| Beetle Supafaust | SNES | GPL-2.0-or-later | libretro/supafaust |
| Gambatte | GB/GBC | GPL-2.0-only | libretro/gambatte-libretro |
| mGBA | GB/GBC/GBA | MPL-2.0 | libretro/mgba |
| gpSP | GBA | GPL-2.0-or-later | libretro/gpsp |
| PicoDrive | MD/SMS/32X/Sega CD | **Licença estilo MAME (não comercial)** | libretro/picodrive |
| Genesis Plus GX | MD/SMS/GG/Sega CD | **Licença Genesis Plus GX (não comercial)** | libretro/Genesis-Plus-GX |
| Beetle PCE Fast | PC Engine | GPL-2.0-only | libretro/beetle-pce-fast-libretro |
| PCSX ReARMed | PlayStation | GPL-2.0-or-later | libretro/pcsx_rearmed |
| RACE | Neo Geo Pocket | GPL-2.0-only | libretro/RACE |
| Beetle WonderSwan | WonderSwan | GPL-2.0-only | libretro/beetle-wswan-libretro |
| Handy | Lynx | Zlib | libretro/libretro-handy |
| Stella 2014 | Atari 2600 | GPL-2.0-or-later | libretro/stella2014-libretro |
| ProSystem | Atari 7800 | GPL-2.0-only | libretro/prosystem-libretro |
| FinalBurn Neo | Arcade/Neo Geo | **Licença FBNeo (não comercial)** | libretro/FBNeo |
| PrBoom (+ `prboom.wad`) | DOOM | GPL-2.0-or-later | libretro/libretro-prboom |
| TyrQuake | Quake | GPL-2.0-or-later | libretro/tyrquake |
| NXEngine | Cave Story | GPL-3.0-only | libretro/nxengine-libretro |
| stb_truetype v1.26 | renderização de fonte no menu | MIT / domínio público | github.com/nothings/stb (commit em `src/ui/third_party/STB_COMMIT`) |
| DejaVu Sans | fonte do menu | Bitstream Vera / DejaVu (livre) | pacote Debian `fonts-dejavu-core` |

**Componentes não comerciais:** Snes9x 2005, PicoDrive, Genesis Plus GX e
FinalBurn Neo permitem uso e redistribuição gratuitos, **sem fins
comerciais**. Quem redistribuir a imagem comercialmente deve removê-los
(apagar os `.so` correspondentes em `TriMux/retroarch/cores/`; o TriMux passa a
usar as alternativas GPL quando existem).

**GPL:** a imagem distribui binários GPL. O código-fonte correspondente é
obtido exatamente pelos commits de `sources/sources.lock`, e o processo de
compilação completo está neste repositório (`make all-docker`).

## Usados na compilação ou nos testes (não incluídos na imagem)

| Componente | Uso | Licença |
|---|---|---|
| Debian bullseye (snapshot 2026-08-24): GCC 10, glibc 2.31, binutils, mtools, dosfstools, QEMU | contêiner de compilação | várias livres (GPL/LGPL) |
| SDL 2.30.8 | cabeçalhos e biblioteca para *linkar* (no aparelho é usada a cópia do firmware) | Zlib |
| Firmware oficial TrimUI v1.1.1 | auditoria de hardware e testes de ABI em QEMU; **não redistribuído** | proprietário (TrimUI) |

## Não incluídos de propósito

Jogos, BIOS, firmware de consoles, chaves, binários do firmware TrimUI
(MainUI, drivers PowerVR, kernel) e qualquer dado pessoal. O teste
`tests/py/test_data.py::test_no_copyrighted_payload_in_repo` falha se arquivos
com extensões de ROM/imagem forem adicionados ao repositório, e
`test_image.py::test_release_image` verifica a imagem final.
