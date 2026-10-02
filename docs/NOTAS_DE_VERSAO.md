# TriMux 0.1.0 — primeira versão (pré-lançamento)

**Estado: não testado em um Brick Pro físico.** Todo o código foi compilado,
testado automaticamente e verificado contra as bibliotecas do firmware oficial
v1.1.1 em QEMU, mas inicialização, imagem na tela, controles, áudio, suspensão
e desempenho reais ainda precisam ser confirmados no aparelho
(veja [TESTES.md](TESTES.md)).

## O que tem

* Menu próprio em português do Brasil (inglês incluído), feito para os botões
  do aparelho, com assistente inicial, continuar, recentes, favoritos, busca,
  filtros e informações por plataforma.
* 22 plataformas com 20 núcleos libretro compilados para Cortex-A53 e RetroArch
  1.22.2 (GLES via SDL2 do firmware, ALSA, controle via `/dev/input/js*`).
* Ports: DOOM (PrBoom), Quake (TyrQuake), Cave Story (NXEngine) e scripts `.sh`
  no estilo PortMaster (experimental).
* Botões extras configuráveis: F1/F2 (favoritar, jogo surpresa, busca,
  recentes, menu rápido, trocar perfil, LEDs) e chave lateral (modo economia,
  LEDs apagados, alto-falante mudo ou 2,0 GHz, também durante o jogo).
* Proteção do limite de CPU: se um atalho de FN do firmware oficial subir a
  CPU (até 2,0 GHz), o TriMux devolve ao limite do perfil em até 10 s.
* Troca de emulador por jogo ou por plataforma; restauração da configuração
  recomendada (arquiva, não apaga).
* Perfis de energia com teto de 1,8 GHz, aplicados antes de cada jogo,
  proteção térmica adicional e restauração automática do padrão.
* Opcional: chave lateral libera 2,0 GHz (máximo da tabela do firmware) só
  enquanto estiver ligada; exige confirmação; desligada, o teto é 1,8 GHz.
* Controle de LEDs pelo driver oficial (opcional, desligado por padrão).
* Retorno automático ao sistema oficial em caso de falhas repetidas; SELECT ao
  ligar abre o sistema oficial.
* Imagem `.img.xz` para Rufus (MBR + FAT32) com expansão segura da partição no
  aparelho; pacote `-update.zip` para atualizar sem perder dados.

## Limitações conhecidas

* Funciona **sobre** o firmware oficial (não é uma distribuição que inicializa
  sozinha pelo cartão). Mudanças futuras no firmware da TrimUI podem exigir
  ajustes.
* F1/F2 não têm ação dentro dos jogos (no RetroArch todo atalho exige segurar
  MENU); a chave lateral funciona também nos jogos.
* Numeração de botões/eixos baseada em documentação comunitária da família
  TG5040; se algo estiver trocado, use *Controles › Testar controles* e ajuste
  `[input]` em `trimux.ini` (menu) ou remapeie no menu do RetroArch (jogos).
* Volume e brilho são controlados pelas teclas do firmware (sem indicador na
  tela dentro do TriMux, porque o serviço de OSD oficial não é iniciado).
* Bluetooth e Wi-Fi não são gerenciados pelo TriMux (use o sistema oficial).
* PlayStation 2: removido (nenhum emulador viável; análise em docs/PS2.md).
* N64, PSP, Dreamcast e outros sistemas mais pesados não estão incluídos.
* Sem swap: jogos de arcade muito grandes podem não caber na memória.
* Snes9x 2005, PicoDrive, Genesis Plus GX e FinalBurn Neo têm licenças não
  comerciais.

## Arquivos

* `TriMux-0.1.0-brickpro.img.xz` — gravar com Rufus/balenaEtcher.
* `TriMux-0.1.0-update.zip` — atualizar/instalar em cartão existente.
* `TriMux-0.1.0-brickpro.sha256` — hashes SHA-256.
