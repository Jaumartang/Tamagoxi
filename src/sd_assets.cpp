#include "sd_assets.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "pins.h"

namespace {

/* --- Estat ----------------------------------------------------------------- */
SPIClass gSpiSd(VSPI);       /* bus VSPI, independent del HSPI del LCD */
bool     gMounted = false;
uint8_t  gPetCount = 0;
SdAssets::Report      gReport;
SdAssets::Backgrounds gBgs;
SdAssets::Pet         gPets[SdAssets::kMaxPets];

constexpr size_t  kPathLen = 96;
constexpr uint8_t kMountAttempts = 4;

/* --- Utilitats ------------------------------------------------------------- */

void setError(const char* msg)
{
    if (gReport.firstError[0] == '\0') {
        strlcpy(gReport.firstError, msg, sizeof(gReport.firstError));
    }
    Serial.printf("[SD] %s\n", msg);
}

/* Retorna el nom base d'un cami ("/backgrounds/x.bin" -> "x.bin"). */
const char* baseName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

/* --- Muntatge de la targeta ------------------------------------------------ */

bool mountCard()
{
    if (gMounted) {
        return true;
    }

    gSpiSd.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    /* Pull-up intern a MISO: sense ell, algunes targetes responen de manera
     * marginal. */
    pinMode(PIN_SD_MISO, INPUT_PULLUP);

    bool mounted = false;
    uint32_t usedSpeed = 0;
    SD.end();
    for (uint8_t attempt = 0; attempt < kMountAttempts && !mounted; ++attempt) {
        if (attempt > 0) {
            SD.end();
            digitalWrite(PIN_SD_CS, LOW);
            delay(5);
            digitalWrite(PIN_SD_CS, HIGH);
            delay(50UL << (attempt - 1 > 3 ? 3 : attempt - 1));
        }
        /* Escala de velocitats: primer les rapides i, si fallen, les segures. */
        static const uint32_t kSpeeds[kMountAttempts] = {
            25000000UL, 20000000UL, 10000000UL, 4000000UL};
        const uint32_t speed = kSpeeds[attempt];
        mounted = SD.begin(PIN_SD_CS, gSpiSd, speed);
        if (mounted) {
            usedSpeed = speed;
        }
    }

    if (!mounted) {
        setError("No s'ha pogut muntar la microSD (reintentant...)");
        gMounted = false;
        return false;
    }

    gMounted = true;
    Serial.printf("[SD] MicroSD muntada (%u MHz, tipus %u, %llu MB)\n",
                  static_cast<unsigned>(usedSpeed / 1000000UL),
                  static_cast<unsigned>(SD.cardType()),
                  SD.cardSize() / (1024ULL * 1024ULL));
    return true;
}

/* --- Lectura del manifest de fons ----------------------------------------- */

bool readBackgrounds()
{
    File f = SD.open("/backgrounds/manifest.json", FILE_READ);
    if (!f) {
        setError("Falta /backgrounds/manifest.json");
        return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        setError("manifest de fons: JSON invalid");
        return false;
    }

    gBgs.width  = doc["width"] | 0;
    gBgs.height = doc["height"] | 0;
    gBgs.count  = 0;

    JsonArray files = doc["files"].as<JsonArray>();
    for (JsonVariant v : files) {
        if (gBgs.count >= SdAssets::kMaxBackgrounds) {
            Serial.println(F("[SD] avís: massa fons al manifest (truncat)"));
            break;
        }
        strlcpy(gBgs.names[gBgs.count], v.as<const char*>(), SdAssets::kNameLen);
        ++gBgs.count;
    }

    Serial.printf("[SD/BG] %u fons  %ux%u  (%u B/fitxer)\n",
                  static_cast<unsigned>(gBgs.count),
                  static_cast<unsigned>(gBgs.width),
                  static_cast<unsigned>(gBgs.height),
                  static_cast<unsigned>(static_cast<uint32_t>(gBgs.width) * gBgs.height * 2));
    return gBgs.count > 0;
}

/* --- Lectura dels manifests de mascotes ----------------------------------- */

bool readPetManifest(const char* folder, SdAssets::Pet& p)
{
    char path[kPathLen];
    snprintf(path, sizeof(path), "/pets/%s/manifest.json", folder);
    File f = SD.open(path, FILE_READ);
    if (!f) {
        setError("Falta un /pets/<nom>/manifest.json");
        return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        setError("manifest de mascota: JSON invalid");
        return false;
    }

    strlcpy(p.folder, folder, SdAssets::kNameLen);

    /* Mides: el manifest "vell" les posa planes ("width"/"height"); el pack nou
     * les posa dins una llista ("size": [128, 128]). Acceptam les dues. */
    p.width  = doc["width"] | 0;
    p.height = doc["height"] | 0;
    if (p.width == 0 || p.height == 0) {
        JsonArray size = doc["size"].as<JsonArray>();
        if (size.size() >= 2) {
            p.width  = size[0] | 0;
            p.height = size[1] | 0;
        }
    }
    p.fps = doc["fps"] | 6;

    /* Color transparent: "transparent" (vell) o "transparent_color" (nou). */
    const char* transparent = doc["transparent"] | "";
    if (transparent == nullptr || transparent[0] == '\0') {
        transparent = doc["transparent_color"] | "";
    }
    p.hasTransparent = (transparent != nullptr && transparent[0] != '\0');
    p.transparent = p.hasTransparent
                        ? static_cast<uint16_t>(strtoul(transparent, nullptr, 0))
                        : 0;

    p.animCount = 0;
    JsonObject anims = doc["animations"].as<JsonObject>();
    if (!anims.isNull()) {
        /* Format vell: { "IDLE": 24, "HAPPY": 24, ... } */
        for (JsonPair kv : anims) {
            if (p.animCount >= SdAssets::kMaxAnims) {
                Serial.println(F("[SD] avís: massa animacions (truncat)"));
                break;
            }
            strlcpy(p.anims[p.animCount].name, kv.key().c_str(), SdAssets::kNameLen);
            p.anims[p.animCount].frames = kv.value().as<uint8_t>();
            ++p.animCount;
        }
    } else {
        /* Format nou: "states": ["IDLE", ...] + "frames_per_state": 24 */
        const uint8_t perState = doc["frames_per_state"] | 0;
        JsonArray states = doc["states"].as<JsonArray>();
        if (perState == 0 && states.size() > 0) {
            Serial.println(F("[SD] avís: manifest sense frames_per_state"));
        }
        for (JsonVariant v : states) {
            if (p.animCount >= SdAssets::kMaxAnims) {
                Serial.println(F("[SD] avís: massa estats (truncat)"));
                break;
            }
            strlcpy(p.anims[p.animCount].name, v.as<const char*>(), SdAssets::kNameLen);
            p.anims[p.animCount].frames = perState;
            ++p.animCount;
        }
    }

    return (p.width > 0) && (p.height > 0) && (p.animCount > 0);
}

bool readPets()
{
    File dir = SD.open("/pets");
    if (!dir || !dir.isDirectory()) {
        setError("Falta la carpeta /pets");
        return false;
    }

    gPetCount = 0;
    File entry;
    while ((entry = dir.openNextFile())) {
        if (entry.isDirectory() && gPetCount < SdAssets::kMaxPets) {
            SdAssets::Pet& p = gPets[gPetCount];
            if (readPetManifest(baseName(entry.name()), p)) {
                Serial.printf("[SD/PET] %s  %ux%u fps=%u  %u animacions  transparent=0x%04X\n",
                              p.folder,
                              static_cast<unsigned>(p.width),
                              static_cast<unsigned>(p.height),
                              static_cast<unsigned>(p.fps),
                              static_cast<unsigned>(p.animCount),
                              static_cast<unsigned>(p.transparent));
                ++gPetCount;
            }
        }
        entry.close();
    }
    dir.close();

    if (gPetCount == 0) {
        setError("Cap mascota valida dins /pets");
    }
    return gPetCount > 0;
}

/* --- Validacio de mides --------------------------------------------------- */

void validateSizes()
{
    char path[kPathLen];

    const uint32_t bgBytes = static_cast<uint32_t>(gBgs.width) * gBgs.height * 2u;
    gReport.expectedBgBytes = bgBytes;
    gReport.bgFilesOk = 0;
    gReport.bgFilesBad = 0;
    for (uint8_t i = 0; i < gBgs.count; ++i) {
        snprintf(path, sizeof(path), "/backgrounds/%s.bin", gBgs.names[i]);
        if (SdAssets::fileHasSize(path, bgBytes)) {
            ++gReport.bgFilesOk;
        } else {
            ++gReport.bgFilesBad;
            Serial.printf("[SD/BG] mida incorrecta: %s (esperat %u B)\n",
                          gBgs.names[i], static_cast<unsigned>(bgBytes));
        }
    }

    gReport.petFilesOk = 0;
    gReport.petFilesBad = 0;
    for (uint8_t pi = 0; pi < gPetCount; ++pi) {
        const SdAssets::Pet& p = gPets[pi];
        const uint32_t frameBytes = static_cast<uint32_t>(p.width) * p.height * 2u;
        for (uint8_t ai = 0; ai < p.animCount; ++ai) {
            for (uint8_t fr = 0; fr < p.anims[ai].frames; ++fr) {
                snprintf(path, sizeof(path), "/pets/%s/%s/%02u.bin",
                         p.folder, p.anims[ai].name, static_cast<unsigned>(fr));
                if (SdAssets::fileHasSize(path, frameBytes)) {
                    ++gReport.petFilesOk;
                } else {
                    ++gReport.petFilesBad;
                    Serial.printf("[SD/PET] mida incorrecta: %s (esperat %u B)\n",
                                  path, static_cast<unsigned>(frameBytes));
                }
            }
        }
    }

    gReport.sizesOk = (gReport.bgFilesBad == 0) && (gReport.petFilesBad == 0);
}

}  // namespace

namespace SdAssets {

bool begin()
{
    gReport = Report{};  /* reset */
    gPetCount = 0;

    if (!mountCard()) {
        gReport.mounted = false;
        return false;
    }

    gReport.mounted    = true;
    gReport.cardSizeMB = SD.cardSize() / (1024ULL * 1024ULL);
    gReport.usedMB     = SD.usedBytes() / (1024ULL * 1024ULL);

    gReport.bgsLoaded  = readBackgrounds();
    gReport.petsLoaded = readPets();

    if (gReport.bgsLoaded && gReport.petsLoaded) {
        validateSizes();
    }

    gReport.bgCount = gBgs.count;
    gReport.petCount = gPetCount;
    if (gPetCount > 0) {
        const Pet& p = gPets[0];
        gReport.petWidth          = p.width;
        gReport.petHeight         = p.height;
        gReport.petFps            = p.fps;
        gReport.animCount         = p.animCount;
        gReport.framesFirstAnim   = (p.animCount > 0) ? p.anims[0].frames : 0;
        gReport.expectedFrameBytes = static_cast<uint32_t>(p.width) * p.height * 2u;
    }

    Serial.printf("[SD] resum: fons %u (ok %u / bad %u) | mascotes %u | "
                  "frames ok %u / bad %u | %s\n",
                  static_cast<unsigned>(gReport.bgCount),
                  static_cast<unsigned>(gReport.bgFilesOk),
                  static_cast<unsigned>(gReport.bgFilesBad),
                  static_cast<unsigned>(gReport.petCount),
                  static_cast<unsigned>(gReport.petFilesOk),
                  static_cast<unsigned>(gReport.petFilesBad),
                  gReport.firstError[0] != '\0' ? gReport.firstError : "tot correcte");

    return gReport.mounted && gReport.bgsLoaded && gReport.petsLoaded;
}

bool isMounted()
{
    return gMounted;
}

const Report& report()
{
    return gReport;
}

const Backgrounds& backgrounds()
{
    return gBgs;
}

uint8_t petCount()
{
    return gPetCount;
}

const Pet& pet(uint8_t index)
{
    if (index >= gPetCount) {
        index = 0;
    }
    return gPets[index];
}

bool fileHasSize(const char* path, uint32_t expected)
{
    File f = SD.open(path, FILE_READ);
    if (!f) {
        return false;
    }
    const uint32_t size = static_cast<uint32_t>(f.size());
    f.close();
    return size == expected;
}

bool readPetFrame(uint8_t petIndex, const char* animName, uint8_t frame,
                  uint8_t* dst, size_t dstBytes, const char** error)
{
    if (petIndex >= gPetCount) {
        if (error != nullptr) { *error = "mascota no valida"; }
        return false;
    }
    const Pet& p = gPets[petIndex];

    char path[kPathLen];
    snprintf(path, sizeof(path), "/pets/%s/%s/%02u.bin",
             p.folder, animName, static_cast<unsigned>(frame));

    const uint32_t need = static_cast<uint32_t>(p.width) * p.height * 2u;
    if (dstBytes < need) {
        if (error != nullptr) { *error = "buffer massa petit"; }
        return false;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) {
        if (error != nullptr) { *error = "frame no trobat"; }
        return false;
    }
    if (static_cast<uint32_t>(f.size()) != need) {
        f.close();
        if (error != nullptr) { *error = "mida de frame incorrecta"; }
        return false;
    }

    size_t readTotal = 0;
    while (readTotal < need) {
        const size_t got = f.read(dst + readTotal, need - readTotal);
        if (got == 0) {
            break;
        }
        readTotal += got;
    }
    f.close();

    if (readTotal != need) {
        if (error != nullptr) { *error = "lectura incompleta"; }
        return false;
    }
    return true;
}

static void printDirRecursive(Print& out, File& dir, const char* path,
                              uint8_t depth, uint8_t maxDepth)
{
    File entry;
    while ((entry = dir.openNextFile())) {
        const char* base = baseName(entry.name());
        for (uint8_t i = 0; i < depth; ++i) {
            out.print("  ");
        }
        if (entry.isDirectory()) {
            out.printf("[%s/]\n", base);
            if (depth + 1 < maxDepth) {
                char sub[kPathLen];
                snprintf(sub, sizeof(sub), "%s/%s", path, base);
                File child = SD.open(sub);
                if (child) {
                    printDirRecursive(out, child, sub, static_cast<uint8_t>(depth + 1), maxDepth);
                    child.close();
                }
            }
        } else {
            out.printf("%s  %lu B\n", base, static_cast<unsigned long>(entry.size()));
        }
        entry.close();
    }
}

void printTree(Print& out, const char* path, uint8_t maxDepth)
{
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        out.printf("[SD] no es un directori: %s\n", path);
        return;
    }
    out.printf("--- arbre de %s ---\n", path);
    printDirRecursive(out, dir, path, 0, maxDepth);
    dir.close();
}

}  // namespace SdAssets



