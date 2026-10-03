#pragma once

#include <stdint.h>

/*
 * touch.h - Driver del tactil resistiu XPT2046 (comparteix el bus HSPI amb el
 * LCD: MOSI 13 / MISO 12 / SCK 14; CS propi al GPIO 33).
 *
 * Fa servir el driver XPT2046 que ja porta TFT_eSPI (Extensions/Touch.*): el
 * tactil comparteix bus amb el LCD i TFT_eSPI ja en gestiona el CS i el canvi
 * de velocitat (SPI_TOUCH_FREQUENCY a include/tft_setup.h).
 *
 * Lectura rapida: es fa servir getTouchRawZ() + getTouchRaw() + convertRawXY()
 * (unes desenes de microsegons per lectura). Deliberadament NO es fa servir
 * tft.getTouch(), que internament fa 5 validacions amb delay() (5-25 ms) i
 * bloquejaria el bucle principal.
 *
 * El calibratge (5 valors de TFT_eSPI) es desa a NVS amb Preferences: nomes cal
 * calibrar un cop per placa.
 *
 * REQUISIT D'ORDRE: Touch::init() s'ha de cridar DESPRES de Display::init().
 */

namespace Touch {

/* Inicialitza el tactil i carrega el calibratge desat de la NVS. Idempotent. */
void init();

/* Executa l'assistent de calibratge a pantalla (4 fletxes) i en desa el
 * resultat a NVS. Es BLOQUEJANT (uns quants segons): nomes des de setup() o
 * des d'un gest explicit de l'usuari, mai des del bucle d'animacio. */
void calibrate();

/* Hi ha un calibratge desat a la NVS? */
bool isCalibrated();

/* Fa una lectura del panell (com a molt una cada TOUCH_MIN_READ_INTERVAL_MS) i
 * actualitza l'estat intern. Retorna true si hi ha un dit a sobre. */
bool update();

/* Hi ha un dit a sobre la pantalla (segons l'ultima lectura). */
bool isPressed();

/* Ultima posicio en PIXELS de pantalla, ja mapejada. Si no hi ha calibratge,
 * x i y queden a -1. */
void getCoords(int16_t& x, int16_t& y);

/* Ultima posicio crua del controlador (0..4095), per diagnostics i calibratge.
 * Retorna false si no hi ha contacte. */
bool getRaw(int16_t& rawX, int16_t& rawY);

/* Llindar de pressio (z) en calent. */
void setPressureThreshold(uint16_t threshold);
uint16_t pressureThreshold();

}  // namespace Touch
