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

## Resultado da execução desta versão (0.4.2)

| Conjunto | Categoria | Resultado |
|---|---|---|
| `tests/unit/test_core.c` — 304 verificações (INI, gravação atômica, caminhos, idiomas, catálogo, varredura da biblioteca, `.m3u`, índice, favoritos/recentes, perfis de energia, LEDs, proteção térmica, validação do pedido de lançamento, migração de configurações, sysinfo, rede: leitura da saída do `wpa_cli` com nomes escapados, redes ocultas e repetidas, SSID em hex, regra da senha WPA, conta do RetroAchievements, comandos enviados a ferramentas falsas, tempo limite de processos; registro de desempenho: amostras, resumo, nomes com `;`, valores ausentes, consumo por hora, ordem das sessões, limite de arquivos, apagar só os próprios arquivos; capas: regra de nomes do libretro-thumbnails, endereços com acentos e caracteres especiais, nomes alternativos (região, disco), caminho em `Imgs/`, lista de títulos de arcade, redução de imagem mantendo a proporção sem ampliar, arquivo que não é imagem; atualização: vetores oficiais do SHA-256 (inclusive 1 milhão de "a"), comparação de versões, escolha do lançamento ignorando rascunhos, pré-lançamentos (quando desligados), lançamentos sem o pacote, endereços sem HTTPS e nomes de versão inválidos, notas sem markdown e com `\uXXXX`, JSON quebrado, leitura do arquivo de hashes), com ASan/UBSan | A | **passou** |
| `tests/py/test_ctl.py` — `trimuxctl` com Brick Pro simulado: perfis ≤ 1,8 GHz e sem governador `performance`, arquivos térmicos intocados, LEDs só quando ativados (e, quando ativados, chave geral `enable` ligada, animações por quadros desligadas e repetições 30000, contra o estado que o firmware deixa no boot; `leds keep` restaura cor, brilho, efeito e a chave geral depois que o "firmware" reaplica as opções dele ao mudar o `system.json`, apaga tudo inclusive a chave geral quando o usuário desliga os LEDs, não mexe em nada sem "Controlar LEDs" e termina com o supervisor), contador de boot/modo seguro, varredura | A/S | **passou** |
| `tests/py/test_ctl.py` (rede) — `trimuxctl net`: SSH do firmware desligado por padrão no boot, Wi-Fi só muda se o usuário escolheu, ligar Wi-Fi usa os mesmos argumentos do `/etc/init.d/wpa_supplicant`, Bluetooth iniciado da pasta e com as bibliotecas do firmware, aparelho sem Wi-Fi | S | **passou** |
| `tests/py/test_supervisor.py` — scripts de boot com comandos do firmware simulados: port `.sh` roda da própria pasta com limite de energia e scripts auxiliares não aparecem; modelo errado → oficial, laço de falhas → oficial, modo seguro, lançamento aplica limite **antes** do emulador e restaura depois, perfil automático por emulador, pedido adulterado recusado, desligar, `preload.sh`, guarda de reinício do `MainUI`, POSIX/LF/`sh -n` | S | **passou** |
| `tests/py/test_image.py` — imagem: MBR, partição 0x0C em 2048, área de boot zerada (sem `eGON.BT0`), FAT32 válido (`fsck.fat`), nomes longos; expansão para cartão de 8 GiB com `fsck` limpo e escrita de 200 MB no espaço novo; expansão com o cartão marcado "em uso" pelo Linux (byte 65 do setor de boot principal, como fica enquanto o TriMux roda); marcador de primeiro boot só na imagem; a imagem real da 0.4.1 num cartão simulado de 128 GB, marcado "em uso", cresce de 1 GiB para 250.067.584 setores (≈119 GiB) com `fsck.fat` limpo; recusa de cartões com 2 partições ou sem FAT; limite de 1 TiB; imagem final sem ROMs/BIOS/firmware | A (contêiner) | **passou** |
| `tests/py/test_data.py` — todas as chaves de tradução usadas existem em pt_BR, especificadores de formato iguais entre idiomas, catálogo consistente, nenhum arquivo de ROM/imagem no repositório | A | **passou** |
| `tests/py/test_ui.py` — menu com entradas roteirizadas: assistente, lançamento grava pedido válido e recentes, favorito e emulador por jogo persistem, confirmação para sistema oficial/desligar, configuração corrompida não trava; rede: ligar Wi-Fi, procurar, digitar a senha no teclado e conectar (senha fora do log e do `trimux.ini`), senha curta não é enviada, FTP só roda com a janela aberta e para quando o menu fecha, SSH pede confirmação (padrão "Não"), conta do RetroAchievements com aspas recusada | S | **passou** (no computador com SDL; ignorado no contêiner, que não tem SDL nativo) |
| `scripts/smoke_qemu.sh` — com bibliotecas do firmware v1.1.1 e `LD_BIND_NOW=1`: `trimuxctl` identifica o Brick Pro; `trimux-ui` resolve todos os símbolos da libSDL2/glibc do firmware (o SDL do firmware só tem o driver de vídeo "mali", por isso a janela não abre em QEMU); `retroarch --features` (com SSL ativo; suporte a RetroAchievements confirmado no binário); os 20 núcleos carregam e o nome/extensões batem com o catálogo | S | **passou** (5/5) |
| Registro de desempenho (em `test_supervisor.py`) — desligado não grava nada; ligado grava sessão e amostras com temperatura e bateria do aparelho simulado, só o nome do jogo; FPS e log do RetroArch chegam à configuração do jogo só quando ativados. Menu (em `test_ui.py`): opções salvas, tela de sessões, apagar pede confirmação e não toca em outros arquivos | S | **passou** |
| Capas (em `test_ctl.py`, com `curl` falso) — sem Wi-Fi não baixa; baixa e reduz para 480×480; não procura de novo os não encontrados (só com `--retry`); arcade usa o título; tipo de imagem respeitado; automático só quando ligado; para após 3 erros de rede sem marcar como não encontrado. Menu (em `test_ui.py`): capa aparece no painel e some com a opção desligada; download pelo menu sem Wi-Fi explica, com Wi-Fi baixa em segundo plano; opções salvas | S | **passou** |
| Atualização online (`tests/py/test_update.py`, GitHub simulado pelo `curl` falso) — encontra a versão nova e grava as notas; respeita "pré-lançamentos" e "verificar ao ligar"; sem Wi-Fi e sem rede informam o erro; instalação troca `TriMux`/`trimui`, guarda `.old`, mantém saves e jogos, remove a pasta temporária e marca a atualização a confirmar; hash errado, pacote de outra versão e bateria a 20% não alteram nada (bateria baixa nem baixa o pacote); `boot ok` só confirma depois que o `MainUI` novo rodou; voltar à versão anterior; `MainUI` restaura a versão anterior na 4ª tentativa sem menu e registra no log, recupera uma troca de pastas interrompida e não faz nada em boots normais; pacote `.tar.gz` reproduzível e sem dados do usuário; o pacote real da 0.4.0, extraído pelo `busybox tar` do firmware v1.1.1 em QEMU, é idêntico à árvore do cartão e o `busybox sha256sum` do firmware confere o hash. Menu: procurar, confirmação com padrão "Não", instalar com andamento e reiniciar no fim, jogos bloqueados até reiniciar, opções salvas, voltar à versão anterior | S | **passou** |
| Uso do cartão inteiro (em `test_ctl.py`, `test_supervisor.py`, `test_ui.py`) — automático só com o marcador da imagem; tentativa única, sem travar o menu quando o cartão não pode ser identificado; cartão encontrado pelo nome da montagem ou por `/sys/dev/block` (só a partição 1 de um `mmcblk`); pedido pelo menu que falha volta ao menu com o motivo no log; aviso depois do reinício com o espaço novo, ou de falha | S | **passou** |
| Conta do RetroAchievements (em `test_supervisor.py`) — só chega ao RetroArch quando ativada, nunca vai para o log | S | **passou** |
| Capas contra o servidor real (manual, no computador) — os 22 repositórios do catálogo existem em `thumbnails.libretro.com`, e endereços gerados pelo TriMux para 5 jogos (SNES, PS com disco, GBA e arcade com `:` no título) responderam HTTP 200 | A | **passou** |
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
| H14 | LEDs: detectados, cor/brilho/efeito por zona, inclusive com a opção de LED do sistema oficial desligada; continuam acesos depois de 1 minuto e depois de voltar de um jogo; desligar "Controlar LEDs" devolve o padrão do firmware | pendente |
| H48 | LEDs continuam como escolhidos depois de mudar o volume várias vezes, de abrir e fechar um jogo e de 10 minutos; "apagar todos" deixa tudo escuro; com bateria abaixo de 10% o aviso vermelho do firmware aparece | pendente |
| H49 | Chave lateral em 2,0 GHz: perfil mostra "Desempenho máximo (2,0 GHz)" e não muda (menu, menu rápido, F1/F2); desligar a chave libera a escolha | pendente |
| H47 | Tela de início do TriMux aparece logo depois do logotipo da TrimUI e enquanto os jogos são indexados | pendente |
| H15 | Primeiro boot de um cartão de 32 GB e de 128 GB gravado com a imagem: expansão automática, um reinício, aviso com o espaço total; `chkdsk`/`fsck` limpo depois. Repetir pela opção do menu num cartão regravado | pendente |
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
| H29 | Wi-Fi: ligar/desligar, procurar, conectar a WPA2 com senha, rede aberta, senha errada (mensagem), esquecer; rede salva no TriMux aparece no sistema oficial e vice-versa | pendente |
| H30 | Wi-Fi ligado/desligado pelo TriMux se mantém após reiniciar | pendente |
| H31 | Bluetooth: controle/fone pareado no sistema oficial reconecta no TriMux com "Bluetooth da TrimUI" ligado; consumo de memória | pendente |
| H32 | RetroAchievements: login ao abrir um jogo com conquistas, notificação de conquista, modo hardcore bloqueia estados; sem Wi-Fi o jogo abre normalmente | pendente |
| H33 | FTP: copiar um jogo pelo Windows Explorer e FileZilla; servidor para ao fechar a janela; não acessa fora do cartão | pendente |
| H34 | SSH: desligado após boot com o TriMux; ligar/desligar pelo menu; SFTP com WinSCP | pendente |
| H35 | Registro de desempenho: sessão de 30 min gera `sessions.csv` e amostras com temperatura, CPU e bateria coerentes com *Informações*; abre em planilha | pendente |
| H36 | FPS na tela e log do RetroArch em `TriMuxData/logs/retroarch/` quando ativados; nada gravado quando desligados | pendente |
| H38 | Capas: baixar pelo menu com Wi-Fi; andamento na tela; capas aparecem na lista e no sistema oficial (`Imgs/`) | pendente |
| H39 | Capas automáticas ao ligar e após reindexar; pausa durante o jogo; parar o download | pendente |
| H40 | Capas de arcade (`mslug.zip`) e de PlayStation com vários discos | pendente |
| H41 | Atualização online: procurar e instalar uma versão nova pelo menu com Wi-Fi real (GitHub e HTTPS verdadeiros); andamento na tela; reinicia na versão nova; saves e configurações intactos | pendente |
| H42 | Voltar à versão anterior pelo menu e atualizar de novo | pendente |
| H43 | Volta automática: versão com menu quebrado (ex.: `trimux-ui` apagado de propósito) é trocada pela anterior na 4ª tentativa | pendente |
| H44 | Atualização recusada com bateria abaixo de 30% sem carregador; aceita no carregador | pendente |
| H45 | Desligar no meio do download: na volta, versão atual intacta; a tentativa seguinte funciona | pendente |
| H46 | "Verificar ao ligar": aviso no menu quando há versão nova; nada instalado sozinho | pendente |
| H37 | Comparação de perfis (Economia × Equilibrado × 2,0 GHz) no mesmo jogo, 2 sessões cada, seguindo [DIAGNOSTICO.md](DIAGNOSTICO.md) | pendente |
