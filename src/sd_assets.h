#pragma once

#include <Print.h>
#include <stdint.h>

/*
 * sd_assets.h - Cataleg d'actius de la microSD (/backgrounds i /pets).
 *
 * Munta la targeta al seu bus VSPI dedicat (SCK 18 / MISO 19 / MOSI 23 / CS 5),
 * independent del HSPI del LCD i el tactil, i llegeix NOMES els manifests (mai
 * cap pixel: els .bin es llegeixen mes endavant, a la Fase 2/3).
 *
 * Descobreix els noms de fitxers i el nombre de frames des dels manifests, aixi
 * que afegir fons o mascotes noves no requereix tocar el firmware.
 *
 * Tot son estructures de mida fixa (sense String dinamic) per no fragmentar
 * l'heap. Cap operacio bloqueja mes d'uns quants ms i, si no hi ha targeta,
 * retorna false perque el cridador mostri la pantalla d'error.
 */

namespace SdAssets {

constexpr uint8_t kMaxPets        = 8;
constexpr uint8_t kMaxBackgrounds = 48;
constexpr uint8_t kMaxAnims       = 12;
constexpr size_t  kNameLen        = 20;

struct Anim {
    char    name[kNameLen];
    uint8_t frames;
};

struct Pet {
    char     folder[kNameLen];
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    bool     hasTransparent;
    uint16_t transparent;
    uint8_t  animCount;
    Anim     anims[kMaxAnims];
};

struct Backgrounds {
    uint16_t width;
    uint16_t height;
    uint8_t  count;
    char     names[kMaxBackgrounds][kNameLen];
};

struct Report {
    bool     mounted;
    bool     bgsLoaded;         /* manifest de fons llegit */
    bool     petsLoaded;        /* almenys una mascota llegida */
    bool     sizesOk;           /* totes les mides de fitxer correctes */
    uint8_t  bgCount;
    uint32_t expectedBgBytes;
    uint8_t  bgFilesOk;
    uint8_t  bgFilesBad;
    uint8_t  petCount;
    uint16_t petWidth;
    uint16_t petHeight;
    uint16_t petFps;
    uint8_t  animCount;
    uint8_t  framesFirstAnim;
    uint32_t expectedFrameBytes;
    uint8_t  petFilesOk;
    uint8_t  petFilesBad;
    uint64_t cardSizeMB;
    uint64_t usedMB;
    char     firstError[64];    /* "" si tot be */
};

/* Muntatge amb reintents + escaneig dels manifests + validacio de mides.
 * Retorna true si la SD esta muntada i els manifests principals s'han llegit. */
bool begin();

bool isMounted();

const Report& report();
const Backgrounds& backgrounds();
uint8_t petCount();
const Pet& pet(uint8_t index);

/* Comprova que el fitxer te exactament 'expected' bytes. */
bool fileHasSize(const char* path, uint32_t expected);

/* Llegeix el frame complet 'frame' de l'animacio 'animName' de la mascota
 * 'petIndex' a 'dst' (ha de tenir width*height*2 bytes). Omple 'error' si falla. */
bool readPetFrame(uint8_t petIndex, const char* animName, uint8_t frame,
                  uint8_t* dst, size_t dstBytes, const char** error);

/* Escriu l'arbre de fitxers (recursiu fins a maxDepth) al stream indicat. */
void printTree(Print& out, const char* path, uint8_t maxDepth);

}  // namespace SdAssets
