#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "led.h"
#include "pins.h"

/*
 * main.cpp - Tamagoxi v2 (Tamagotchi gegant per a la Noa)
 *
 * FASE 0 - Esquelet i verificacio de maquinari.
 *   - Inicialitza la pantalla ST7796S 320x480 (vertical).
 *   - Pinta franges de colors + text per verificar ordre de bytes i geometria.
 *   - Parpelleja el LED RGB (vermell, verd, blau, blanc, groc, apagat).
 *   - Fa un test de retroil-luminacio (rampa) i despres el deixa al 100 %.
 *
 * El bucle es 100 % no bloquejant (tot amb millis()): mai no es penja.
 */

namespace {

/* --- Sequencia del test del LED RGB (nivells logics 0..255) --------------- */
struct LedStep {
    uint8_t r, g, b;
    const char* name;
};

constexpr LedStep kLedSteps[] = {
    {255,   0,   0, "VERMELL"},
    {  0, 255,   0, "VERD"},
    {  0,   0, 255, "BLAU"},
    {255, 255, 255, "BLANC"},
    {255, 255,   0, "GROC"},
    {  0,   0,   0, "APAGAT"},
};
constexpr uint8_t kLedStepCount = sizeof(kLedSteps) / sizeof(kLedSteps[0]);

uint32_t gLedTimer   = 0;
uint8_t  gLedStep    = 0;
uint32_t gBlStart    = 0;
uint32_t gHeapTimer  = 0;

/* Pinta la pantalla de test de maquinari: capcalera + franges de colors. */
void drawHardwareTestScreen()
{
    TFT_eSPI& t = Display::driver();
    t.fillScreen(TFT_BLACK);

    struct Band {
        uint16_t color;
        uint16_t textColor;
        const char* name;
    };
    /* Colors triats per revelar intercanvi de bytes (vermell<->blau) o inversio. */
    const Band bands[] = {
        {TFT_RED,     TFT_WHITE, "VERMELL"},
        {TFT_GREEN,   TFT_BLACK, "VERD"},
        {TFT_BLUE,    TFT_WHITE, "BLAU"},
        {TFT_YELLOW,  TFT_BLACK, "GROC"},
        {TFT_CYAN,    TFT_BLACK, "CIAN"},
        {TFT_MAGENTA, TFT_WHITE, "MAGENTA"},
        {TFT_WHITE,   TFT_BLACK, "BLANC"},
        {0x8410,      TFT_WHITE, "GRIS"},
    };
    const int bandCount = static_cast<int>(sizeof(bands) / sizeof(bands[0]));

    const int headerH = 40;
    const int bandH   = (SCREEN_H - headerH) / bandCount;

    /* Capcalera. */
    t.setTextFont(2);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(TFT_WHITE, TFT_BLACK);
    t.drawString("TAMAGOXIV2  FASE 0  HW TEST", 6, 6);
    t.setTextDatum(TR_DATUM);
    t.drawString("320x480", SCREEN_W - 6, 6);

    /* Franges. */
    for (int i = 0; i < bandCount; ++i) {
        const int y = headerH + i * bandH;
        t.fillRect(0, y, SCREEN_W, bandH, bands[i].color);
        t.setTextDatum(ML_DATUM);
        t.setTextColor(bands[i].textColor, bands[i].color);
        t.drawString(bands[i].name, 10, y + bandH / 2);
    }

    /* Marc i marques de cantonada (verifiquen geometria i orientacio). */
    t.drawRect(0, 0, SCREEN_W, SCREEN_H, TFT_WHITE);
    t.fillRect(0, headerH, 6, 6, TFT_WHITE);                     /* dalt-esquerra */
    t.fillRect(SCREEN_W - 6, headerH, 6, 6, TFT_RED);            /* dalt-dreta    */
    t.fillRect(0, SCREEN_H - 6, 6, 6, TFT_GREEN);                /* baix-esquerra */
    t.fillRect(SCREEN_W - 6, SCREEN_H - 6, 6, 6, TFT_BLUE);      /* baix-dreta    */

    Serial.println(F("[GFX] pantalla de test pintada (franges RGB + text)"));
}

}  // namespace

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println();
    Serial.println(F("=== Tamagoxi v2 - Fase 0: test de maquinari ==="));
    Serial.printf("[SYS] chip=%s cores=%d rev=%d flash=%uMB\n",
                  ESP.getChipModel(), ESP.getChipCores(), ESP.getChipRevision(),
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)));
    Serial.printf("[SYS] heap=%u maxAlloc=%u\n",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));

    if (!Display::init()) {
        Serial.println(F("[GFX] ERROR: no s'ha pogut inicialitzar la pantalla"));
    }

    Led::begin();
    drawHardwareTestScreen();

    gBlStart   = millis();
    gLedTimer  = millis();
    gHeapTimer = millis();

    Serial.println(F("[SYS] test en marxa. Hauries de veure 8 franges de colors "
                     "i el LED RGB canviant de color."));
}

void loop()
{
    const uint32_t now = millis();

    /* Sequencia del LED RGB. */
    if (now - gLedTimer >= LED_TEST_STEP_MS) {
        gLedTimer = now;
        const LedStep& s = kLedSteps[gLedStep];
        Led::setRgb(s.r, s.g, s.b);
        Serial.printf("[LED] %s (r=%u g=%u b=%u)\n",
                      s.name, s.r, s.g, s.b);
        gLedStep = static_cast<uint8_t>((gLedStep + 1) % kLedStepCount);
    }

    /* Test de retroil-luminacio: rampa suau i, en acabar, fix al 100 %. */
    if (now - gBlStart < BL_TEST_DURATION_MS) {
        const uint32_t half   = BL_TEST_PERIOD_MS / 2;
        const uint32_t phase  = (now - gBlStart) % BL_TEST_PERIOD_MS;
        const uint32_t span   = 100 - BL_TEST_MIN_PERCENT;
        uint8_t pct;
        if (phase < half) {
            pct = static_cast<uint8_t>(BL_TEST_MIN_PERCENT + span * phase / half);
        } else {
            pct = static_cast<uint8_t>(100 - span * (phase - half) / half);
        }
        Display::setBacklight(pct);
    } else if (Display::backlight() != 100) {
        Display::setBacklight(100);
        Serial.println(F("[GFX] retroil-luminacio -> 100 % (test acabat)"));
    }

    /* Informe periodic de memoria (detecta fuites d'heap). */
    if (now - gHeapTimer >= HEAP_LOG_INTERVAL_MS) {
        gHeapTimer = now;
        Serial.printf("[SYS] up=%lus heap=%u maxAlloc=%u minFree=%u\n",
                      static_cast<unsigned long>(now / 1000),
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getMaxAllocHeap()),
                      static_cast<unsigned>(ESP.getMinFreeHeap()));
    }

    delay(5);
}
