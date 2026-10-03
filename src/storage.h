#pragma once

#include <stdint.h>

/*
 * storage.h - Persistencia de configuracio a la NVS (Preferences).
 *
 * Espai de noms "tg_cfg". De moment hi viu la disposicio de la mascota (mida i
 * posicio), que es pot ajustar en calent. La Fase 5 hi afegira les necessitats
 * i la resta d'ajustos del joc.
 */

namespace Storage {

/* (no cal cridar res per inicialitzar; Preferences s'obre per operacio) */
void begin();

/* Disposicio de la mascota. 'valid' indica si hi havia un valor desat. */
struct PetLayout {
    bool    valid;
    int16_t x;
    int16_t y;
    uint8_t scale;
};

PetLayout loadPetLayout();
void savePetLayout(int16_t x, int16_t y, uint8_t scale);
void clearPetLayout();

/* Configuracio de la pantalla principal: fons preferit i tema de color. */
struct HomeCfg {
    bool    bgValid;        /* hi ha un fons fixat a ma */
    char    bg[24];
    bool    themeValid;
    uint8_t theme;
    bool    autoValid;      /* s'ha desat la preferencia de fons automatic */
    bool    autoBg;         /* true = el fons segueix la meteo (weather_NN) */
};

HomeCfg loadHomeCfg();
void saveHomeBg(const char* name);
void saveHomeAuto(bool autoBg);
void saveUiTheme(uint8_t theme);

}  // namespace Storage
