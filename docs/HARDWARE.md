# Hardware do TrimUI Brick Pro — o que foi verificado

Este documento separa **o que foi confirmado no firmware oficial** (arquivos
extraídos do pacote oficial, reprodutível com `scripts/fetch_firmware.sh`) do
que vem apenas da **ficha técnica do fabricante** e do que **ainda precisa ser
medido no aparelho**. Nenhum dado aqui foi medido em um Brick Pro físico.

Fonte primária: firmware oficial **v1.1.1 (2026-07-17)** do repositório
[`trimui/firmware_brickpro`](https://github.com/trimui/firmware_brickpro/releases),
arquivo `trimui_tg4040_20260717_v1.1.1.7z`
(SHA-256 `36189b7966717c520901e6ef7318d1b092067598959b030cc0fd66b0747e5585`) e
`sd_recovery_tg4040_brick_pro_v1.1.1_20260717.7z`
(SHA-256 `385cd2affdd9f6d73421012c8c6a78ef601f69589d5addb6b0a6b01b335102d1`).

## Identificação do modelo

| Item | Valor | Evidência |
|---|---|---|
| Código do modelo | **TG4040** | nome do pacote `trimui_tg4040.awimg`; `/etc/tg4040.gpt` citado em `runtrimui.sh` |
| Nome | "Trimui Brick Pro" | string embutida em `/usr/trimui/bin/MainUI` (usada pelo TriMux para confirmar o aparelho) |
| Versão do firmware | 1.1.1 | `/etc/version` |
| Sistema | Tina Linux (OpenWrt) "Neptune", glibc 2.33, BusyBox | `/etc/openwrt_release`, `libc-2.33.so` |
| Kernel | Linux 4.9.191 (PREEMPT, HZ=250) | `boot.fex` (Android boot image) + `IKCONFIG` |

Não confundir com: **TG3040** (Brick original), **TG5040** (Smart Pro), Brick
Hammer. O TriMux recusa iniciar se o firmware não se identificar como Brick Pro
e devolve o controle ao sistema oficial.

## SoC, memória e armazenamento

| Item | Firmware (confirmado) | Fabricante |
|---|---|---|
| SoC | `allwinner,a133` / `sun50iw10p1`, 4× Cortex-A53 (`cpu@0..3`) | Allwinner A133P 1,8 GHz |
| Tabela de frequências da CPU | 408, 600, 816, 1008, 1200, 1320, 1416, 1464, 1512, 1608, 1800, **2000** MHz (por bin de silício; o bin "c0" inclui 1608/1800/2000) | máx. 1,8 GHz |
| GPU | `img,gpu` PowerVR, OPPs 228/399/**700** MHz; driver DDK `1.19@6345021`, OpenGL ES 3.2; `libVK_IMG.so` presente (Vulkan não configurado: sem ICD em `/etc/vulkan`) | GE8300 até 660 MHz |
| RAM | DTB base traz 512 MiB (valor preenchido pelo U-Boot em tempo de execução) | 1 GB LPDDR3 |
| eMMC | tabela sunxi: `bootloader, env, boot, rootfs (560 MiB), rootfs_data (2 GiB), private, recovery, pstore, UDISK` | 8 GB |
| Cartão | montado em `/mnt/SDCARD` (`/dev/mmcblk1p1` ou `/dev/mmcblk1`), opções `rw,sync`; kernel com **VFAT e exFAT** | microSD até 1 TB |
| zram | **ausente** no kernel (`CONFIG_ZSMALLOC` desativado) | — |

## Tela, controles e periféricos

| Item | Firmware (confirmado) | Fabricante |
|---|---|---|
| Painel | `lcd0`: `otm1289a`, MIPI-DSI 4 lanes, **1024×768**, backlight PWM | 3,95" IPS 1024×768 60 Hz |
| Controle | `trimui_inputd` lê o microcontrolador por `/dev/i2c-3` e cria o dispositivo uinput **"TRIMUI Player1"**; `/dev/input/js*` disponível (`CONFIG_INPUT_JOYDEV=y`) | D-pad, ABXY, L1/R1/L2/R2, Menu/Select/Start/Home, dois analógicos Hall com clique |
| Teclas de sistema | `keymon`: volume, brilho (MENU + volume), POWER (suspender com `echo mem > /sys/power/state`, desligar com 6 s) | — |
| LEDs | driver `led_anim` em `/sys/class/led_anim/` com zonas **m, lr, f1, f2, rear** (`effect_*`, `effect_rgb_hex_*`, `effect_duration_*`, `effect_cycles_*`, `max_scale*`); LEDC do SoC com 23 LEDs RGB no pino PE5 | barra superior, anéis dos analógicos, F1/F2, gatilhos |
| Vibração | `/sys/class/motor/voltage` + GPIO 227 | motor único |
| Bateria | `/sys/class/power_supply/axp2202-battery/{capacity,status}` (PMIC AXP2202) | 5000 mAh |
| Temperatura | `thermal-zones`: `cpu_thermal_zone` (trips passivos 58 °C e 63 °C, crítico 90 °C, mapeado ao resfriamento da CPU), `gpu_thermal_zone`, `ddr_thermal_zone` | — |
| Áudio | codec interno sun50iw10 (ALSA); `runtrimui.sh` ajusta `tinymix set 9 1` e `tinymix set 1 0` | 2 alto-falantes, P2 |

## Como o aparelho inicializa (relevante para a imagem)

1. A ROM de boot da Allwinner procura um cabeçalho `eGON.BT0` a 8 KiB do
   início do **cartão SD primeiro** e, se não encontrar, usa a eMMC. A imagem
   oficial de recuperação (`sd_recovery_...img`) tem `eGON.BT0` em 0x2004 e um
   pacote U-Boot ("sunxi-package") em 16 MiB — é assim que ela reprograma o
   aparelho quando inserida.
2. Com o boot pela eMMC, o firmware monta o cartão em `/mnt/SDCARD` e executa
   `/usr/trimui/bin/runtrimui.sh`, que:
   * roda scripts de `/mnt/SDCARD/System/starts/*.sh`;
   * a cada volta do laço principal, **se existir `/mnt/SDCARD/trimui`, executa
     `trimui/app/premainui.sh`, `trimui/app/MainUI` e `trimui/app/preload.sh`
     do cartão** e decide pelo código de saída de `preload.sh` se o lançador
     oficial deve abrir.

O TriMux usa esse segundo mecanismo, que já existe no firmware oficial.

## Ainda não verificado (depende do aparelho)

* Numeração real dos botões/eixos do "TRIMUI Player1" no Brick Pro (o TriMux
  usa a numeração documentada pela comunidade para a família TG5040 e oferece
  uma tela de teste e configuração em `trimux.ini`, seção `[input]`).
* Quais frequências o bin de silício de cada unidade realmente expõe em
  `scaling_available_frequencies`.
* Unidade da temperatura em `thermal_zone*/temp` (o TriMux só aceita valores
  plausíveis em mili-graus e desativa a leitura caso contrário).
* Semântica exata dos números de efeito do `led_anim` (o TriMux usa apenas 0
  = desligado, 2 = respiração e 4 = fixo).
* Desempenho, consumo e temperaturas reais.
