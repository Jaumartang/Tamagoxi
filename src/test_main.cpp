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
#include <SD.h>
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
        uint16_t runColor = 0;
        for (int col = 0; col <= p.width; ++col) {
            /* Els .bin son RGB565 big-endian: cal girar els bytes ✓ */
            uint16_t c = 0;
            bool solid = false;
            if (col < p.width) {
                const uint16_t raw = buf[row * p.width + col];
                c = static_cast<uint16_t>((raw >> 8) | (raw << 8));
                solid = !(p.hasTransparent && c == p.transparent);
            }
            if (solid && runStart < 0) {
                runStart = col;
                runColor = c;
            } else if (!solid && runStart >= 0) {
                t.fillRect(x + runStart * scale, y + row * scale,
                           (col - runStart) * scale, scale, runColor);
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

/* --- Generador de manifest.json ----------------------------------------- */
/* Compta els frames de cada estat i escriu el manifest de la mascota que
 * enten el firmware. Serveix per packs nous amb un altre nombre de frames. */
void writeManifest()
{
    File pets = SD.open("/pets");
    if (!pets || !pets.isDirectory()) {
        Serial.println(F("[TEST] falta la carpeta /pets"));
        return;
    }
    File entry = pets.openNextFile();
    if (!entry) {
        pets.close();
        Serial.println(F("[TEST] /pets es buit"));
        return;
    }
    const char* nm = entry.name();
    const char* b = strrchr(nm, '/');
    char petName[24];
    strlcpy(petName, b ? b + 1 : nm, sizeof(petName));
    entry.close();
    pets.close();

    char dirPath[64];
    snprintf(dirPath, sizeof(dirPath), "/pets/%s", petName);
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        Serial.println(F("[TEST] no puc obrir la mascota"));
        return;
    }

    static const uint8_t kMaxStates = 24;
    static char    names[kMaxStates][24];
    static uint8_t counts[kMaxStates];
    uint8_t nStates = 0;
    File st;
    while ((st = dir.openNextFile()) && nStates < kMaxStates) {
        if (!st.isDirectory()) {
            st.close();
            continue;
        }
        const char* sn = st.name();
        const char* sb = strrchr(sn, '/');
        uint8_t n = 0;
        File f;
        while ((f = st.openNextFile())) {
            const char* fn = f.name();
            const char* fb = strrchr(fn, '/');
            if (strstr(fb ? fb + 1 : fn, ".bin")) {
                ++n;
            }
            f.close();
            if (n >= 250) {
                break;
            }
        }
        if (n > 0) {
            strlcpy(names[nStates], sb ? sb + 1 : sn, 24);
            counts[nStates] = n;
            ++nStates;
        }
        st.close();
    }
    dir.close();

    if (nStates == 0) {
        Serial.println(F("[TEST] cap estat amb frames"));
        return;
    }

    /* Color transparent: el mes repetit del primer frame (els .bin son BE). */
    char firstPath[96];
    snprintf(firstPath, sizeof(firstPath), "%s/%s/00.bin", dirPath, names[0]);
    uint16_t transparent = 0xF81F;
    File f0 = SD.open(firstPath, FILE_READ);
    if (f0) {
        static uint8_t buf[1024];
        const int n = f0.read(buf, sizeof(buf));
        f0.close();
        uint16_t best = 0;
        int bestCount = 0;
        for (int i = 0; i + 1 < n; i += 2) {
            const uint16_t c = static_cast<uint16_t>((buf[i] << 8) | buf[i + 1]);
            int cnt = 0;
            for (int j = 0; j + 1 < n; j += 2) {
                if (static_cast<uint16_t>((buf[j] << 8) | buf[j + 1]) == c) {
                    ++cnt;
                }
            }
            if (cnt > bestCount) {
                bestCount = cnt;
                best = c;
            }
        }
        if (bestCount > (n / 2) / 3) {      /* si domina, es el fons */
            transparent = best;
        }
    }

    snprintf(dirPath, sizeof(dirPath), "/pets/%s/manifest.json", petName);
    File out = SD.open(dirPath, FILE_WRITE);
    if (!out) {
        Serial.println(F("[TEST] no puc escriure el manifest"));
        return;
    }
    char line[128];
    out.print(F("{\"width\":128,\"height\":128,\"fps\":6,\"transparent\":\"0x"));
    snprintf(line, sizeof(line), "%04X\",\"animations\":{", transparent);
    out.print(line);
    for (uint8_t i = 0; i < nStates; ++i) {
        snprintf(line, sizeof(line), "%s\"%s\":%u", i ? "," : "", names[i], counts[i]);
        out.print(line);
    }
    out.print(F("}}"));
    out.close();

    Serial.printf("[TEST] manifest escrit: %u estats, transparent 0x%04X\n", nStates,
                  transparent);
    for (uint8_t i = 0; i < nStates; ++i) {
        Serial.printf("[TEST]   %-10s %u frames\n", names[i], counts[i]);
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
            } else if (strcmp(line, "manifest") == 0) {
                writeManifest();
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
