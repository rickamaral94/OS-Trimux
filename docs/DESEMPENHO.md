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
| CPU | Perfis com teto de 1,8 GHz; "Automático" usa Economia (≤ 1,2 GHz) para 8/16 bits. | `src/core/power.c`, `emulators.ini` |

Tamanhos da versão 0.1.0 (medidos no build): `trimux-ui` 171 KB,
`trimuxctl` 81 KB, RetroArch 13,7 MB, 17 núcleos 126 MB (FinalBurn Neo 81 MB),
árvore do cartão 138 MB, imagem 1 GiB (21 MB em `.img.xz`).

## Protocolo

1. Aparelho: Brick Pro, anote número de série/lote se possível.
2. Firmware oficial (`/etc/version`) e versão do TriMux (`TriMux/VERSION`).
3. Bateria ≥ 80 %, sem carregador (exceto testes de carga), temperatura ambiente
   anotada (termômetro), aparelho em repouso 10 min antes.
4. Perfil de energia usado e frequência real (*Informações do aparelho*).
5. Jogo **legal** (homebrew/próprio), plataforma, emulador, resolução/escala.
6. Duração mínima: **20 min** para jogabilidade, **60 min** para estabilidade
   térmica, **2 h** para teste prolongado. Um benchmark curto não prova nada.
7. Registre temperatura da CPU no início e a cada 5 min (TriMux mostra em
   *Configurações › Energia*; o log registra quando a proteção térmica atua),
   bateria inicial/final, quedas de quadro perceptíveis, travamentos.
8. Anexe `TriMuxData/logs/trimux.log` da sessão.

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
