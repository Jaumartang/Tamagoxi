#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "led.h"
#include "pins.h"
#include "touch.h"

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

/* --- Consola serie -------------------------------------------------------- */
void printHelp()
{
    Serial.println(F("[CON] comandes: help | info | bl <0-100> | bltest | loaddemo "
                     "| cal | touch | colortest | tth <n>"));
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
        Touch::calibrate();
        drawTouchScreen();
        updateLed();
    } else if (strcmp(cmd, "touch") == 0) {
        drawTouchScreen();
    } else if (strcmp(cmd, "colortest") == 0) {
        drawColourBands();
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

/* Comprova que hi ha tactil, esperant com a maxim timeoutMs (mai pengem). */
bool waitForTouch(uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (Touch::isPressed()) {
            return true;
        }
        delay(20);
    }
    return false;
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

    /* Primera arrencada sense calibratge: demanem un toc i calibrem. */
    if (!Touch::isCalibrated()) {
        Display::driver().fillScreen(TFT_BLACK);
        Display::driver().setTextDatum(MC_DATUM);
        Display::driver().setTextColor(TFT_WHITE, TFT_BLACK);
        Display::driver().setTextFont(4);
        Display::driver().drawString("Toca la pantalla", SCREEN_W / 2, SCREEN_H / 2 - 20);
        Display::driver().drawString("per calibrar", SCREEN_W / 2, SCREEN_H / 2 + 20);
        Serial.println(F("[Touch] Toca la pantalla per calibrar (15 s d'espera)"));
        if (waitForTouch(15000)) {
            delay(300);  /* que l'usuari aixequi el dit abans de calibrar */
            Touch::calibrate();
        } else {
            Serial.println(F("[Touch] sense resposta: segueixo sense calibrar ('cal')"));
        }
    }

    if (BL_DIAG_ON_BOOT) {
        runBacklightDiagnostic();
    }

    drawTouchScreen();
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
    int16_t x = -1;
    int16_t y = -1;
    if (pressed) {
        Touch::getCoords(x, y);
    }

    /* Boto CALIBRAR (zona de baix). Cal calibratge previ per tenir coordenades. */
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



