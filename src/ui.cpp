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
constexpr uint16_t kBarBg     = 0x39E7;   /* gris fosc */
constexpr uint16_t kColHunger = 0xFD20;   /* taronja */
constexpr uint16_t kColHappy  = 0xFFE0;   /* groc */
constexpr uint16_t kColEnergy = 0x07E0;   /* verd */
constexpr uint16_t kColHealth = 0xF800;   /* vermell */
constexpr uint16_t kDim       = 0x7BEF;   /* gris clar */

constexpr int kBarsH = (UI_BARS_BOT - UI_BARS_TOP) / 4;   /* 16 px */
constexpr int kBtnW  = SCREEN_W / 4;                      /* 80 px */

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

/* Una fila de barra: icona + barra amb valor 0..100. */
void drawBarRow(int row, int kind, uint16_t col, uint8_t value)
{
    const int y = UI_BARS_TOP + row * kBarsH;
    const int cy = y + kBarsH / 2;
    const int is = kBarsH - 4;

    switch (kind) {
        case 0: iconApple(12, cy, is, col, TFT_BLACK); break;
        case 1: iconSmiley(12, cy, is / 2 + 1, col); break;
        case 2: iconBattery(12, cy, is, col, value); break;
        default: iconHeart(12, cy, is + 2, col); break;
    }

    const int bx = 26;
    const int bw = SCREEN_W - bx - 4;
    const int bh = kBarsH - 7;
    const int by = y + 3;
    gfx().fillRoundRect(bx, by, bw, bh, 3, kBarBg);
    const int fillW = (bw * ((value > 100) ? 100 : value)) / 100;
    if (fillW > 8) {
        gfx().fillRoundRect(bx, by, fillW, bh, 3, col);
    } else if (fillW > 0) {
        gfx().fillRect(bx, by, fillW, bh, col);
    }
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

    gfx().fillRect(0, UI_BARS_TOP, SCREEN_W, UI_BARS_BOT - UI_BARS_TOP, TFT_BLACK);
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

constexpr uint16_t kBtnCol[4] = {0xFC60, 0x2C7F, 0x7A1F, 0x07E0};  /* tarong., blau, lila, verd */
constexpr const char* kBtnLabel[4] = {"Menjar", "Jugar", "Dormir", "Curar"};

int buttonIndex(Zone zone)
{
    switch (zone) {
        case Zone::Feed:  return 0;
        case Zone::Play:  return 1;
        case Zone::Sleep: return 2;
        case Zone::Heal:  return 3;
        default:          return -1;
    }
}

}  // namespace

void drawButtons()
{
    TFT_eSPI& t = gfx();
    for (int i = 0; i < 4; ++i) {
        const int x = i * kBtnW;
        t.fillRect(x, UI_BUTTONS_TOP, kBtnW, UI_BUTTONS_BOT - UI_BUTTONS_TOP, kBtnCol[i]);
        t.drawLine(x, UI_BUTTONS_TOP, x, UI_BUTTONS_BOT, TFT_BLACK);

        const int cx = x + kBtnW / 2;
        const int iy = UI_BUTTONS_TOP + 28;
        switch (i) {
            case 0: iconApple(cx, iy, 34, TFT_WHITE, kBtnCol[i]); break;
            case 1: iconBall(cx, iy, 34, TFT_WHITE, kBtnCol[i]); break;
            case 2: iconMoon(cx, iy, 34, TFT_WHITE, kBtnCol[i]); break;
            default: iconCross(cx, iy, 34, TFT_WHITE); break;
        }

        t.setTextDatum(MC_DATUM);
        t.setTextFont(2);
        t.setTextColor(TFT_WHITE, kBtnCol[i]);
        t.drawString(kBtnLabel[i], cx, UI_BUTTONS_BOT - 13);
    }
}

void setButtonPressed(Zone zone, bool pressed)
{
    const int idx = buttonIndex(zone);
    if (idx < 0) {
        return;
    }
    const int x = idx * kBtnW;
    const int h = UI_BUTTONS_BOT - UI_BUTTONS_TOP;
    TFT_eSPI& t = gfx();
    const uint16_t col = pressed ? TFT_WHITE : kBtnCol[idx];
    t.drawRect(x + 1, UI_BUTTONS_TOP + 1, kBtnW - 2, h - 2, col);
    t.drawRect(x + 2, UI_BUTTONS_TOP + 2, kBtnW - 4, h - 4, col);
}

Zone hitTest(int16_t x, int16_t y)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
        return Zone::None;
    }

    int16_t px = 0;
    int16_t py = 0;
    int16_t pw = 0;
    int16_t ph = 0;
    petRect(px, py, pw, ph);
    if (x >= px && x < px + pw && y >= py && y < py + ph) {
        return Zone::Pet;
    }

    if (y < UI_HUD_H) {
        return (x < 90) ? Zone::HudClock : Zone::HudOther;
    }

    if (y >= UI_BUTTONS_TOP) {
        switch (x / kBtnW) {
            case 0: return Zone::Feed;
            case 1: return Zone::Play;
            case 2: return Zone::Sleep;
            case 3: return Zone::Heal;
            default: return Zone::None;
        }
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


