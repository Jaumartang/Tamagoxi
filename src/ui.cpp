#include "ui.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "bg_renderer.h"
#include "sprite_renderer.h"

#include "tg_config.h"
#include "display.h"
#include "sprite_renderer.h"

namespace {

TFT_eSPI& gfx()
{
    return Display::driver();
}

/* Colors de les necessitats (RGB565), fixes perque tenen significat. */
constexpr uint16_t kColHunger = 0xFD4B;   /* préssec */
constexpr uint16_t kColHappy  = 0xFC77;   /* rosa */
constexpr uint16_t kColEnergy = 0x8F15;   /* menta */
constexpr uint16_t kColHealth = 0xEA30;   /* fúcsia */
constexpr uint16_t kDim       = 0x7BEF;   /* gris clar */

/* Temes de color de la UI (es poden canviar en calent amb la comanda 'theme'). */
struct Theme {
    uint16_t panelBg;      /* fons de panells i HUD */
    uint16_t panelEdge;    /* vores i separadors */
    uint16_t barTrack;     /* fons de les barres */
    uint16_t accent;       /* boto MENU */
    uint16_t close;        /* boto de tancar */
    uint16_t row[4];       /* botons d'accio */
};

const Theme kThemes[] = {
    /* rosa/lila (per defecte) */
    {0x418B, 0xCD1B, 0x6AB0, 0xEA94, 0xC9ED, {0xF3CF, 0xE294, 0x8A36, 0xAB79}},
    /* nit violeta */
    {0x28E8, 0xAC1B, 0x520D, 0xCADB, 0xB98B, {0xE332, 0xDA99, 0x8A9C, 0xABDC}},
    /* rosa pastel */
    {0x7A8C, 0xFEFD, 0xAC12, 0xFBD5, 0xD28F, {0xFC72, 0xF376, 0xBB79, 0xDCBC}},
};
constexpr uint8_t kThemeCount = static_cast<uint8_t>(sizeof(kThemes) / sizeof(kThemes[0]));
const char* const kThemeNames[kThemeCount] = {"rosa-lila", "nit-violeta", "rosa-pastel"};

uint8_t gThemeIndex = 0;

const Theme& th()
{
    return kThemes[gThemeIndex];
}

constexpr int kBarRowH = (UI_BARS_BOT - UI_BARS_TOP) / 4;                          /* 17 px */
constexpr int kActionW = UI_BARS_PANEL_R / 4;                                      /* 64 px */
constexpr int kActionH = (UI_BARS_BOT - UI_BARS_TOP) - UI_MENU_STRIP;              /* 56 px */

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

/* Una fila de barra (compacta): icona + barra rodona + valor numeric. */
/* (declarats abans: glassCard els fa servir) */
uint16_t blend565(uint16_t a, uint16_t b, uint8_t pa);
bool insideRound(int x, int y, int rx, int ry, int rw, int rh, int r);

/* Pinta una TARGETA DE VIDRE: llegeix el fons i hi barreja 'tint' (translucid).
 * Serveix per a les barres i per al boto MENU: el fons es veu a travers. */
void glassCard(int x, int y, int w, int h, uint16_t tint, uint8_t pct, int radius)
{
    const char* bgName = SpriteRenderer::backgroundName();
    TFT_eSPI& t = gfx();
    if (bgName == nullptr || bgName[0] == '\0' || !BgRenderer::beginStrip(bgName)) {
        t.fillRoundRect(x, y, w, h, radius, tint);     /* sense SD: pla */
        t.drawRoundRect(x, y, w, h, radius, blend565(tint, 0xFFFF, 70));
        return;
    }

    uint8_t srck[320 * 2];
    uint8_t outk[320 * 2];
    t.startWrite();
    t.setAddrWindow(x, y, w, h);
    for (int ry = y; ry < y + h; ++ry) {
        if (!BgRenderer::readStripRow(ry, x, w, srck)) {
            break;
        }
        for (int i = 0; i < w; ++i) {
            const uint16_t b = static_cast<uint16_t>((srck[i * 2] << 8) | srck[i * 2 + 1]);
            uint16_t c = b;
            if (insideRound(i, ry, 0, y, w, h, radius)) {
                c = blend565(tint, c, pct);
            }
            outk[i * 2]     = static_cast<uint8_t>(c >> 8);
            outk[i * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        t.pushPixels(reinterpret_cast<uint16_t*>(outk), w);
    }
    t.endWrite();
    BgRenderer::endStrip();
}

/* Barreja dos colors RGB565: 'pa' per cent del primer. */
uint16_t blend565(uint16_t a, uint16_t b, uint8_t pa)
{
    const int pb = 100 - pa;
    const int r  = ((a >> 11) & 0x1F) * pa + ((b >> 11) & 0x1F) * pb;
    const int g  = ((a >> 5) & 0x3F) * pa + ((b >> 5) & 0x3F) * pb;
    const int bl = (a & 0x1F) * pa + (b & 0x1F) * pb;
    return static_cast<uint16_t>(((r / 100) << 11) | ((g / 100) << 5) | (bl / 100));
}

/* Es el punt (x,y) dins d'un rectangle arrodonit? (per barrejar-hi color). */
bool insideRound(int x, int y, int rx, int ry, int rw, int rh, int r)
{
    if (rw <= 0 || rh <= 0 || x < rx || x >= rx + rw || y < ry || y >= ry + rh) {
        return false;
    }
    int rr = r;
    if (rr > rw / 2) { rr = rw / 2; }
    if (rr > rh / 2) { rr = rh / 2; }
    if (rr <= 0) {
        return true;
    }
    const int cx = (x < rx + rr) ? (rx + rr) : ((x >= rx + rw - rr) ? (rx + rw - rr - 1) : x);
    const int cy = (y < ry + rr) ? (ry + rr) : ((y >= ry + rh - rr) ? (ry + rh - rr - 1) : y);
    const int dx = x - cx;
    const int dy = y - cy;
    return (dx * dx + dy * dy) <= (rr * rr);
}

void drawBarRow(int row, int kind, uint16_t col, uint8_t value)
{
    const int y = UI_BARS_TOP + row * kBarRowH;
    const int cy = y + kBarRowH / 2;
    const uint8_t v = (value > 100) ? 100 : value;

    switch (kind) {
        case 0: iconApple(9, cy, 10, col, th().panelBg); break;
        case 1: iconSmiley(9, cy, 5, col); break;
        case 2: iconBattery(9, cy, 10, col, v); break;
        default: iconHeart(9, cy, 11, col); break;
    }

    const int bx = 18;
    const int bw = UI_BARS_PANEL_R - bx - 24;
    const int bh = 8;
    const int by = cy - bh / 2;
    gfx().fillRoundRect(bx, by, bw, bh, bh / 2, th().barTrack);
    const int fillW = (bw * v) / 100;
    if (fillW > bh) {
        gfx().fillRoundRect(bx, by, fillW, bh, bh / 2, col);
    } else if (fillW > 0) {
        gfx().fillRect(bx, by, fillW, bh, col);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
    gfx().setTextDatum(MR_DATUM);
    gfx().setTextFont(1);
    gfx().setTextColor(col, th().panelBg);
    gfx().drawString(buf, UI_BARS_PANEL_R - 2, cy);
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

uint8_t themeCount()
{
    return kThemeCount;
}

const char* themeName(uint8_t index)
{
    if (index >= kThemeCount) {
        return "?";
    }
    return kThemeNames[index];
}

uint8_t theme()
{
    return gThemeIndex;
}

void setTheme(uint8_t index)
{
    if (index >= kThemeCount) {
        return;
    }
    gThemeIndex = index;
    invalidate();
}

uint16_t colorPanelBg()
{
    return th().panelBg;
}

uint16_t colorPanelEdge()
{
    return th().panelEdge;
}

uint16_t colorAccent()
{
    return th().accent;
}

uint16_t colorTrack()
{
    return th().barTrack;
}

uint16_t colorClose()
{
    return th().close;
}

uint16_t colorRow(uint8_t index)
{
    return th().row[index & 3u];
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
    t.fillRect(0, UI_HUD_TOP, SCREEN_W, UI_HUD_H, th().panelBg);
    t.drawLine(0, UI_HUD_H - 1, SCREEN_W - 1, UI_HUD_H - 1, th().panelEdge);

    /* Hora (esquerra). */
    char buf[16];
    t.setTextDatum(ML_DATUM);
    t.setTextFont(4);
    t.setTextColor(TFT_WHITE, th().panelBg);
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
        t.setTextColor(TFT_WHITE, th().panelBg);
        t.drawString(buf, tx, UI_HUD_TOP + UI_HUD_H / 2);
        t.drawCircle(tx + t.textWidth(buf) + 5, UI_HUD_TOP + UI_HUD_H / 2 - 8, 4, TFT_WHITE);
    } else {
        t.setTextDatum(MR_DATUM);
        t.setTextFont(2);
        t.setTextColor(kDim, th().panelBg);
        t.drawString("--", SCREEN_W / 2, UI_HUD_TOP + UI_HUD_H / 2);
    }

    /* WiFi (dreta, abans del boto de menu): 3 barres d'intensitat. */
    const int wx = SCREEN_W - 78;
    const int wy = UI_HUD_TOP + UI_HUD_H - 9;
    const uint16_t wc = hud.wifi ? TFT_GREEN : 0x39E7;
    for (int i = 0; i < 3; ++i) {
        const int bh = 5 + i * 5;
        t.fillRect(wx + i * 7, wy - bh, 5, bh, wc);
    }

    /* Boto de menu (dreta): 3 ratlles sobre fons destacat. */
    const int mbx = SCREEN_W - 42;
    const int mby = UI_HUD_TOP + 2;
    const int mbw = 38;
    const int mbh = UI_HUD_H - 4;
    t.fillRoundRect(mbx, mby, mbw, mbh, 5, th().accent);
    const int mbcx = mbx + mbw / 2;
    const int mbcy = mby + mbh / 2;
    for (int i = -1; i <= 1; ++i) {
        t.fillRect(mbcx - 9, mbcy + i * 6 - 1, 18, 2, TFT_WHITE);
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

    /* Barres "de vidre": es llegeix el fons de la SD, s'hi barregen els colors
     * (translucid) i s'envia la fila sencera ja composta. */
    static const uint16_t kCols[4] = {kColHunger, kColHappy, kColEnergy, kColHealth};
    const int areaH = UI_BARS_BOT - UI_BARS_TOP;     /* 84 px */
    const int rowH  = areaH / 4;                     /* 21 px per barra */

    /* Trossos comuns (geometria de la pista). */
    constexpr int kTx = 36;
    constexpr int kTw = 152;
    constexpr int kTh = 13;
    constexpr int kTr = 6;

    TFT_eSPI& t = gfx();

    /* Cada barra va dins una TARGETA DE VIDRE CLAR (blanc translucid): aixi el
     * fons es veu a travers pero tot te contrast. */
    for (int i = 0; i < 4; ++i) {
        glassCard(0, UI_BARS_TOP + i * rowH + 1, UI_BARS_PANEL_R, rowH - 3, 0xFFFF, 46, 9);
    }

    /* La pista i l'omplert, opacs al damunt del vidre (aixi els colors son
     * exactament els del tema i es veuen vius) + brillantor i icona/número. */
    for (int i = 0; i < 4; ++i) {
        const int cy    = UI_BARS_TOP + i * rowH + rowH / 2;
        const uint8_t v = (vals[i] > 100) ? 100 : vals[i];
        const uint16_t col = kCols[i];
        const int ty    = cy - kTh / 2;
        const int fillW = (kTw * v) / 100;

        t.fillRoundRect(kTx, ty, kTw, kTh, kTr, blend565(th().barTrack, 0xFFFF, 35));
        if (fillW > 6) {
            t.fillRoundRect(kTx, ty, fillW, kTh, kTr, col);
            /* Brillantor de dalt i ombra de baix: sembla un tub de vidre. */
            const uint16_t lite = blend565(col, 0xFFFF, 62);
            const uint16_t dark = blend565(col, 0x0000, 32);
            if (fillW > 16) {
                t.fillRoundRect(kTx + 3, ty + 2, fillW - 6, 4, 2, lite);
                t.fillRoundRect(kTx + 3, ty + kTh - 4, fillW - 6, 2, 1, dark);
            }
            /* Vora fosca: fa ressaltar l'omplert damunt del vidre clar. */
            t.drawRoundRect(kTx, ty, fillW, kTh, kTr, blend565(col, 0x0000, 45));
        }
    }

    /* Icones i numeros al damunt: colors foscos (el vidre es clar) perque es
     * llegeixin be, amb una ombra blanca que els fa ressaltar. */
    const uint16_t cardCol = 0xD69A;                       /* el vidre clar */
    for (int i = 0; i < 4; ++i) {
        const int cy = UI_BARS_TOP + i * rowH + rowH / 2;
        const uint8_t v = (vals[i] > 100) ? 100 : vals[i];
        const uint16_t col  = kCols[i];
        const uint16_t deep = blend565(col, 0x0000, 42);   /* versio fosca */
        switch (i) {
            case 0: iconApple(16, cy, 11, deep, cardCol); break;
            case 1: iconSmiley(16, cy, 6, deep); break;
            case 2: iconBattery(16, cy, 11, deep, v); break;
            default: iconHeart(16, cy, 12, deep); break;
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
        t.setTextDatum(MR_DATUM);
        t.setTextFont(2);
        t.setTextColor(0xFFFF, cardCol);
        t.drawString(buf, UI_BARS_PANEL_R - 5, cy + 1);     /* ombra blanca */
        t.setTextColor(deep, cardCol);
        t.drawString(buf, UI_BARS_PANEL_R - 6, cy);
    }

    for (int i = 0; i < 4; ++i) {
        gLastBars[i] = vals[i];
    }
    gBarsDrawn = true;
}

namespace {

void drawActionButton(int i, uint16_t col, const char* label)
{
    const int x = i * kActionW;
    const int y = UI_BARS_TOP;
    const int w = kActionW;
    const int h = kActionH;

    TFT_eSPI& t = gfx();
    t.fillRect(x, y, w, h, col);
    t.drawLine(x, y, x, y + h - 1, th().panelEdge);

    const int cx = x + w / 2;
    const int iy = y + 12;
    switch (i) {
        case 0: iconApple(cx, iy, 19, TFT_WHITE, col); break;
        case 1: iconBall(cx, iy, 19, TFT_WHITE, col); break;
        case 2: iconMoon(cx, iy, 19, TFT_WHITE, col); break;
        default: iconCross(cx, iy, 19, TFT_WHITE); break;
    }

    t.setTextDatum(MC_DATUM);
    t.setTextFont(1);
    t.setTextColor(TFT_WHITE, col);
    t.drawString(label, cx, y + h - 6);
}

void drawCompactBars()
{
    TFT_eSPI& t = gfx();
    const int y0 = UI_BARS_BOT - UI_MENU_STRIP;
    t.drawLine(0, y0, SCREEN_W - 1, y0, th().panelEdge);

    const int cw = SCREEN_W / 4;
    const uint16_t cols[4] = {kColHunger, kColHappy, kColEnergy, kColHealth};
    for (int i = 0; i < 4; ++i) {
        const int bx = i * cw + 4;
        const int bw = cw - 8;
        const int bh = 5;
        const int by = y0 + (UI_MENU_STRIP - bh) / 2;
        const uint8_t v = (gLastBars[i] > 100) ? 100 : gLastBars[i];
        t.fillRoundRect(bx, by, bw, bh, 2, th().barTrack);
        const int fw = (bw * v) / 100;
        if (fw > 2) {
            t.fillRoundRect(bx, by, fw, bh, 2, cols[i]);
        } else if (fw > 0) {
            t.fillRect(bx, by, fw, bh, cols[i]);
        }
    }
}

}  // namespace

void drawMenuButton()
{
    const int x = UI_MENU_BTN_L;
    const int y = UI_MENU_BTN_TOP;
    const int w = SCREEN_W - x;
    const int h = UI_MENU_BTN_BOT - y;

    /* La mateixa targeta de vidre clar que les barres: tot lliga. */
    glassCard(x + 2, y + 1, w - 4, h - 3, 0xFFFF, 46, 12);

    TFT_eSPI& t = gfx();
    /* Tres ratlles gruixudes i vives (amb ombra perque ressaltin). */
    const int hxc = x + w / 2;
    const int hy  = y + 30;
    for (int i = -1; i <= 1; ++i) {
        t.fillRoundRect(hxc - 20, hy + i * 12 - 2, 40, 6, 3, 0xFFFF);
        t.fillRoundRect(hxc - 20, hy + i * 12 - 1, 40, 5, 3, 0xF81F);
    }

    t.setTextDatum(MC_DATUM);
    t.setTextFont(2);
    t.setTextColor(0xFFFF, 0xD69A);
    t.drawString("MENU", hxc, y + 67);
    t.setTextColor(blend565(0xF81F, 0x0000, 40), 0xD69A);
    t.drawString("MENU", hxc, y + 66);
}

void drawMenu(bool sleeping)
{
    TFT_eSPI& t = gfx();

    t.fillRect(0, UI_BARS_TOP, SCREEN_W, UI_BARS_BOT - UI_BARS_TOP, th().panelBg);

    drawActionButton(0, th().row[0], "Menjar");
    drawActionButton(1, th().row[1], "Jugar");
    drawActionButton(2, th().row[2], sleeping ? "Desperta" : "Dormir");
    drawActionButton(3, th().row[3], "Curar");

    /* Boto de tancar (dreta) amb una X. */
    const int bx = UI_MENU_BTN_L;
    const int by = UI_MENU_BTN_TOP;
    const int bw = SCREEN_W - bx;
    const int bh = UI_MENU_BTN_BOT - by;
    const uint16_t closeCol = th().close;
    t.fillRect(bx, by, bw, bh, closeCol);
    t.drawLine(bx, by, bx, by + bh - 1, th().panelEdge);
    t.drawLine(bx, by, SCREEN_W - 1, by, th().panelEdge);
    const int cx = bx + bw / 2;
    const int cy = by + bh / 2 - 4;
    t.drawLine(cx - 8, cy - 8, cx + 8, cy + 8, TFT_WHITE);
    t.drawLine(cx - 8, cy + 8, cx + 8, cy - 8, TFT_WHITE);
    t.setTextDatum(MC_DATUM);
    t.setTextFont(1);
    t.setTextColor(TFT_WHITE, closeCol);
    t.drawString("Tanca", cx, by + bh - 6);

    drawCompactBars();
}

Zone hitTest(int16_t x, int16_t y, bool menuOpen)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) {
        return Zone::None;
    }

    if (menuOpen) {
        if (y < UI_BARS_TOP) {
            return Zone::None;
        }
        if (x >= UI_MENU_BTN_L) {
            return Zone::MenuClose;
        }
        if (y < UI_BARS_TOP + kActionH) {
            switch (x / kActionW) {
                case 0: return Zone::MenuFeed;
                case 1: return Zone::MenuPlay;
                case 2: return Zone::MenuSleep;
                case 3: return Zone::MenuHeal;
                default: return Zone::None;
            }
        }
        return Zone::None;   /* franja de barres compactes: no fa res */
    }

    if (y < UI_HUD_H) {
        if (x >= SCREEN_W - 46) {
            return Zone::HudMenu;
        }
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


