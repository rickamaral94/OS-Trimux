# Instalação, primeiro uso, atualização e recuperação

> O TriMux roda **por cima do firmware oficial** da TrimUI. Gravar o cartão não
> altera a memória interna do aparelho. Para voltar ao sistema original, basta
> tirar o cartão.

## Requisitos

* TrimUI Brick Pro (TG4040) com firmware oficial **v1.1.1** (testado em
  ambiente simulado com essa versão; versões mais novas devem funcionar, mas
  confira as notas de versão).
* microSD de **2 GB ou mais** (recomendado: 16 GB+, classe A1/U1 ou melhor).
* Windows com [Rufus](https://rufus.ie) 3.x/4.x (ou balenaEtcher / Raspberry Pi
  Imager em qualquer sistema).
* **Use um cartão sem dados importantes**: a gravação apaga tudo.

## 1. Gravar com o Rufus

1. Baixe `TriMux-<versão>-brickpro.img.xz` e o arquivo `.sha256`.
2. (Recomendado) Confira o hash no PowerShell:
   `Get-FileHash .\TriMux-<versão>-brickpro.img.xz -Algorithm SHA256`
   e compare com o `.sha256`.
3. Abra o Rufus e escolha o cartão em **Dispositivo**. Se o leitor de cartão não
   aparecer, abra *Mostrar propriedades avançadas do dispositivo* e marque
   *Listar discos rígidos USB*.
4. Em **Seleção de boot** clique em **SELECIONAR** e escolha o `.img.xz`
   (não precisa descompactar). Os demais campos ficam bloqueados: é normal,
   a imagem já traz a partição.
5. Clique em **INICIAR** e confirme o aviso de que o cartão será apagado.
6. Ao terminar, o Windows mostra uma unidade **TRIMUX**. Ejete com segurança.

Alternativa sem Rufus (cartão já formatado em FAT32 ou exFAT): extraia o
conteúdo de `TriMux-<versão>-update.zip` na raiz do cartão. Funciona igual,
mas pule o passo de expansão abaixo.

## 2. Primeiro boot

1. Com o aparelho desligado, insira o cartão e ligue normalmente.
2. O firmware oficial inicia e, em poucos segundos, abre o TriMux.
3. O **assistente inicial** pede: idioma → criar pastas de jogos → procurar
   jogos → botão de confirmar → perfil de energia. Tudo pode ser pulado
   (START) e reaberto em *Configurações › Sistema › Assistente inicial*.
4. Para abrir o sistema oficial uma vez: **segure SELECT enquanto o TriMux
   abre** ou use *MENU › Abrir sistema oficial TrimUI*. O TriMux volta na
   próxima vez que o aparelho ligar.

### Expandir a partição (só para quem gravou a imagem)

A partição gravada tem 1 GiB. Para usar o cartão inteiro:
*Configurações › Armazenamento › Expandir partição*. O TriMux confirma o
tamanho final, deixa o cartão somente leitura, ajusta só a tabela de partição e
o setor de boot (nenhum arquivo é movido) e reinicia. Não desligue durante a
operação. Se a opção estiver cinza, a descrição explica o motivo (por exemplo,
o cartão já ocupa todo o espaço ou não foi gravado com a imagem do TriMux).

Também é possível expandir pelo computador com qualquer ferramenta que
redimensione FAT32 sem formatar.

## 3. Adicionar jogos e BIOS

1. Desligue o aparelho, retire o cartão e conecte ao computador.
2. Copie os jogos para `Roms/<PLATAFORMA>` (veja
   [ESTRUTURA_CARTAO.md](ESTRUTURA_CARTAO.md)) e as BIOS para `Bios/`.
3. Ejete o cartão, coloque no aparelho e ligue. Pastas alteradas são
   reindexadas automaticamente; também há *Configurações › Biblioteca ›
   Reindexar*. Reindexar nunca apaga jogos nem saves.

**Pelo Wi-Fi, sem tirar o cartão:** *Configurações › Rede e conexões ›
Transferir arquivos (FTP)* mostra um endereço `ftp://…` para abrir no
computador. O servidor só fica ligado enquanto a janela está aberta e, ao
fechá-la, a biblioteca é reindexada. Detalhes e cuidados em [REDE.md](REDE.md).

Jogos, BIOS e firmware de consoles **não são incluídos**. Use apenas cópias de
mídias e aparelhos que você possui.

## 4. Atualizar o TriMux sem perder nada

O TriMux guarda tudo do usuário fora da pasta do sistema:

| Pasta | Conteúdo | Na atualização |
|---|---|---|
| `TriMux/`, `trimui/` | sistema | **substituir** |
| `TriMuxData/` | configurações, favoritos, recentes, registros, config. do RetroArch | manter |
| `Roms/`, `Bios/`, `Saves/`, `States/`, `Screenshots/` | seus arquivos | manter |

Passos:

1. Faça uma cópia de `TriMuxData/`, `Saves/` e `States/` no computador
   (precaução).
2. Apague as pastas `TriMux` e `trimui` do cartão.
3. Extraia `TriMux-<nova versão>-update.zip` na raiz do cartão.
4. Ligue o aparelho. Se o formato das configurações mudar, o TriMux cria uma
   cópia `trimux.ini.bak-v<N>` antes de migrar.

**Não** grave a imagem `.img.xz` de novo para atualizar: isso apaga o cartão.

## 5. Recuperação

### 5.1 TriMux não abre / tela preta no TriMux

* O TriMux volta sozinho ao sistema oficial se o menu falhar 3 vezes em um
  minuto ou se o firmware não for de um Brick Pro. Se o menu não chegar a
  aparecer em 3 inicializações seguidas (por exemplo, travou e você desligou
  segurando POWER), a 4ª abre o sistema oficial ("modo seguro") e a seguinte
  tenta o TriMux de novo. O motivo fica em `TriMuxData/logs/trimux.log`.
* Segure **SELECT** ao ligar para abrir o sistema oficial.
* Segure **POWER por 6 s** para desligar à força (função do firmware oficial).
* No computador: apague a pasta `trimui` do cartão. O aparelho passa a abrir
  apenas o sistema oficial; nada mais muda.

### 5.2 Voltar ao sistema oficial definitivamente

Retire o cartão do TriMux (ou apague `trimui/` e `TriMux/`). Como nada foi
gravado na memória interna, não há outro passo.

### 5.3 Reinstalar o firmware oficial (procedimentos da TrimUI)

Só é necessário se o firmware oficial em si estiver com problemas — o TriMux
não o modifica.

**Atualizador oficial (`.awimg`)** — guia da TrimUI incluído no pacote v1.1.1:
1. Baixe `trimui_tg4040_<data>_v<versão>.7z` em
   <https://github.com/trimui/firmware_brickpro/releases> e extraia
   `trimui_tg4040.awimg`.
2. Copie o arquivo para a **raiz de um cartão FAT32** (o cartão do TriMux é
   FAT32 e serve).
3. Com o aparelho ligado e o cartão inserido: segure **Volume −**, aperte
   **POWER por 3 s**, solte o POWER e continue segurando Volume − até aparecer
   a barra verde. O aparelho reinicia ao final.

**Cartão de recuperação oficial (aparelho que não liga)**:
1. Baixe `sd_recovery_tg4040_brick_pro_<versão>.7z` (mesma página) e extraia a
   `.img` (2,5 GB).
2. Grave em um **segundo cartão, vazio**, com o Rufus ou Win32DiskImager.
3. Desconecte o carregador, segure POWER por 20 s para garantir que está
   desligado, insira o cartão e conecte o carregador **sem apertar botões**.
   A barra verde aparece, a reprogramação termina e o aparelho desliga.
4. Retire esse cartão. **Atenção:** enquanto estiver com essa imagem, ele
   reprograma qualquer Brick Pro em que for inserido. Para reutilizá-lo,
   grave por cima (por exemplo, com a imagem do TriMux, que zera a área de boot)
   ou limpe com o PhoenixCard incluído no pacote da TrimUI.

## 6. Desinstalar

Apague `trimui/`, `TriMux/` e, se quiser, `TriMuxData/` do cartão. Jogos,
saves e BIOS continuam onde estão e funcionam no sistema oficial ou em outros
firmwares que usem as mesmas pastas.

## Perguntas frequentes

### Dá para instalar o TriMux na memória interna, sem o cartão?

**Não nesta versão, e não há passo a passo para isso de propósito.** O TriMux
roda só a partir do microSD.

O firmware oficial procura a interface em dois lugares, nesta ordem: a pasta
`trimui/` do cartão (o que o TriMux usa) e um arquivo de imagem na memória
interna, `/mnt/UDISK/trimui.img`. Esse arquivo interno é o lugar da própria
interface da TrimUI, gravado pelo atualizador oficial. Instalar o TriMux ali
teria estes problemas:

* **Some a volta ao sistema oficial.** Hoje basta tirar o cartão ou segurar
  SELECT ao ligar. Na memória interna, o TriMux ocuparia o lugar da interface
  oficial.
* **Uma falha se repete a cada inicialização** e tirar o cartão não resolve.
  A saída seria o cartão de recuperação da TrimUI, que regrava a memória
  interna inteira.
* **O formato desse arquivo não é documentado** pela TrimUI, e o TriMux não
  grava na memória interna com base em suposições (veja
  [VIABILIDADE.md](VIABILIDADE.md)).
* **O cartão continua necessário:** jogos, BIOS e saves ficam nele.

Pode ser estudado depois que os testes no aparelho passarem
([TESTES.md](TESTES.md)), com o arquivo de atualização oficial para entender
o formato, um cartão de recuperação já testado e uma volta automática para a
interface oficial em caso de falha.
