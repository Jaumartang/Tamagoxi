#include "pet.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#include "config.h"

namespace {

constexpr const char* kNamespace = "tg_pet";
constexpr uint16_t    kMagic     = 0x9E01;

float gFood       = 70.0f;
float gHappy      = 70.0f;
float gEnergy     = 70.0f;
float gHealth     = 90.0f;
bool  gSleeping   = false;

uint32_t gLastMs     = 0;
uint32_t gLastSaveMs = 0;
uint32_t gEpoch      = 0;    /* hora actual si la sabem (NTP, Fase 6) */
uint32_t gSavedEpoch = 0;    /* hora del darrer desament */

char     gTempAnim[16] = {0};
uint32_t gTempUntil    = 0;
uint32_t gNextExtraMs  = 0;

void clamp01(float& v)
{
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 100.0f) {
        v = 100.0f;
    }
}

void clampAll()
{
    clamp01(gFood);
    clamp01(gHappy);
    clamp01(gEnergy);
    clamp01(gHealth);
}

uint8_t toU8(float v)
{
    clamp01(v);
    return static_cast<uint8_t>(v + 0.5f);
}

/* Fa passar 'minutes' de joc: decaiment de necessitats i salut. */
void applyDecay(float minutes)
{
    const bool poorly = (gFood < 20.0f) || (gHappy < 20.0f) || (gEnergy < 20.0f);

    gFood = gFood - (PET_DECAY_FOOD * minutes);
    gHappy = gHappy - (PET_DECAY_HAPPY * minutes);

    if (gSleeping) {
        gEnergy = gEnergy + (PET_SLEEP_RECOVER * minutes);
    } else {
        gEnergy = gEnergy - (PET_DECAY_ENERGY * minutes);
    }

    if (poorly) {
        gHealth = gHealth - (PET_HEALTH_DROP * minutes);
    } else {
        gHealth = gHealth + (PET_HEALTH_RECOVER * minutes);
    }

    clampAll();

    if (gSleeping && gEnergy >= PET_WAKE_ENERGY) {
        gSleeping = false;
    }
}

void load()
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
        return;
    }
    if (prefs.getUShort("magic", 0) == kMagic) {
        gFood = static_cast<float>(prefs.getUChar("food", 70));
        gHappy = static_cast<float>(prefs.getUChar("happy", 70));
        gEnergy = static_cast<float>(prefs.getUChar("energy", 70));
        gHealth = static_cast<float>(prefs.getUChar("health", 90));
        gSleeping = prefs.getBool("sleep", false);
        gSavedEpoch = prefs.getUInt("epoch", 0);
    }
    prefs.end();
}

void playTempInternal(const char* name, uint32_t ms)
{
    strlcpy(gTempAnim, name, sizeof(gTempAnim));
    gTempUntil = millis() + ms;
}

const char* moodName(Pet::Mood m)
{
    switch (m) {
        case Pet::Mood::Hungry:   return "GANA";
        case Pet::Mood::Sad:      return "TRIST";
        case Pet::Mood::Sick:     return "MALALT";
        case Pet::Mood::Sleeping: return "DORMINT";
        default:                  return "TRANQUIL";
    }
}

}  // namespace

namespace Pet {

void begin()
{
    load();
    gLastMs = millis();
    gLastSaveMs = gLastMs;
    gNextExtraMs = gLastMs + 180000u + (esp_random() % 300000u);

    Serial.printf("[PET] estat: menjar=%u felicitat=%u energia=%u salut=%u dormint=%d%s\n",
                  static_cast<unsigned>(toU8(gFood)),
                  static_cast<unsigned>(toU8(gHappy)),
                  static_cast<unsigned>(toU8(gEnergy)),
                  static_cast<unsigned>(toU8(gHealth)),
                  gSleeping ? 1 : 0,
                  (gSavedEpoch != 0) ? " (amb marca de temps desada)" : "");
}

void update(uint32_t nowMs)
{
    const uint32_t dt = nowMs - gLastMs;
    if (dt < 1000) {
        return;   /* actualitzem com a molt 1 cop per segon */
    }
    gLastMs = nowMs;

    applyDecay(static_cast<float>(dt) / 60000.0f);

    /* Auto-adormir si esta cansat de bon de veritat. */
    if (!gSleeping && gEnergy < PET_AUTO_SLEEP) {
        gSleeping = true;
        Serial.println(F("[PET] s'ha adormit de cansat"));
    }

    /* De tant en tant fa una animacio especial: aixi es veu que es viu. */
    if (gTempUntil == 0 && !gSleeping && nowMs >= gNextExtraMs) {
        gNextExtraMs = nowMs + 180000u + (esp_random() % 420000u);
        playTempInternal("EXTRA", 2000);
    }

    if (gTempUntil != 0 && static_cast<int32_t>(nowMs - gTempUntil) >= 0) {
        gTempUntil = 0;
        gTempAnim[0] = '\0';
    }

    if (nowMs - gLastSaveMs >= PET_SAVE_PERIOD_MS) {
        save();
    }
}

void save()
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        Serial.println(F("[PET] no s'ha pogut desar a la NVS"));
        return;
    }
    prefs.putUShort("magic", kMagic);
    prefs.putUChar("food", toU8(gFood));
    prefs.putUChar("happy", toU8(gHappy));
    prefs.putUChar("energy", toU8(gEnergy));
    prefs.putUChar("health", toU8(gHealth));
    prefs.putBool("sleep", gSleeping);
    prefs.putUInt("epoch", gEpoch);
    prefs.end();

    gSavedEpoch = gEpoch;
    gLastSaveMs = millis();
}

void setEpoch(uint32_t epochSeconds)
{
    gEpoch = epochSeconds;

    if (gSavedEpoch != 0 && epochSeconds > gSavedEpoch) {
        uint32_t secs = epochSeconds - gSavedEpoch;
        const uint32_t cap = 72u * 3600u;    /* com a molt, 3 dies */
        if (secs > cap) {
            secs = cap;
        }
        applyDecay(static_cast<float>(secs) / 60.0f);
        Serial.printf("[PET] decaiment fora de linia aplicat: %lu min\n",
                      static_cast<unsigned long>(secs / 60u));
        gSavedEpoch = 0;
    }
}

const Needs& needs()
{
    static Needs n;
    n.food = toU8(gFood);
    n.happiness = toU8(gHappy);
    n.energy = toU8(gEnergy);
    n.health = toU8(gHealth);
    return n;
}

Mood mood()
{
    if (gHealth < PET_LOW_HEALTH) {
        return Mood::Sick;
    }
    if (gSleeping) {
        return Mood::Sleeping;
    }
    if (gFood < PET_LOW_FOOD) {
        return Mood::Hungry;
    }
    if (gHappy < PET_LOW_HAPPY) {
        return Mood::Sad;
    }
    if (gEnergy < PET_LOW_ENERGY) {
        return Mood::Sleeping;
    }
    return Mood::Idle;
}

bool sleeping()
{
    return gSleeping;
}

const char* animation()
{
    if (gTempUntil != 0) {
        return gTempAnim;
    }
    switch (mood()) {
        case Mood::Sick:     return "SICK";
        case Mood::Sleeping: return "SLEEP";
        case Mood::Hungry:   return "HUNGRY";
        case Mood::Sad:      return "SAD";
        default:             return "IDLE";
    }
}

bool feed()
{
    if (gFood > 95.0f) {
        Serial.println(F("[PET] no te gana"));
        return false;
    }
    gFood = gFood + PET_FEED_FOOD;
    gHappy = gHappy + PET_FEED_HAPPY;
    clampAll();
    playTempInternal("EAT", 1600);
    Serial.printf("[PET] menja -> menjar %u\n", static_cast<unsigned>(toU8(gFood)));
    save();
    return true;
}

bool play()
{
    if (gEnergy < 15.0f) {
        Serial.println(F("[PET] massa cansat per jugar"));
        return false;
    }
    gHappy = gHappy + PET_PLAY_HAPPY;
    gEnergy = gEnergy + PET_PLAY_ENERGY;
    gFood = gFood + PET_PLAY_FOOD;
    clampAll();
    playTempInternal("PLAY", 1600);
    Serial.printf("[PET] juga -> felicitat %u\n", static_cast<unsigned>(toU8(gHappy)));
    save();
    return true;
}

bool toggleSleep()
{
    gSleeping = !gSleeping;
    gTempUntil = 0;
    playTempInternal(gSleeping ? "SLEEP" : "HAPPY", gSleeping ? 1500 : 1000);
    Serial.println(gSleeping ? F("[PET] a dormir") : F("[PET] despert"));
    save();
    return true;
}

bool heal()
{
    if (gHealth > 98.0f) {
        Serial.println(F("[PET] ja esta be"));
        return false;
    }
    gHealth = gHealth + PET_HEAL_HEALTH;
    clampAll();
    playTempInternal("CELEBRATE", 1400);
    Serial.printf("[PET] curat -> salut %u\n", static_cast<unsigned>(toU8(gHealth)));
    save();
    return true;
}

bool pet()
{
    gHappy = gHappy + PET_PET_HAPPY;
    clampAll();
    playTempInternal("HAPPY", 1000);
    return true;
}

void printStatus()
{
    Serial.printf("[PET] menjar=%u felicitat=%u energia=%u salut=%u | estat=%s | anim=%s | dormint=%d\n",
                  static_cast<unsigned>(toU8(gFood)),
                  static_cast<unsigned>(toU8(gHappy)),
                  static_cast<unsigned>(toU8(gEnergy)),
                  static_cast<unsigned>(toU8(gHealth)),
                  moodName(mood()), animation(), gSleeping ? 1 : 0);
}

void debugSet(uint8_t food, uint8_t happiness, uint8_t energy, uint8_t health)
{
    gFood = static_cast<float>(food);
    gHappy = static_cast<float>(happiness);
    gEnergy = static_cast<float>(energy);
    gHealth = static_cast<float>(health);
    clampAll();
    printStatus();
}

}  // namespace Pet
