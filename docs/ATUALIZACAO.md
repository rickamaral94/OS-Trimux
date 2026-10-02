# Atualização online

A partir da 0.4.0, o TriMux se atualiza pelo Wi-Fi. Você não precisa gravar o
cartão de novo nem copiar arquivos pelo computador. Jogos, BIOS, saves,
estados, capas e configurações continuam como estão.

**Estado:** testado só no computador, com um Brick Pro simulado e um GitHub
simulado (veja [TESTES.md](TESTES.md), H41–H46). Ainda falta confirmar no
aparelho.

![Atualização](img/20-atualizacao.png)

<sub>Captura ilustrativa (versões fictícias), renderizada no computador.</sub>

## Como usar

1. Conecte-se a uma rede Wi-Fi (*Configurações › Rede e conexões*).
2. Abra *Configurações › Sistema › Atualização* e escolha
   **Procurar atualização**.
3. Se houver uma versão nova, aparece **Instalar** com o número da versão. O
   painel à direita mostra as notas da versão.
4. Escolha **Instalar** e confirme. O download roda em segundo plano, com a
   porcentagem na tela. Enquanto ele roda, os jogos ficam bloqueados.
5. No fim, o TriMux pergunta se pode reiniciar. A nova versão vale a partir do
   próximo boot.

Opções:

* **Incluir pré-lançamentos** (ligado por padrão): oferece também versões de
  teste. Todas as versões do TriMux até agora são pré-lançamentos, então
  desligar isto faz o atualizador não oferecer nada por enquanto.
* **Verificar ao ligar** (desligado por padrão): no boot, se o Wi-Fi conectar
  em até 60 s, consulta o GitHub uma vez e mostra um aviso no menu quando há
  versão nova. **Nunca instala sozinho.**
* **Voltar à versão anterior**: aparece depois de uma atualização. Troca pela
  versão guardada e reinicia.

## O que acontece por baixo

1. O atualizador consulta a lista de lançamentos do projeto pela API pública
   do GitHub (`api.github.com/repos/rickamaral94/OS-Trimux/releases`, sem
   conta e sem token). Rascunhos são ignorados. Pré-lançamentos só entram se a
   opção estiver ligada. A versão escolhida é a maior que seja mais nova que a
   instalada (`TriMux/VERSION`).
2. A versão só é oferecida se trouxer dois arquivos:
   `TriMux-<versão>-update.tar.gz` (o pacote) e
   `TriMux-<versão>-brickpro.sha256` (os hashes). As versões até a 0.3.0 não
   têm o `.tar.gz` e nunca são oferecidas.
3. Antes de baixar, o atualizador confere:
   * se a bateria tem 30% ou mais, ou se o carregador está ligado;
   * se há espaço livre (seis vezes o tamanho do pacote, mais 64 MB);
   * se o Wi-Fi está conectado.
4. O download usa o `curl` do próprio firmware, só por HTTPS (inclusive nos
   redirecionamentos), com o certificado conferido pelo pacote de CAs do
   cartão (`TriMux/share/cacert.pem`). O tamanho máximo é 200 MB.
5. O SHA-256 do pacote precisa bater com o que está no arquivo `.sha256`. Se
   não bater, o pacote é apagado e nada muda no cartão.
6. O pacote é extraído em `.trimux-new/` no cartão (com o `tar` do busybox do
   firmware). O atualizador confere se os arquivos essenciais estão lá e se
   `TriMux/VERSION` dentro do pacote é a versão esperada.
7. Só então as pastas são trocadas por renomeação: `TriMux` vira
   `TriMux.old`, a nova vira `TriMux`, e o mesmo com `trimui`. Se uma etapa
   falhar, as anteriores são desfeitas. O `LEIA-ME.txt` também é atualizado.
8. Fica registrado em `TriMuxData/state/update_pending` que a versão nova
   ainda não foi provada.

Mais nada no cartão é alterado. Na memória interna, nada é gravado.

## Se a nova versão não abrir

O script de entrada (`trimui/app/MainUI`) é um shell script. Ele roda antes de
qualquer programa do TriMux, então funciona mesmo se os programas novos
estiverem quebrados. Enquanto existir `update_pending`, ele conta cada
tentativa de abrir o TriMux. Quando o menu aparece, a contagem é apagada e a
atualização está confirmada.

Se o menu não aparecer em **3 tentativas**, na 4ª o script:

1. move a versão nova para `TriMux.bad` e `trimui.bad`;
2. traz de volta `TriMux.old` e `trimui.old`;
3. registra o motivo em `TriMuxData/logs/trimux.log`.

As pastas `.bad` são apagadas na próxima atualização. Você também pode
apagá-las pelo computador.

Se o aparelho desligar no meio da troca de pastas (uma janela de
milissegundos), o script também traz `TriMux.old` de volta quando não encontra
`TriMux`. Se faltar a pasta `trimui`, o aparelho abre o sistema oficial; para
voltar ao TriMux, renomeie `trimui.old` para `trimui` no computador.

A volta manual (**Voltar à versão anterior**) troca `TriMux` ↔ `TriMux.old` e
`trimui` ↔ `trimui.old`. Dá para trocar de novo depois.

## Pelo terminal (SSH)

```sh
/mnt/SDCARD/TriMux/bin/trimuxctl update check      # 0 = em dia, 2 = há versão nova, 1 = erro
/mnt/SDCARD/TriMux/bin/trimuxctl update install    # instala o que o check encontrou
/mnt/SDCARD/TriMux/bin/trimuxctl update status
/mnt/SDCARD/TriMux/bin/trimuxctl update rollback   # depois: reboot
```

## Primeira atualização vindo da 0.3.0 ou anterior

As versões até a 0.3.0 não têm o atualizador. Para chegar à 0.4.0, atualize
uma vez pelo `update.zip` ([INSTALACAO.md](INSTALACAO.md), seção 4). Da 0.4.0
em diante, use o menu.

## Para quem publica versões

O workflow de release anexa tudo o que `make image` gera em `build/out/`,
inclusive o `.tar.gz` e o `.sha256` que já traz o hash dele. O repositório
consultado fica em `TriMux/share/update.ini` (`[update] repo = dono/projeto`),
então um fork pode apontar para os próprios lançamentos.
