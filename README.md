# Tamagoxi v2 — Tamagotchi gegant per a la Noa

Mascota virtual (un **dragó**) en una placa ESP32 amb pantalla de 4" (LCDWiki
**E32R40T** / ESP32-32E, panell **ST7796S 320×480** vertical). Els gràfics viuen
en una targeta **microSD**; el firmware els carrega, els anima i hi afegeix la
lògica del joc.

> Estat actual: **Fase 1** — SD muntada, manifests llegits i mides validades.

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

Recomanació: fes servir una targeta **neta** amb només `backgrounds/` i `pets/`
(el firmware ignora la resta, però una targeta amb brossa macOS/Windows i fitxers
grossos fa l'escaneig i l'arbre més lents). Validació des del PC abans de posar-la:

```bash
python3 tools/validate_sd.py /ruta/al/tamagochi_sd
```

## Bring-up de maquinari (Fase 0+): tàctil, backlight i consola sèrie

En arrencar:

- Pel sèrie: `=== Tamagoxi v2 - bring-up ... ===`, `[GFX] ST7796S 320x480 ...`,
  `[Touch] XPT2046 a punt ...`.
- **Diagnòstic de retroil·luminació**: la pantalla fa FOSC / ENCESA (pin digital)
  i després una escala PWM 0 / 25 / 50 / 100 % (mira si la brillantor canvia).
- Si la placa no té calibratge desat, mostra "Toca la pantalla per calibrar" i
  fa l'assistent de 4 fletxes (es desa a NVS).
- Pantalla **TEST TÀCTIL**: un reticle verd segueix el dit i es veuen les
  coordenades (PX i RAW). El botó gran **CALIBRAR** torna a fer la calibració.

### Consola sèrie (115200 baud)

| Comanda | Efecte |
|---|---|
| `bl <0-100>` | Fixa la brillantor de la retroil·luminació (0 = fosca) |
| `bltest` | Repeteix el diagnòstic digital + PWM del backlight |
| `loaddemo` | Demostra el patró "backlight a 0 mentre es carrega el fons" |
| `cal` | Executa la calibració del tàctil |
| `touch` | Redibuixa la pantalla de test tàctil |
| `colortest` | Franges de colors (per verificar l'ordre RGB/BGR) |
| `sd` | Remunta la SD, valida i mostra el resum a pantalla |
| `lssd` | Escriu l'arbre de fitxers de la SD pel sèrie |
| `tth <n>` | Llindar de pressió del tàctil (per defecte 400) |
| `info` | Estat: heap, backlight, tàctil |

> **Backlight a 0 durant la càrrega del fons (per a la Fase 2):** el patró és
> posar `Display::setBacklight(0)` abans de pintar el fons per franges i
> `Display::setBacklight(100)` quan tot el fons ja és a la pantalla. Ho demostra
> `runLoadDemo()` / la comanda `loaddemo`.

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
src/      main.cpp  display.*  led.*  touch.*
```

Les fases següents hi afegeixen: `sd_assets`, `bg_renderer`, `sprite_renderer`,
`pet`, `ui`, `weather`, `storage`.
