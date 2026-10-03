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
/* Files per franja en pintar un fons per streaming (320 x 24 x 2 B = 15.360 B). */
static constexpr uint16_t BG_BAND_LINES = 24;
/* Patro de carrega: apagar la retroil-luminacio mentre es pinta el fons, per
 *que no es vegin les franges a mig pintar. */
static constexpr bool BG_BACKLIGHT_OFF_ON_LOAD = true;
/* --- Galeria de la Fase 2: temps entre fons (auto-avenc). */
static constexpr uint32_t BG_GALLERY_INTERVAL_MS = 5000;

/* --- Mascota (Fase 3) ----------------------------------------------------- */
/* Escala entera de l'sprite (128 px -> 128*PET_SCALE px). Valors 1..3. */
static constexpr uint8_t PET_SCALE = 2;
/* Fila (y) on comenca el rectangle de la mascota. X = centrat + PET_OFFSET_X. */
static constexpr int PET_AREA_TOP = 44;
static constexpr int PET_OFFSET_X = 0;
/* Galeria d'animacions: temps entre animacio i animacio. */
static constexpr uint32_t PET_ANIM_SWITCH_MS = 6000;
/* Fons que es fa servir a la pantalla de prova de la mascota. */
static constexpr const char* PET_TEST_BG = "weather_00";

/* --- UI (Fase 4) ---------------------------------------------------------- */
/* Franja del HUD superior (hora, temps, WiFi). */
static constexpr int UI_HUD_TOP = 0;
static constexpr int UI_HUD_H   = 40;

/* Panell de barres de necessitats (4 files compactes). */
static constexpr int UI_BARS_TOP = 304;
static constexpr int UI_BARS_BOT = 356;

/* Zona inferior: boto MENU (menu tancat) o panell desplegable (obert). */
static constexpr int UI_MENU_BTN_TOP = 420;
static constexpr int UI_MENU_BTN_BOT = 476;
static constexpr int UI_MENU_BTN_L   = 60;
static constexpr int UI_MENU_BTN_R   = 260;

/* Panell del menu desplegable (cobreix les barres i el boto MENU, no la mascota). */
static constexpr int UI_MENU_TOP    = 304;
static constexpr int UI_MENU_BOT    = 480;
static constexpr int UI_MENU_HEADER = 32;

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
