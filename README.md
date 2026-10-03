# Tamagoxi v2 — Tamagotchi gegant per a la Noa

Mascota virtual (un **dragó**) en una placa ESP32 amb pantalla de 4" (LCDWiki
**E32R40T** / ESP32-32E, panell **ST7796S 320×480** vertical). Els gràfics viuen
en una targeta **microSD**; el firmware els carrega, els anima i hi afegeix la
lògica del joc.

> Estat actual: **Fase 0** — esquelet del projecte i verificació de maquinari.

## Maquinari

| Perifèric | Detall |
|---|---|
| MCU | ESP32-WROOM-32E, 240 MHz, 4 MB flash, **sense PSRAM** |
| Pantalla | ST7796S 320×480, SPI 4 fils (bus **HSPI**), RGB565 |
| Tàctil | XPT2046 resistiu (E32R40T), comparteix el bus SPI del LCD |
| Targeta | microSD per SPI en un bus independent (**VSPI**) |
| Sèrie | pont USB-sèrie CH340 |

Mapa de pins complet a [`include/pins.h`](include/pins.h) (font de veritat única).

## Instal·lació

1. Instal·la [VS Code](https://code.visualstudio.com/) i l'extensió **PlatformIO IDE**.
2. Obre aquesta carpeta (`File > Open Folder…`); PlatformIO detectarà `platformio.ini`.

Des de la terminal (o la barra de PlatformIO de VS Code):

```bash
pio run                 # compila
pio run -t upload       # compila i puja a la placa
pio device monitor      # obre el monitor sèrie (115200 baud)
```

Si `pio` no és al `PATH`, fes servir la instal·lació de PlatformIO Core:
`~/.platformio/penv/bin/pio`.

## Targeta SD

Copia el contingut de `tamagochi_sd/` **a l'arrel** d'una targeta formatejada en
**FAT32** (la carpeta `_preview/` és només de referència; no cal).

```
/backgrounds/   manifest.json + weather_00.bin … weather_15.bin + 12 escenaris
/pets/dragon/   manifest.json + IDLE/ HAPPY/ … (3 frames .bin cadascuna)
```

- Fons: `320×480` → **307.200 bytes** cadascun.
- Sprite: `128×128` → **32.768 bytes** per frame (transparent = `0xF81F`).
- Tots els `.bin` són **RGB565 big-endian, píxels crus, sense capçalera**.

## Què hauries de veure a la Fase 0

En arrencar, pel monitor sèrie:

- `=== Tamagoxi v2 - Fase 0: test de maquinari ===`
- `[GFX] ST7796S 320x480 a punt (HSPI, LCD_CS=15, DC=2, RST=-1)`
- `[LED] …` canviant de color cada 800 ms
- `[SYS] up=…s heap=… maxAlloc=… minFree=…` cada 5 s

A la pantalla:

- 8 franges de colors (VERMELL, VERD, BLAU, GROC, CIAN, MAGENTA, BLANC, GRIS)
  amb una capçalera i un marc blanc.
- El LED RGB de la placa ha de parpellejar en vermell, verd, blau, blanc, groc.
- La retroil·luminació fa una rampa suau durant 12 s i després queda al 100 %.

## Resolució de problemes

- **El vermell i el blau surten intercanviats** → falta el *byte-swap* correcte;
  comprova `tft.setSwapBytes(false)` a `src/display.cpp` i l'ordre dels `.bin`.
- **La imatge surt invertida (negatiu)** → cal ajustar la inversió del panell
  (ST7796S). Ho corregirem a `display.cpp` quan passi.
- **Pantalla en blanc / no arrenca** → revisa que `LCD_CS` sigui el GPIO 15 i que
  `include/tft_setup.h` s'hagi carregat (error de compilació si no).
- **No compila** → comprova que `-I include` sigui a `build_flags`
  (TFT_eSPI necessita trobar `include/tft_setup.h`).
- **`pio: command not found`** → fes servir `~/.platformio/penv/bin/pio`.

## Credencials WiFi

Copia `include/secrets.example.h` a `include/secrets.h` i omple'l. `secrets.h`
està al `.gitignore` i **no** es puja mai al repositori.

## Estructura del projecte

```
include/  pins.h (mapa de pins)  tft_setup.h (TFT_eSPI)  config.h  secrets.example.h
src/      main.cpp  display.*  led.*
```

Les fases següents hi afegeixen: `sd_assets`, `bg_renderer`, `sprite_renderer`,
`pet`, `ui`, `touch`, `weather`, `storage`.
