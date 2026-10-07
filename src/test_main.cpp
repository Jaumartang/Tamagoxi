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

constexpr int kHomeX = 96;              /* on descansa el drago (centre) */
constexpr int kHomeY = 170;
constexpr int kHopX  = 84;              /* quant es desplacen els salts */
constexpr int kHopY  = 46;              /* quant s'enlairen */

enum class Tab : uint8_t { Bg, Anim };
enum class Act : uint8_t { Idle, Patrol };

Tab      gTab      = Tab::Bg;
Act      gAct      = Act::Idle;
int      gBg       = 0;
int      gAnim     = 0;
int      gFrame    = 0;
bool     gPlaying  = true;
bool     gFlip     = false;             /* mirall (quan va cap a l'esquerra) */
uint32_t gNextMs   = 0;                 /* seguent frame de l'animacio */
uint32_t gActUntil = 0;                 /* quan s'acaba el que esta fent */
uint32_t gBobUntil = 0;                 /* estiradeta curta en quedar-se quiet */
uint32_t gMoveT0   = 0;                 /* inici del cicle de la volta */
bool     gPatrolNext = false;           /* despres de la pausa, volta? */
int      gLastX    = kHomeX;
int      gLastY    = kHomeY;
uint8_t*  gFrameBuf = nullptr;          /* 128x128x2: el frame llegit */
uint16_t* gDrawBuf  = nullptr;          /* zona que canvia (unio vella+nova) */
constexpr int kZoneW = 296;             /* 128 + els dos salts */
constexpr int kZoneH = 174;             /* 128 + el botet */

/* --- El que fa el drago: quiet o fent la volta --------------------------- */

void enterIdle()
{
    gAct = Act::Idle;
    gActUntil = millis() + 3000 + random(0, 6500);   /* quiet de 3 a 9,5 s */
    gPatrolNext = (random(0, 100) < 65);             /* 65%: despres volta */
    gBobUntil = millis() + 1400;                     /* una estiradeta curta */
}

void enterPatrol()
{
    gAct = Act::Patrol;
    gMoveT0 = millis();
    gActUntil = gMoveT0 + 6600;                      /* la volta dura 6,6 s */
}

const SdAssets::Pet& pet()
{
    return SdAssets::pet(0);
}

/* Pinta el frame actual de la mascota. Compose el frame al buffer (els pixels
 * transparents passen a ser el color del fons pla) i l'envia DE COP amb
 * pushImage: es rapid i no parpelleja gens. */
void drawPet(int x, int y)
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
    uint16_t* buf = reinterpret_cast<uint16_t*>(gFrameBuf);
    const int n = p.width * p.height;
    for (int row = 0; row < p.height; ++row) {
        for (int col = 0; col < p.width; ++col) {
            /* Amb mirall llegim la columna al reves ✓ */
            const int sc = gFlip ? (p.width - 1 - col) : col;
            const uint16_t raw = buf[row * p.width + sc];
            uint16_t c = static_cast<uint16_t>((raw >> 8) | (raw << 8));   /* BE -> LE */
            if (p.hasTransparent && c == p.transparent) {
                c = kPanel;                                                /* fons pla */
            }
            buf[row * p.width + col] = c;
        }
    }
    TFT_eSPI& t = Display::driver();
    t.setSwapBytes(true);         /* el buffer es LE i el display vol BE ✓ */
    t.pushImage(x, y, p.width, p.height, buf); /* un sol enviament ✓ */
}

/* La posicio del drago: quiet al centre o fent la volta (amb mirall quan toca).
 * El drago mira sempre cap on va ✓ */
void patrol(int& x, int& y, bool& flip)
{
    x = kHomeX;
    y = kHomeY;
    flip = false;
    if (gAct != Act::Patrol) {
        return;                                         /* quiet al centre */
    }
    const uint32_t p = (millis() - gMoveT0) % 6600;
    int      dx     = 0;
    uint32_t mStart = 0;
    uint32_t mLen   = 0;
    flip = false;

    if (p < 1200) {                                     /* pausa al centre */
        flip = false;                                   /* mira cap a la dreta */
    } else if (p < 2400) {                              /* cap a la dreta */
        dx = (kHopX * static_cast<int>(p - 1200)) / 1200;
        mStart = 1200;
        mLen = 1200;
        flip = false;                                   /* mira a la dreta ✓ */
    } else if (p < 3200) {                              /* pausa a la dreta */
        dx = kHopX;
        flip = true;                                    /* ja mira cap a l'esquerra ✓ */
    } else if (p < 4400) {                              /* tornada al centre */
        dx = kHopX - (kHopX * static_cast<int>(p - 3200)) / 1200;
        mStart = 3200;
        mLen = 1200;
        flip = true;                                    /* MIRALL ✓ */
    } else if (p < 5400) {                              /* cap a l'esquerra */
        dx = -(kHopX * static_cast<int>(p - 4400)) / 1000;
        mStart = 4400;
        mLen = 1000;
        flip = true;                                    /* MIRALL ✓ */
    } else if (p < 6200) {                              /* pausa a l'esquerra */
        dx = -kHopX;
        flip = false;                                   /* ja mira cap a la dreta ✓ */
    } else {                                            /* tornada al centre */
        dx = -kHopX + (kHopX * static_cast<int>(p - 6200)) / 400;
        mStart = 6200;
        mLen = 400;
        flip = false;                                   /* mira a la dreta ✓ */
    }

    x = kHomeX + dx;
    y = kHomeY;
    if (mLen > 0) {                                     /* el botet */
        const float f = static_cast<float>(p - mStart) / static_cast<float>(mLen);
        y = kHomeY - static_cast<int>(kHopY * sinf(f * 3.14159265f));
    }
}

/* Pinta el frame actual. Compon la ZONA QUE CANVIA (unio de la posicio vella
 * i la nova) al buffer i l'envia DE COP: aixi no hi ha cap moment en que la
 * pantalla quedi a mig fer i no parpelleja gens. */
void drawFrameOnly(bool withCounter)
{
    if (SdAssets::petCount() == 0 || pet().animCount == 0 || gDrawBuf == nullptr) {
        return;
    }
    int  x    = kHomeX;
    int  y    = kHomeY;
    bool flip = false;
    if (gPlaying) {
        patrol(x, y, flip);
    }
    gFlip = flip;
    const SdAssets::Pet& p = pet();

    /* Zona = unio de la posicio d'abans i la d'ara ✓ */
    const int x0 = (gLastX < x) ? gLastX : x;
    const int y0 = (gLastY < y) ? gLastY : y;
    const int x1 = ((gLastX > x) ? gLastX : x) + p.width;
    const int y1 = ((gLastY > y) ? gLastY : y) + p.height;
    const int zw = x1 - x0;
    const int zh = y1 - y0;
    if (zw <= 0 || zh <= 0 || zw > kZoneW || zh > kZoneH) {
        return;
    }

    /* Omplim la zona: a ANIMACIONS amb el fons pla, i a FONS amb el fons de
     * veritat (llegit de la SD), perque el drago hi quedi integrat ✓ */
    const int zn = zw * zh;
    bool bgOk = false;
    if (gTab == Tab::Bg) {
        const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
        if (bgs.count > 0 && BgRenderer::beginStrip(bgs.names[gBg])) {
            static uint8_t rowbuf[320 * 2];
            bgOk = true;
            for (int r = 0; r < zh && bgOk; ++r) {
                bgOk = BgRenderer::readStripRow(y0 + r, x0, zw, rowbuf);
                if (!bgOk) {
                    break;
                }
                for (int c = 0; c < zw; ++c) {
                    gDrawBuf[r * zw + c] =
                        static_cast<uint16_t>((rowbuf[c * 2] << 8) | rowbuf[c * 2 + 1]);
                }
            }
            BgRenderer::endStrip();
        }
    }
    if (!bgOk) {
        for (int i = 0; i < zn; ++i) {
            gDrawBuf[i] = kPanel;
        }
    }

    /* Hi posam el drago a sobre, amb mirall si toca ✓ */
    const char* err = nullptr;
    if (SdAssets::readPetFrame(0, p.anims[gAnim].name, static_cast<uint8_t>(gFrame),
                               gFrameBuf, static_cast<size_t>(p.width) * p.height * 2u,
                               &err)) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(gFrameBuf);
        for (int row = 0; row < p.height; ++row) {
            for (int col = 0; col < p.width; ++col) {
                const int sc = flip ? (p.width - 1 - col) : col;
                const uint16_t raw = src[row * p.width + sc];
                uint16_t c = static_cast<uint16_t>((raw >> 8) | (raw << 8));
                if (p.hasTransparent && c == p.transparent) {
                    continue;
                }
                gDrawBuf[(y - y0 + row) * zw + (x - x0 + col)] = c;
            }
        }
    }

    TFT_eSPI& t = Display::driver();
    t.setSwapBytes(true);
    t.pushImage(x0, y0, zw, zh, gDrawBuf);     /* UN sol enviament ✓✓ */
    gLastX = x;
    gLastY = y;

    if (withCounter && gTab == Tab::Anim) {    /* el comptador de la barra */
        char info[32];
        snprintf(info, sizeof(info), "%s  %d/%d", pet().anims[gAnim].name, gFrame + 1,
                 pet().anims[gAnim].frames);
        const int by = 480 - kBarBot + 8;
        t.fillRect(70, by, 180, 22, kPanel);
        t.setTextDatum(MC_DATUM);
        t.setTextFont(2);
        t.setTextColor(kText, kPanel);
        t.drawString(info, 160, by + 10);
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
        /* El fons de veritat ✓ */
        const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
        if (bgs.count > 0) {
            BgRenderer::drawFull(bgs.names[gBg]);
        } else {
            t.fillScreen(kPanel);
        }
    } else {
        /* Fons pla per veure-hi be el drago ✓ */
        t.fillScreen(kPanel);
    }
    /* El drago, sempre a sobre: a FONS quedara integrat a l'escena ✓ */
    gMoveT0 = millis();
    gLastX = kHomeX;
    gLastY = kHomeY;
    gFrame = 0;
    drawFrameOnly(false);
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
    /* Al mig de la pantalla */
    int dx = 0;
    int dy = 0;
    bool df = false;
    patrol(dx, dy, df);
    const SdAssets::Pet& p = pet();
    if (x >= dx && x < dx + p.width && y >= dy && y < dy + p.height) {
        enterPatrol();                 /* li has tocat: fa la volta ✓ */
        return;
    }
    if (gTab == Tab::Anim && p.animCount > 0) {
        gFrame = (gFrame + 1) % p.anims[gAnim].frames;
        drawFrameOnly(true);
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
        gDrawBuf = static_cast<uint16_t*>(malloc(static_cast<size_t>(kZoneW) * kZoneH * 2u));
        BgRenderer::begin();          /* reserva el buffer per llegir els fons ✓ */
        randomSeed(esp_random());     /* que les voltes siguin ben aleatories ✓ */
        enterIdle();
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

    /* El que fa el drago: quiet o fent la volta ✓ */
    const uint32_t now = millis();
    if (now >= gActUntil) {
        if (gAct == Act::Patrol || !gPatrolNext) {
            enterIdle();
        } else {
            enterPatrol();
        }
    }

    if (gPlaying && SdAssets::petCount() > 0 && pet().animCount > 0) {
        /* Els frames nomes van quan es mou o durant l'estiradeta ✓ */
        const bool anim = (gAct == Act::Patrol) || (now < gBobUntil);
        bool need = false;
        bool counter = false;
        if (anim && now >= gNextMs) {
            const uint32_t fps = (pet().fps > 0) ? pet().fps : 6;
            gNextMs = now + 1000u / fps;
            gFrame = (gFrame + 1) % pet().anims[gAnim].frames;
            need = true;
            counter = true;
        }
        int  px = 0;
        int  py = 0;
        bool pf = false;
        patrol(px, py, pf);                       /* es mou? ✓ */
        if (px != gLastX || py != gLastY) {
            need = true;
        }
        if (need) {
            drawFrameOnly(counter);
        }
    }
    delay(5);
}
