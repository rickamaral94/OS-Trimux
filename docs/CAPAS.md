# Capas dos jogos

*Configurações › Biblioteca › Capas dos jogos.* O TriMux baixa capas pela
internet e mostra a do jogo selecionado no painel da lista.

> Estado: testado só em ambiente simulado (servidor de imagens e Wi-Fi
> falsos). O download real no aparelho é o teste H38–H40 em [TESTES.md](TESTES.md).
> As capturas usam uma imagem gerada, não capas reais.

| Lista com capa | Opções |
|---|---|
| ![Capa no painel](img/18-capas.png) | ![Opções de capas](img/19-capas-opcoes.png) |

## Como usar

1. Conecte o Wi-Fi (*Configurações › Rede e conexões*).
2. *Biblioteca › Capas dos jogos › Baixar capas agora*.
3. O download roda em segundo plano. Você pode voltar à lista e até jogar:
   durante o jogo ele pausa e continua quando você volta ao menu. A tela de
   capas mostra o andamento ("120 de 450, 98 encontradas").

**Capas automáticas** (desligado por padrão): com o Wi-Fi conectado, as capas
de jogos novos são baixadas sozinhas ao ligar o aparelho, depois de
reindexar a biblioteca e ao conectar a uma rede.

## De onde vêm as imagens

Do [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`thumbnails.libretro.com`), a mesma base que o RetroArch usa: gratuita, sem
conta e sem limite de uso. O TriMux não inclui nenhuma imagem; cada uma é
baixada sob demanda para o seu cartão.

*Tipo de imagem:* caixa do jogo (padrão), tela durante o jogo ou tela de
título.

### Por que não ScreenScraper?

O ScreenScraper identifica jogos pelo conteúdo do arquivo (hash), mas a API
exige credenciais de desenvolvedor que não podem ser publicadas num projeto
aberto. Pode ser adicionado depois como opção com a conta de cada pessoa.

## Como o jogo é reconhecido

Pelo **nome do arquivo**, como no RetroArch:

* Arquivos no padrão No-Intro/Redump funcionam melhor:
  `Celeste Classic (World).gba`, `Final Fantasy VII (USA) (Disc 1).cue`.
* Nomes sem região (`Celeste.gba`) são tentados com `(USA)`, `(World)`,
  `(Europe)` e `(Japan)`.
* Jogos em vários discos também são procurados sem o `(Disc N)`.
* **Arcade e Neo Geo** (`mslug.zip`): o nome curto é traduzido para o título
  ("Metal Slug - Super Vehicle-001") pela lista do FinalBurn Neo que vai no
  cartão (`TriMux/share/arcade-names.tsv`).
* Ports e DOOM/Quake/Cave Story não têm capas nessa base.

Jogos não encontrados ficam numa lista
(`TriMuxData/cache/covers-missing.txt`) e não são procurados de novo a cada
vez. Depois de renomear arquivos, use **Tentar de novo os não encontrados**.

## Onde ficam

`Imgs/<pasta do jogo>/<nome do arquivo>.png`, por exemplo
`Imgs/GBA/Celeste Classic (World).png`. É a **mesma pasta do sistema
oficial da TrimUI**, então as capas aparecem nos dois sistemas.

* As imagens são reduzidas para no máximo 480×480 antes de gravar
  (≈ 100–250 KB cada), para economizar espaço e abrir rápido.
* Capas que você mesmo colocar em `Imgs/` (PNG ou JPEG com o mesmo nome do
  jogo) também aparecem; o TriMux nunca substitui uma capa existente.
* *Mostrar capas na lista* desliga a exibição sem apagar nada.

## Segurança

* Download só por HTTPS, com o certificado do servidor sempre verificado
  (`TriMux/share/cacert.pem`, a lista de autoridades da Mozilla, porque o
  firmware não tem nenhuma).
* Arquivos maiores que 8 MB ou que não sejam PNG/JPEG são descartados.
* O download roda com prioridade baixa e para sozinho depois de 3 erros de
  rede seguidos.

## Configurações (`TriMuxData/config/trimux.ini`)

```ini
[covers]
show = 1        ; mostrar capas na lista
kind = boxart   ; boxart | snap | title
auto = 0        ; capas automáticas
```

Linha de comando (por SSH): `trimuxctl scrape` (baixa),
`trimuxctl scrape --retry`, `trimuxctl scrape --status`,
`trimuxctl scrape --stop`.
