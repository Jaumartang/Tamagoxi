#pragma once

#include <stdint.h>

/*
 * bg_renderer.h - Pintat d'un fons de pantalla completa (320x480) per franges.
 *
 * Un fons sencer son 307.200 B: NO cap a RAM. Es pinta en franges petites
 * (BG_BAND_LINES files), llegint-les de la SD i enviant-les a la pantalla en
 * streaming dins d'una unica finestra de direccions (setAddrWindow + pushPixels),
 * sense tornar a fixar la finestra a cada franja.
 *
 * ORDRE DE BYTES: els .bin son RGB565 big-endian (pixels crus). Amb el
 * setSwapBytes(false) del projecte, TFT_eSPI els envia tal qual i el panell els
 * interpreta correctament; per tant NO es gira cap byte en llegir.
 */

namespace BgRenderer {

struct Status {
    bool     lastOk;
    uint16_t bandLines;
    uint16_t width;
    uint16_t height;
    uint32_t lastMs;         /* temps de l'ultim fons pintat */
    uint32_t lastBytes;      /* bytes llegits de la SD */
    char     lastName[24];
};

/* Reserva el buffer de franja. Cal sd_assets.begin() abans (mides del manifest). */
bool begin();
void end();

/* Pinta el fons 'name' (sense extensio, p.ex. "weather_00") a pantalla completa.
 * Retorna els ms emprats (0 si hi ha hagut un error; vegeu status().lastOk). */
uint32_t drawFull(const char* name);

/* Pinta NOMES un rectangle del fons 'name' (per restaurar una zona que un menu
 * hi ha tapat, sense repintar tota la pantalla). Retorna els ms emprats. */
uint32_t drawRegion(const char* name, int x, int y, int w, int h);

/* --- Llegir el fons per franges (per barrejar-hi la UI a sobre) ----------- */

/* Obre el fons 'name' per llegir-lo fila a fila (una sola vegada per repintat).
 * Serveix per fer efectes translucids: es llegeix la fila, s'hi barreja el
 * color de la UI i s'envia el resultat. Retorna fals si no s'ha pogut obrir. */
bool beginStrip(const char* name);

/* Llegeix la fila y (des de x, w pixels) en bytes crus del .bin (big-endian),
 * a 'out' (cal espai per w*2 bytes). Cal beginStrip() abans. */
bool readStripRow(int y, int x, int w, uint8_t* out);

/* Tanca el fitxer obert per beginStrip(). */
void endStrip();

/* Diagnostica: mesura per separat el temps de lectura de la SD i el de push a
 * pantalla per a un fons complet (mateix buffer). Ho escriu pel port serie. */
bool bench(const char* name);

const Status& status();

}  // namespace BgRenderer
