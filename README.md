# Tamagoxi v2 — Tamagotchi gegant per a la Noa

Mascota virtual (un **dragó**) en una placa ESP32 amb pantalla de 4" (LCDWiki
**E32R40T** / ESP32-32E, panell **ST7796S 320×480** vertical). Els gràfics viuen
en una targeta **microSD**; el firmware els carrega, els anima i hi afegeix la
lògica del joc.

> Estat actual: **Fase 4** — tàctil calibrat, HUD, barres i 4 botons grans.

## Pantalla principal (Fase 4)

```
y 0..40    HUD: hora · icona/temperatura · WiFi
y 80..336  MASCOTA (256×256)         <- només la toca SpriteRenderer
y 336..400 barres: gana · felicitat · energia · salut
y 400..480 botons: Menjar · Jugar · Dormir · Curar  (80×80)
```

- El **HUD i les barres** es repinten només quan canvia alguna cosa; els botons, un sol cop.
- **Tocar la mascota** = carícia (anim `HAPPY` + cor, amb cooldown).
- **Botons** = accions (de moment ajusten valors de prova; la Fase 5 hi posa el joc real).
- **Mantenir premut el rellotge** (2 s) = futur menú d'ajustos (Fase 7).
- El rellotge mostra l'**uptime** com a marcador fins que el NTP arribi a la Fase 6.

## Rendiment (mesurat)

- Fons complet `320×480` (307.200 B) per franges de 24 files: **~271 ms**
  (lectura SD ~204 ms → 1,5 MB/s; push a pantalla ~67 ms → 4,6 MB/s).
- Frame de la mascota (compost amb el fons llegit de la SD, mode **streaming**):
  escala 2 (`256×256`) **~148 ms → ~6,7 fps**; escala 1 (`128×128`) **~65 ms →
  ~15 fps**. Heap estable.
- Píxel transparent: `0xF81F` al fitxer (big-endian); en memòria little-endian
  es compara contra el valor byte-swapat `0x1FF8`.
- Escala i posició de la mascota: `PET_SCALE`, `PET_AREA_TOP`, `PET_OFFSET_X`
  a `config.h`, ajustables en calent amb `petscale` i `petpos`.
- La lectura de la SD és el coll d'ampolla (depèn de la targeta; una targeta
  moderna/Class 10 va més de pressa).
- Patró de càrrega: mentre es pinta un fons, la retroil·luminació es posa a 0
  (`BG_BACKLIGHT_OFF_ON_LOAD` a `config.h`) i es torna al 100 % quan el fons
  sencer ja és a la pantalla, per no veure mai les franges a mig pintar.

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
| `bg <name\|next\|N>` | Mostra un fons concret, el següent o el número N |
| `pet [bg]` | Mostra la mascota animada sobre un fons (per defecte `PET_TEST_BG`) |
| `anim <NAME\|next>` | Canvia l'animació de la mascota |
| `petscale <1-3>` | Canvia la mida de la mascota (limitada al que cap a la pantalla) |
| `petpos <x> <y>\|center` | Mou la mascota (cantó superior-esquerre) o la centra horitzontalment |
| `petreset` | Torna la mascota a la disposició per defecte de `config.h` |
| `home` | Torna a la pantalla principal (HUD + mascota + barres + botons) |
| `act <feed\|play\|sleep\|heal\|pet>` | Executa una acció com si s'hagués tocat el botó |
| `bench [name]` | Mesura lectura SD vs push a pantalla d'un fons |
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
src/      main.cpp  display.*  led.*  touch.*  sd_assets.*  bg_renderer.*
          sprite_renderer.*  ui.*  storage.*
tools/    validate_sd.py
```

Les fases següents hi afegeixen: `pet` (joc) i `weather`.
