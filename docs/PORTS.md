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
confirmação antes de abrir. Nenhum port foi validado neste aparelho.

## 3. PortMaster integrado (a partir da 0.4.8)

O [PortMaster](https://portmaster.games) oferece centenas de ports de jogos
de PC, como Stardew Valley, Celeste e Half-Life. Ele precisa de bash, Python
e várias bibliotecas que o firmware da TrimUI não tem. Por isso o TriMux usa
o pacote [MinUI PortMaster](https://github.com/ben16w/minui-portmaster)
(licença MIT), que traz tudo junto. O pacote **não vem na imagem**: é baixado
quando você pede.

1. Conecte-se ao Wi-Fi e abra **Aplicativos › Loja de aplicativos ›
   PortMaster › Instalar** (149 MB).
   * O TriMux baixa a versão fixada (2.14.0) direto do GitHub do projeto.
   * Antes de gravar, confere o SHA-256.
   * Extrai em `Emus/tg5040/PORTS.pak` no cartão.
   * Cria a entrada `Roms/PORTS/Portmaster.sh`.
2. Em **Ports**, abra **Portmaster**. Na primeira vez ele descompacta suas
   ferramentas, o que leva alguns minutos. Depois aparece a loja de ports do
   PortMaster.
3. Os ports instalados viram scripts em `Roms/PORTS/` e aparecem na mesma
   lista de Ports. Os dados deles ficam em `Roms/PORTS/.ports/`.
4. Ports que exigem arquivos do jogo original: copie os arquivos para a pasta
   do port em `Roms/PORTS/.ports/<port>/`, seguindo a página do port em
   portmaster.games.

Como o TriMux escolhe o jeito de abrir cada script de Ports:

* `Portmaster.sh` e scripts que usam a pasta de controle do PortMaster
  (`controlfolder`) abrem pelo PortMaster, se ele estiver instalado.
* Os demais scripts continuam com o `/bin/sh` do firmware, como antes.
* Dá para trocar por jogo em **SELECT › Emulador**.

Por baixo, o TriMux chama o `launch.sh` do pacote com as variáveis que ele
espera do MinUI:

* `SDCARD_PATH`;
* `PLATFORM=tg5040`;
* `USERDATA_PATH` e `SHARED_USERDATA_PATH` em `TriMuxData/portmaster`;
* `LOGS_PATH` em `TriMuxData/logs`.

Enquanto o PortMaster está aberto, o próprio pacote:

* ajusta o governador e os limites da CPU (até 1,8 GHz) e devolve os valores
  anteriores ao sair;
* monta `Roms/PORTS/.ports` em `/.ports_temp`, só durante a sessão.

Tudo fica no cartão. Na memória interna nada é gravado.

Para remover, use **Aplicativos › Loja › PortMaster › Remover**. Isso apaga
o pacote e a entrada `Portmaster.sh`. Os ports instalados e os dados deles
continuam no cartão.

O PortMaster e seus ports não são suportados oficialmente neste aparelho.
Não reporte problemas ao projeto PortMaster: o MinUI PortMaster pede o mesmo.

Segurança: um port é um programa com acesso total ao aparelho. Use apenas
ports de fontes confiáveis.

## 4. Ports do sistema oficial da TrimUI (a partir da 0.5.1)

O cartão original da TrimUI guarda os ports na pasta `Ports` da raiz do
cartão, uma pasta por jogo, com um `config.json` (nome, ícone e o script de
início, normalmente `launch.sh`) e os arquivos do jogo.

* O TriMux encontra esses ports em **`Ports/`** (a mesma pasta do sistema
  oficial) e também em **`Roms/PORTS/<jogo>/`**.
* O nome que aparece é o `label` do `config.json`, e o ícone da pasta vira a
  capa no painel.
* O jogo começa pelo script indicado em `launch`, executado de dentro da
  própria pasta, como no sistema oficial.
* Prefira copiar a pasta `Ports` inteira para a raiz do cartão do TriMux,
  como estava no cartão original. Alguns scripts usam o caminho completo
  `/mnt/SDCARD/Ports/...`, e numa outra pasta eles não acham os próprios
  arquivos.
* Pastas sem `config.json` válido (sem `launch` ou com um script que não
  existe) são ignoradas.
