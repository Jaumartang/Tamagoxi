#include "storage.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

constexpr const char* kNamespace = "tg_cfg";
/* Canviar la marca invalida les dades desades anteriors (nou format/disposicio). */
constexpr uint16_t    kMagic     = 0x5A02;

}  // namespace

namespace Storage {

void begin()
{
    /* Preferences s'obre i es tanca per operacio; no cal estat persistent. */
}

PetLayout loadPetLayout()
{
    PetLayout layout{false, 0, 0, 2};

    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
        return layout;
    }
    if (prefs.getUShort("plmagic", 0) == kMagic) {
        layout.valid = true;
        layout.x     = static_cast<int16_t>(prefs.getShort("plx", 0));
        layout.y     = static_cast<int16_t>(prefs.getShort("ply", 0));
        layout.scale = prefs.getUChar("plscale", 2);
    }
    prefs.end();
    return layout;
}

void savePetLayout(int16_t x, int16_t y, uint8_t scale)
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        Serial.println(F("[STORE] NVS no disponible"));
        return;
    }
    prefs.putUShort("plmagic", kMagic);
    prefs.putShort("plx", x);
    prefs.putShort("ply", y);
    prefs.putUChar("plscale", scale);
    prefs.end();
}

void clearPetLayout()
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        return;
    }
    prefs.remove("plmagic");
    prefs.end();
}

HomeCfg loadHomeCfg()
{
    HomeCfg cfg{false, {0}, false, 0, false, false};

    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
        return cfg;
    }
    const String bg = prefs.getString("hbg", "");
    if (bg.length() > 0 && bg.length() < sizeof(cfg.bg)) {
        strlcpy(cfg.bg, bg.c_str(), sizeof(cfg.bg));
        cfg.bgValid = true;
    }
    const uint8_t t = prefs.getUChar("uitheme", 0xFF);
    if (t != 0xFF) {
        cfg.theme = t;
        cfg.themeValid = true;
    }
    const uint8_t a = prefs.getUChar("bgauto", 0xFF);
    if (a != 0xFF) {
        cfg.autoBg = (a != 0);
        cfg.autoValid = true;
    }
    prefs.end();
    return cfg;
}

void saveHomeBg(const char* name)
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        Serial.println(F("[STORE] NVS no disponible"));
        return;
    }
    prefs.putString("hbg", name);
    prefs.end();
}

void saveUiTheme(uint8_t theme)
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        Serial.println(F("[STORE] NVS no disponible"));
        return;
    }
    prefs.putUChar("uitheme", theme);
    prefs.end();
}

void saveHomeAuto(bool autoBg)
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        Serial.println(F("[STORE] NVS no disponible"));
        return;
    }
    prefs.putUChar("bgauto", autoBg ? 1 : 0);
    prefs.end();
}

}  // namespace Storage
