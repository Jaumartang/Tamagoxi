# Canvis del projecte (CHANGELOG)

Tamagoxi v2 — el dragó de la Noa (ESP32 + pantalla ST7796S de 4" + tàctil).

## Xarxa pròpia des de la pantalla (punt d'accés)
- Al panell **WiFi** hi ha ara el botó **«Crear xarxa pròpia (Tamagoxi)»**: crea
  un punt d'accés obert i posa en marxa el servidor web, perquè es puguin pujar
  fitxers a la SD **sense cap router** (`http://192.168.4.1/`).
- El mateix botó passa a **«Aturar xarxa pròpia»** per tancar-la.
- Mentre la xarxa pròpia és encesa, la mascota deixa anar els seus buffers
  (com amb el Bluetooth) i la pantalla d'inici ho explica amb l'adreça.
- El punt d'accés **no sobreviu reinicis** (es torna a encendre amb el botó): és
  una eina de manteniment, no l'estat normal de la mascota.
- La llista de xarxes passa de 7 a 6 files per fer-hi lloc.
- Fins ara només es podia encendre amb la comanda de consola `ap`.

## Compatibilitat amb el pack d'assets nou (tamagochi_sd-3)
- El firmware ara entén **els dos formats de manifest de mascota**:
  - el clàssic: `width`/`height`, `animations: {"IDLE": 24, ...}`, `transparent`
  - el del pack nou: `size: [128, 128]`, `states: [...]`, `frames_per_state`,
    `transparent_color`
  Així els packs nous funcionen sense haver de convertir-los.
- **Límits ampliats**: 128 fons (abans 48) i 20 animacions per mascota (abans 12),
  perquè el pack hi càpiga sencer (112 fons, 16 estats): abans es perdien
  SAD, SICK, SLEEP i WALK.
- **Fons de 320×400** i 4 frames per escena (el pack nou): s'accepten (les 80
  files que falten fins a 480 les tapen l'HUD i les barres de baix) i la lectura
  per franges repeteix la darrera fila perquè les targetes de vidre també es
  vegin bé a la part inferior.
- **Meteo**: si l'escena `weather_NN` no hi és, es fa servir el seu primer frame
  (`weather_NN_00`).
- **Selector de fons (Ajustos)**: passa només per les escenes, saltant els
  frames `_01.._03`, i amaga el sufix `_00` del nom que es mostra.
- **Fons per defecte**: `spring_00`; si el fons desat ja no existeix al pack, se
  n'escull un altre tot sol (mai una pantalla buida).

## Estil: interfície de vidre
- **Targetes de vidre translúcid**: `Ui::glassCard()` llegeix el fons de la SD
  fila a fila i hi barreja un tint, de manera que el dibuix del fons es veu a
  través de les barres, els botons i els panells.
- Aplicat a les barres d'estat, el botó MENU, els botons d'acció, la barra
  superior (HUD: rellotge, meteo i WiFi) i el menú desplegable de dalt.
- `Ui::mix()` i `Ui::inkOn()` són públics: `inkOn()` tria text clar o fosc
  segons el fons perquè sempre es llegeixi bé.

## Fase 10 — Jocs didàctics i recompenses
- **5 jocs** per aprendre jugant, pensats per a una nena de 6 anys:
  **Sumes**, **Restes** (1-17, es fan més difícils a mesura que juga),
  **Lletres** (amb quina lletra comença aquest dibuix?), **Paraules**
  (quin dibuix és?) i **Escriure** (quina lletra falta?).
- **Sistema escalable**: tots comparteixen un motor de preguntes; afegir un joc
  nou és escriure una funció que genera la pregunta i una línia a la taula
  (`kGames`).
- **Reforç positiu**: verd si encerta, vermell si no (sense penalitzar), soet de
  celebració i cap pressa per respondre.
- **Recompenses**: cada encert dona una **estrella** que es desa a la NVS i es
  veu sempre a dalt; cada 10 estrelles surt una **pantalla de celebració** i el
  drac menja content; cada joc guarda els encerts de la Noa.
- **Visualment atractiu**: targetes grans de colors vius, text ben gros, dibuixos
  fets amb primitives (sol, casa, peix, flor, lluna, estrella, poma, cor, núvol,
  gat, arbre, pilota, barca) i només 3 opcions per pantalla.
- Comandes: `joc [n]` (obre un joc), `jocs` (estat i encerts), `jocs reset`.

## Fase 9 — Missatges al mòbil
- Enviar missatges des del Tamagoxi (Telegram), amb una **llista de números**
  seleccionats i avisos automàtics del drac (gana, malaltia, tristesa, canyera).
- **Rebre** missatges: la placa pregunta cada 10 s i surt una **finestreta** a la
  pantalla amb el missatge (20 s o fins que es toca).
- **Sistema de notificacions**: avisos a la pantalla per als missatges i per a
  les necessitats del drac, i **panell de Missatges** al menú de dalt (amb
  comptador de pendents). Comandes: `msg`, `msg inbox`, `msg add/del`, `msg test`.

## Fase 8 — Pujar fitxers per WiFi
- **Servidor web** a la placa: llista i esborra fitxers de la SD i n'hi puja de
  nous des del navegador del mòbil (música, fons, mascotes) — a
  `http://tamagoxi.local` o, amb punt d'accés propi, a `http://192.168.4.1`.
- Comandes: `web [on|off|status]`, `ap [off]`, `webtest`, `webdump`.
- **Panell de Bluetooth** al menú: **escaneig** dels aparells d'àudio del
  voltant (nom i senyal) i **enllaçament amb un toc**; tria auriculars/altaveu i
  apaga/encén. Comandes: `bt source|sink|off|scan|list|connect`.

## Fase 7 — So i música
- Reproductor **MP3/WAV** de la carpa `/music` de la SD, amb el desco­dificador
  `arduino-audio-tools` + `libhelix`, en una tasca pròpia (la mascota no s'hi
  encalla mai).
- Sortida pel **DAC intern** (GPIO26 → amplificador FM8002E de la placa) i pels
  **auriculars/altaveus Bluetooth** (emissor A2DP).
- **Panell de Música**: cançó actual amb temps i barra de progrés, transport,
  volum i llista de cançons.

## Fase 6.5 — Menús i panells
- **Desplegable de dalt** (botó de ratlles del HUD) amb 7 opcions: WiFi, Música,
  Bluetooth, Missatges, Jocs, Ajustos i Sobre.
- **Panell de WiFi**: llista de xarxes (senyal i cadenat), **teclat en pantalla**
  (majúscules, números i símbols), mostrar/amagar contrasenya i connexió amb
  resultat.
- **Ajustos**: tema de colors (rosa-lila, nit violeta, rosa pastel), fons de la
  pantalla (inclòs "automàtic segons la meteo") i brillantor. Es desa a la NVS.

## Fase 6 — Xarxa
- **WiFi + NTP + meteo** (Open-Meteo) en una tasca pròpia del nucli 0, amb
  caché a la NVS i reintents. El HUD mostra l'hora i la temperatura.
- Comandes: `wifi`, `net`, `meteo`, `geo`, `wifiprobe`.

## Fase 5 — El joc
- Necessitats de la mascota (menjar, felicitat, energia, salut) que passen amb
  el temps, amb **memòria a la NVS**: sobreviuen als reinicis.
- Accions: **Menjar · Jugar · Dormir · Curar**, amb animacions i efectes.

## Fase 4 — Interfície i tàctil
- HUD (hora, meteo, WiFi), **4 barres** de necessitats i botons grans.
- **Tàctil XPT2046**: calibratge guiat, llindar de pressió ajustable i mapa
  cru→píxels desat a la NVS.
- Paleta i temes rosa/lila (pensats per a la Noa).

## Fases 1–3 — Base, fons i mascota
- **Placa**: ESP32-WROOM-32E (LCDWiki E32R40T / ESP32-32E) amb ST7796S 320x480
  per HSPI, SD a 25 MHz i amplificador FM8002E. PlatformIO + Arduino + TFT_eSPI.
- **Fons** de 307.200 B pintats per **streaming** en franges (no caben a RAM).
- **Mascota**: sprite de 128x128 ampliat 2x, composat sobre el fons amb
  transparència, per franges i llegint de la SD (fps ajustable).
- `wavgen`, `beep`, `shot`, `tap`, `sd`, `lssd`, `bg`, `pet`, `anim`... per
  provar-ho tot des del port sèrie, i utilitats a `tools/` per fer proves
  automàtiques de la UI (tocs simulats i captures de pantalla).

---

> Memòria: aquesta placa **no té PSRAM**, així que el projecte està afinat al
> kB (buffers de franges, decodificador, pila Bluetooth...). Detalls de cada
> decisió al `README.md`.
