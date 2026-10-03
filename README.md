# Tamagoxi v2 — Tamagotchi gegant per a la Noa

Mascota virtual (un **dragó**) en una placa ESP32 amb pantalla de 4" (LCDWiki
**E32R40T** / ESP32-32E, panell **ST7796S 320×480** vertical). Els gràfics viuen
en una targeta **microSD**; el firmware els carrega, els anima i hi afegeix la
lògica del joc.

> Estat actual: **Fase 6** — xarxa: WiFi, hora del NTP i temperatura/meteo d'Open-Meteo.

## Xarxa i meteo (Fase 6) — `src/net.*`

Tot passa en una **tasca pròpia de FreeRTOS al nucli 0**: el bucle principal (que
anima la mascota i atén el tàctil) **mai** es bloqueja per WiFi, NTP o HTTPS.

- **WiFi**: credencials desades a la NVS amb `wifi <ssid> <contrasenya>`
  (o a `include/secrets.h`, que no es puja mai al repositori). `wifi off`
  les esborra. Reintents automàtics cada ~15 s.
- **Hora (NTP)**: `pool.ntp.org` / `time.google.com` amb zona horària
  `CET-1CEST,M3.5.0,M10.5.0/3` (Europa/Madrid, canvi d'hora automàtic). Quan
  arriba la primera hora, el rellotge del HUD passa de `--:--` a l'hora real i
  s'aplica el **decaïment fora de línia** a la mascota (`Pet::setEpoch`).
- **Meteo (Open-Meteo)**: cada **30 min** es consulta
  `temperature_2m`, `weather_code`, `wind_speed_10m` i `is_day` (sense clau
  d'API). Ubicació per defecte **Barcelona**, canviable amb `geo <lat> <lon>`.
- **Fons automàtic**: si no has fixat cap fons (`homebg <nom>`), el fons de la
  pantalla principal **segueix el temps** amb els 16 fons `weather_NN`:

  | Codi WMO / condició | Fons |
  |---|---|
  | 95-99 | `weather_10` tempesta |
  | 45,48 | `weather_11` boira |
  | 71-77, 85, 86 | `weather_05` neu |
  | 56,57,66,67 | `weather_06` fred (pluja gelada) |
  | 51-65, 80-82 | `weather_04` pluja |
  | 3 | `weather_03` ennuvolat (dia) / `weather_01` (nit) |
  | clar + ≤3 °C | `weather_06` fred |
  | clar + ≥32 °C | `weather_07` calor |
  | clar + vent ≥30 km/h | `weather_08` vent |
  | clar + 5-8 h | `weather_12` sortida de sol |
  | clar + 18-21 h | `weather_13` posta de sol |
  | clar de dia | `weather_00` assolellat |
  | clar de nit | `weather_02` estelada / `weather_01` nit |
  | clar de nit + ≤2 °C | `weather_15` aurores |
  | després de pluja + clar (dia) | `weather_09` arc de sant Martí |

  Torna al mode manual amb `homebg <nom>` i al mode automàtic amb `homebg auto`.

## Joc (Fase 5) — `src/pet.*`

- **Necessitats** (0-100, en floats interns): menjar, felicitat, energia i salut.
- **Decaïment** (per minut, a `config.h`): menjar −0.40 · felicitat −0.50 ·
  energia −0.35 (mentre dorm, **+2.50**) · salut −0.15 si alguna necessitat
  està per sota de 20, i **+0.10** si tot va bé.
- **Estat → animació**: salut < 30 → `SICK`; dormint → `SLEEP`; menjar < 30 →
  `HUNGRY`; felicitat < 30 → `SAD`; energia < 25 → s'adorm sol; si no, `IDLE`.
- **Accions**: Menjar (+35 menjar) · Jugar (+30 felicitat, −15 energia,
  −10 menjar) · Dormir/Despertar · Curar (+50 salut) · Carícia (+5 felicitat).
  Si l'acció no té sentit (tip, cansat, ja està bé) es refusa i ho diu pel sèrie.
- **Animacions temporals**: `EAT`, `PLAY`, `HAPPY`, `CELEBRATE` i, de tant en
  tant, `EXTRA` (perquè es vegi que és viu).
- **Persistència**: es desa a la NVS **a cada acció** i cada 3 minuts; en
  arrencar es recupera (verificat).
- **Decaïment fora de línia**: `Pet::setEpoch()` (que cridarà el NTP de la
  Fase 6) aplica el temps que ha estat apagat, amb un màxim de 3 dies.

## Personalització

- **Temes de color** (`theme`): `rosa-lila` (per defecte), `nit-violeta`,
  `rosa-pastel`. Es canvia en calent amb `theme <n>` i es desa.
- **Fons principal** (`homebg`): qualsevol nom del catàleg de la SD
  (`spring`, `dawn`, `fireflies`, `crystal_cave`, `beach`, `underwater`,
  `space`, `forest`, `autumn`, `desert`, `volcano`, `city_night`, `weather_00..15`).
  Es canvia amb `homebg <nom>` i es desa. **Els més "de nena"**: `spring`
  (prat amb cirerer florit), `dawn` (postal·lila) i `crystal_cave` (cristalls liles).
  Amb **`homebg auto`** el fons torna a seguir la meteo (Fase 6).
- La disposició de la mascota (`petscale`/`petpos`) també es desa.

## Tàctil (calibració)

El tàctil (XPT2046) fa servir un **mapa cru→píxels propi** (lineal, amb la Y
invertida tal com és en aquesta placa) i porta un **mapa per defecte ja bo**:
funciona des del primer moment, sense calibrar.

- Calibratge fi: comanda **`cal`** (4 objectius) o **premuda llarga (2 s) al
  rellotge** del HUD. Es desa a la NVS.
- El llindar de pressió és ajustable en calent: `tth <n>` (per defecte 400; el
  panell dóna 400–2500 en tocar).
- Diagnòstic: `tmon [s]` mostra la pressió crua i les coordenades mentre es toca.

## Pantalla principal (Fase 4)

La UI ocupa **només ~15%** de la pantalla (72 px de 480) perquè la mascota i el fons
siguin els protagonistes:

```
y 0..28    HUD compacte: hora · temperatura · WiFi
y 104..360 MASCOTA (256×256)         <- només la toca SpriteRenderer
y 436..480 FRANJA INFERIOR (44 px):
             · barres compactes (esquerra): gana · felicitat · energia · salut
             · boto ≡ MENU (dreta)
```

- El **HUD** i les **barres** es repinten només quan canvia alguna cosa.
- El botó **MENU** desplega **dins la mateixa franja** 4 botons d'acció:
  **Menjar · Jugar · Dormir/Despertar · Curar** + **✕ Tanca**, amb una **franja de
  barres compactes** a sota (per veure l'estat sense sortir del menú).
- Al **tancar** es repinten les barres i el botó (la zona queda neta).
- **Tocar la mascota** = carícia (anim `HAPPY` + cor, amb cooldown).
- **Premuda llarga (2 s) al rellotge** = calibrar el tàctil.
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
| `home` | Torna a la pantalla principal (HUD + mascota + barres + botó MENU) |
| `needs` | Mostra les necessitats, l'estat i l'animació actual de la mascota |
| `theme [n]` | Llista o canvia el tema de color de la UI (es desa) |
| `homebg [nom\|auto]` | Consulta o canvia el fons de la pantalla principal (es desa) |
| `wifi [ssid pass]` | Guarda les credencials WiFi a la NVS (o mostra l'estat); `wifi off` les esborra |
| `net` | Estat de la xarxa: WiFi, hora del NTP, ubicació i meteo |
| `meteo` | Refresca la meteo ara mateix |
| `geo [lat lon]` | Consulta o canvia la ubicació de la meteo (es desa) |
| `sets <f> <h> <e> <s>` | (proves) Força les necessitats 0-100 per veure els estats |
| `menu` | Obre el menú desplegable d'accions |
| `act <feed\|play\|sleep\|heal\|pet>` | Executa una acció com si s'hagués triat al menú |
| `shot [x y w h]` | Captura la pantalla pel port (`tools/shot_decode.py`) |
| `baud <n>` | Canvia la velocitat del port (per a la captura) |
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
