#include <Arduino.h>
#include <time.h>

#include "bg_renderer.h"
#include "config.h"
#include "display.h"
#include "led.h"
#include "net.h"
#include "pet.h"
#include "pins.h"
#include "sd_assets.h"
#include "sprite_renderer.h"
#include "storage.h"
#include "touch.h"
#include "ui.h"

/*
 * main.cpp - Tamagoxi v2 (Tamagotchi gegant per a la Noa)
 *
 * FASE 0+ - Bring-up de maquinari:
 *   - Pantalla ST7796S 320x480 (vertical).
 *   - Retroil-luminacio: diagnostic de pin digital vs PWM (comandes 'bltest',
 *     'bl <n>'). Polaritat a config.h (BL_ACTIVE_LOW).
 *   - Tactil XPT2046: pantalla de test amb reticle + coordenades + boto CALIBRAR.
 *   - Consola serie per diagnostica.
 *
 * REQUISIT DE DISSENY (Fase 2): mentre es carrega un fons de la SD, la
 * retroil-luminacio es posa a 0 (pantalla fosca) fins que el fons sencer esta
 * pintat. Vegeu runLoadDemo() i la comanda 'loaddemo'.
 *
 * El bucle es no bloquejant (tret de la calibracio, que es un proces explicit).
 */

namespace {

/* --- Disposicio de la pantalla del test tactil ---------------------------- */
constexpr int kHeaderH   = 40;
constexpr int kInfoTop   = kHeaderH;
constexpr int kInfoH     = 40;
constexpr int kCanvasTop = kHeaderH + kInfoH;      /* 80  */
constexpr int kButtonH   = 60;
constexpr int kButtonTop = SCREEN_H - kButtonH;    /* 420 */

constexpr uint16_t kCanvasBg = 0x0841;   /* blau molt fosc */
constexpr uint16_t kCrossCol = 0x07E0;   /* verd */
constexpr int      kCrossR   = 14;

int16_t  gCrossX = 0;
int16_t  gCrossY = 0;
bool     gHasCross   = false;
bool     gBtnLatched = false;
uint32_t gHeapTimer  = 0;
uint32_t gSdRetry    = 0;

/* Pantalla activa: galeria de fons / test tactil / estatica (dibuixada per una
 * comanda). El bucle nomes fa el que toca a cada mode. */
enum class Screen : uint8_t { Home, Gallery, TouchTest, PetTest, Static };
Screen   gScreen = Screen::Static;
uint8_t  gBgIndex = 0;
uint32_t gBgTimer = 0;
bool     gTouchWasPressed = false;
uint32_t gPetAnimTimer = 0;
uint32_t gPetOverlayTimer = 0;

/* --- Estat de la UI / joc (Fase 4-5) -------------------------------------- */
char     gHomeBg[24] = {0};
bool     gAutoBg = true;         /* el fons segueix la meteo (Fase 6) */
bool     gTimeApplied = false;   /* ja s'ha passat l'hora del NTP a la mascota */
uint32_t gHeartUntil = 0;
bool     gWasSleeping = false;
int16_t  gHeartX = 0;
int16_t  gHeartY = 0;
uint32_t gPetCooldown = 0;
uint32_t gPressStart = 0;
bool     gLongPressDone = false;
bool     gMenuOpen = false;
uint32_t gLastTapMs = 0;
volatile bool    gTouchLatch = false;
volatile int16_t gTouchLatchX = 0;
volatile int16_t gTouchLatchY = 0;

char     gLine[CONSOLE_LINE_MAX];
uint16_t gLineLen = 0;

/* --- Utilitats de pantalla ------------------------------------------------ */

void drawButton(bool pressed)
{
    TFT_eSPI& t = Display::driver();
    const uint16_t bg = pressed ? TFT_GREEN : TFT_DARKGREEN;
    t.fillRect(0, kButtonTop, SCREEN_W, kButtonH, bg);
    t.setTextDatum(MC_DATUM);
    t.setTextColor(TFT_WHITE, bg);
    t.setTextFont(4);
    t.drawString("CALIBRAR", SCREEN_W / 2, kButtonTop + kButtonH / 2);
    t.setTextFont(2);
}

void updateInfo()
{
    TFT_eSPI& t = Display::driver();
    t.fillRect(0, kInfoTop, SCREEN_W, kInfoH, TFT_BLACK);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    t.setTextFont(2);

    char buf[48];
    if (!Touch::isCalibrated()) {
        t.drawString("Tactil SENSE calibrar", 6, kInfoTop + 2);
    } else {
        int16_t x = -1;
        int16_t y = -1;
        Touch::getCoords(x, y);
        snprintf(buf, sizeof(buf), "PX  X=%4d  Y=%4d", x, y);
        t.drawString(buf, 6, kInfoTop + 2);
    }

    int16_t rx = 0;
    int16_t ry = 0;
    if (Touch::getRaw(rx, ry)) {
        snprintf(buf, sizeof(buf), "RAW X=%4d  Y=%4d", rx, ry);
    } else {
        snprintf(buf, sizeof(buf), "RAW --");
    }
    t.drawString(buf, 6, kInfoTop + 20);
}

void eraseCrosshair()
{
    if (!gHasCross) {
        return;
    }
    Display::driver().fillRect(gCrossX - kCrossR - 2, gCrossY - kCrossR - 2,
                               (2 * kCrossR) + 4, (2 * kCrossR) + 4, kCanvasBg);
    gHasCross = false;
}

void drawCrosshair(int16_t x, int16_t y)
{
    /* Clamp dins de la zona del canvas. */
    if (x < kCrossR) {
        x = kCrossR;
    }
    if (x > SCREEN_W - 1 - kCrossR) {
        x = SCREEN_W - 1 - kCrossR;
    }
    if (y < kCanvasTop + kCrossR) {
        y = kCanvasTop + kCrossR;
    }
    if (y > kButtonTop - 1 - kCrossR) {
        y = kButtonTop - 1 - kCrossR;
    }

    if (gHasCross && x == gCrossX && y == gCrossY) {
        return;  /* ni s'ha mogut: no repintem */
    }

    eraseCrosshair();
    TFT_eSPI& t = Display::driver();
    t.drawLine(x - kCrossR, y, x + kCrossR, y, kCrossCol);
    t.drawLine(x, y - kCrossR, x, y + kCrossR, kCrossCol);
    t.drawCircle(x, y, kCrossR - 4, kCrossCol);
    gCrossX = x;
    gCrossY = y;
    gHasCross = true;
}

void drawTouchScreen()
{
    TFT_eSPI& t = Display::driver();
    t.fillScreen(TFT_BLACK);

    t.setTextFont(2);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    t.drawString("Tamagoxi v2  -  TEST TACTIL", 6, 4);
    t.setTextDatum(TR_DATUM);
    t.drawString(Touch::isCalibrated() ? "CAL OK" : "CAL NO", SCREEN_W - 6, 4);

    t.fillRect(0, kCanvasTop, SCREEN_W, kButtonTop - kCanvasTop, kCanvasBg);
    t.drawRect(0, kCanvasTop, SCREEN_W, kButtonTop - kCanvasTop, TFT_WHITE);

    drawButton(false);
    gHasCross = false;
    updateInfo();
}

/* Pantalla de franges de colors (diagnostic d'ordre de bytes / inversio). */
void drawColourBands()
{
    TFT_eSPI& t = Display::driver();
    struct Band {
        uint16_t color;
        uint16_t textColor;
        const char* name;
    };
    const Band bands[] = {
        {TFT_RED, TFT_WHITE, "VERMELL"}, {TFT_GREEN, TFT_BLACK, "VERD"},
        {TFT_BLUE, TFT_WHITE, "BLAU"}, {TFT_YELLOW, TFT_BLACK, "GROC"},
        {TFT_CYAN, TFT_BLACK, "CIAN"}, {TFT_MAGENTA, TFT_WHITE, "MAGENTA"},
        {TFT_WHITE, TFT_BLACK, "BLANC"}, {0x8410, TFT_WHITE, "GRIS"},
    };
    const int bandCount = static_cast<int>(sizeof(bands) / sizeof(bands[0]));
    const int headerH = 40;
    const int bandH = (SCREEN_H - headerH) / bandCount;

    t.fillScreen(TFT_BLACK);
    t.setTextFont(2);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    t.drawString("TEST DE COLORS", 6, 6);

    for (int i = 0; i < bandCount; ++i) {
        const int y = headerH + i * bandH;
        t.fillRect(0, y, SCREEN_W, bandH, bands[i].color);
        t.setTextDatum(ML_DATUM);
        t.setTextColor(bands[i].textColor, bands[i].color);
        t.drawString(bands[i].name, 10, y + bandH / 2);
    }
    t.drawRect(0, 0, SCREEN_W, SCREEN_H, TFT_WHITE);
}

/* --- LED d'estat: groc = necessita atencio, verd = tot be ----------------- */
void updateLed()
{
    const bool ok = Touch::isCalibrated();
    Led::setRgb(0, ok ? 40 : 0, ok ? 0 : 55);   /* verd suau / groc suau */
}

/* --- Diagnostica de retroil-luminacio ------------------------------------- */
void runBacklightDiagnostic()
{
    Serial.println(F("[BL] --- diagnostic de retroil-luminacio ---"));
    Serial.println(F("[BL] (a) pin DIGITAL: la pantalla hauria de fer FOSC / ENCES"));

    for (uint8_t i = 0; i < 2; ++i) {
        Display::setBacklightDigital(false);
        Serial.println(F("[BL] digital OFF -> pantalla FOSCA?"));
        delay(BL_DIAG_DIGITAL_MS);
        Display::setBacklightDigital(true);
        Serial.println(F("[BL] digital ON  -> pantalla ENCESA?"));
        delay(BL_DIAG_DIGITAL_MS);
    }

    Serial.println(F("[BL] (b) PWM: 0 / 25 / 50 / 100 / 0 %"));
    const uint8_t pcts[] = {0, 25, 50, 100, 0, 100};
    for (uint8_t i = 0; i < sizeof(pcts); ++i) {
        Display::setBacklight(pcts[i]);
        Serial.printf("[BL] PWM %u %%\n", pcts[i]);
        delay(BL_DIAG_PWM_MS);
    }

    Display::setBacklight(100);
    Serial.println(F("[BL] --- fi del diagnostic (queda a 100 %) ---"));
}

/* --- Demo del patro 'backlight a 0 mentre es carrega el fons' ------------- */
void runLoadDemo()
{
    Serial.println(F("[LOAD] backlight a 0 mentre es 'carrega' el fons..."));
    const uint32_t t0 = millis();

    Display::setBacklight(0);  /* pantalla fosca durant tota la carrega */

    TFT_eSPI& t = Display::driver();
    constexpr int bandLines = 16;
    for (int y = 0; y < SCREEN_H; y += bandLines) {
        t.fillRect(0, y, SCREEN_W, bandLines, ((y / bandLines) & 1) ? 0x0010 : 0x0210);
        delay(3);  /* simula el temps de lectura de la SD */
    }

    Display::setBacklight(100);  /* tornem a encendre quan tot el fons esta pintat */
    Serial.printf("[LOAD] fons pintat en %lu ms; backlight restaurat\n",
                  static_cast<unsigned long>(millis() - t0));
    delay(400);
    drawTouchScreen();
}

/* --- Galeria de fons (Fase 2) --------------------------------------------- */

void drawBgOverlay(const char* name, uint8_t idx, uint8_t count, uint32_t ms)
{
    TFT_eSPI& t = Display::driver();
    t.fillRect(0, 0, SCREEN_W, 22, TFT_BLACK);
    t.setTextFont(2);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_YELLOW, TFT_BLACK);
    char buf[48];
    snprintf(buf, sizeof(buf), "%u/%u  %s  %lu ms", static_cast<unsigned>(idx + 1),
             static_cast<unsigned>(count), name, static_cast<unsigned long>(ms));
    t.drawString(buf, 4, 3);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
}

/* Mostra el fons 'idx'. Apaga la retroil-luminacio mentre es pinta (patro de
 * carrega) i la torna a encendre quan el fons sencer ja es a la pantalla. */
void showBackgroundIndex(uint8_t idx)
{
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    if (bgs.count == 0) {
        return;
    }
    gBgIndex = static_cast<uint8_t>(idx % bgs.count);
    const char* name = bgs.names[gBgIndex];

    if (BG_BACKLIGHT_OFF_ON_LOAD) {
        Display::setBacklight(0);
    }
    const uint32_t ms = BgRenderer::drawFull(name);
    if (BG_BACKLIGHT_OFF_ON_LOAD) {
        Display::setBacklight(100);
    }

    drawBgOverlay(name, gBgIndex, bgs.count, ms);
    Serial.printf("[BG] %u/%u  %s  %lu ms  (%u B)  %s\n",
                  static_cast<unsigned>(gBgIndex + 1), static_cast<unsigned>(bgs.count),
                  name, static_cast<unsigned long>(ms),
                  static_cast<unsigned>(BgRenderer::status().lastBytes),
                  BgRenderer::status().lastOk ? "ok" : "ERROR");
}

/* --- Prova de la mascota (Fase 3) ----------------------------------------- */

void drawPetOverlay()
{
    const SpriteRenderer::Status& s = SpriteRenderer::status();
    TFT_eSPI& t = Display::driver();
    t.fillRect(0, 0, SCREEN_W, 22, TFT_BLACK);
    t.setTextFont(2);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_YELLOW, TFT_BLACK);
    const uint32_t avg = s.totalFrames ? (s.totalMs / s.totalFrames) : 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s x%u f%u/%u %lums fps~%lu (%d,%d)",
             s.anim, static_cast<unsigned>(SpriteRenderer::scale()),
             static_cast<unsigned>(s.frameIndex + 1), static_cast<unsigned>(s.frameCount),
             static_cast<unsigned long>(s.lastFrameMs),
             avg ? static_cast<unsigned long>(1000 / avg) : 0,
             s.x, s.y);
    t.drawString(buf, 4, 3);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
}

void startPetTest(const char* bgName)
{
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    if (bgs.count == 0) {
        Serial.println(F("[PET] no hi ha fons (falta la SD?)"));
        return;
    }
    if (!SpriteRenderer::begin()) {
        return;
    }

    Display::setBacklight(0);
    BgRenderer::drawFull(bgName);
    Display::setBacklight(100);

    SpriteRenderer::setBackground(bgName);
    SpriteRenderer::setAnimation("IDLE");
    const uint32_t ms = SpriteRenderer::drawFrame();

    gScreen = Screen::PetTest;
    gPetAnimTimer = millis();
    gPetOverlayTimer = millis();
    drawPetOverlay();
    Serial.printf("[PET] prova: fons=%s anim=%s primer frame %lu ms\n",
                  bgName, SpriteRenderer::animationName(),
                  static_cast<unsigned long>(ms));
}

/* Redibuixa el fons sencer + la mascota (quan canvia la mida o la posicio). */
void redrawPetTest()
{
    if (!SpriteRenderer::isActive()) {
        return;
    }
    Display::setBacklight(0);
    BgRenderer::drawFull(SpriteRenderer::backgroundName());
    Display::setBacklight(100);
    SpriteRenderer::drawFrame();
    drawPetOverlay();
}

/* --- Pantalla principal amb UI (Fase 4) ----------------------------------- */

/* Manté la mascota entre el HUD i les barres. */
void applyHomeLayout()
{
    if (!SpriteRenderer::isActive()) {
        return;
    }
    const int16_t boxH = static_cast<int16_t>(SpriteRenderer::status().boxH);
    int16_t y = SpriteRenderer::status().y;
    const int16_t maxY = static_cast<int16_t>(UI_BARS_TOP - boxH);
    if (y > maxY) {
        y = maxY;
    }
    if (y < 0) {
        y = 0;
    }
    SpriteRenderer::setPosition(SpriteRenderer::status().x, y);
}

void homeHud()
{
    Ui::Hud hud;
    hud.timeValid = false;
    hud.hour      = 0;
    hud.minute    = 0;

    if (Net::timeSynced()) {
        struct tm now {};
        if (getLocalTime(&now, 0)) {
            hud.timeValid = true;
            hud.hour      = static_cast<uint8_t>(now.tm_hour);
            hud.minute    = static_cast<uint8_t>(now.tm_min);
        }
    }

    const Net::Weather& w = Net::weather();
    hud.wifi         = Net::connected();
    hud.weatherValid = w.valid;
    hud.temperature  = w.temperature;
    Ui::drawHud(hud);
}

void startHome()
{
    if (gHomeBg[0] == '\0') {
        strlcpy(gHomeBg, PET_TEST_BG, sizeof(gHomeBg));
    }
    if (!SpriteRenderer::begin()) {
        return;
    }

    applyHomeLayout();
    SpriteRenderer::setBackground(gHomeBg);

    Display::setBacklight(0);
    BgRenderer::drawFull(gHomeBg);
    /* Respecta l'atenuacio de "dormint". */
    Display::setBacklight(Pet::sleeping() ? 35 : 100);

    SpriteRenderer::setAnimation(Pet::animation());
    SpriteRenderer::drawFrame();
    gWasSleeping = Pet::sleeping();

    gMenuOpen = false;
    /* No registrem el hook de franges: llegir el tactil dins la transaccio SPI
     * del display pot corrompre les dues coses. El tactil es mostreja al bucle. */
    SpriteRenderer::setBandHook(nullptr);

    Ui::invalidate();
    Ui::begin();
    Ui::drawMenuButton();
    homeHud();
    const Pet::Needs& n0 = Pet::needs();
    Ui::drawBars(n0.food, n0.happiness, n0.energy, n0.health);

    gScreen = Screen::Home;
    Serial.printf("[UI] pantalla principal: fons=%s mascota %ux%u a (%d,%d)\n",
                  gHomeBg,
                  static_cast<unsigned>(SpriteRenderer::status().boxW),
                  static_cast<unsigned>(SpriteRenderer::status().boxH),
                  SpriteRenderer::status().x, SpriteRenderer::status().y);
}

void doFeed()
{
    Pet::feed();
}

void doPlay()
{
    Pet::play();
}

void doSleepToggle()
{
    Pet::toggleSleep();
    Display::setBacklight(Pet::sleeping() ? 35 : 100);
}

void doHeal()
{
    Pet::heal();
}

void doPet()
{
    if (millis() - gPetCooldown < UI_PET_COOLDOWN_MS) {
        return;
    }
    gPetCooldown = millis();
    Pet::pet();

    int16_t px = 0;
    int16_t py = 0;
    int16_t pw = 0;
    int16_t ph = 0;
    Ui::petRect(px, py, pw, ph);
    gHeartX = static_cast<int16_t>(px + pw - pw / 4 + (static_cast<int>(millis() % 21) - 10));
    gHeartY = static_cast<int16_t>(py + ph / 4);
    gHeartUntil = millis() + UI_HEART_MS;
    Serial.println(F("[UI] Caricia!"));
}

/* Obre/plega el menu desplegable i processa un toc a la pantalla principal. */
void openMenu()
{
    if (gMenuOpen) {
        return;
    }
    gMenuOpen = true;
    Ui::drawMenu(Pet::sleeping());
    Serial.println(F("[UI] menu obert"));
}

void closeMenu()
{
    if (!gMenuOpen) {
        return;
    }
    gMenuOpen = false;
    Ui::invalidate();
    homeHud();
    const Pet::Needs& n = Pet::needs();
    Ui::drawBars(n.food, n.happiness, n.energy, n.health);
    Ui::drawMenuButton();
    Serial.println(F("[UI] menu tancat"));
}

/* Repinta la UI de la pantalla principal (sense tocar el fons ni la mascota). */
void redrawHomeUi()
{
    Ui::invalidate();
    homeHud();
    const Pet::Needs& n = Pet::needs();
    Ui::drawBars(n.food, n.happiness, n.energy, n.health);
    if (gMenuOpen) {
        Ui::drawMenu(Pet::sleeping());
    } else {
        Ui::drawMenuButton();
    }
}

/* Comprova que el fons existeix al cataleg de la SD. */
bool backgroundExists(const char* name)
{
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    for (uint8_t i = 0; i < bgs.count; ++i) {
        if (strcmp(bgs.names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

void handleHomeTap(int16_t tx, int16_t ty)
{
    const Ui::Zone z = Ui::hitTest(tx, ty, gMenuOpen);
    if (gMenuOpen) {
        switch (z) {
            case Ui::Zone::MenuFeed:  doFeed();  closeMenu(); break;
            case Ui::Zone::MenuPlay:  doPlay();  closeMenu(); break;
            case Ui::Zone::MenuSleep: doSleepToggle(); closeMenu(); break;
            case Ui::Zone::MenuHeal:  doHeal();  closeMenu(); break;
            case Ui::Zone::MenuClose: closeMenu(); break;
            default: break;
        }
        return;
    }
    switch (z) {
        case Ui::Zone::MenuButton: openMenu(); break;
        case Ui::Zone::Pet:        doPet();    break;
        case Ui::Zone::HudClock:
            gPressStart = millis();
            gLongPressDone = false;
            break;
        default: break;
    }
}

/* --- Pantalles de la targeta SD ------------------------------------------- */

void drawSdError()
{
    TFT_eSPI& t = Display::driver();
    t.fillScreen(TFT_RED);
    t.setTextDatum(MC_DATUM);
    t.setTextColor(TFT_WHITE, TFT_RED);
    t.setTextFont(4);
    t.drawString("Posa la", SCREEN_W / 2, SCREEN_H / 2 - 70);
    t.drawString("targeta SD", SCREEN_W / 2, SCREEN_H / 2 - 30);
    t.setTextFont(2);
    t.drawString("Es reintenta cada 3 s...", SCREEN_W / 2, SCREEN_H / 2 + 30);
    const char* err = SdAssets::report().firstError;
    if (err[0] != '\0') {
        t.drawString(err, SCREEN_W / 2, SCREEN_H / 2 + 60);
    }
}

void drawSdScreen()
{
    const SdAssets::Report& r = SdAssets::report();
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    TFT_eSPI& t = Display::driver();

    t.fillScreen(TFT_BLACK);
    t.setTextFont(2);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    t.drawString("TARGETA SD", 6, 6);
    t.setTextDatum(TR_DATUM);
    t.setTextColor(r.mounted ? TFT_GREEN : TFT_RED, TFT_BLACK);
    t.drawString(r.mounted ? "MUNTADA" : "NO TROBADA", SCREEN_W - 6, 6);

    if (!r.mounted) {
        drawSdError();
        return;
    }

    char buf[48];
    int y = 38;
    t.setTextDatum(TL_DATUM);

    t.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(buf, sizeof(buf), "SD %llu MB  (usat %llu MB)",
             static_cast<unsigned long long>(r.cardSizeMB),
             static_cast<unsigned long long>(r.usedMB));
    t.drawString(buf, 6, y);
    y += 24;

    if (r.bgsLoaded) {
        t.setTextColor(TFT_WHITE, TFT_BLACK);
        snprintf(buf, sizeof(buf), "Fons: %u  %ux%u", static_cast<unsigned>(r.bgCount),
                 static_cast<unsigned>(bgs.width), static_cast<unsigned>(bgs.height));
        t.drawString(buf, 6, y);
        y += 20;
        snprintf(buf, sizeof(buf), "  mides ok:%u  bad:%u",
                 static_cast<unsigned>(r.bgFilesOk), static_cast<unsigned>(r.bgFilesBad));
        t.setTextColor(r.bgFilesBad ? TFT_RED : TFT_DARKGREEN, TFT_BLACK);
        t.drawString(buf, 6, y);
        y += 20;
    } else {
        t.setTextColor(TFT_RED, TFT_BLACK);
        t.drawString("Fons: ERROR", 6, y);
        y += 20;
    }

    if (r.petsLoaded) {
        t.setTextColor(TFT_WHITE, TFT_BLACK);
        snprintf(buf, sizeof(buf), "Mascota: %s  %ux%u  fps %u",
                 SdAssets::pet(0).folder,
                 static_cast<unsigned>(r.petWidth), static_cast<unsigned>(r.petHeight),
                 static_cast<unsigned>(r.petFps));
        t.drawString(buf, 6, y);
        y += 20;
        snprintf(buf, sizeof(buf), "Anim: %u  (%u frames)", static_cast<unsigned>(r.animCount),
                 static_cast<unsigned>(r.framesFirstAnim));
        t.drawString(buf, 6, y);
        y += 20;
        snprintf(buf, sizeof(buf), "  frames ok:%u  bad:%u",
                 static_cast<unsigned>(r.petFilesOk), static_cast<unsigned>(r.petFilesBad));
        t.setTextColor(r.petFilesBad ? TFT_RED : TFT_DARKGREEN, TFT_BLACK);
        t.drawString(buf, 6, y);
        y += 20;
    } else {
        t.setTextColor(TFT_RED, TFT_BLACK);
        t.drawString("Mascota: ERROR", 6, y);
        y += 20;
    }

    y += 8;
    if (r.firstError[0] != '\0') {
        t.setTextColor(TFT_RED, TFT_BLACK);
        t.drawString(r.firstError, 6, y);
    } else {
        t.setTextColor(TFT_GREEN, TFT_BLACK);
        t.drawString("Tot correcte!", 6, y);
    }
    y += 24;
    t.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    t.drawString("Arbre de fitxers pel port serie.", 6, y);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
}

/* --- Captura de pantalla pel port serie (diagnostic) ---------------------
 * Protocol:  SHOT BEGIN x y w h  /  SHOT <fila> <hex>  /  SHOT END w h
 * Es llegeix el panell ST7796S amb readRect (MISO al GPIO12) i s'envia en hex.
 * Reconstruccio al PC: tools/shot_decode.py */
constexpr uint16_t kShotMaxW = 320;
constexpr uint16_t kShotBandRows = 6;    /* 320*6*2 = 3840 B per lectura */
constexpr uint16_t kShotHexBytes = 96;   /* bytes per linia de text */
uint16_t gShotBuf[kShotMaxW * kShotBandRows];

void cmdShot(char* arg)
{
    int x = 0;
    int y = 0;
    int w = SCREEN_W;
    int h = SCREEN_H;
    if (arg != nullptr && sscanf(arg, "%d %d %d %d", &x, &y, &w, &h) != 4) {
        Serial.println(F("[CON] us: shot [x y w h]"));
        return;
    }
    if (x < 0) { x = 0; }
    if (y < 0) { y = 0; }
    if (x >= SCREEN_W || y >= SCREEN_H) {
        Serial.println(F("[CON] fora de pantalla"));
        return;
    }
    if (x + w > SCREEN_W) { w = SCREEN_W - x; }
    if (y + h > SCREEN_H) { h = SCREEN_H - y; }
    if (w > kShotMaxW) { w = kShotMaxW; }

    static const char kHex[] = "0123456789abcdef";
    char line[kShotHexBytes * 2 + 1];
    TFT_eSPI& t = Display::driver();

    Serial.printf("SHOT BEGIN %d %d %d %d\n", x, y, w, h);
    for (int row = 0; row < h; row += kShotBandRows) {
        int rows = kShotBandRows;
        if (row + rows > h) { rows = h - row; }
        t.readRect(x, y + row, w, rows, gShotBuf);

        const uint8_t* raw = reinterpret_cast<const uint8_t*>(gShotBuf);
        const size_t total = static_cast<size_t>(w) * rows * 2u;
        for (size_t off = 0; off < total; off += kShotHexBytes) {
            const size_t chunk = ((total - off) < kShotHexBytes) ? (total - off) : kShotHexBytes;
            for (size_t i = 0; i < chunk; ++i) {
                const uint8_t b = raw[off + i];
                line[i * 2] = kHex[b >> 4];
                line[i * 2 + 1] = kHex[b & 0x0F];
            }
            line[chunk * 2] = '\0';
            Serial.print("SHOT ");
            Serial.print(row);
            Serial.print(' ');
            Serial.println(line);
        }
    }
    Serial.printf("SHOT END %d %d\n", w, h);
}

void cmdBaud(char* arg)
{
    if (arg == nullptr) {
        Serial.printf("[CON] baud %lu\n", static_cast<unsigned long>(SERIAL_BAUD));
        return;
    }
    const uint32_t b = static_cast<uint32_t>(strtoul(arg, nullptr, 10));
    if (b < 9600 || b > 921600) {
        Serial.println(F("[CON] baud invalid"));
        return;
    }
    Serial.println(F("[CON] BAUD OK"));
    Serial.flush();
    delay(50);
    Serial.begin(b);
    Serial.printf("[CON] baud %lu\n", static_cast<unsigned long>(b));
}

/* Monitor del tactil: mostra la pressio crua (z) i les coordenades mentre dura,
 * per diagnosticar si el panell detecta el dit i com es mapeja. */
void cmdTouchMon(char* arg)
{
    int secs = (arg != nullptr) ? atoi(arg) : 15;
    if (secs <= 0 || secs > 120) {
        secs = 15;
    }

    TFT_eSPI& t = Display::driver();
    const uint16_t thr = Touch::pressureThreshold();
    Serial.printf("[TOUCH] monitor %d s (llindar z=%u). TOCA LA PANTALLA ARA!\n",
                  secs, static_cast<unsigned>(thr));

    const uint32_t end = millis() + static_cast<uint32_t>(secs) * 1000u;
    uint32_t lastBeat = 0;
    uint32_t lastPrint = 0;
    uint16_t maxZ = 0;
    uint32_t detected = 0;

    while (static_cast<int32_t>(millis() - end) < 0) {
        Touch::update();   /* refresca l'estat intern (gX/gY) */
        const uint16_t z = t.getTouchRawZ();
        uint16_t rx = 0;
        uint16_t ry = 0;
        t.getTouchRaw(&rx, &ry);
        if (z > maxZ) {
            maxZ = z;
        }
        if (z >= thr) {
            ++detected;
            if (millis() - lastPrint >= 120) {
                lastPrint = millis();
                int16_t mx = -1;
                int16_t my = -1;
                Touch::getCoords(mx, my);
                Serial.printf("[TOUCH] TOC z=%4u raw=(%4u,%4u) map=(%d,%d)\n",
                              static_cast<unsigned>(z), static_cast<unsigned>(rx),
                              static_cast<unsigned>(ry), mx, my);
            }
        } else if (millis() - lastBeat >= 2000) {
            lastBeat = millis();
            Serial.printf("[TOUCH] repos z=%4u raw=(%4u,%4u)\n",
                          static_cast<unsigned>(z), static_cast<unsigned>(rx),
                          static_cast<unsigned>(ry));
        }
        delay(5);
    }

    Serial.printf("[TOUCH] fi: z max=%u, mostres>=llindar=%lu\n",
                  static_cast<unsigned>(maxZ), static_cast<unsigned long>(detected));
    if (detected == 0) {
        const uint16_t suggest = (maxZ > 20) ? static_cast<uint16_t>(maxZ * 2 / 3) : 60;
        Serial.printf("[TOUCH] CAP TOC. El panell pot respondre amb menys pressio: "
                      "prova 'tth %u'\n", static_cast<unsigned>(suggest));
    } else {
        Serial.println(F("[TOUCH] OK: el panell detecta el dit."));
    }
}

/* --- Consola serie -------------------------------------------------------- */
void printHelp()
{
    Serial.println(F("[CON] comandes: help | info | bl <0-100> | bltest | loaddemo "
                     "| cal | touch | colortest | tth <n> | sd | lssd | bg <name|next|N> "
                     "| pet [bg] | anim <NAME|next> | petscale <1-3> | petpos <x> <y>|center "
                     "| petreset | home | menu | act <feed|play|sleep|heal|pet> "
                     "| shot [x y w h] | baud <n> | tmon [s] | needs | sets <f> <h> <e> <s> "
                     "| theme [n] | homebg [nom|auto] | wifi [ssid pass] | net | meteo "
                     "| geo <lat> <lon>"));
}

void printInfo()
{
    Serial.printf("[SYS] up=%lus heap=%u maxAlloc=%u minFree=%u\n",
                  static_cast<unsigned long>(millis() / 1000),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()));
    Serial.printf("[BL]  backlight=%u%% (actiu %s)\n",
                  static_cast<unsigned>(Display::backlight()),
                  BL_ACTIVE_LOW ? "baix" : "alt");
    Serial.printf("[TOUCH] calibrated=%d threshold=%u\n",
                  Touch::isCalibrated() ? 1 : 0,
                  static_cast<unsigned>(Touch::pressureThreshold()));
}

void handleCommand(char* cmd)
{
    char* arg = strchr(cmd, ' ');
    if (arg != nullptr) {
        *arg = '\0';
        ++arg;
        while (*arg == ' ') {
            ++arg;
        }
    }

    if (*cmd == '\0') {
        return;
    }
    if (strcmp(cmd, "help") == 0) {
        printHelp();
    } else if (strcmp(cmd, "info") == 0) {
        printInfo();
    } else if (strcmp(cmd, "bl") == 0) {
        if (arg == nullptr) {
            Serial.println(F("[BL] us: bl <0-100>"));
        } else {
            int v = atoi(arg);
            if (v < 0) {
                v = 0;
            }
            if (v > 100) {
                v = 100;
            }
            Display::setBacklight(static_cast<uint8_t>(v));
            Serial.printf("[BL] %d %%\n", v);
        }
    } else if (strcmp(cmd, "bltest") == 0) {
        runBacklightDiagnostic();
    } else if (strcmp(cmd, "loaddemo") == 0) {
        runLoadDemo();
    } else if (strcmp(cmd, "cal") == 0) {
        gScreen = Screen::TouchTest;
        Touch::calibrate();
        drawTouchScreen();
        updateLed();
    } else if (strcmp(cmd, "touch") == 0) {
        gScreen = Screen::TouchTest;
        drawTouchScreen();
    } else if (strcmp(cmd, "colortest") == 0) {
        gScreen = Screen::Static;
        drawColourBands();
    } else if (strcmp(cmd, "menu") == 0) {
        if (gScreen != Screen::Home) {
            startHome();
        }
        openMenu();
    } else if (strcmp(cmd, "act") == 0) {
        if (arg == nullptr) {
            Serial.println(F("[UI] us: act <feed|play|sleep|heal|pet>"));
        } else if (strcmp(arg, "feed") == 0) {
            doFeed();
        } else if (strcmp(arg, "play") == 0) {
            doPlay();
        } else if (strcmp(arg, "sleep") == 0) {
            doSleepToggle();
        } else if (strcmp(arg, "heal") == 0) {
            doHeal();
        } else if (strcmp(arg, "pet") == 0) {
            doPet();
        } else {
            Serial.println(F("[UI] accio desconeguda"));
        }
    } else if (strcmp(cmd, "home") == 0) {
        startHome();
    } else if (strcmp(cmd, "bg") == 0) {
        gScreen = Screen::Gallery;
        const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
        if (bgs.count == 0) {
            Serial.println(F("[BG] cap fons (falta la SD?)"));
        } else if (arg == nullptr || strcmp(arg, "next") == 0) {
            showBackgroundIndex(static_cast<uint8_t>((gBgIndex + 1) % bgs.count));
        } else if (arg[0] >= '0' && arg[0] <= '9') {
            showBackgroundIndex(static_cast<uint8_t>(atoi(arg)));
        } else {
            bool found = false;
            for (uint8_t i = 0; i < bgs.count; ++i) {
                if (strcmp(bgs.names[i], arg) == 0) {
                    showBackgroundIndex(i);
                    found = true;
                    break;
                }
            }
            if (!found) {
                Serial.printf("[BG] fons desconegut: %s\n", arg);
            }
        }
        gBgTimer = millis();
    } else if (strcmp(cmd, "pet") == 0) {
        startPetTest(arg != nullptr ? arg : PET_TEST_BG);
    } else if (strcmp(cmd, "anim") == 0) {
        if (SpriteRenderer::isActive()) {
            gScreen = Screen::PetTest;
            if (arg == nullptr || strcmp(arg, "next") == 0) {
                Serial.printf("[PET] animacio -> %s\n", SpriteRenderer::nextAnimation());
            } else {
                SpriteRenderer::setAnimation(arg);
            }
            SpriteRenderer::drawFrame();
            drawPetOverlay();
            gPetAnimTimer = millis();
        } else {
            Serial.println(F("[PET] la mascota no esta activa (fes 'pet' primer)"));
        }
    } else if (strcmp(cmd, "petscale") == 0) {
        if (arg != nullptr) {
            SpriteRenderer::setScale(static_cast<uint8_t>(atoi(arg)));
        }
        redrawPetTest();
        Storage::savePetLayout(SpriteRenderer::status().x, SpriteRenderer::status().y,
                               SpriteRenderer::scale());
        Serial.printf("[PET] scale=%u box=%ux%u pos=(%d,%d) [desat]\n",
                      static_cast<unsigned>(SpriteRenderer::scale()),
                      static_cast<unsigned>(SpriteRenderer::status().boxW),
                      static_cast<unsigned>(SpriteRenderer::status().boxH),
                      SpriteRenderer::status().x, SpriteRenderer::status().y);
    } else if (strcmp(cmd, "petpos") == 0) {
        if (arg == nullptr) {
            Serial.println(F("[PET] us: petpos <x> <y> | petpos center"));
        } else if (strcmp(arg, "center") == 0) {
            SpriteRenderer::centerX();
        } else {
            int px = 0;
            int py = 0;
            if (sscanf(arg, "%d %d", &px, &py) == 2) {
                SpriteRenderer::setPosition(static_cast<int16_t>(px), static_cast<int16_t>(py));
            } else {
                Serial.println(F("[PET] us: petpos <x> <y> | petpos center"));
            }
        }
        redrawPetTest();
        Storage::savePetLayout(SpriteRenderer::status().x, SpriteRenderer::status().y,
                               SpriteRenderer::scale());
        Serial.printf("[PET] pos=(%d,%d) box=%ux%u [desat]\n",
                      SpriteRenderer::status().x, SpriteRenderer::status().y,
                      static_cast<unsigned>(SpriteRenderer::status().boxW),
                      static_cast<unsigned>(SpriteRenderer::status().boxH));
    } else if (strcmp(cmd, "petreset") == 0) {
        SpriteRenderer::resetLayout();
        Storage::clearPetLayout();
        redrawPetTest();
        Serial.printf("[PET] disposicio per defecte: x%u pos=(%d,%d)\n",
                      static_cast<unsigned>(SpriteRenderer::scale()),
                      SpriteRenderer::status().x, SpriteRenderer::status().y);
    } else if (strcmp(cmd, "sd") == 0) {
        gScreen = Screen::Static;
        SdAssets::begin();
        SdAssets::printTree(Serial, "/", 3);
        if (SdAssets::isMounted()) {
            drawSdScreen();
        } else {
            drawSdError();
        }
    } else if (strcmp(cmd, "lssd") == 0) {
        if (!SdAssets::isMounted()) {
            SdAssets::begin();
        }
        SdAssets::printTree(Serial, "/", 5);
    } else if (strcmp(cmd, "bench") == 0) {
        const char* which = (arg != nullptr) ? arg : "weather_00";
        BgRenderer::bench(which);
    } else if (strcmp(cmd, "shot") == 0) {
        cmdShot(arg);
    } else if (strcmp(cmd, "baud") == 0) {
        cmdBaud(arg);
    } else if (strcmp(cmd, "theme") == 0) {
        if (arg == nullptr) {
            for (uint8_t i = 0; i < Ui::themeCount(); ++i) {
                Serial.printf("[UI] tema %u: %-14s%s\n", static_cast<unsigned>(i),
                              Ui::themeName(i), (i == Ui::theme()) ? " <- actual" : "");
            }
        } else {
            const int idx = atoi(arg);
            if (idx < 0 || idx >= static_cast<int>(Ui::themeCount())) {
                Serial.println(F("[UI] tema invalid"));
            } else {
                Ui::setTheme(static_cast<uint8_t>(idx));
                Storage::saveUiTheme(static_cast<uint8_t>(idx));
                redrawHomeUi();
                Serial.printf("[UI] tema -> %s\n", Ui::themeName(static_cast<uint8_t>(idx)));
            }
        }
    } else if (strcmp(cmd, "homebg") == 0) {
        if (arg == nullptr) {
            Serial.printf("[UI] fons principal: %s (%s)\n", gHomeBg,
                          gAutoBg ? "automatic segons la meteo" : "fixat a ma");
        } else if (strcmp(arg, "auto") == 0) {
            gAutoBg = true;
            Storage::saveHomeAuto(true);
            Net::requestRefresh();
            Serial.println(F("[UI] el fons seguira la meteo"));
        } else if (!backgroundExists(arg)) {
            Serial.printf("[UI] el fons '%s' no existeix a la SD\n", arg);
        } else {
            gAutoBg = false;
            Storage::saveHomeAuto(false);
            strlcpy(gHomeBg, arg, sizeof(gHomeBg));
            Storage::saveHomeBg(gHomeBg);
            startHome();
        }
    } else if (strcmp(cmd, "wifi") == 0) {
        if (arg == nullptr) {
            Net::printStatus();
        } else if (strcmp(arg, "off") == 0) {
            Net::clearCredentials();
            Serial.println(F("[NET] credencials esborrades (wifi desactivat)"));
        } else {
            char* ssidArg = arg;
            char* passArg = strchr(arg, ' ');
            if (passArg != nullptr) {
                *passArg++ = '\0';
            }
            if (Net::setCredentials(ssidArg, passArg != nullptr ? passArg : "")) {
                Serial.printf("[NET] credencials desades per \"%s\"; reconnectant...\n", ssidArg);
            } else {
                Serial.println(F("[NET] us: wifi <ssid> <contrasenya> | wifi off"));
            }
        }
    } else if (strcmp(cmd, "net") == 0) {
        Net::printStatus();
    } else if (strcmp(cmd, "meteo") == 0) {
        Net::requestRefresh();
        Serial.println(F("[NET] refrescant la meteo..."));
    } else if (strcmp(cmd, "geo") == 0) {
        if (arg == nullptr) {
            Net::printStatus();
        } else {
            float lat = 0.0f;
            float lon = 0.0f;
            if (sscanf(arg, "%f %f", &lat, &lon) == 2) {
                Net::setLocation(lat, lon);
                Net::requestRefresh();
                Serial.printf("[NET] ubicacio -> %.4f, %.4f\n", static_cast<double>(lat),
                              static_cast<double>(lon));
            } else {
                Serial.println(F("[NET] us: geo <latitud> <longitud>"));
            }
        }
    } else if (strcmp(cmd, "sets") == 0) {
        int a = 0;
        int b = 0;
        int c = 0;
        int d = 0;
        if (arg == nullptr || sscanf(arg, "%d %d %d %d", &a, &b, &c, &d) != 4) {
            Serial.println(F("[PET] us: sets <menjar> <felicitat> <energia> <salut>  (0..100)"));
        } else {
            Pet::debugSet(static_cast<uint8_t>(a), static_cast<uint8_t>(b),
                          static_cast<uint8_t>(c), static_cast<uint8_t>(d));
        }
    } else if (strcmp(cmd, "needs") == 0) {
        Pet::printStatus();
    } else if (strcmp(cmd, "tmon") == 0) {
        cmdTouchMon(arg);
    } else if (strcmp(cmd, "tth") == 0) {
        if (arg != nullptr) {
            Touch::setPressureThreshold(static_cast<uint16_t>(atoi(arg)));
        }
        Serial.printf("[TOUCH] threshold=%u\n",
                      static_cast<unsigned>(Touch::pressureThreshold()));
    } else {
        Serial.printf("[CON] comanda desconeguda: %s\n", cmd);
        printHelp();
    }
}

void pollSerial()
{
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            gLine[gLineLen] = '\0';
            handleCommand(gLine);
            gLineLen = 0;
        } else if (gLineLen < CONSOLE_LINE_MAX - 1) {
            gLine[gLineLen++] = c;
        }
    }
}

}  // namespace

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println();
    Serial.println(F("=== Tamagoxi v2 - bring-up de maquinari (Fase 0+) ==="));
    Serial.printf("[SYS] chip=%s cores=%d rev=%d flash=%uMB heap=%u maxAlloc=%u\n",
                  ESP.getChipModel(), ESP.getChipCores(), ESP.getChipRevision(),
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    if (!Display::init()) {
        Serial.println(F("[GFX] ERROR: no s'ha pogut inicialitzar la pantalla"));
    }
    Led::begin();
    Touch::init();

    /* El tactil funciona amb un mapa per defecte; la calibracio fina es fa amb
     * la comanda 'cal' o amb una premuda llarga al rellotge. */
    if (!Touch::isCalibrated()) {
        Serial.println(F("[Touch] sense calibracio d'usuari: es fa servir el mapa per defecte"));
    }

    if (BL_DIAG_ON_BOOT) {
        runBacklightDiagnostic();
    }

    if (TOUCH_DIAG_ON_BOOT) {
        TFT_eSPI& t = Display::driver();
        t.fillScreen(TFT_BLACK);
        t.setTextDatum(MC_DATUM);
        t.setTextColor(TFT_WHITE, TFT_BLACK);
        t.setTextFont(4);
        t.drawString("DIAGNOSTIC TACTIL", SCREEN_W / 2, 150);
        t.setTextColor(TFT_YELLOW, TFT_BLACK);
        t.drawString("Toca la pantalla", SCREEN_W / 2, 230);
        t.drawString("uns quants cops", SCREEN_W / 2, 270);
        delay(1500);
        char secs[8];
        snprintf(secs, sizeof(secs), "%d", TOUCH_DIAG_SECONDS);
        cmdTouchMon(secs);
    }

    Pet::begin();

    const Storage::HomeCfg home = Storage::loadHomeCfg();
    if (home.themeValid) {
        Ui::setTheme(home.theme);
    }
    if (home.bgValid) {
        strlcpy(gHomeBg, home.bg, sizeof(gHomeBg));
    }
    if (home.autoValid) {
        gAutoBg = home.autoBg;
    }

    Serial.println(F("[SD] escanejant la targeta..."));
    if (SdAssets::begin()) {
        SdAssets::printTree(Serial, "/", 2);
        BgRenderer::begin();
        startHome();
    } else {
        drawSdError();
        gScreen = Screen::Static;
        gSdRetry = millis();
    }

    /* Fase 6: WiFi + NTP + meteo, en una tasca propia (no bloqueja mai el bucle). */
    Net::begin();

    updateLed();
    gHeapTimer = millis();
    printInfo();
    printHelp();
}

void loop()
{
    pollSerial();
    Touch::update();

    const bool pressed = Touch::isPressed();
    const bool tap = pressed && !gTouchWasPressed;  /* flanc de pujada */
    gTouchWasPressed = pressed;

    int16_t x = -1;
    int16_t y = -1;
    if (pressed) {
        Touch::getCoords(x, y);
    }

    if (gScreen == Screen::Home) {
        Pet::update(millis());

        /* L'hora del NTP arriba un cop: la passem a la mascota perque apliqui el
         * decaiment del temps que ha estat apagada. */
        if (Net::timeSynced() && !gTimeApplied) {
            gTimeApplied = true;
            Pet::setEpoch(Net::epoch());
        }

        if (Pet::sleeping() != gWasSleeping) {
            gWasSleeping = Pet::sleeping();
            Display::setBacklight(gWasSleeping ? 35 : 100);
        }

        if (SpriteRenderer::isActive()) {
            const char* anim = Pet::animation();
            if (strcmp(SpriteRenderer::animationName(), anim) != 0) {
                SpriteRenderer::setAnimation(anim);
            }

            SpriteRenderer::update(millis());

            if (gHeartUntil != 0) {
                if (millis() < gHeartUntil) {
                    Ui::drawHeart(gHeartX, gHeartY);
                } else {
                    gHeartUntil = 0;
                }
            }
        }

        if (!gMenuOpen) {
            homeHud();
            const Pet::Needs& n = Pet::needs();
            Ui::drawBars(n.food, n.happiness, n.energy, n.health);

            /* Fons automatic segons la meteo (weather_NN). Nomes si l'usuari no
             * ha fixat cap fons a ma amb 'homebg <nom>'. */
            if (gAutoBg) {
                const char* suggested = Net::backgroundName();
                if (suggested[0] != '\0' && strcmp(suggested, gHomeBg) != 0 &&
                    backgroundExists(suggested)) {
                    Serial.printf("[UI] fons segons la meteo: %s -> %s\n", gHomeBg,
                                  suggested);
                    strlcpy(gHomeBg, suggested, sizeof(gHomeBg));
                    startHome();
                }
            }
        }

        /* Tocs: els detectats pel bucle o els latchats mentre es dibuixava. */
        int16_t tx = x;
        int16_t ty = y;
        bool haveTap = tap;
        if (gTouchLatch) {
            gTouchLatch = false;
            haveTap = true;
            tx = gTouchLatchX;
            ty = gTouchLatchY;
        }
        if (haveTap && (millis() - gLastTapMs >= UI_TAP_DEBOUNCE_MS)) {
            gLastTapMs = millis();
            handleHomeTap(tx, ty);
        }

        /* Premuda llarga al rellotge del HUD = menu d'ajustos (Fase 7). */
        if (!gMenuOpen && pressed && Ui::hitTest(x, y, false) == Ui::Zone::HudClock) {
            if (gPressStart == 0) {
                gPressStart = millis();
            }
            if (!gLongPressDone && (millis() - gPressStart >= UI_LONGPRESS_MS)) {
                gLongPressDone = true;
                Serial.println(F("[UI] premuda llarga -> calibracio del tactil"));
                Touch::calibrate();
                startHome();
            }
        } else {
            gPressStart = 0;
            gLongPressDone = false;
        }
    } else if (gScreen == Screen::Gallery) {
        const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
        if (bgs.count > 0) {
            if (tap || (millis() - gBgTimer >= BG_GALLERY_INTERVAL_MS)) {
                showBackgroundIndex(static_cast<uint8_t>((gBgIndex + 1) % bgs.count));
                gBgTimer = millis();
            }
        }
    } else if (gScreen == Screen::TouchTest) {
        /* Boto CALIBRAR (zona de baix). Cal calibratge previ per tenir coords. */
        if (pressed && Touch::isCalibrated() && y >= kButtonTop) {
            if (!gBtnLatched) {
                gBtnLatched = true;
                drawButton(true);
                Touch::calibrate();
                drawTouchScreen();
                updateLed();
            }
        } else {
            gBtnLatched = false;
            if (pressed && Touch::isCalibrated() && y >= kCanvasTop && y < kButtonTop) {
                drawCrosshair(x, y);
            }
        }
        if (pressed) {
            updateInfo();
        }
    } else if (gScreen == Screen::PetTest) {
        if (SpriteRenderer::isActive()) {
            if (tap) {
                Serial.printf("[PET] animacio -> %s\n", SpriteRenderer::nextAnimation());
                SpriteRenderer::drawFrame();
                gPetAnimTimer = millis();
            } else {
                SpriteRenderer::update(millis());
            }

            if (millis() - gPetAnimTimer >= PET_ANIM_SWITCH_MS) {
                gPetAnimTimer = millis();
                Serial.printf("[PET] animacio -> %s\n", SpriteRenderer::nextAnimation());
                SpriteRenderer::drawFrame();
            }

            if (millis() - gPetOverlayTimer >= 500) {
                gPetOverlayTimer = millis();
                drawPetOverlay();
                const SpriteRenderer::Status& s = SpriteRenderer::status();
                const uint32_t avg = s.totalFrames ? (s.totalMs / s.totalFrames) : 0;
                Serial.printf("[PET] %s f%u/%u last=%lums avg=%lums min=%lu max=%lu n=%lu heap=%u\n",
                              s.anim, static_cast<unsigned>(s.frameIndex + 1),
                              static_cast<unsigned>(s.frameCount),
                              static_cast<unsigned long>(s.lastFrameMs),
                              static_cast<unsigned long>(avg),
                              static_cast<unsigned long>(s.minFrameMs),
                              static_cast<unsigned long>(s.maxFrameMs),
                              static_cast<unsigned long>(s.totalFrames),
                              static_cast<unsigned>(ESP.getFreeHeap()));
            }
        }
    }

    /* Reintent de muntatge de la SD si no n'hi ha (mai ens pengem). */
    if (!SdAssets::isMounted() && (millis() - gSdRetry >= 3000)) {
        gSdRetry = millis();
        Serial.println(F("[SD] reintent de muntatge..."));
        if (SdAssets::begin()) {
            SdAssets::printTree(Serial, "/", 2);
            BgRenderer::begin();
            startHome();
        }
    }

    /* Informe periodic de memoria (detecta fuites d'heap). */
    if (millis() - gHeapTimer >= HEAP_LOG_INTERVAL_MS) {
        gHeapTimer = millis();
        Serial.printf("[SYS] up=%lus heap=%u maxAlloc=%u minFree=%u\n",
                      static_cast<unsigned long>(millis() / 1000),
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getMaxAllocHeap()),
                      static_cast<unsigned>(ESP.getMinFreeHeap()));
    }

    delay(5);
}



