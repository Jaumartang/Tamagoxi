/*
 * test_main.cpp - Firmware de PROVES DE GRAFICS del Tamagoxi (entorn "test").
 *
 * Nomes fa servir la SD, la pantalla i el tactil: res de so, xarxa ni mascota.
 * Serveix per revisar d'un cop d'ull tots els fons i totes les animacions que
 * hi ha a la targeta.
 *
 *   pio run -e test -t upload        (ATENCIO: sobreescriu el firmware normal)
 *   pio run -e esp32dev -t upload    (per tornar-hi)
 *
 * Navegacio (tot amb el dit):
 *   barra de dalt:  [FONS] [ANIMACIONS]  -> pestanyes
 *   barra de baix:  <  >  per canviar     (i el numero gran al mig)
 *   a ANIMACIONS:   [>] tambe reprodueix / atura l'animacio
 *   el fons i el dragó es veuen barrejats, tal com quedaran de veritat.
 */
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "bg_renderer.h"
#include "display.h"
#include "pins.h"
#include "sd_assets.h"
#include "touch.h"

namespace {

constexpr uint16_t kPanel  = 0x2124;    /* gris fosc de la barra */
constexpr uint16_t kTrack  = 0x4228;
constexpr uint16_t kAccent = 0xF81F;    /* magenta */
constexpr uint16_t kGood   = 0x07E0;
constexpr uint16_t kText   = TFT_WHITE;

constexpr int kBarTop = 36;             /* alcada de la barra de pestanyes */
constexpr int kBarBot = 76;             /* alcada de la barra de controls */

enum class Tab : uint8_t { Bg, Anim };

Tab      gTab      = Tab::Bg;
int      gBg       = 0;
int      gAnim     = 0;
int      gFrame    = 0;
bool     gPlaying  = true;
uint32_t gNextMs   = 0;
uint8_t* gFrameBuf = nullptr;           /* 128x128x2 */

const SdAssets::Pet& pet()
{
    return SdAssets::pet(0);
}

/* Pinta el frame actual de la mascota amb les seves transparencies.
 * Ho fa per tongades de pixels seguides: es rapid i queda net. */
void drawPet(int x, int y, int scale)
{
    if (gFrameBuf == nullptr || !SdAssets::report().petsLoaded) {
        return;
    }
    const SdAssets::Pet& p = pet();
    if (p.animCount == 0 || gAnim >= p.animCount) {
        return;
    }
    const char* err = nullptr;
    const size_t bytes = static_cast<size_t>(p.width) * p.height * 2u;
    if (!SdAssets::readPetFrame(0, p.anims[gAnim].name, static_cast<uint8_t>(gFrame),
                                gFrameBuf, bytes, &err)) {
        return;
    }
    const uint16_t* buf = reinterpret_cast<const uint16_t*>(gFrameBuf);
    TFT_eSPI& t = Display::driver();
    for (int row = 0; row < p.height; ++row) {
        int runStart = -1;
        for (int col = 0; col <= p.width; ++col) {
            const bool solid = (col < p.width) &&
                               !(p.hasTransparent && buf[row * p.width + col] == p.transparent);
            if (solid && runStart < 0) {
                runStart = col;
            } else if (!solid && runStart >= 0) {
                /* Pintam la tongada amb el color del primer pixel. */
                t.fillRect(x + runStart * scale, y + row * scale,
                           (col - runStart) * scale, scale,
                           buf[row * p.width + runStart]);
                runStart = -1;
            }
        }
    }
}

/* --- Barres d'interfície ------------------------------------------------ */

void drawTabs()
{
    TFT_eSPI& t = Display::driver();
    t.fillRect(0, 0, 320, kBarTop, kPanel);
    t.setTextDatum(MC_DATUM);
    t.setTextFont(2);

    const char* names[2] = {"FONS", "ANIMACIONS"};
    for (int i = 0; i < 2; ++i) {
        const int x = 4 + i * 156;
        const bool on = (static_cast<int>(gTab) == i);
        t.fillRoundRect(x, 4, 152, kBarTop - 8, 8, on ? kAccent : kTrack);
        t.setTextColor(kText, on ? kAccent : kTrack);
        t.drawString(names[i], x + 76, kBarTop / 2);
    }
}

void drawControls()
{
    TFT_eSPI& t = Display::driver();
    const int y = 480 - kBarBot;
    t.fillRect(0, y, 320, kBarBot, kPanel);

    /* Botons < i > */
    t.fillRoundRect(6, y + 6, 64, kBarBot - 12, 8, kTrack);
    t.fillRoundRect(250, y + 6, 64, kBarBot - 12, 8, kTrack);
    t.setTextDatum(MC_DATUM);
    t.setTextFont(4);
    t.setTextColor(kText, kTrack);
    t.drawString("<", 38, y + kBarBot / 2);
    t.drawString(">", 282, y + kBarBot / 2);

    /* El numero/estat del mig */
    char info[48];
    if (gTab == Tab::Bg) {
        const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
        snprintf(info, sizeof(info), "%d / %d", gBg + 1, bgs.count);
    } else {
        snprintf(info, sizeof(info), "%s  %d/%d", pet().anims[gAnim].name,
                 gFrame + 1, pet().anims[gAnim].frames);
    }
    t.setTextFont(2);
    t.setTextColor(kText, kPanel);
    t.drawString(info, 160, y + 18);

    /* A animacions, el boto de reproduir */
    if (gTab == Tab::Anim) {
        t.setTextFont(1);
        t.setTextColor(gPlaying ? kGood : kText, kPanel);
        t.drawString(gPlaying ? "reproduint" : "aturat", 160, y + 44);
    } else {
        t.setTextFont(1);
        t.setTextColor(kText, kPanel);
        t.drawString("toca < o > per canviar de fons", 160, y + 44);
    }
}

/* --- Pantalla sencera --------------------------------------------------- */

void drawAll()
{
    TFT_eSPI& t = Display::driver();

    if (gTab == Tab::Bg) {
        /* El fons de veritat, a pantalla completa, amb el drago a sobre. */
        const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
        if (bgs.count > 0) {
            BgRenderer::drawFull(bgs.names[gBg]);
        } else {
            t.fillScreen(kPanel);
        }
        drawPet(96, 140, 1);
    } else {
        /* Fons pla per veure-hi be el drago, una mica mes gran. */
        t.fillScreen(kPanel);
        drawPet(32, 110, 2);
    }
    drawTabs();
    drawControls();
}

/* --- Toques ------------------------------------------------------------- */

void changeBg(int delta)
{
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    if (bgs.count == 0) {
        return;
    }
    gBg = (gBg + delta + bgs.count) % bgs.count;
    drawAll();
    Serial.printf("[TEST] fons %d/%d: %s\n", gBg + 1, bgs.count, bgs.names[gBg]);
}

void changeAnim(int delta)
{
    const SdAssets::Pet& p = pet();
    if (p.animCount == 0) {
        return;
    }
    gAnim = (gAnim + delta + p.animCount) % p.animCount;
    gFrame = 0;
    gNextMs = millis();
    drawAll();
    Serial.printf("[TEST] animacio %d/%d: %s (%u frames)\n", gAnim + 1, p.animCount,
                  p.anims[gAnim].name, static_cast<unsigned>(p.anims[gAnim].frames));
}

void handleTap(int16_t x, int16_t y)
{
    if (y < kBarTop) {                       /* pestanyes */
        gTab = (x < 160) ? Tab::Bg : Tab::Anim;
        drawAll();
        return;
    }
    if (y >= 480 - kBarBot) {                /* controls */
        if (x < 76) {
            if (gTab == Tab::Bg) { changeBg(-1); } else { changeAnim(-1); }
        } else if (x > 244) {
            if (gTab == Tab::Bg) { changeBg(+1); } else { changeAnim(+1); }
        } else if (gTab == Tab::Anim) {
            gPlaying = !gPlaying;
            drawControls();
        }
        return;
    }
    /* Al mig de la pantalla: a ANIMACIONS avancam un frame; a FONS, canviem. */
    if (gTab == Tab::Anim && pet().animCount > 0) {
        gFrame = (gFrame + 1) % pet().anims[gAnim].frames;
        drawAll();
    } else {
        changeBg(+1);
    }
}

/* --- Consola minima (per pilotar-lo des del PC o des del mobil) --------- */

void pollSerial()
{
    static char   line[40];
    static size_t n = 0;
    while (Serial.available() > 0) {
        const int ch = Serial.read();
        if (ch < 0) {
            break;
        }
        if (ch == '\n' || ch == '\r') {
            line[n] = '\0';
            n = 0;
            if (strncmp(line, "tap ", 4) == 0) {
                int x = 0;
                int y = 0;
                if (sscanf(line + 4, "%d %d", &x, &y) == 2) {
                    Serial.printf("[TEST] toc %d,%d\n", x, y);
                    handleTap(static_cast<int16_t>(x), static_cast<int16_t>(y));
                }
            } else if (strcmp(line, "bg next") == 0 || strcmp(line, "fons") == 0) {
                changeBg(+1);
            } else if (strncmp(line, "bg ", 3) == 0) {
                const int i = atoi(line + 3);
                const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
                if (i >= 0 && i < bgs.count) {
                    gBg = i;
                    drawAll();
                    Serial.printf("[TEST] fons %d/%d: %s\n", gBg + 1, bgs.count, bgs.names[gBg]);
                }
            } else if (strcmp(line, "anim next") == 0) {
                changeAnim(+1);
            } else if (strncmp(line, "anim ", 5) == 0) {
                const int i = atoi(line + 5);
                if (i >= 0 && i < pet().animCount) {
                    gAnim = i;
                    gFrame = 0;
                    drawAll();
                    Serial.printf("[TEST] animacio: %s\n", pet().anims[gAnim].name);
                }
            } else if (strcmp(line, "tab") == 0) {
                gTab = (gTab == Tab::Bg) ? Tab::Anim : Tab::Bg;
                drawAll();
            } else if (line[0] != '\0') {
                Serial.println(F("[TEST] ordres: tap X Y | bg next | bg N | anim next | anim N | tab"));
            }
            continue;
        }
        if (n + 1 < sizeof(line)) {
            line[n++] = static_cast<char>(ch);
        } else {
            n = 0;
        }
    }
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("=== Tamagoxi: PROVES DE GRAFICS ==="));

    Display::init();
    Display::setBacklight(100);
    Touch::init();

    if (!SdAssets::begin()) {
        /* La targeta de vegades triga una mica a respondre en arrencar en fred:
         * tornam a provar-ho unes quantes vegades abans de donar-nos per vençuts. */
        bool ok = false;
        for (int i = 0; i < 4 && !ok; ++i) {
            delay(400);
            ok = SdAssets::begin();
        }
    }

    if (!SdAssets::report().mounted) {
        TFT_eSPI& t = Display::driver();
        t.fillScreen(TFT_RED);
        t.setTextDatum(MC_DATUM);
        t.setTextFont(4);
        t.setTextColor(TFT_WHITE, TFT_RED);
        t.drawString("Sense targeta SD", 160, 200);
        t.setTextFont(2);
        t.drawString(SdAssets::report().firstError, 160, 240);
    } else {
        const SdAssets::Report& r = SdAssets::report();
        Serial.printf("[TEST] %u fons | mascota %s %ux%u, %u animacions\n",
                      static_cast<unsigned>(r.bgCount), SdAssets::pet(0).folder,
                      static_cast<unsigned>(r.petWidth), static_cast<unsigned>(r.petHeight),
                      static_cast<unsigned>(r.animCount));
        gFrameBuf = static_cast<uint8_t*>(malloc(128u * 128u * 2u));
        drawAll();
    }
    gNextMs = millis();
    Serial.println(F("[TEST] toca: pestanyes a dalt, < > a baix, el mig avanca"));
}

void loop()
{
    pollSerial();

    if (Touch::update()) {
        int16_t x = -1;
        int16_t y = -1;
        Touch::getCoords(x, y);
        handleTap(x, y);
        /* Esperam que aixequi el dit: una toque = una accio. */
        while (Touch::update()) {
            delay(20);
        }
    }

    if (gTab == Tab::Anim && gPlaying && SdAssets::petCount() > 0) {
        const SdAssets::Pet& p = pet();
        if (p.animCount > 0) {
            const uint32_t now = millis();
            const uint32_t fps = (p.fps > 0) ? p.fps : 6;
            if (now >= gNextMs) {
                gNextMs = now + 1000u / fps;
                gFrame = (gFrame + 1) % p.anims[gAnim].frames;
                drawAll();
            }
        }
    }
    delay(5);
}
