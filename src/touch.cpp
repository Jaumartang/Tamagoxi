#include "touch.h"

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "display.h"

namespace {

/* NVS (Preferences): l'espai de noms admet 15 caracters com a maxim. */
constexpr const char* kPrefsNamespace = "tg_touch";
constexpr uint16_t kCalibrationMagic = 0xC0DE;  /* marca de calibratge valid */

/* TFT_eSPI descriu el calibratge amb 5 valors: x0, x1 (delta), y0, y1 (delta)
 * i un byte de bits: rotate | (invert_x << 1) | (invert_y << 2). */
constexpr size_t kCalibrationValues = 5;

bool gReady = false;
bool gCalibrated = false;
bool gPressed = false;
int16_t gX = 0;
int16_t gY = 0;
bool gHasRawSample = false;
uint16_t gLastRawX = 0;
uint16_t gLastRawY = 0;
uint32_t gLastReadMs = 0;
uint16_t gCalibration[kCalibrationValues] = {0, 0, 0, 0, 0};
uint16_t gPressureThreshold = TOUCH_DEFAULT_PRESSURE;

void loadCalibration()
{
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, /*readOnly=*/true)) {
        return;  /* encara no hi ha res desat */
    }
    if (prefs.getUShort("magic", 0) == kCalibrationMagic) {
        gCalibration[0] = prefs.getUShort("x0", 0);
        gCalibration[1] = prefs.getUShort("x1", 0);
        gCalibration[2] = prefs.getUShort("y0", 0);
        gCalibration[3] = prefs.getUShort("y1", 0);
        gCalibration[4] = prefs.getUShort("flags", 0);
        gCalibrated = true;
    }
    prefs.end();
}

void saveCalibration()
{
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, /*readOnly=*/false)) {
        Serial.println(F("[Touch] No s'ha pogut obrir la NVS per desar el calibratge"));
        return;
    }
    prefs.putUShort("magic", kCalibrationMagic);
    prefs.putUShort("x0", gCalibration[0]);
    prefs.putUShort("x1", gCalibration[1]);
    prefs.putUShort("y0", gCalibration[2]);
    prefs.putUShort("y1", gCalibration[3]);
    prefs.putUShort("flags", gCalibration[4]);
    prefs.end();
}

/* Una lectura del panell. Vegeu la nota de touch.h sobre per que no es fa
 * servir tft.getTouch(). */
void readPanel()
{
    TFT_eSPI& tft = Display::driver();

    if (tft.getTouchRawZ() < gPressureThreshold) {
        gPressed = false;
        gHasRawSample = false;  /* el proxim toc comencara de nou */
        return;
    }

    uint16_t rawX = 0;
    uint16_t rawY = 0;
    tft.getTouchRaw(&rawX, &rawY);

    /* Descarta mostres inestables (soroll electric del panell resistiu). */
    if (gHasRawSample &&
        (abs(static_cast<int32_t>(rawX) - static_cast<int32_t>(gLastRawX)) > TOUCH_RAW_TOLERANCE ||
         abs(static_cast<int32_t>(rawY) - static_cast<int32_t>(gLastRawY)) > TOUCH_RAW_TOLERANCE)) {
        return;  /* no canviem la posicio: ens quedem amb l'ultima de bona */
    }
    gLastRawX = rawX;
    gLastRawY = rawY;
    gHasRawSample = true;

    if (!gCalibrated) {
        /* Sense calibratge no podem mapejar a pixels, pero si que hi ha dit. */
        gX = -1;
        gY = -1;
        gPressed = true;
        return;
    }

    tft.convertRawXY(&rawX, &rawY);
    if (rawX >= Display::width() || rawY >= Display::height()) {
        return;  /* fora de pantalla: no es un toc valid */
    }

    gX = static_cast<int16_t>(rawX);
    gY = static_cast<int16_t>(rawY);
    gPressed = true;
}

}  // namespace

namespace Touch {

void init()
{
    if (gReady) {
        return;
    }

    loadCalibration();
    if (gCalibrated) {
        Display::driver().setTouch(gCalibration);
    } else {
        Serial.println(F("[Touch] Sense calibratge desat: cal calibrar un cop per placa"));
    }

    gReady = true;
    Serial.printf("[Touch] XPT2046 a punt (llindar z=%u, %s)\n",
                  static_cast<unsigned>(gPressureThreshold),
                  gCalibrated ? "calibratge carregat de la NVS" : "SENSE calibrar");
}

bool update()
{
    if (!gReady) {
        return false;
    }

    const uint32_t nowMs = millis();
    if (gLastReadMs != 0 && (nowMs - gLastReadMs) < TOUCH_MIN_READ_INTERVAL_MS) {
        return gPressed;  /* lectura massa recent: aprofitem l'anterior */
    }
    gLastReadMs = nowMs;
    readPanel();
    return gPressed;
}

bool isPressed()
{
    return update();
}

void getCoords(int16_t& x, int16_t& y)
{
    if (!gCalibrated) {
        x = -1;
        y = -1;
        return;
    }
    x = gX;
    y = gY;
}

bool getRaw(int16_t& rawX, int16_t& rawY)
{
    if (!gReady) {
        return false;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    Display::driver().getTouchRaw(&x, &y);
    if (Display::driver().getTouchRawZ() < gPressureThreshold) {
        return false;  /* no hi ha prou pressio: lectura de soroll */
    }
    rawX = static_cast<int16_t>(x);
    rawY = static_cast<int16_t>(y);
    return true;
}

bool isCalibrated()
{
    return gCalibrated;
}

void calibrate()
{
    if (!gReady) {
        Serial.println(F("[Touch] calibrate() s'ha de cridar despres d'init()"));
        return;
    }

    Serial.println(F("[Touch] Calibratge: toca les 4 fletxes que aniran sortint "
                     "(dalt-esquerra, baix-esquerra, dalt-dreta, baix-dreta)"));
    Display::driver().calibrateTouch(gCalibration, TFT_WHITE, TFT_BLACK, 15);
    saveCalibration();
    gCalibrated = true;

    Serial.printf("[Touch] Calibratge desat a la NVS: x0=%u x1=%u y0=%u y1=%u flags=%u\n",
                  static_cast<unsigned>(gCalibration[0]),
                  static_cast<unsigned>(gCalibration[1]),
                  static_cast<unsigned>(gCalibration[2]),
                  static_cast<unsigned>(gCalibration[3]),
                  static_cast<unsigned>(gCalibration[4]));
}

void setPressureThreshold(uint16_t threshold)
{
    gPressureThreshold = threshold;
}

uint16_t pressureThreshold()
{
    return gPressureThreshold;
}

}  // namespace Touch
