# Registros e desempenho

*Configurações › Sistema › Registros e desempenho.* Ferramentas para medir no
aparelho o que cada escolha custa e ganha (perfil de energia, 2,0 GHz pela
chave lateral, emulador) e para investigar problemas. **Tudo vem desligado.**
Desligado, nada é gravado no cartão.

> Estado: testado só em ambiente simulado (Brick Pro simulado com valores do
> firmware). Os números reais saem do aparelho (H35–H37 em [TESTES.md](TESTES.md)).
> As capturas abaixo usam sessões de exemplo, não medições.

![Registros e desempenho](img/16-registros.png)

## Medição

| Opção | O que faz |
|---|---|
| Registro de desempenho nos jogos | A cada 10 s de jogo grava: frequência atual da CPU, limite em uso, temperatura, bateria, carregador e perfil em vigor. No fim, uma linha de resumo da sessão. |
| Mostrar FPS nos jogos | Liga o contador de quadros por segundo do RetroArch no canto da tela. |
| Sessões gravadas | Resumo **por perfil** (temperatura máxima média, consumo de bateria em %/hora, CPU média) e a lista das últimas 50 sessões. |

![Sessões gravadas](img/17-sessoes.png)

* O registro aproveita a leitura que o TriMux já faz a cada 10 s para a
  proteção térmica: não cria processos novos nem acorda a CPU mais vezes.
* Custo no cartão: uma linha de ~40 bytes a cada 10 s (≈ 14 KB por hora).
* Só o **nome do arquivo** do jogo é gravado, nunca o caminho completo.
* O consumo em %/hora só é calculado em sessões de **5 min ou mais e sem
  carregador**. A bateria do Brick Pro informa números inteiros de
  porcentagem; sessões curtas dariam um valor sem sentido.
* A frequência anotada é a do momento da leitura. A cada 10 s ela mostra a
  tendência, não cada pico.

### Como comparar duas configurações

1. Ligue *Registro de desempenho* e *Mostrar FPS*.
2. Bateria acima de 80 %, sem carregador, aparelho frio (10 min parado).
3. Jogue o **mesmo jogo, no mesmo trecho**, por 20 a 30 minutos com a
   configuração A (por exemplo, perfil Equilibrado).
4. Deixe esfriar e repita com a configuração B (por exemplo, chave lateral em
   2,0 GHz).
5. Veja *Sessões gravadas*: o resumo por perfil mostra a temperatura máxima
   média e o consumo de cada um. O FPS na tela mostra se a opção mais
   econômica ainda roda a velocidade total (60 FPS na maioria dos consoles,
   50 em jogos europeus).
6. Para gráficos, abra os arquivos no computador (seção abaixo).

Um ganho só vale se aparecer em mais de uma sessão. Repita cada configuração
pelo menos duas vezes. O protocolo completo para medições da matriz da versão
1.0 está em [DESEMPENHO.md](DESEMPENHO.md).

## Arquivos (`TriMuxData/logs/perf/`)

Separador `;` e só números inteiros: abrem direto no Excel, LibreOffice ou
Google Planilhas em português.

`sessions.csv`, uma linha por sessão:

```
inicio;plataforma;emulador;jogo;perfil;duracao_s;cpu_media_mhz;cpu_max_mhz;temp_inicio_c;temp_max_c;temp_fim_c;bateria_inicio;bateria_fim;carregando;protecao_termica;saida
2026-10-02 13:40;GBA;gpsp;Celeste Classic.gba;balanced;1800;1390;1608;45;61;58;83;74;0;0;0
```

`AAAAMMDD-HHMMSS_PLATAFORMA.csv`, uma linha a cada 10 s da sessão:

```
tempo_s;cpu_mhz;limite_mhz;temp_c;bateria;carregando;perfil
0;1200;1608;45;83;0;balanced
10;1608;1608;47;83;0;balanced
```

`-1` significa "não disponível" (por exemplo, sensor sem leitura). Os 30
arquivos de sessão mais recentes são mantidos; o `sessions.csv` vira
`sessions.csv.1` ao passar de 256 KB.

## Logs técnicos

| Opção | Onde grava | Quando usar |
|---|---|---|
| Log detalhado do TriMux | `TriMuxData/logs/trimux.log` (máx. 256 KB + 1 cópia) | Investigar menu, lançamento, rede, energia. |
| Log do RetroArch | `TriMuxData/logs/retroarch/retroarch.log`, substituído a cada jogo | Jogo que não abre, fecha sozinho, BIOS não encontrada, núcleo com erro. |
| Saída de aplicativos e ports (sempre) | `TriMuxData/logs/apps/<nome>.log`, substituído a cada vez que o programa abre; só os últimos 32 KiB | Aplicativo ou port que volta ao menu sem abrir. O menu avisa quando um deles fecha logo depois de abrir. |
| Ver log do TriMux | — | Mostra as últimas 40 linhas no próprio aparelho. |
| Apagar registros de desempenho | — | Apaga sessões, arquivos de amostras e o log do RetroArch (pede confirmação). Não toca em jogos, saves ou configurações. |

Senhas (Wi-Fi, RetroAchievements) nunca vão para nenhum desses arquivos.

## Configurações (`TriMuxData/config/trimux.ini`)

```ini
[diag]
perf = 0           ; registro de desempenho nos jogos
show_fps = 0       ; FPS na tela (RetroArch)
verbose = 0        ; log detalhado do TriMux
retroarch_log = 0  ; log do RetroArch em arquivo
```
