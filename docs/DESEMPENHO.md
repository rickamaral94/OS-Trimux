# Desempenho, temperatura e bateria — protocolo de medição

**Nenhuma medição de desempenho no aparelho físico foi feita até agora.** Este
documento define como medir e onde registrar. Resultados de emulador, QEMU ou
compilação cruzada não contam como prova de desempenho.

## O que o TriMux já faz para economizar recursos (verificável no código)

| Área | Medida | Onde |
|---|---|---|
| Processos | Só o menu **ou** o RetroArch fica aberto; o menu sai antes do jogo e volta depois. Serviços do firmware que não são necessários (`trimui_scened`, `trimui_osdd`, `musicserver`, `trimui_btmanager`) não são iniciados. | `TriMux/scripts/supervisor.sh` |
| Polling | Menu redesenha só quando algo muda; parado, acorda 1×/s para relógio/bateria. Proteção térmica lê **um** arquivo a cada 10 s durante o jogo. | `src/ui/app.c`, `src/tools/trimuxctl.c` |
| Biblioteca | Índice em `TriMuxData/cache/library.tsv`; reindexação só quando o `mtime` das pastas muda ou a pedido. | `src/core/library.c` |
| Gravações no cartão | Configuração salva ao sair dos menus (não a cada tecla); RetroArch com `config_save_on_exit=false`, sem autosave periódico, sem histórico; arquivos de trabalho em `/tmp` (RAM). Log limitado a 256 KiB + 1 rotação. | `retroarch.base.cfg`, `src/core/log.c` |
| Memória | Sem swap no cartão; rewind e run-ahead desligados; cache de extração do RetroArch em RAM e limpo a cada volta ao menu. | `retroarch.base.cfg`, `premenu.sh` |
| CPU | Perfis com teto de 1,8 GHz; "Automático" usa Economia (≤ 1,2 GHz) para 8/16 bits. 2,0 GHz só por escolha do usuário: pela chave lateral ou, a partir da 0.5.6, em *Energia › Emuladores pesados até 2,0 GHz* (o mesmo limite do modo normal do firmware oficial). | `src/core/power.c`, `emulators.ini` |
| Emuladores pesados | Na primeira vez que N64, Dreamcast ou PSP abrem (0.5.6), o TriMux grava nas opções do núcleo os valores mais leves para esta GPU (veja abaixo). Mudanças feitas depois no menu do RetroArch ficam. | `src/core/video.c` (`tm_video_tune`) |

### N64, Dreamcast e PSP comparados ao sistema oficial (0.5.6)

No sistema oficial esses jogos rodavam melhor. O que foi encontrado no
firmware e nos núcleos (sem medição no aparelho ainda):

* **CPU:** o modo normal do firmware oficial
  (`/usr/trimui/apps/fn_editor/com.trimui.cpuperformance.sh`, aplicado pelo
  `trimui_scened`) deixa a CPU entre 1,008 e 2,0 GHz. O TriMux parava em
  1,8 GHz. Agora há a opção *Emuladores pesados até 2,0 GHz* (pede
  confirmação; vale para o perfil Automático).
* **Padrões dos núcleos**, pensados para PCs. O TriMux grava uma vez os
  valores mais leves que cada núcleo oferece (versão em
  `TriMuxData/state/tuned.ini`):

| Núcleo | Opção | Padrão do núcleo | TriMux |
|---|---|---|---|
| Flycast | `reicast_anisotropic_filtering` | 4 | off |
| Flycast | `reicast_alpha_sorting` | per-triangle | per-strip (padrão das builds LOW_END) |
| Flycast | `reicast_enable_dsp` | enabled | disabled (padrão das builds LOW_END) |
| Mupen64Plus-Next | `mupen64plus-EnableNativeResFactor` | 0 (640×480) | 1 (nativa, 320×240) |
| PPSSPP | `ppsspp_texture_anisotropic_filtering` | 16x | disabled |

A resolução interna continua escolhível em *Emuladores › <plataforma>*; o
*Modo rápido* (0.5.3) segue como opção à parte. O ganho real tem de ser
medido no aparelho (TESTES.md, H86), com *FPS na tela* e o registro de
desempenho.

Tamanhos da versão 0.1.0 (medidos no build): `trimux-ui` 208 KB,
`trimuxctl` 97 KB, RetroArch 14,9 MB (com rede, HTTPS e RetroAchievements),
20 núcleos ~131 MB (FinalBurn Neo 81 MB), árvore do cartão ~145 MB, imagem
1 GiB (23 MB em `.img.xz`).

Rede: nada de rede roda por padrão além do que o firmware já liga. O
`trimui_btmanager` só é iniciado se o usuário ativar; o servidor FTP só existe
com a janela aberta; o SSH do firmware fica desligado. A tela de Rede consulta
o `wpa_cli` no máximo a cada 2 s e só enquanto está aberta.

## Protocolo

1. Aparelho: Brick Pro, anote número de série/lote se possível.
2. Firmware oficial (`/etc/version`) e versão do TriMux (`TriMux/VERSION`).
3. Bateria ≥ 80 %, sem carregador (exceto testes de carga), temperatura ambiente
   anotada (termômetro), aparelho em repouso 10 min antes.
4. Perfil de energia usado e frequência real (*Informações do aparelho*).
5. Jogo **legal** (homebrew/próprio), plataforma, emulador, resolução/escala.
6. Duração mínima: **20 min** para jogabilidade, **60 min** para estabilidade
   térmica, **2 h** para teste prolongado. Um benchmark curto não prova nada.
7. Ligue *Configurações › Sistema › Registros e desempenho › Registro de
   desempenho* e *Mostrar FPS*: o TriMux grava temperatura, CPU e bateria a
   cada 10 s e um resumo por sessão ([DIAGNOSTICO.md](DIAGNOSTICO.md)). Anote à
   parte quedas de quadro perceptíveis e travamentos.
8. Anexe `TriMuxData/logs/perf/sessions.csv`, o arquivo de amostras da sessão e
   `TriMuxData/logs/trimux.log`.

## Modelo de registro

| Data | Firmware | TriMux | Perfil | Plataforma / emulador | Jogo | Resolução | Duração | Temp. ambiente | CPU início → fim | Bateria início → fim | Resultado |
|---|---|---|---|---|---|---|---|---|---|---|---|
| _pendente_ | | | | | | | | | | | |

## Matriz mínima a medir antes da versão 1.0

| Plataforma | Emulador | Perfil esperado | Status |
|---|---|---|---|
| NES | FCEUmm | Economia | pendente (hardware) |
| SNES | Snes9x 2005 Plus / Supafaust | Equilibrado / Desempenho seguro | pendente |
| GB/GBC | Gambatte | Economia | pendente |
| GBA | gpSP / mGBA | Economia / Equilibrado | pendente |
| Mega Drive | PicoDrive | Economia | pendente |
| PlayStation | PCSX ReARMed | Equilibrado | pendente |
| Arcade | FinalBurn Neo (CPS1/CPS2/Neo Geo) | Equilibrado | pendente |
| Menu ocioso | — | Equilibrado | pendente (consumo/temperatura) |
| Suspensão 8 h | — | — | pendente (bateria) |
