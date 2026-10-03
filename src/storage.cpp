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

}  // namespace Storage
