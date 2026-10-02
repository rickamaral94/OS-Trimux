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
| CPU | Perfis com teto de 1,8 GHz; "Automático" usa Economia (≤ 1,2 GHz) para 8/16 bits. 2,0 GHz só pela chave lateral, por escolha do usuário. | `src/core/power.c`, `emulators.ini` |

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
