# Ferramentas do TriMux

Na aba **Aplicativos**, acima dos aplicativos instalados, ficam as ferramentas
do próprio TriMux. Nenhuma delas precisa de internet, a não ser *Arquivos pelo
navegador*, que usa a sua rede Wi-Fi.

**Estado: testadas só em ambiente simulado** (veja *Como foi testado*, no fim
desta página). Ainda não foram testadas num Brick Pro físico.

## Estatísticas de jogo

Mostra quanto tempo você jogou:

* tempo total, número de sessões e quantos jogos diferentes você jogou;
* o último jogo aberto;
* os 10 jogos mais jogados;
* o tempo por plataforma.

O tempo vem de `TriMuxData/state/plays.ini`, que o TriMux preenche desde a
versão 0.4.7 a cada sessão de mais de 10 segundos (veja *Ordem dos jogos ›
Mais jogados*). Só aparecem jogos que ainda estão no cartão. A num jogo abre
as opções dele, de onde dá para jogar.

Por SSH: `trimuxctl stats`.

## Jogo surpresa

Escolhe um jogo ao acaso e mostra ele na lista da plataforma, pronto para
jogar com A. Dá preferência a jogos que você nunca abriu. Quando já jogou
todos, vale qualquer um. Aperte de novo para sortear outro. É o mesmo sorteio
que pode ser colocado no F1 ou F2 (*Configurações › Botões extras*).

## Gerenciador de arquivos

Navega pelas pastas do cartão:

* **A** numa pasta entra nela e **B** volta uma pasta;
* o painel mostra o tamanho e a data de cada arquivo;
* **A** num arquivo pergunta se você quer apagar. A resposta já vem marcada
  em *Não*, e nada é apagado sem confirmar;
* pastas com muitos arquivos são mostradas em páginas de 56 itens.

As pastas `TriMux`, `TriMux.old`, `trimui` e `trimui.old` (o sistema do TriMux,
a versão guardada para voltar atrás e o sistema oficial) são **somente
leitura**. Apagar um jogo atualiza a lista de jogos na hora.

Copiar e mover arquivos não está no aparelho. Para isso, use *Arquivos pelo
navegador* ou FTP.

## Arquivos pelo navegador

Com o Wi-Fi conectado, abra *Aplicativos › Arquivos pelo navegador*. A tela
mostra um endereço como `http://192.168.0.7:8080`. Abra esse endereço no
navegador do computador ou do celular, na mesma rede. A página permite:

* navegar pelas pastas e ver o espaço livre;
* **baixar** arquivos (saves, capturas, registros);
* **enviar** arquivos pelo botão *Enviar arquivos* ou arrastando para a
  página, com barra de progresso. Arquivos grandes, como jogos de
  PlayStation, funcionam. Se já existe um arquivo com o mesmo nome, a página
  pergunta antes de substituir;
* criar pastas;
* apagar arquivos e pastas vazias, sempre com confirmação.

Segurança:

* O servidor só funciona **enquanto a janela estiver aberta** no aparelho.
  A ou B fecha a janela e desliga o servidor. Enquanto ela está aberta, o
  aparelho não desliga sozinho por inatividade.
* **Não há senha**, como no FTP. Qualquer aparelho da mesma rede pode ver e
  mudar os arquivos enquanto a janela estiver aberta. Use em redes de
  confiança.
* As mesmas pastas do gerenciador são somente leitura. Caminhos fora do
  cartão são recusados.
* Mudanças (enviar, criar ou apagar) só são aceitas da própria página. Outro
  site aberto no mesmo navegador não consegue apagar nada, e um link ou
  imagem nunca muda arquivos.
* O envio é gravado num arquivo temporário escondido. O arquivo só ganha o
  nome final quando chega inteiro. Se a conexão cair, nada pela metade
  fica no cartão.
* Ao fechar a janela, a lista de jogos é atualizada com o que foi enviado.

Como funciona: o servidor é a BusyBox httpd do próprio firmware, e nada é
instalado. A página fica em `TriMux/share/web/index.html` e é copiada para a
memória RAM (`/tmp/trimux/www`) junto com um pequeno programa CGI
(`trimuxctl webcgi`), que faz todas as operações com arquivos.

## Limpeza do cartão

Procura arquivos que não servem para nada no aparelho, mostra quantos são, o
tamanho e alguns exemplos, e só apaga depois da sua confirmação:

| Categoria | O que apaga | Vem marcada? |
|---|---|---|
| Arquivos do computador | `._arquivo`, `.DS_Store`, `.Trashes`, `.Spotlight-V100`, `.fseventsd`, `.TemporaryItems` (macOS) e `Thumbs.db` (Windows) | sim |
| Registros antigos | `trimux.log.1`, o registro do RetroArch e os registros de desempenho (`TriMuxData/logs/perf/*.csv`) | não |
| Restos de instalações | `.trimux-new` e `.trimux-store-new` de uma atualização ou instalação interrompida, e `TriMuxData/.store-download.zip` | sim |

O que **nunca** é apagado por esta tela:

* jogos, saves, estados salvos, BIOS, capas e configurações;
* as pastas do TriMux e do sistema oficial;
* a versão guardada para voltar atrás (`TriMux.old`).

Os restos de instalação só entram na lista quando nenhuma atualização ou
instalação está em andamento.

Por SSH: `trimuxctl clean scan` (só mostra) e
`trimuxctl clean run computer logs temp` (apaga as categorias escolhidas).

## Como foi testado

* **Testes automáticos** (no computador, não no aparelho):
  * estatísticas, sorteio e pastas protegidas (`tests/unit`);
  * o programa CGI com o ambiente que a httpd passa: listar, baixar, enviar
    com e sem substituir, envio interrompido, criar pasta, apagar, caminhos
    fora do cartão, pastas protegidas, pedidos de outro site;
  * a limpeza (o que é achado, o que é apagado e o que fica);
  * as telas no menu, com entrada simulada (`tests/py/test_tools.py`).
* **Em QEMU**: a BusyBox httpd do firmware v1.1.1 (aarch64) roda sob emulação
  e serve a página e o CGI. Verifica envio e download de 20 MB com conteúdo
  idêntico e as proteções (`scripts/smoke_qemu.sh`, item 5). É uma verificação
  emulada; o Wi-Fi real e o desempenho do cartão só podem ser confirmados no
  aparelho (TESTES.md, H70–H74).
