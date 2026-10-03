# Loja de aplicativos

Em **Aplicativos**, abaixo dos aplicativos instalados, fica a **Loja de
aplicativos**. É uma lista curta, escolhida a dedo, de aplicativos da
comunidade que o TriMux baixa quando você pede. Nada desses projetos vai na
imagem do TriMux.

| Aplicativo | O que faz | Versão | Licença | Onde fica |
|---|---|---|---|---|
| PortMaster | Ports de jogos de PC, integrados em **Ports** ([PORTS.md](PORTS.md)) | MinUI PortMaster 2.14.0 | MIT; o pacote traz programas de terceiros, cada um com sua licença | `Emus/tg5040/PORTS.pak` |
| Grout | Cliente do [RomM](https://romm.app): baixa jogos, saves e capas do seu servidor RomM | 5.3.1.2 | MIT | `Apps/Grout` |

**Estado:** testado só no computador, com downloads simulados (veja
[TESTES.md](TESTES.md), H64–H66). Ainda falta confirmar no aparelho.

## Como funciona

1. A lista está em `TriMux/share/store.ini`. Cada aplicativo tem:
   * uma versão fixa;
   * o endereço do pacote no GitHub do projeto;
   * o SHA-256 e o tamanho do pacote;
   * a pasta de destino.
2. **Instalar** (pede confirmação e precisa de Wi-Fi):
   * confere bateria (20% ou mais, ou no carregador) e espaço livre (quatro
     vezes o tamanho do pacote, mais 64 MB);
   * baixa o pacote para o cartão (`TriMuxData/.store-download.zip`), só por
     HTTPS, com o certificado verificado;
   * confere o SHA-256. Se não bater, o arquivo é apagado e nada muda.
3. O TriMux extrai o pacote com o próprio extrator, porque o firmware não tem
   `unzip`:
   * confere o CRC de cada arquivo;
   * mantém as permissões de execução;
   * recusa caminhos que saiam da pasta, como `..`, caminhos absolutos ou
     links.

   A extração vai para uma pasta temporária e só depois o conteúdo é movido
   para o destino. Numa atualização, as pastas marcadas como `keep` (por
   exemplo `PortMaster`, com as configurações e runtimes do PortMaster) são
   mantidas.
4. **Remover** apaga apenas os caminhos listados para aquele aplicativo.
5. A loja só pode escrever em `Apps/`, `Emus/` e `Roms/PORTS/` no cartão.
   Entradas da lista que apontem para outro lugar são ignoradas. Jogos,
   saves, BIOS, capas e o próprio TriMux nunca são tocados.

Pelo terminal (SSH):

```sh
/mnt/SDCARD/TriMux/bin/trimuxctl store list
/mnt/SDCARD/TriMux/bin/trimuxctl store install portmaster
/mnt/SDCARD/TriMux/bin/trimuxctl store status
/mnt/SDCARD/TriMux/bin/trimuxctl store remove grout
```

## Aplicativos da TrimUI e do CrossMix

A seção **Aplicativos** lista qualquer aplicativo no formato da TrimUI (uma
pasta com `config.json` e `launch.sh`) colocado em `Apps/`. Muitos
aplicativos feitos para o sistema oficial ou para o CrossMix funcionam assim,
mas alguns:

* dependem de arquivos do CrossMix (`/mnt/SDCARD/System`);
* ou mudam configurações do sistema oficial na memória interna.

Esses não entram na loja. Use-os por sua conta.
