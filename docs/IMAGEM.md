# Imagem dos jogos

Em *Configurações › Emuladores*, cada plataforma tem uma seção **Imagem dos
jogos**. No topo da lista, **Imagem de todas as plataformas** muda o formato e
o visual de todas de uma vez. A mesma tela também abre pelo jogo: **START ›
Imagem da plataforma**.

Use ◀ ▶ para trocar cada opção. O painel à direita explica o efeito da escolha.
Tudo vale a partir do próximo jogo aberto.

**Estado:** testado só no computador (Brick Pro simulado; veja
[TESTES.md](TESTES.md), H57–H61). O efeito na tela e o desempenho ainda
precisam ser conferidos no aparelho.

## Opções

| Opção | Escolhas | Onde aparece |
|---|---|---|
| Formato da tela | Original (recomendado) · Pixels perfeitos · Tela cheia | todas as plataformas |
| Visual | Nítido · Suave · Pixel suave · Tela de LCD · TV antiga | todas as plataformas |
| Resolução interna | Original · 2x · 3x / igual à tela | PlayStation, DOOM, Quake |
| Cores do aparelho original | Ligado · Desligado | Game Boy, Game Boy Color, Game Boy Advance |
| Rastro de tela LCD | Ligado · Desligado | Game Boy, GBC, GBA, Game Gear, Mega Drive/Master System (Genesis Plus GX), Lynx |
| Texturas HD | Ligado · Desligado | NES (FCEUmm) |

Enquanto você não muda uma opção, o TriMux não mexe nela: vale o que estiver
no RetroArch, como antes.

### Formato da tela

* **Original:** o formato do console, o maior possível na tela de 4:3.
* **Pixels perfeitos:** escala inteira (todos os pixels do mesmo tamanho).
  Sobram bordas pretas.
* **Tela cheia:** estica para a tela toda e pode distorcer.

### Visual (filtros)

Os três efeitos são shaders GLSL leves do projeto libretro/glsl-shaders,
escritos para GPUs de celular. Eles ficam em `TriMux/retroarch/shaders/`:

| Visual | Shader | Bom para |
|---|---|---|
| Nítido | nenhum | quem gosta do pixel puro |
| Suave | filtro bilinear do RetroArch | esconder os quadradinhos (borra o texto) |
| Pixel suave | `sharp-bilinear-simple` | quase todo jogo 2D: nítido e sem serrilhado |
| Tela de LCD | `zfast-lcd` | portáteis: Game Boy, GBA, Game Gear, NGP, WonderSwan, Lynx |
| TV antiga | `zfast-crt` | consoles de mesa: NES, SNES, Mega Drive, PS1, arcade |

### Resolução interna

Só existe onde o emulador desenha em 3D ou tem um motor próprio:

* **PlayStation** (PCSX ReARMed): *2x* liga o "Enhanced Resolution" do
  renderizador NEON. Os modelos 3D ficam bem mais nítidos, mas é pesado:
  se um jogo ficar lento, volte para *Original*.
* **DOOM** (PrBoom): 320×200, 640×400 ou 960×600.
* **Quake** (TyrQuake): 320×240, 640×480 ou 1024×768.

Nos consoles 2D (NES, SNES, Mega Drive, GBA…) a resolução do jogo é fixa. O
que melhora a imagem neles é o **Visual**.

### Cores e rastro de LCD

* **Cores do aparelho original:** correção de cor do Gambatte, mGBA e gpSP.
  Imita a tela menos saturada do portátil.
* **Rastro de tela LCD:** mistura de quadros. Alguns jogos dependem disso para
  mostrar transparências e não piscar, por exemplo F-Zero e Wave Race.

### Texturas HD (NES)

O FCEUmm lê pacotes de texturas no formato **Mesen HD Pack**, que trocam os
gráficos por versões em alta resolução. Às vezes também trazem música nova.

1. Baixe o pacote do jogo. Ele não vem com o TriMux, e cada pacote tem seus
   próprios termos.
2. Copie a pasta para `Bios/HdPacks/<nome do arquivo do jogo, sem a
   extensão>/`, de forma que o `hires.txt` fique dentro dela.
   Exemplo: `Roms/FC/Jogo.nes` → `Bios/HdPacks/Jogo/hires.txt`.
3. Deixe o jogo como `.nes`, fora do `.zip`: dentro de um zip o nome que o
   núcleo enxerga é outro.
4. Em *Configurações › Emuladores › Nintendo (NES / Famicom)*, **Texturas HD**
   fica ligado por padrão. A tela mostra quantos pacotes o TriMux encontrou.

Pacotes grandes usam mais memória e demoram mais para abrir. O Nestopia não
lê pacotes HD: para usá-los, o emulador da plataforma (ou do jogo) precisa
ser o FCEUmm.

## Como funciona

As escolhas ficam em `TriMuxData/config/trimux.ini`, na seção
`[video.<PLATAFORMA>]`. Ao abrir um jogo, o `trimuxctl`:

1. escreve as linhas de vídeo do RetroArch (`aspect_ratio_index`,
   `video_scale_integer`, `video_smooth`, `video_shader_enable`) na
   configuração temporária em RAM, a mesma dos diretórios de saves;
2. passa o shader escolhido com `--set-shader`, só para aquele jogo. Se o
   arquivo do shader não existir, o jogo abre com a imagem simples;
3. grava as opções do núcleo (resolução, cores, rastro, texturas) no arquivo
   de opções do próprio núcleo, `TriMuxData/retroarch/config/<núcleo>/<núcleo>.opt`.
   Só as chaves escolhidas no TriMux mudam; o resto do arquivo fica como
   estava.

Limites:

* Uma *override* do RetroArch para o núcleo ou o jogo, criada pelo menu do
  RetroArch, vence as escolhas do TriMux.
* Um arquivo de opções só daquele jogo (`<jogo>.opt`) também vence.
* "Restaurar configuração recomendada" arquiva a pasta do núcleo, como antes.
  As escolhas de imagem continuam no `trimux.ini` e voltam a ser aplicadas no
  próximo jogo.
