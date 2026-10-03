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
