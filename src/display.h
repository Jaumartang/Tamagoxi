#pragma once

#include <TFT_eSPI.h>

#include <stdint.h>

/*
 * display.h - Pantalla principal (LCDWiki E32R40T: ST7796S 320x480 per SPI).
 *
 * Encapsula TFT_eSPI i la retroil-luminacio per PWM. Cap altre modul no ha de
 * tocar TFT_eSPI directament: sempre a traves d'aquest modul (o del driver()
 * que exposa, que es el que la resta de renderitzadors de la SD fan servir).
 *
 * Configuracio de maquinari a include/tft_setup.h (pins a include/pins.h).
 * Reaprofitat/adaptat de la HAL validada del projecte v1 (Tamagoxi).
 *
 * A partir de la Fase 4 (UI) aquest modul tambe registrara el dispositiu LVGL
 * i el pont de flush, com al v1; la Fase 0 nomes necessita pintar pel TFT.
 */

namespace Display {

/* Inicialitza TFT_eSPI i la retroil-luminacio. Idempotent.
 * Retorna false si no s'ha pogut inicialitzar (mai deixa el sistema penjat). */
bool init();

/* Driver TFT_eSPI de sota. El comparteixen els renderitzadors de fons/sprite. */
TFT_eSPI& driver();

/* Mides en pixels segons l'orientacio configurada (320x480 vertical). */
uint16_t width();
uint16_t height();

/* Retroil-luminacio per PWM: 0 = apagada, 100 = maxima. */
void setBacklight(uint8_t percent);

/* Ultim percentatge aplicat amb setBacklight() (comenca a 100). */
uint8_t backlight();

/* Forca el pin de retroil-luminacio com a sortida DIGITAL (surt del mode PWM).
 * Serveix per diagnosticar si un problema de brillantor es del pin o del PWM:
 * si amb el pin digital tambe no canvia res, el problema es de maquinari o de
 * pin. La seguent crida a setBacklight() hi torna (reactiva el PWM). */
void setBacklightDigital(bool on);

}  // namespace Display
