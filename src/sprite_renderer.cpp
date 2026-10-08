#include "sprite_renderer.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

#include "tg_config.h"
#include "display.h"
#include "sd_assets.h"
#include "storage.h"

namespace {

uint8_t* gSpriteFrame = nullptr;   /* spriteW * spriteH * 2 */
uint8_t* gBgBand      = nullptr;   /* SCREEN_W * BG_BAND_LINES * 2 */
uint8_t* gComp        = nullptr;   /* boxW * BG_BAND_LINES * 2 */

SpriteRenderer::Status gStatus;
bool     gSuspended   = false;
int8_t   gAnimIndex   = -1;
uint8_t  gFrame       = 0;
int8_t   gLoadedAnim  = -1;
int8_t   gLoadedFrame = -1;
uint32_t gFrameTimer  = 0;
char     gBgName[24]  = {0};
constexpr uint8_t kMinScale = 1;
constexpr uint8_t kMaxScale = 3;
/* Marge de moviment de la mascota (en pixels d'sprite): el rectangle a pantalla
 * s'engrandeix una mica perque el drac pugui saltar/botar sense sortir-se'n ni
 * quedar tallat. El fons del rectangle es repinta a cada frame, aixi que el
 * moviment no deixa rastre ni parpelleig. */
constexpr int8_t  kMoveMaxX = 16;
constexpr int8_t  kMoveMaxY = 16;
constexpr int16_t kPadX     = kMoveMaxX * 2;   /* marge total horitzontal */
constexpr int16_t kPadY     = kMoveMaxY;       /* marge vertical (cap amunt) */

uint8_t  gScale       = PET_SCALE;
uint8_t  gMaxScale    = kMaxScale;   /* maxima escala que cap a la pantalla */
void   (*gBandHook)() = nullptr;     /* mostreig del tactil entre franges */

/* Moviment de la mascota (en pixels d'sprite) i com es mou segons l'estat. */
int8_t   gMoveX = 0;
int8_t   gMoveY = 0;
bool     gFlip  = false;             /* mirall: mira cap a l'esquerra */
enum class Act : uint8_t { Still, Patrol, Bounce };
Act      gAct        = Act::Still;
uint32_t gActUntil   = 0;
uint32_t gBobUntil   = 0;
uint32_t gMoveT0     = 0;
bool     gPatrolNext = false;
uint32_t gMoveTimer  = 0;
constexpr uint32_t kMoveStepMs = 60;   /* refresc del moviment (~16 fps) */

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
    gStatus.boxW = static_cast<uint16_t>((pet.width + kPadX) * gScale);
    gStatus.boxH = static_cast<uint16_t>((pet.height + kPadY) * gScale);
    gStatus.x = static_cast<int16_t>((SCREEN_W - gStatus.boxW) / 2 + PET_OFFSET_X);
    gStatus.y = static_cast<int16_t>(PET_AREA_TOP - kPadY * gScale);
    gStatus.fps = pet.fps ? pet.fps : 6;
    gStatus.frameCount = pet.anims[0].frames;

    const size_t spriteBytes = static_cast<size_t>(pet.width) * pet.height * 2u;
    const size_t bgBytes = static_cast<size_t>(SCREEN_W) * BG_BAND_LINES * 2u;
    /* Buffer de composicio dels valors maxes (escala maxima) per no reallocar. */
    const size_t compBytes = static_cast<size_t>(pet.width + kPadX) * gMaxScale
                             * BG_BAND_LINES * 2u;

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
    gStatus.boxW = static_cast<uint16_t>((gStatus.spriteW + kPadX) * s);
    gStatus.boxH = static_cast<uint16_t>((gStatus.spriteH + kPadY) * s);
    setPosition(static_cast<int16_t>((SCREEN_W - gStatus.boxW) / 2 + PET_OFFSET_X),
                static_cast<int16_t>(PET_AREA_TOP - kPadY * s));
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
                static_cast<int16_t>(PET_AREA_TOP - kPadY * gScale));
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

/* Allibera els buffers grans i deixa la mascota aturada. Els torna a reservar
 * amb resume(). Serveix per fer lloc a la pila Bluetooth (Fase 7). */
bool suspend()
{
    if (gSuspended) {
        return true;
    }
    const size_t spriteBytes = static_cast<size_t>(gStatus.spriteW) * gStatus.spriteH * 2u;
    const size_t bgBytes     = static_cast<size_t>(SCREEN_W) * BG_BAND_LINES * 2u;
    const size_t compBytes   = static_cast<size_t>(gStatus.spriteW + kPadX) * gMaxScale
                               * BG_BAND_LINES * 2u;
    freeBuffers();
    gSuspended    = true;
    gStatus.active = false;
    gLoadedAnim   = -1;
    gLoadedFrame  = -1;
    Serial.printf("[PET] mascota suspesa: %u B alliberats (sprite %u + franges %u)\n",
                  static_cast<unsigned>(spriteBytes + bgBytes + compBytes),
                  static_cast<unsigned>(spriteBytes),
                  static_cast<unsigned>(bgBytes + compBytes));
    return true;
}

bool resume()
{
    if (!gSuspended) {
        return true;
    }
    gSuspended = false;
    gStatus.active = false;      /* forcem que begin() torni a reservar */
    if (begin()) {
        return true;
    }
    /* No hi ha hagut prou memoria contigua: tornam a l'estat suspes perque el
     * cridador ho pugui tornar a provar mes endavant. */
    gSuspended = true;
    return false;
}

bool suspended()
{
    return gSuspended;
}

/* Segons l'estat de la mascota decideix com es mou: quiet, botet o volta.
 * Retorna true si el desplacament ha canviat (cal repintar el rectangle). */
bool stepBehaviour(uint32_t now)
{
    const SdAssets::Pet& pet = SdAssets::pet(0);
    const char* name = (gAnimIndex >= 0 && gAnimIndex < static_cast<int8_t>(pet.animCount))
                       ? pet.anims[gAnimIndex].name : "";

    Act want = Act::Patrol;
    if (ieq(name, "SAD") || ieq(name, "SLEEP") || ieq(name, "SICK") ||
        ieq(name, "HUNGRY") || ieq(name, "ANGRY") || ieq(name, "EAT") ||
        ieq(name, "DRINK")) {
        want = Act::Still;              /* trist, dormit, malalt...: quiet ✓ */
    } else if (ieq(name, "HAPPY") || ieq(name, "PLAY") || ieq(name, "LOVE") ||
               ieq(name, "CELEBRATE") || ieq(name, "CURIOUS")) {
        want = Act::Bounce;             /* content: botet al lloc ✓ */
    }
    if (want != gAct) {
        gAct = want;
        gMoveT0 = now;
        gActUntil = now + 1500;
        gPatrolNext = true;
        gBobUntil = now + 1200;
    }

    const int8_t px = gMoveX;
    const int8_t py = gMoveY;
    const bool   pf = gFlip;
    gMoveX = 0;
    gMoveY = 0;
    gFlip  = false;

    if (gAct == Act::Bounce) {
        /* Botet al lloc: puja i baixa sense moure's de costat ✓ */
        const uint32_t half = 700u;
        const uint32_t ph = (now - gMoveT0) % (2u * half);
        const uint32_t up = (ph < half) ? ph : (2u * half - ph);
        gMoveY = -static_cast<int8_t>((static_cast<uint32_t>(kMoveMaxY) * up) / (2u * half));
    } else if (gAct == Act::Patrol) {
        /* Pausa, salt a la dreta, pausa, tornada, salt a l'esquerra (AMB MIRALL),
         * pausa i tornada. Amb pauses llargues i aleatories entre voltes. */
        if (now >= gActUntil) {
            if (gPatrolNext) {
                gMoveT0 = now;
                gActUntil = now + 6600;
                gPatrolNext = false;
            } else {
                gActUntil = now + 3000u + static_cast<uint32_t>(random(0, 6500));
                gPatrolNext = true;
            }
        }
        const uint32_t p = (now - gMoveT0) % 6600u;
        int  dx = 0;
        uint32_t hopStart = 0;
        uint32_t hopLen = 0;
        if (p < 1200) {                                 /* pausa al centre */
            gFlip = false;                              /* mira cap a la dreta */
        } else if (p < 2400) {                          /* cap a la dreta */
            dx = (kMoveMaxX * static_cast<int>(p - 1200)) / 1200;
            hopStart = 1200;
            hopLen = 1200;
        } else if (p < 3200) {                          /* pausa a la dreta */
            dx = kMoveMaxX;
            gFlip = true;                               /* gira i mira a l'esquerra */
        } else if (p < 4400) {                          /* tornada al centre */
            dx = kMoveMaxX - (kMoveMaxX * static_cast<int>(p - 3200)) / 1200;
            hopStart = 3200;
            hopLen = 1200;
            gFlip = true;                               /* MIRALL ✓ */
        } else if (p < 5400) {                          /* cap a l'esquerra */
            dx = -(kMoveMaxX * static_cast<int>(p - 4400)) / 1000;
            hopStart = 4400;
            hopLen = 1000;
            gFlip = true;                               /* MIRALL ✓ */
        } else if (p < 6200) {                          /* pausa a l'esquerra */
            dx = -kMoveMaxX;
            gFlip = false;                              /* gira i mira a la dreta */
        } else {                                        /* tornada al centre */
            dx = -kMoveMaxX + (kMoveMaxX * static_cast<int>(p - 6200)) / 400;
            hopStart = 6200;
            hopLen = 400;
        }
        gMoveX = static_cast<int8_t>(dx);
        if (hopLen > 0) {                               /* el botet */
            const float t = static_cast<float>(p - hopStart) / static_cast<float>(hopLen);
            gMoveY = -static_cast<int8_t>(kMoveMaxY * sinf(t * 3.14159265f));
        }
    }
    return (px != gMoveX) || (py != gMoveY) || (pf != gFlip);
}

uint32_t drawFrame()
{
    if (!gStatus.active || gSuspended || gSpriteFrame == nullptr || gAnimIndex < 0) {
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
    /* El fons tambe es pot deduir de les cantonades del propi frame, aixi cada
     * estat pot tenir el seu color de fons encara que el manifest no ho digui. */
    uint16_t frameBack = 0;
    const bool hasFrameBack = SdAssets::frameKey(
        reinterpret_cast<const uint16_t*>(gSpriteFrame), pet.width, pet.height,
        frameBack);

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
            const uint16_t* bgRow = bgWords + static_cast<size_t>(r) * bgW + bx;
            uint16_t* outRowPtr = comp + static_cast<size_t>(r) * boxW;

            memcpy(outRowPtr, bgRow, static_cast<size_t>(boxW) * 2u);

            /* Fila de l'sprite que toca (tenint en compte el desplacament). */
            const int16_t srow = (static_cast<int16_t>(outRow)
                                  - (kPadY + gMoveY) * N) / N;
            if (srow < 0 || srow >= static_cast<int16_t>(pet.height)) {
                continue;                       /* aquesta fila es nomes fons */
            }
            const uint16_t* sRowPtr = reinterpret_cast<const uint16_t*>(gSpriteFrame)
                                      + static_cast<size_t>(srow) * SW;
            /* Columna on comenca el drac dins el rectangle (marge + desplacament). */
            const int16_t xoff = (kMoveMaxX + gMoveX) * N;

            for (uint16_t sx = 0; sx < SW; ++sx) {
                /* Amb mirall llegim la columna al reves ✓ */
                const uint16_t sv = sRowPtr[gFlip ? (SW - 1 - sx) : sx];
                if ((hasFrameBack && sv == frameBack) ||
                    (pet.hasTransparent && sv == transparent)) {
                    continue;
                }
                uint16_t* dst = outRowPtr + xoff + static_cast<size_t>(sx) * N;
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
    /* El moviment es recalcula com a molt cada kMoveStepMs (per no ofegar res). */
    bool moved = false;
    if (gMoveTimer == 0 || (nowMs - gMoveTimer) >= kMoveStepMs) {
        gMoveTimer = nowMs;
        moved = stepBehaviour(nowMs);
    }

    const uint32_t interval = gStatus.fps ? (1000u / gStatus.fps) : 167u;
    const bool frameDue = (gFrameTimer == 0) || ((nowMs - gFrameTimer) >= interval);
    if (!moved && !frameDue) {
        return 0;
    }
    const uint32_t ms = drawFrame();
    if (frameDue) {
        gFrameTimer = nowMs;
        gStatus.frameIndex = gFrame;
        gFrame = static_cast<uint8_t>((gFrame + 1) % gStatus.frameCount);
    }
    return ms;
}

const Status& status()
{
    return gStatus;
}

}  // namespace SpriteRenderer

