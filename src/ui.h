#pragma once

#include <stdint.h>

/*
 * ui.h - Interficie en pantalla (Fase 4), dibuixada amb primitives de TFT_eSPI.
 *
 * Disposicio (320x480 vertical):
 *   y 0..40    HUD: hora · icona/temperatura · WiFi
 *   y 80..336  MASCOTA (256x256) - aquesta zona NOMES la toca SpriteRenderer
 *   y 336..400 4 barres: gana · felicitat · energia · salut
 *   y 400..480 4 botons grans (80x80): Menjar · Jugar · Dormir · Curar
 *
 * El HUD i les barres es repinten NOMES quan canvia alguna cosa; els botons,
 * un sol cop. Cap funcio d'aqui pinta dins la zona de la mascota.
 */

namespace Ui {

enum class Zone : uint8_t {
    None,
    Pet,        /* damunt la mascota -> caricia */
    HudClock,   /* rellotge del HUD -> premuda llarga = ajustos */
    HudMenu,    /* boto de menu del HUD -> desplegable */
    HudOther,
    MenuButton, /* boto MENU (menu tancat) */
    MenuFeed,   /* opcions del menu desplegable (obert) */
    MenuPlay,
    MenuSleep,
    MenuHeal,
    MenuClose,
};

struct Hud {
    bool    timeValid;
    uint8_t hour;
    uint8_t minute;
    bool    wifi;
    bool    weatherValid;
    int8_t  temperature;    /* graus C */
};

void begin();

/* Tema de color de la UI (rosa/lila, nit violeta, rosa pastel...). */
uint8_t theme();
uint8_t themeCount();
const char* themeName(uint8_t index);
void setTheme(uint8_t index);

/* Targeta de vidre translucida damunt del fons: la fan servir la UI i els
 * panells per tenir-ho tot amb el mateix estil. 'pct' = tant per cent del tint
 * (mes alt = mes ple de color i menys transparent). */
void glassCard(int x, int y, int w, int h, uint16_t tint, uint8_t pct, int radius);
/* Barreja dos colors RGB565 (pa = % del primer). */
uint16_t mix(uint16_t a, uint16_t b, uint8_t pa);
/* Color de text/icona que es llegeix be damunt de 'bg' (clar o fosc). */
uint16_t inkOn(uint16_t bg);

/* Colors del tema actual (els panells modals també els fan servir). */
uint16_t colorPanelBg();
uint16_t colorPanelEdge();
uint16_t colorAccent();
uint16_t colorTrack();
uint16_t colorClose();
uint16_t colorRow(uint8_t index);

/* HUD superior (només repinta si canvia respecte de l'ultim cop). */
void drawHud(const Hud& hud);

/* Barres de necessitats, 0..100 (només repinta les que canvien). */
void drawBars(uint8_t hunger, uint8_t happiness, uint8_t energy, uint8_t health);

/* Boto MENU (menu tancat) i panell desplegable (menu obert). */
void drawMenuButton();
void drawMenu(bool sleeping);

/* Zona de pantalla on s'ha tocat (cal saber si el menu es obert). */
Zone hitTest(int16_t x, int16_t y, bool menuOpen);

/* Rectangle de la mascota (x,y,w,h) tal com el te SpriteRenderer. */
void petRect(int16_t& x, int16_t& y, int16_t& w, int16_t& h);

/* Forca que el proper drawHud/drawBars repinti (en canviar de pantalla). */
void invalidate();

/* Cor de caricia sobre la mascota (es pinta despres de cada frame). */
void drawHeart(int16_t cx, int16_t cy);

}  // namespace Ui
