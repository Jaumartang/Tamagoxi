#include "ui.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "display.h"
#include "sprite_renderer.h"

namespace {

TFT_eSPI& gfx()
{
    return Display::driver();
}

/* Colors (RGB565) vius i amables. */
constexpr uint16_t kColHunger = 0xFD20;   /* taronja */
constexpr uint16_t kColHappy  = 0xFFE0;   /* groc */
constexpr uint16_t kColEnergy = 0x07E0;   /* verd */
constexpr uint16_t kColHealth = 0xF800;   /* vermell */
constexpr uint16_t kDim       = 0x7BEF;   /* gris clar */
constexpr uint16_t kPanelBg   = 0x18E3;   /* gris fosc dels panells */
constexpr uint16_t kPanelEdge = 0x4A49;   /* vora dels panells */
constexpr uint16_t kBarTrack  = 0x39E7;   /* fons de les barres */
constexpr uint16_t kAccent    = 0x2C7F;   /* blau del boto MENU / capcalera */

constexpr int kBarsH    = (UI_BARS_BOT - UI_BARS_TOP) / 4;                       /* 45 px */
constexpr int kMenuRowH = (UI_MENU_BOT - UI_MENU_STRIP -
                           (UI_MENU_TOP + UI_MENU_HEADER)) / 4;                  /* 32 px */

constexpr int menuRowTop(int i) { return UI_MENU_TOP + UI_MENU_HEADER + i * kMenuRowH; }

/* --- Icones dibuixades amb primitives ------------------------------------- */
/* 'col' es el color principal; 'bg' el fons (per fer-hi retalls, p.ex. un mos). */

void iconApple(int cx, int cy, int s, uint16_t col, uint16_t bg)
{
    const int r = (s / 2 > 2) ? (s / 2) : 3;
    gfx().fillCircle(cx, cy, r, col);
    gfx().fillCircle(cx + r, cy - r, (r / 2 > 1) ? (r / 2) : 2, bg);  /* mos */
}

void iconBall(int cx, int cy, int s, uint16_t col, uint16_t bg)
{
    const int r = (s / 2 > 2) ? (s / 2) : 3;
    gfx().fillCircle(cx, cy, r, col);
    gfx().drawCircle(cx, cy, r - 2, bg);
    gfx().drawLine(cx - r, cy, cx + r, cy, bg);
}

void iconMoon(int cx, int cy, int s, uint16_t col, uint16_t bg)
{
    const int r = (s / 2 > 2) ? (s / 2) : 3;
    gfx().fillCircle(cx, cy, r, col);
    gfx().fillCircle(cx + r / 2, cy - r / 3, r, bg);   /* forma de mitja lluna */
}

void iconCross(int cx, int cy, int s, uint16_t col)
{
    const int a = (s / 4 > 2) ? (s / 4) : 3;
    const int b = (s / 2 > 3) ? (s / 2) : 4;
    gfx().fillRect(cx - a, cy - b, 2 * a, 2 * b, col);
    gfx().fillRect(cx - b, cy - a, 2 * b, 2 * a, col);
}

void iconSmiley(int cx, int cy, int r, uint16_t col)
{
    gfx().fillCircle(cx, cy, r, col);
    gfx().fillCircle(cx - r / 2, cy - r / 3, (r / 6 > 1) ? (r / 6) : 1, TFT_BLACK);
    gfx().fillCircle(cx + r / 2, cy - r / 3, (r / 6 > 1) ? (r / 6) : 1, TFT_BLACK);
    gfx().fillCircle(cx, cy + r / 2, r / 2, TFT_BLACK);
    gfx().fillCircle(cx, cy + r / 2 - r / 4, r / 2, col);
}

void iconBattery(int cx, int cy, int s, uint16_t col, uint8_t pct)
{
    const int w = s;
    const int h = (s * 2) / 3;
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    gfx().drawRoundRect(x, y, w, h, 2, TFT_WHITE);
    gfx().fillRect(x + w, y + h / 3, 2, h / 3, TFT_WHITE);
    const int inner = ((w - 4) * pct) / 100;
    if (inner > 0) {
        gfx().fillRect(x + 2, y + 2, inner, h - 4, col);
    }
}

void iconHeart(int cx, int cy, int s, uint16_t col)
{
    const int r = (s / 4 > 2) ? (s / 4) : 2;
    gfx().fillCircle(cx - r, cy - r / 2, r, col);
    gfx().fillCircle(cx + r, cy - r / 2, r, col);
    gfx().fillTriangle(cx - 2 * r, cy, cx + 2 * r, cy, cx, cy + (s / 2), col);
}

/* Una fila de barra (disseny gran): icona + barra rodona + valor numeric. */
void drawBarRow(int row, int kind, uint16_t col, uint8_t value)
{
    const int y = UI_BARS_TOP + row * kBarsH;
    const int cy = y + kBarsH / 2;
    const uint8_t v = (value > 100) ? 100 : value;

    switch (kind) {
        case 0: iconApple(28, cy, 32, col, kPanelBg); break;
        case 1: iconSmiley(28, cy, 17, col); break;
        case 2: iconBattery(28, cy, 32, col, v); break;
        default: iconHeart(28, cy, 34, col); break;
    }

    const int bx = 54;
    const int bw = UI_BARS_PANEL_R - bx - 32;
    const int bh = 18;
    const int by = cy - bh / 2;
    gfx().fillRoundRect(bx, by, bw, bh, bh / 2, kBarTrack);
    const int fillW = (bw * v) / 100;
    if (fillW > bh) {
        gfx().fillRoundRect(bx, by, fillW, bh, bh / 2, col);
    } else if (fillW > 0) {
        gfx().fillRect(bx, by, fillW, bh, col);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
    gfx().setTextDatum(MR_DATUM);
    gfx().setTextFont(2);
    gfx().setTextColor(col, kPanelBg);
    gfx().drawString(buf, UI_BARS_PANEL_R - 6, cy);
}

/* --- Estat per no repintar el que no canvia ------------------------------- */
bool    gHudDrawn = false;
Ui::Hud gLastHud{false, 0, 0, false, false, 0};
bool    gBarsDrawn = false;
uint8_t gLastBars[4] = {255, 255, 255, 255};

}  // namespace

namespace Ui {

void begin()
{
    /* Res a inicialitzar; l'estat el porta aquest modul. */
}

void drawHud(const Hud& hud)
{
    if (gHudDrawn && gLastHud.timeValid == hud.timeValid && gLastHud.hour == hud.hour &&
        gLastHud.minute == hud.minute && gLastHud.wifi == hud.wifi &&
        gLastHud.weatherValid == hud.weatherValid &&
        gLastHud.temperature == hud.temperature) {
        return;  /* res ha canviat */
    }
    gLastHud = hud;
    gHudDrawn = true;

    TFT_eSPI& t = gfx();
    t.fillRect(0, UI_HUD_TOP, SCREEN_W, UI_HUD_H, TFT_BLACK);

    /* Hora (esquerra). */
    char buf[16];
    t.setTextDatum(ML_DATUM);
    t.setTextFont(4);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    if (hud.timeValid) {
        snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(hud.hour),
                 static_cast<unsigned>(hud.minute));
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    t.drawString(buf, 6, UI_HUD_TOP + UI_HUD_H / 2);

    /* Temps (centre): solet + temperatura. */
    if (hud.weatherValid) {
        snprintf(buf, sizeof(buf), "%d", hud.temperature);
        const int tx = SCREEN_W / 2 - 4;
        t.fillCircle(tx - 18, UI_HUD_TOP + UI_HUD_H / 2, 8, 0xFFE0);
        t.setTextDatum(ML_DATUM);
        t.setTextFont(4);
        t.setTextColor(TFT_WHITE, TFT_BLACK);
        t.drawString(buf, tx, UI_HUD_TOP + UI_HUD_H / 2);
        t.drawCircle(tx + t.textWidth(buf) + 5, UI_HUD_TOP + UI_HUD_H / 2 - 8, 4, TFT_WHITE);
    } else {
        t.setTextDatum(MR_DATUM);
        t.setTextFont(2);
        t.setTextColor(kDim, TFT_BLACK);
        t.drawString("--", SCREEN_W / 2, UI_HUD_TOP + UI_HUD_H / 2);
    }

    /* WiFi (dreta): 3 barres d'intensitat. */
    const int wx = SCREEN_W - 26;
    const int wy = UI_HUD_TOP + UI_HUD_H - 9;
    const uint16_t wc = hud.wifi ? TFT_GREEN : kDim;
    for (int i = 0; i < 3; ++i) {
        const int bh = 5 + i * 5;
        t.fillRect(wx + i * 7, wy - bh, 5, bh, wc);
    }
}

void drawBars(uint8_t hunger, uint8_t happiness, uint8_t energy, uint8_t health)
{
    const uint8_t vals[4] = {hunger, happiness, energy, health};

    bool any = !gBarsDrawn;
    for (int i = 0; i < 4 && !any; ++i) {
        if (vals[i] != gLastBars[i]) {
            any = true;
        }
    }
    if (!any) {
        return;  /* cap valor ha canviat */
    }

    gfx().fillRect(0, UI_BARS_TOP, UI_BARS_PANEL_R, UI_BARS_BOT - UI_BARS_TOP, kPanelBg);
    gfx().drawLine(0, UI_BARS_TOP, UI_BARS_PANEL_R - 1, UI_BARS_TOP, kPanelEdge);
    drawBarRow(0, 0, kColHunger, hunger);
    drawBarRow(1, 1, kColHappy, happiness);
    drawBarRow(2, 2, kColEnergy, energy);
    drawBarRow(3, 3, kColHealth, health);

    for (int i = 0; i < 4; ++i) {
        gLastBars[i] = vals[i];
    }
    gBarsDrawn = true;
}

namespace {

constexpr uint16_t kRowCol[4] = {0xFC60, 0x2C7F, 0x7A1F, 0x07E0};  /* tarong., blau, lila, verd */

void drawMenuRow(int i, uint16_t col, const char* label)
{
    const int x = 4;
    const int w = SCREEN_W - 8;
    const int y = menuRowTop(i);
    const int h = kMenuRowH - 6;
    const int cy = y + 3 + h / 2;

    TFT_eSPI& t = gfx();
    t.fillRoundRect(x, y + 3, w, h, 7, col);

    const int cx = x + 24;
    switch (i) {
        case 0: iconApple(cx, cy, 20, TFT_WHITE, col); break;
        case 1: iconBall(cx, cy, 20, TFT_WHITE, col); break;
        case 2: iconMoon(cx, cy, 20, TFT_WHITE, col); break;
        default: iconCross(cx, cy, 20, TFT_WHITE); break;
    }

    t.setTextDatum(ML_DATUM);
    t.setTextFont(2);
    t.setTextColor(TFT_WHITE, col);
    t.drawString(label, x + 46, cy);
}

}  // namespace

void drawMenuButton()
{
    const int x = UI_MENU_BTN_L;
    const int y = UI_MENU_BTN_TOP;
    const int w = SCREEN_W - x;
    const int h = UI_MENU_BTN_BOT - y;

    TFT_eSPI& t = gfx();
    t.fillRect(x, y, w, h, kAccent);
    t.drawLine(x, y, x, y + h - 1, kPanelEdge);
    t.drawLine(x, y, SCREEN_W - 1, y, kPanelEdge);

    const int hxc = x + w / 2;
    const int hy = y + 52;
    for (int i = -1; i <= 1; ++i) {
        t.fillRect(hxc - 19, hy + i * 12 - 3, 38, 6, TFT_WHITE);
    }

    t.setTextDatum(MC_DATUM);
    t.setTextFont(4);
    t.setTextColor(TFT_WHITE, kAccent);
    t.drawString("MENU", hxc, y + 124);
}

void drawMenu(bool sleeping)
{
    TFT_eSPI& t = gfx();

    t.fillRect(0, UI_MENU_TOP, SCREEN_W, UI_MENU_BOT - UI_MENU_TOP, kPanelBg);

    /* Capcalera amb el titol i la X de tancar. */
    t.fillRect(0, UI_MENU_TOP, SCREEN_W, UI_MENU_HEADER, kAccent);
    t.setTextDatum(ML_DATUM);
    t.setTextFont(2);
    t.setTextColor(TFT_WHITE, kAccent);
    t.drawString("Que vols fer?", 8, UI_MENU_TOP + UI_MENU_HEADER / 2);

    const int xx = SCREEN_W - 22;
    const int xy = UI_MENU_TOP + UI_MENU_HEADER / 2;
    t.drawLine(xx - 8, xy - 8, xx + 8, xy + 8, TFT_WHITE);
    t.drawLine(xx - 8, xy + 8, xx + 8, xy - 8, TFT_WHITE);

    drawMenuRow(0, kRowCol[0], "Menjar");
    drawMenuRow(1, kRowCol[1], "Jugar");
    drawMenuRow(2, kRowCol[2], sleeping ? "Despertar" : "Dormir");
    drawMenuRow(3, kRowCol[3], "Curar");

    /* Franja inferior: les 4 necessitats en versio compacta (alternativa). */
    const int y0 = UI_MENU_BOT - UI_MENU_STRIP;
    t.drawLine(0, y0, SCREEN_W - 1, y0, kPanelEdge);
    const int cw = SCREEN_W / 4;
    const uint16_t cols[4] = {kColHunger, kColHappy, kColEnergy, kColHealth};
    for (int i = 0; i < 4; ++i) {
        const int bx = i * cw + 5;
        const int bw = cw - 10;
        const int bh = 10;
        const int by = y0 + (UI_MENU_STRIP - bh) / 2;
        const uint8_t v = (gLastBars[i] > 100) ? 100 : gLastBars[i];
        t.fillRoundRect(bx, by, bw, bh, bh / 2, kBarTrack);
        const int fw = (bw * v) / 100;
        if (fw > bh) {
            t.fillRoundRect(bx, by, fw, bh, bh / 2, cols[i]);
        } else if (fw > 0) {
            t.fillRect(bx, by, fw, bh, cols[i]);
        }
    }
}

Zone hitTest(int16_t x, int16_t y, bool menuOpen)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
        return Zone::None;
    }

    if (menuOpen) {
        if (y < UI_MENU_TOP) {
            return Zone::None;
        }
        if (y < UI_MENU_TOP + UI_MENU_HEADER) {
            return (x > SCREEN_W - 56) ? Zone::MenuClose : Zone::None;
        }
        const int row = (y - (UI_MENU_TOP + UI_MENU_HEADER)) / kMenuRowH;
        switch (row) {
            case 0: return Zone::MenuFeed;
            case 1: return Zone::MenuPlay;
            case 2: return Zone::MenuSleep;
            case 3: return Zone::MenuHeal;
            default: return Zone::None;
        }
    }

    if (y < UI_HUD_H) {
        return (x < 90) ? Zone::HudClock : Zone::HudOther;
    }

    int16_t px = 0;
    int16_t py = 0;
    int16_t pw = 0;
    int16_t ph = 0;
    petRect(px, py, pw, ph);
    if (x >= px && x < px + pw && y >= py && y < py + ph) {
        return Zone::Pet;
    }

    if (x >= UI_MENU_BTN_L && y >= UI_MENU_BTN_TOP) {
        return Zone::MenuButton;
    }
    return Zone::None;
}

void petRect(int16_t& x, int16_t& y, int16_t& w, int16_t& h)
{
    const SpriteRenderer::Status& s = SpriteRenderer::status();
    x = s.x;
    y = s.y;
    w = static_cast<int16_t>(s.boxW);
    h = static_cast<int16_t>(s.boxH);
}

void invalidate()
{
    gHudDrawn = false;
    gBarsDrawn = false;
}

void drawHeart(int16_t cx, int16_t cy)
{
    const int r = 7;
    TFT_eSPI& t = gfx();
    t.fillCircle(cx - r, cy - r / 2, r, TFT_RED);
    t.fillCircle(cx + r, cy - r / 2, r, TFT_RED);
    t.fillTriangle(cx - 2 * r, cy, cx + 2 * r, cy, cx, cy + 2 * r, TFT_RED);
}

}  // namespace Ui


