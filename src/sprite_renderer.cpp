#include "sprite_renderer.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "display.h"
#include "sd_assets.h"
#include "storage.h"

namespace {

uint8_t* gSpriteFrame = nullptr;   /* spriteW * spriteH * 2 */
uint8_t* gBgBand      = nullptr;   /* SCREEN_W * BG_BAND_LINES * 2 */
uint8_t* gComp        = nullptr;   /* boxW * BG_BAND_LINES * 2 */

SpriteRenderer::Status gStatus;
int8_t   gAnimIndex   = -1;
uint8_t  gFrame       = 0;
int8_t   gLoadedAnim  = -1;
int8_t   gLoadedFrame = -1;
uint32_t gFrameTimer  = 0;
char     gBgName[24]  = {0};
constexpr uint8_t kMinScale = 1;
constexpr uint8_t kMaxScale = 3;

uint8_t  gScale       = PET_SCALE;
uint8_t  gMaxScale    = kMaxScale;   /* maxima escala que cap a la pantalla */
void   (*gBandHook)() = nullptr;     /* mostreig del tactil entre franges */

bool ieq(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') { ca = static_cast<char>(ca + 32); }
        if (cb >= 'A' && cb <= 'Z') { cb = static_cast<char>(cb + 32); }
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

void freeBuffers()
{
    heap_caps_free(gSpriteFrame);
    heap_caps_free(gBgBand);
    heap_caps_free(gComp);
    gSpriteFrame = nullptr;
    gBgBand = nullptr;
    gComp = nullptr;
}

}  // namespace

namespace SpriteRenderer {

bool begin()
{
    if (gStatus.active) {
        return true;
    }
    const SdAssets::Pet& pet = SdAssets::pet(0);
    if (pet.width == 0 || pet.height == 0 || pet.animCount == 0) {
        Serial.println(F("[PET] manifest de mascota no valid"));
        return false;
    }

    gStatus.spriteW = pet.width;
    gStatus.spriteH = pet.height;
    /* Escala maxima que cap a la pantalla: evita desbordar la finestra i els
     * buffers (p.ex. un sprite de 128 no pot anar a escala 3 en una pantalla de
     * 320 d'ample). */
    const uint8_t fitW = static_cast<uint8_t>(SCREEN_W / pet.width);
    const uint8_t fitH = static_cast<uint8_t>(SCREEN_H / pet.height);
    gMaxScale = (fitW < fitH) ? fitW : fitH;
    if (gMaxScale < kMinScale) { gMaxScale = kMinScale; }
    if (gMaxScale > kMaxScale) { gMaxScale = kMaxScale; }

    gScale = (PET_SCALE < kMinScale) ? kMinScale
                                     : ((PET_SCALE > gMaxScale) ? gMaxScale : PET_SCALE);
    gStatus.boxW = static_cast<uint16_t>(pet.width * gScale);
    gStatus.boxH = static_cast<uint16_t>(pet.height * gScale);
    gStatus.x = static_cast<int16_t>((SCREEN_W - gStatus.boxW) / 2 + PET_OFFSET_X);
    gStatus.y = static_cast<int16_t>(PET_AREA_TOP);
    gStatus.fps = pet.fps ? pet.fps : 6;
    gStatus.frameCount = pet.anims[0].frames;

    const size_t spriteBytes = static_cast<size_t>(pet.width) * pet.height * 2u;
    const size_t bgBytes = static_cast<size_t>(SCREEN_W) * BG_BAND_LINES * 2u;
    /* Buffer de composicio dels valors maxes (escala maxima) per no reallocar. */
    const size_t compBytes = static_cast<size_t>(pet.width) * gMaxScale * BG_BAND_LINES * 2u;

    gSpriteFrame = static_cast<uint8_t*>(heap_caps_malloc(spriteBytes, MALLOC_CAP_DMA));
    gBgBand = static_cast<uint8_t*>(heap_caps_malloc(bgBytes, MALLOC_CAP_DMA));
    gComp = static_cast<uint8_t*>(heap_caps_malloc(compBytes, MALLOC_CAP_DMA));
    if (gSpriteFrame == nullptr || gBgBand == nullptr || gComp == nullptr) {
        Serial.printf("[PET] sense memoria (sprite %u, bg %u, comp %u)\n",
                      static_cast<unsigned>(spriteBytes),
                      static_cast<unsigned>(bgBytes),
                      static_cast<unsigned>(compBytes));
        freeBuffers();
        return false;
    }

    gStatus.active = true;
    Serial.printf("[PET] sprite %ux%u -> %ux%u a (%d,%d), fps %u, %u animacions\n",
                  static_cast<unsigned>(pet.width), static_cast<unsigned>(pet.height),
                  static_cast<unsigned>(gStatus.boxW), static_cast<unsigned>(gStatus.boxH),
                  gStatus.x, gStatus.y,
                  static_cast<unsigned>(gStatus.fps),
                  static_cast<unsigned>(pet.animCount));

    /* Si hi ha una disposicio desada a la NVS, mana sobre config.h. */
    const Storage::PetLayout saved = Storage::loadPetLayout();
    if (saved.valid) {
        setScale(saved.scale);
        setPosition(saved.x, saved.y);
        Serial.printf("[PET] disposicio carregada de la NVS: x%u a (%d,%d)\n",
                      static_cast<unsigned>(gScale), gStatus.x, gStatus.y);
    }

    setAnimation("IDLE");
    return true;
}

void end()
{
    freeBuffers();
    gStatus = Status{};
}

bool isActive()
{
    return gStatus.active;
}

void setBackground(const char* name)
{
    strlcpy(gBgName, name, sizeof(gBgName));
}

const char* backgroundName()
{
    return gBgName;
}

void setBandHook(void (*hook)())
{
    gBandHook = hook;
}

void setScale(uint8_t s)
{
    if (s < kMinScale) { s = kMinScale; }
    if (s > gMaxScale) { s = gMaxScale; }
    gScale = s;
    if (!gStatus.active) { return; }
    gStatus.boxW = static_cast<uint16_t>(gStatus.spriteW * s);
    gStatus.boxH = static_cast<uint16_t>(gStatus.spriteH * s);
    setPosition(static_cast<int16_t>((SCREEN_W - gStatus.boxW) / 2 + PET_OFFSET_X), gStatus.y);
}

uint8_t scale()
{
    return gScale;
}

void setPosition(int16_t x, int16_t y)
{
    const int16_t maxX = static_cast<int16_t>(SCREEN_W - gStatus.boxW);
    const int16_t maxY = static_cast<int16_t>(SCREEN_H - gStatus.boxH);
    gStatus.x = (x < 0) ? 0 : ((x > maxX) ? maxX : x);
    gStatus.y = (y < 0) ? 0 : ((y > maxY) ? maxY : y);
}

void centerX()
{
    gStatus.x = static_cast<int16_t>((SCREEN_W - gStatus.boxW) / 2 + PET_OFFSET_X);
}

void resetLayout()
{
    if (!gStatus.active) {
        return;
    }
    setScale(PET_SCALE);
    setPosition(static_cast<int16_t>((SCREEN_W - gStatus.boxW) / 2 + PET_OFFSET_X),
                static_cast<int16_t>(PET_AREA_TOP));
}

uint16_t boxWidth()
{
    return gStatus.boxW;
}

uint16_t boxHeight()
{
    return gStatus.boxH;
}

bool setAnimation(const char* name)
{
    if (!gStatus.active) {
        return false;
    }
    const SdAssets::Pet& pet = SdAssets::pet(0);

    int8_t idx = -1;
    for (uint8_t i = 0; i < pet.animCount; ++i) {
        if (ieq(pet.anims[i].name, name)) {
            idx = static_cast<int8_t>(i);
            break;
        }
    }
    const bool exact = (idx >= 0);
    if (idx < 0) {
        for (uint8_t i = 0; i < pet.animCount; ++i) {
            if (ieq(pet.anims[i].name, "IDLE")) {
                idx = static_cast<int8_t>(i);
                break;
            }
        }
    }
    if (idx < 0) {
        idx = 0;
    }

    gAnimIndex = idx;
    gFrame = 0;
    gLoadedAnim = -1;  /* forca recarregar el frame de la SD */
    gStatus.frameCount = pet.anims[idx].frames;
    gStatus.frameIndex = 0;
    strlcpy(gStatus.anim, pet.anims[idx].name, sizeof(gStatus.anim));
    gFrameTimer = 0;

    if (!exact) {
        Serial.printf("[PET] animacio '%s' desconeguda; uso '%s'\n", name, gStatus.anim);
    }
    return exact;
}

const char* nextAnimation()
{
    if (!gStatus.active) {
        return "";
    }
    const SdAssets::Pet& pet = SdAssets::pet(0);
    const int8_t nxt = static_cast<int8_t>((gAnimIndex + 1) % pet.animCount);
    setAnimation(pet.anims[nxt].name);
    return gStatus.anim;
}

const char* animationName()
{
    return gStatus.anim;
}

uint32_t drawFrame()
{
    if (!gStatus.active || gAnimIndex < 0) {
        return 0;
    }
    const SdAssets::Pet& pet = SdAssets::pet(0);

    char path[64];
    snprintf(path, sizeof(path), "/backgrounds/%s.bin", gBgName);
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[PET] no s'ha pogut obrir %s\n", path);
        return 0;
    }

    /* Carrega el frame de l'sprite si ha canviat. */
    if ((gLoadedAnim != gAnimIndex) || (gLoadedFrame != static_cast<int8_t>(gFrame))) {
        const char* err = nullptr;
        const size_t spriteBytes = static_cast<size_t>(pet.width) * pet.height * 2u;
        if (!SdAssets::readPetFrame(0, pet.anims[gAnimIndex].name, gFrame,
                                    gSpriteFrame, spriteBytes, &err)) {
            Serial.printf("[PET] frame %u de %s no llegit: %s\n",
                          static_cast<unsigned>(gFrame), pet.anims[gAnimIndex].name,
                          err != nullptr ? err : "?");
            f.close();
            return 0;
        }
        gLoadedAnim = gAnimIndex;
        gLoadedFrame = static_cast<int8_t>(gFrame);
    }

    const uint16_t SW = pet.width;
    const uint16_t boxW = gStatus.boxW;
    const uint16_t boxH = gStatus.boxH;
    const uint16_t bgW = SCREEN_W;
    const uint32_t bgRowBytes = static_cast<uint32_t>(bgW) * 2u;
    const uint16_t bx = static_cast<uint16_t>(gStatus.x);  /* offset x dins el fons */
    const uint8_t  N = gScale;                             /* escala entera */
    /* Els .bin son big-endian: en memoria (little-endian) el pixel transparent
     * 0xF81F es llegeix com 0x1FF8. Per aixo comparem amb el valor byte-swapat. */
    const uint16_t transparent = pet.hasTransparent
        ? static_cast<uint16_t>((pet.transparent >> 8) | (pet.transparent << 8))
        : 0u;

    TFT_eSPI& t = Display::driver();
    const uint32_t t0 = millis();

    t.startWrite();
    t.setAddrWindow(gStatus.x, gStatus.y, boxW, boxH);

    /* Les franges son consecutives dins el rectangle: llegim el fons d'un tros. */
    if (!f.seek(static_cast<uint32_t>(gStatus.y) * bgRowBytes)) {
        t.endWrite();
        f.close();
        return 0;
    }

    bool ok = true;
    for (uint16_t oy = 0; oy < boxH; oy += BG_BAND_LINES) {
        uint16_t lines = BG_BAND_LINES;
        if (static_cast<uint32_t>(oy) + lines > boxH) {
            lines = static_cast<uint16_t>(boxH - oy);
        }

        const size_t need = static_cast<size_t>(bgW) * lines * 2u;
        if (f.read(gBgBand, need) != need) {
            ok = false;
            break;
        }

        const uint16_t* bgWords = reinterpret_cast<const uint16_t*>(gBgBand);
        uint16_t* comp = reinterpret_cast<uint16_t*>(gComp);
        for (uint16_t r = 0; r < lines; ++r) {
            const uint16_t outRow = static_cast<uint16_t>(oy + r);
            const uint16_t sRow = static_cast<uint16_t>(outRow / N);  /* vei mes proxim */
            const uint16_t* sRowPtr = reinterpret_cast<const uint16_t*>(gSpriteFrame)
                                      + static_cast<size_t>(sRow) * SW;
            const uint16_t* bgRow = bgWords + static_cast<size_t>(r) * bgW + bx;
            uint16_t* outRowPtr = comp + static_cast<size_t>(r) * boxW;

            memcpy(outRowPtr, bgRow, static_cast<size_t>(boxW) * 2u);

            for (uint16_t sx = 0; sx < SW; ++sx) {
                const uint16_t sv = sRowPtr[sx];
                if (pet.hasTransparent && sv == transparent) {
                    continue;
                }
                uint16_t* dst = outRowPtr + static_cast<size_t>(sx) * N;
                for (uint8_t k = 0; k < N; ++k) {
                    dst[k] = sv;
                }
            }
        }
        t.pushPixels(reinterpret_cast<uint16_t*>(gComp), static_cast<uint32_t>(boxW) * lines);

        /* Aprofitem entre franges per mostrejar el tactil (el bucle esta ocupat). */
        if (gBandHook != nullptr) {
            gBandHook();
        }
    }

    t.endWrite();
    f.close();

    const uint32_t ms = millis() - t0;
    gStatus.lastFrameMs = ms;
    if (!ok) {
        Serial.println(F("[PET] lectura de fons incompleta"));
        return ms;
    }
    if (gStatus.minFrameMs == 0 || ms < gStatus.minFrameMs) {
        gStatus.minFrameMs = ms;
    }
    if (ms > gStatus.maxFrameMs) {
        gStatus.maxFrameMs = ms;
    }
    ++gStatus.totalFrames;
    gStatus.totalMs += ms;
    return ms;
}

uint32_t update(uint32_t nowMs)
{
    if (!gStatus.active || gAnimIndex < 0) {
        return 0;
    }
    const uint32_t interval = gStatus.fps ? (1000u / gStatus.fps) : 167u;
    if (gFrameTimer != 0 && (nowMs - gFrameTimer) < interval) {
        return 0;
    }
    gFrameTimer = nowMs;
    const uint32_t ms = drawFrame();
    gStatus.frameIndex = gFrame;
    gFrame = static_cast<uint8_t>((gFrame + 1) % gStatus.frameCount);
    return ms;
}

const Status& status()
{
    return gStatus;
}

}  // namespace SpriteRenderer

