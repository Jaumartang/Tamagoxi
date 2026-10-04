#pragma once

#include <stdint.h>

/*
 * config.h - Parametres del projecte. Cap numero magic al codi: tot es aqui
 * (geometria, temps de decaiment, hores de nit, meteo...) o a pins.h.
 *
 * Nomes conte el que les fases ja implementades fan servir; la resta de
 * parametres (decaiment de necessitats, hores de nit, coordenades de meteo,
 * aniversari de la Noa...) s'aniran afegint amb les seves fases.
 */

/* --- Geometria de la pantalla (vertical) ---------------------------------- */
/* Han de coincidir amb TFT_WIDTH/TFT_HEIGHT de include/tft_setup.h (320x480). */
static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 480;

/* --- Port serie ----------------------------------------------------------- */
static constexpr uint32_t SERIAL_BAUD = 115200;

/* --- Diagnostica de maquinari (Fase 0) ------------------------------------ */
/* El test de retroil-luminacio fa una rampa suau aquests ms i despres deixa
 * la pantalla al 100 %. */
static constexpr uint32_t BL_TEST_DURATION_MS = 12000;
static constexpr uint32_t BL_TEST_PERIOD_MS   = 4000;  /* periode de la rampa */
static constexpr uint8_t  BL_TEST_MIN_PERCENT = 25;    /* minim de la rampa */

/* Sequencia del LED RGB del test: un pas cada aquests ms. */
static constexpr uint32_t LED_TEST_STEP_MS = 800;

/* Cada quan s'escriu l'estat de memoria pel port serie. */
static constexpr uint32_t HEAP_LOG_INTERVAL_MS = 5000;

/* --- Retroil-luminacio ---------------------------------------------------- */
/* A la placa ESP32-32E el backlight va al GPIO 27 i es ACTIU ALT (HIGH =
 * ences). Confirmat a la documentacio de la placa i a projectes de referencia
 * del mateix maquinari. Si algun dia una placa el portes actiu baix, nomes cal
 * canviar aquesta constant. */
static constexpr bool BL_ACTIVE_LOW = false;

/* --- Tactil XPT2046 ------------------------------------------------------- */
/* Llindar de pressio (z) per sobre del qual una lectura es considera un dit.
 * Ajustable en calent des de la consola serie amb 'tth <n>'. */
static constexpr uint16_t TOUCH_DEFAULT_PRESSURE = 400;
/* Com a maxim una lectura del panell cada aquests ms. */
static constexpr uint32_t TOUCH_MIN_READ_INTERVAL_MS = 15;
/* Diferencia maxima (en unitats crues) entre mostres seguides per considerar
 * que son el mateix punt (filtre de soroll electric del panell resistiu). */
static constexpr int32_t TOUCH_RAW_TOLERANCE = 40;

/* --- Consola serie -------------------------------------------------------- */
static constexpr size_t CONSOLE_LINE_MAX = 48;

/* --- Diagnostica de retroil-luminacio a l'arrencada ----------------------- */
/* Fa un test visible de pin digital (OFF/ON) i despres de PWM (0/25/50/100 %)
 * per confirmar que el backlight respon i amb quina polaritat. Desactivat un cop
 * verificat el maquinari; es pot repetir en calent amb la comanda 'bltest'. */
static constexpr bool     BL_DIAG_ON_BOOT    = false;
static constexpr uint32_t BL_DIAG_DIGITAL_MS = 900;
static constexpr uint32_t BL_DIAG_PWM_MS     = 700;

/* --- Renderitzat del fons (Fase 2) ---------------------------------------- */
/* Files per franja en pintar un fons per streaming. Amb 8 files son 320 x 8 x 2
 * = 5.120 B (abans 24 -> 15.360 B): estalviem ~29 kB entre els buffers del fons
 * i els de la mascota, que es el que permet tenir WiFi i Bluetooth alhora. */
static constexpr uint16_t BG_BAND_LINES = 8;
/* Patro de carrega: apagar la retroil-luminacio mentre es pinta el fons, per
 *que no es vegin les franges a mig pintar. */
static constexpr bool BG_BACKLIGHT_OFF_ON_LOAD = true;
/* --- Galeria de la Fase 2: temps entre fons (auto-avenc). */
static constexpr uint32_t BG_GALLERY_INTERVAL_MS = 5000;

/* --- Mascota (Fase 3) ----------------------------------------------------- */
/* Escala entera de l'sprite (128 px -> 128*PET_SCALE px). Valors 1..3. */
static constexpr uint8_t PET_SCALE = 2;
/* Fila (y) on comenca el rectangle de la mascota. X = centrat + PET_OFFSET_X. */
static constexpr int PET_AREA_TOP = 104;
static constexpr int PET_OFFSET_X = 0;
/* Galeria d'animacions: temps entre animacio i animacio. */
static constexpr uint32_t PET_ANIM_SWITCH_MS = 6000;
/* --- Fase 6: xarxa, hora i meteo ----------------------------------------- */
/* Zona horaria POSIX (Europa/Madrid, amb canvi d'hora automatic). */
static constexpr const char* NET_TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
/* Ubicacio per defecte (Barcelona) si no se n'ha desat cap altra. */
static constexpr float    NET_DEFAULT_LATITUDE  = 41.3874f;
static constexpr float    NET_DEFAULT_LONGITUDE = 2.1686f;
/* Cada quant es consulta la meteo i cada quant es reintenta si falla. */
static constexpr uint32_t NET_WEATHER_PERIOD_MS = 30u * 60u * 1000u;
static constexpr uint32_t NET_WEATHER_RETRY_MS  = 90u * 1000u;
/* Temps maxim d'espera per connectar el WiFi (es fa dins la tasca de xarxa). */
static constexpr uint32_t NET_WIFI_TIMEOUT_MS   = 20000u;
/* Pila de la tasca de xarxa (el TLS necessita uns quants kB). */
static constexpr uint32_t NET_TASK_STACK        = 8192u;

/* --- Fase 7: so ----------------------------------------------------------- */
/* Freqüencia de mostreig de la sortida d'audio (DAC intern / Bluetooth). */
static constexpr uint32_t AUDIO_SAMPLE_RATE = 44100;
/* Pila de la tasca de so (el decodificador MP3 en demana uns quants kB). */
static constexpr uint32_t AUDIO_TASK_STACK  = 8192;

/* Bluetooth (A2DP). La pila Bluetooth d'aquest ESP32 (sense PSRAM) reserva uns
 * 90 kB estatics i, amb el WiFi, la pantalla i el reproductor, la memoria no hi
 * arriba: per aixo, quan s'encen el Bluetooth es DESACTIVA el WiFi (la radio no
 * pot fer les dues coses alhora de totes maneres) i es torna a activar en
 * apagar-lo. Amb -D AUDIO_BT=0 es compila sense Bluetooth.
 */
#ifndef AUDIO_BT
#define AUDIO_BT 1
#endif

/* Fons que es fa servir a la pantalla de prova de la mascota. */
static constexpr const char* PET_TEST_BG = "spring";

/* --- Mascota: joc (Fase 5) ------------------------------------------------ */
/* Velocitat de decaiment de les necessitats, en unitats per minut. */
static constexpr float PET_DECAY_FOOD      = 0.40f;
static constexpr float PET_DECAY_HAPPY     = 0.50f;
static constexpr float PET_DECAY_ENERGY     = 0.35f;
static constexpr float PET_SLEEP_RECOVER   = 2.50f;   /* energia/min dormint */
static constexpr float PET_HEALTH_DROP     = 0.15f;   /* si alguna necessitat < 20 */
static constexpr float PET_HEALTH_RECOVER  = 0.10f;   /* si tot va be */

/* Llindars d'estat. */
static constexpr float PET_LOW_FOOD     = 30.0f;
static constexpr float PET_LOW_HAPPY    = 30.0f;
static constexpr float PET_LOW_ENERGY   = 25.0f;
static constexpr float PET_LOW_HEALTH   = 30.0f;
static constexpr float PET_AUTO_SLEEP   = 15.0f;      /* s'adorm sol */
static constexpr float PET_WAKE_ENERGY  = 98.0f;      /* es desperta */

/* Efectes de les accions. */
static constexpr float PET_FEED_FOOD     = 35.0f;
static constexpr float PET_FEED_HAPPY    = 5.0f;
static constexpr float PET_PLAY_HAPPY    = 30.0f;
static constexpr float PET_PLAY_ENERGY   = -15.0f;
static constexpr float PET_PLAY_FOOD     = -10.0f;
static constexpr float PET_HEAL_HEALTH   = 50.0f;
static constexpr float PET_PET_HAPPY     = 5.0f;

/* Cada quan es desa l'estat a la NVS (ms). */
static constexpr uint32_t PET_SAVE_PERIOD_MS = 180000;
/* --- UI (Fase 4) ---------------------------------------------------------- */
/* HUD compacte a dalt (28 px). */
static constexpr int UI_HUD_TOP = 0;
static constexpr int UI_HUD_H   = 28;

/* Franja inferior compacta (44 px): barres compactes (esquerra) + boto MENU
 * (dreta). El menu d'accions es desplega DINS la mateixa franja.
 * HUD + franja = 72 px = 15% de 480. */
static constexpr int UI_BARS_TOP     = 396;
static constexpr int UI_BARS_BOT     = 480;
static constexpr int UI_BARS_PANEL_R = 232;   /* limit de barres/botons d'accio */

static constexpr int UI_MENU_BTN_L   = 232;   /* boto MENU / tancar */
static constexpr int UI_MENU_BTN_TOP = 436;
static constexpr int UI_MENU_BTN_BOT = 480;

/* Franja de barres compactes quan el menu es obert (a sota dels botons). */
static constexpr int UI_MENU_STRIP  = 8;

/* Premuda llarga del rellotge del HUD per al menu d'ajustos (ms). */
static constexpr uint32_t UI_LONGPRESS_MS = 2000;
/* Cooldown minim entre carícies (anti-spam). */
static constexpr uint32_t UI_PET_COOLDOWN_MS = 1200;
/* Durada del cor de caricia a pantalla (ms). */
static constexpr uint32_t UI_HEART_MS = 1200;
/* Anti-rebot del tactil: no processar dos tocs seguits en menys d'aixo (ms). */
static constexpr uint32_t UI_TAP_DEBOUNCE_MS = 180;

/* Diagnostica del tactil a l'arrencada: mostra una pantalla "toca ara" i
 * enregistra la pressio crua durant uns segons. Desactivat en produccio. */
static constexpr bool TOUCH_DIAG_ON_BOOT = false;
static constexpr int  TOUCH_DIAG_SECONDS = 30;
