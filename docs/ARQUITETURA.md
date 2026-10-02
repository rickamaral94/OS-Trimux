# Arquitetura

## Fluxo de inicialização

```
ROM de boot (SoC) ── sem eGON.BT0 no cartão ──> boot pela eMMC (firmware TrimUI intacto)
  └─ /etc/init.d/runtrimui → /usr/trimui/bin/runtrimui.sh     (oficial)
       ├─ monta /mnt/SDCARD, LEDs de boot, syslog, Bluetooth, modo USB, Wi-Fi
       └─ laço principal: existe /mnt/SDCARD/trimui ?
            ├─ trimui/app/premainui.sh            (TriMux: modo padrão do controle)
            ├─ trimui/app/MainUI                  (TriMux: guarda de reinício → supervisor)
            │    └─ TriMux/scripts/supervisor.sh
            │         ├─ trimuxctl device         → não é Brick Pro? volta ao oficial
            │         ├─ trimuxctl boot begin     → 3 boots sem menu? modo seguro (oficial)
            │         ├─ inicia keymon, trimui_inputd, hardwareservice (binários oficiais)
            │         ├─ trimuxctl power default; trimuxctl leds apply
            │         └─ laço: trimux-ui ──código de saída──┐
            │               10 jogar  → trimuxctl launch (valida, aplica limites, RetroArch, guarda térmica)
            │               20 oficial→ /tmp/trimux/to_stock
            │               30/31     → desligar / reiniciar
            │               40        → trimuxctl card-grow → reiniciar
            │               falha     → 3 em 1 min → oficial
            └─ trimui/app/preload.sh: sai 1 se to_stock (abre o MainUI oficial), senão 0
```

## Módulos

| Módulo | Arquivos | Responsabilidade |
|---|---|---|
| Núcleo portátil (C11, sem SDL) | `src/core/` | |
| ├ util, log | `util.c`, `log.c` | caminhos seguros, gravação atômica, log com rotação |
| ├ config | `ini.c`, `paths.c` | INI tolerante a erros, padrões, migração com backup |
| ├ idiomas | `i18n.c` | arquivos `.lang`, fallback para pt_BR |
| ├ catálogo | `catalog.c` | plataformas/emuladores a partir de `systems.ini`/`emulators.ini`; resolução jogo → plataforma → padrão |
| ├ biblioteca | `library.c`, `lists.c` | varredura de pastas conhecidas, índice, `.m3u`, favoritos/recentes |
| ├ lançamento | `launch.c` | pedido de lançamento em RAM, validação de caminho, máquina de estados térmica |
| ├ hardware | `power.c`, `leds.c`, `sysinfo.c` | cpufreq com teto de 1,8 GHz (2,0 GHz só pela chave, opt-in), LEDs detectados, bateria/memória/cartão |
| └ cartão | `fatgrow.c` | expansão FAT32 só por metadados |
| Menu (SDL2) | `src/ui/` | `gfx.c` (stb_truetype), `input.c`, telas `home.c`, `games.c`, `menus.c`, `wizard.c`, `keyboard.c` |
| Ferramenta | `src/tools/trimuxctl.c` | comandos usados pelos scripts (energia, LEDs, lançamento, boot, expansão) |
| Scripts do cartão | `sdcard/` | ponto de entrada `trimui/app`, supervisor, configuração do RetroArch |
| Build | `docker/`, `scripts/`, `Makefile` | contêiner fixado, fontes fixadas, núcleos, imagem, testes |

## Princípios aplicados

* **Firmware primeiro:** controle, teclas de sistema, suspensão, áudio e
  limites térmicos são do firmware oficial; o TriMux não substitui serviços que
  funcionam.
* **Nada destrutivo sem confirmação:** sistema oficial, desligar, reiniciar,
  criar pastas, restaurar emulador (arquiva, não apaga) e expandir partição
  pedem confirmação; o padrão do diálogo é "Não".
* **Um processo por vez:** o menu sai antes do jogo; nenhum daemon próprio fica
  residente além do `trimuxctl launch` (que dorme e lê a temperatura a cada
  10 s).
* **Sem shell com dados do usuário:** caminhos de jogos nunca passam por
  `sh -c`; o RetroArch é iniciado com `execv`.
* **Falha segura:** qualquer erro no TriMux leva ao lançador oficial, nunca a
  um aparelho travado.

## Botões extras

| Controle | Origem | Uso no TriMux |
|---|---|---|
| F1, F2 | botões 11 e 12 do "TRIMUI Player1" (configuráveis em `[input]`) | ação escolhida em *Controles › Botões extras* (`[buttons] f1/f2`) |
| Chave lateral | GPIO 243, exportado pelo `runtrimui.sh` oficial | leitura 1×/s no menu e no `trimuxctl launch`; ação `[buttons] switch` = `economy`, `boost` (2,0 GHz, exige `[power] boost_ack = 1`), `leds_off` ou `mute` (via `/sys/class/speaker/mute`, a mesma interface do `keymon`) |
| HOME | botão 15 | menu rápido no TriMux; menu do RetroArch no jogo |
| MENU | botão 8 | menu rápido no TriMux; tecla de atalho no RetroArch |
