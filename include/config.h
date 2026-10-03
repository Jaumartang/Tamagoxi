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
/* Galeria de la Fase 2: temps entre fons (auto-avenc). */
static constexpr uint32_t BG_GALLERY_INTERVAL_MS = 5000;
