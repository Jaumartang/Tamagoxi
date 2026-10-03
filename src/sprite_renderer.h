#pragma once

#include <stdint.h>

/*
 * sprite_renderer.h - Dibuix de la mascota (sprite 2x sobre el fons).
 *
 * Estrategia (Fase 3, streaming):
 *   - El rectangle de la mascota a pantalla es (2*spriteW) x (2*spriteH) (256x256
 *     per a un sprite de 128x128), centrat horitzontalment i comencant a
 *     PET_AREA_TOP.
 *   - A cada frame es repinta NOMES aquest rectangle: per franges, es llegeix de
 *     la SD la fila de fons corresponent, s'hi sobreposa el pixel de l'sprite
 *     ampliat 2x (vei mes proxim) saltant el color transparent, i s'envia la
 *     franja. Mai es fa fillScreen.
 *   - El frame actual de l'sprite es guarda a RAM (spriteW*spriteH*2). Les files
 *     de fons de la zona de la mascota es tornen a llegir de la SD a cada frame
 *     (mode STREAMING; mes endavant es pot afegir una cache si cal fps).
 *
 * Requereix: Display::init(), SdAssets::begin() i BgRenderer::begin() abans.
 */

namespace SpriteRenderer {

struct Status {
    bool     active;
    uint16_t spriteW;
    uint16_t spriteH;
    uint16_t boxW;         /* amplada a pantalla (2x) */
    uint16_t boxH;
    int16_t  x;
    int16_t  y;
    uint8_t  frameIndex;
    uint8_t  frameCount;
    char     anim[20];
    uint16_t fps;
    uint32_t lastFrameMs;
    uint32_t totalFrames;
    uint32_t totalMs;
    uint32_t minFrameMs;
    uint32_t maxFrameMs;
};

/* Reserva buffers i situa la mascota. Cal SdAssets::begin() abans. */
bool begin();
void end();
bool isActive();

/* Allibera els buffers grossos (sprite + franja + composicio, ~60 kB) per
 * deixar memoria a la pila Bluetooth, i els torna a reservar. Mentre esta
 * suspesa, la mascota no es dibuixa (isActive() retorna false). */
bool suspend();
bool resume();
bool suspended();

/* Fons sobre el qual es compondra la mascota (nom sense extensio). */
void setBackground(const char* name);
const char* backgroundName();

/* Escala entera de l'sprite (1..3). Recalcula la mida i recentra X. */
void setScale(uint8_t scale);
uint8_t scale();

/* Posicio (cantó superior-esquerre) i centrat horitzontal. */
void setPosition(int16_t x, int16_t y);
void centerX();

/* Hook cridat entre franges mentre es dibuixa un frame (per mostrejar el
 * tactil tot i que el bucle estigui ocupat). Passa nullptr per treure'l. */
void setBandHook(void (*hook)());

/* Torna a la disposicio per defecte de config.h (mida i posicio). */
void resetLayout();

/* Mida actual del rectangle de la mascota a pantalla. */
uint16_t boxWidth();
uint16_t boxHeight();

/* Canvia d'animacio amb fallback (nom -> IDLE -> primera del manifest). */
bool setAnimation(const char* name);
/* Seguent animacio del manifest (circular). Retorna el nom nou. */
const char* nextAnimation();
const char* animationName();

/* Redibuixa el rectangle de la mascota amb el frame actual. Retorna els ms. */
uint32_t drawFrame();

/* Si ha passat el periode del fps, avanca el frame i el redibuixa. */
uint32_t update(uint32_t nowMs);

const Status& status();

}  // namespace SpriteRenderer
