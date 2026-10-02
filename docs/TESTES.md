# Testes e validação

Três categorias, mantidas separadas:

* **A — Automatizado no computador:** testes unitários e de integração do
  código do TriMux, rodando em x86-64.
* **S — Ambiente simulado:** binários ARM rodando em QEMU com as bibliotecas
  reais do firmware, menu renderizado com o driver SDL *offscreen*, Brick Pro
  simulado (arquivos de sysfs/proc criados a partir dos valores do firmware).
  Prova lógica, compatibilidade binária e formato — **não** prova o hardware.
* **H — Aparelho físico:** só conta o que foi executado num Brick Pro real.
  **Nenhum teste H foi executado até agora.**

Como rodar: `make test` (A + partes S que não exigem Docker),
`make docker-test` (A no contêiner, inclui imagem/expansão),
`make docker-smoke` ou `scripts/smoke_qemu.sh` dentro do contêiner (S, QEMU),
`python3 tools/screenshots.py` (S, capturas).

## Resultado da execução desta versão (0.1.0)

| Conjunto | Categoria | Resultado |
|---|---|---|
| `tests/unit/test_core.c` — 166 verificações (INI, gravação atômica, caminhos, idiomas, catálogo, varredura da biblioteca, `.m3u`, índice, favoritos/recentes, perfis de energia, LEDs, proteção térmica, validação do pedido de lançamento, migração de configurações, sysinfo), com ASan/UBSan | A | **passou** |
| `tests/py/test_ctl.py` — `trimuxctl` com Brick Pro simulado: perfis ≤ 1,8 GHz e sem governador `performance`, arquivos térmicos intocados, LEDs só quando ativados, contador de boot/modo seguro, varredura | A/S | **passou** |
| `tests/py/test_supervisor.py` — scripts de boot com comandos do firmware simulados: port `.sh` roda da própria pasta com limite de energia e scripts auxiliares não aparecem; modelo errado → oficial, laço de falhas → oficial, modo seguro, lançamento aplica limite **antes** do emulador e restaura depois, perfil automático por emulador, pedido adulterado recusado, desligar, `preload.sh`, guarda de reinício do `MainUI`, POSIX/LF/`sh -n` | S | **passou** |
| `tests/py/test_image.py` — imagem: MBR, partição 0x0C em 2048, área de boot zerada (sem `eGON.BT0`), FAT32 válido (`fsck.fat`), nomes longos; expansão para cartão de 8 GiB com `fsck` limpo e escrita de 200 MB no espaço novo; recusa de cartões com 2 partições ou sem FAT; limite de 1 TiB; imagem final sem ROMs/BIOS/firmware | A (contêiner) | **passou** |
| `tests/py/test_data.py` — todas as chaves de tradução usadas existem em pt_BR, especificadores de formato iguais entre idiomas, catálogo consistente, nenhum arquivo de ROM/imagem no repositório | A | **passou** |
| `tests/py/test_ui.py` — menu com entradas roteirizadas: assistente, lançamento grava pedido válido e recentes, favorito e emulador por jogo persistem, confirmação para sistema oficial/desligar, configuração corrompida não trava | S | **passou** (no computador com SDL; ignorado no contêiner, que não tem SDL nativo) |
| `scripts/smoke_qemu.sh` — com bibliotecas do firmware v1.1.1 e `LD_BIND_NOW=1`: `trimuxctl` identifica o Brick Pro; `trimux-ui` resolve todos os símbolos da libSDL2/glibc do firmware (o SDL do firmware só tem o driver de vídeo "mali", por isso a janela não abre em QEMU); `retroarch --features`; os 20 núcleos carregam e o nome/extensões batem com o catálogo | S | **passou** (5/5) |
| `scripts/check_abi.py` — RetroArch, núcleos e binários exigem no máximo GLIBC 2.33 / GLIBCXX 3.4.28 e só bibliotecas presentes no firmware | A | **passou** |
| Reprodutibilidade — duas gerações da imagem a partir da mesma árvore produziram o mesmo SHA-256 do `.img` | A | **passou** |

Também cobertos: modo 2,0 GHz pela chave (só com confirmação, nunca acima da
maior frequência listada até 2,0 GHz, desligado = 1,8 GHz, confirmação na
interface com padrão "Não"), chave lateral (leitura, inversão, valor inválido), modo
economia pela chave sobrepondo o perfil no lançamento, restauração do limite
de CPU quando um processo externo o sobe a 2,0 GHz durante o jogo, detecção de
pastas de Atari em formatos diferentes (`atari2600`, `A7800`).

Problemas encontrados e corrigidos por esses testes: núcleo mGBA com símbolo
`crc32` não resolvido (compilado agora sem zlib externo); nome de configuração
do Supafaust diferente do nome reportado pelo núcleo; extensões que alguns
núcleos não aceitam (`bs`, `st`, `cgb`, `sgx`, `ccd`, `dmg`, `agb`) removidas
do catálogo; escrita em arquivos de sysfs simulados sem truncar.

## Pendentes no aparelho físico (H)

Marque com data, firmware, versão do TriMux e observações ao executar.

| # | Teste | Status |
|---|---|---|
| H1 | Gravar a imagem com Rufus (Windows) e balenaEtcher; cartão reconhecido como TRIMUX | pendente |
| H2 | Primeiro boot com firmware v1.1.1: TriMux abre; tempo até o menu | pendente |
| H3 | SELECT ao ligar abre o sistema oficial; MENU › Sistema oficial; retorno ao TriMux no boot seguinte | pendente |
| H4 | Remover a pasta `trimui` → aparelho volta ao lançador oficial | pendente |
| H5 | Navegação com d-pad, analógico esquerdo, A/B/X/Y, L1/R1/L2/R2, SELECT/START/MENU/HOME; tela "Testar controles" mostra todos os botões; conferir números brutos | pendente |
| H6 | Volume (+/−) e brilho (MENU + +/−) no menu e nos jogos | pendente |
| H7 | Suspender/acordar com POWER no menu e durante um jogo; desligar; reiniciar | pendente |
| H8 | Abrir um jogo de cada plataforma (homebrew legal), áudio, imagem 4:3, fechar com MENU + START | pendente |
| H9 | Salvar/carregar estado (MENU + R1/L1), saves em `Saves/<PLATAFORMA>`, persistem após desligar | pendente |
| H10 | HOME abre o menu do RetroArch; L3 + R3 como alternativa | pendente |
| H11 | Trocar emulador por plataforma e por jogo; restaurar configuração recomendada | pendente |
| H12 | Perfis de energia: frequência lida em *Informações* corresponde ao perfil durante o jogo; volta ao Equilibrado no menu | pendente |
| H13 | Temperatura exibida plausível; proteção térmica registra no log quando aciona | pendente |
| H14 | LEDs: detectados, cor/brilho/efeito por zona; desligar "Controlar LEDs" devolve o padrão do firmware | pendente |
| H15 | Expandir partição num cartão de 32 GB e 128 GB; `chkdsk`/`fsck` limpo depois | pendente |
| H16 | Cartão cheio: aviso em Armazenamento; salvar estado falha sem corromper | pendente |
| H17 | Remover o cartão com o aparelho ligado no menu (recuperação ao reinserir/reiniciar) | pendente |
| H18 | Falha de inicialização simulada (renomear `trimux-ui`) → volta ao oficial | pendente |
| H19 | Atualização com `-update.zip` preservando `TriMuxData`, saves e favoritos | pendente |
| H20 | Atualizador oficial `.awimg` a partir do cartão TriMux (FAT32) | pendente |
| H21 | Uso de RAM no menu e em jogo (`/proc/meminfo` via *Informações*) | pendente |
| H22 | Sessões longas (60 min e 2 h) por plataforma com registro térmico ([DESEMPENHO.md](DESEMPENHO.md)) | pendente |
| H23 | Bateria: consumo no menu, em jogo leve e pesado, suspensão por 8 h | pendente |
| H26 | F1/F2 executam a ação escolhida; chave lateral muda o valor em *Botões extras* e aplica economia/LEDs/mudo no menu e no jogo | pendente |
| H28 | Chave lateral em "2,0 GHz": frequência máxima em *Informações* vai a 2000 MHz com a chave ligada e volta a ≤ 1800 MHz desligada; temperatura em sessão longa | pendente |
| H27 | Com um atalho FN oficial de CPU configurado, o TriMux volta a frequência ao limite do perfil | pendente |
| H25 | Ports: DOOM com Freedoom, Quake shareware, Cave Story freeware; um script `.sh` simples | pendente |
