#pragma once

#include <stdint.h>

/*
 * pet.h - Estat de la mascota (Fase 5): necessitats, animacio d'estat i
 * persistencia a la NVS (sobreviu reinicis).
 *
 * Els valors interns son floats (0..100) perque el decaiment es gradual; cap
 * enfora es donen arrodonits.
 */

namespace Pet {

/* Anim d'estat segons com esta la mascota. */
enum class Mood : uint8_t { Idle, Hungry, Sad, Sick, Sleeping };

struct Needs {
    uint8_t food;       /* satietat (100 = tip, 0 = mort de gana) */
    uint8_t happiness;
    uint8_t energy;
    uint8_t health;
};

void begin();                   /* carrega de la NVS */
void update(uint32_t nowMs);    /* fa passar el temps i actualitza l'estat */
void save();                    /* desa a la NVS */

/* Amb el NTP (Fase 6): permet aplicar el decaiment del temps que ha estat
 * apagat. Si no s'informa, no s'aplica. */
void setEpoch(uint32_t epochSeconds);

const Needs& needs();
Mood mood();
bool sleeping();
const char* animation();        /* animacio a mostrar ara (temporal o d'estat) */

/* Accions del menu. Retornen false si l'accio no te sentit ara mateix. */
bool feed();
bool play();
bool toggleSleep();
bool heal();
bool pet();

void printStatus();             /* volcat per consola */

/* Nomes per proves: forca les necessitats (per veure les animacions d'estat). */
void debugSet(uint8_t food, uint8_t happiness, uint8_t energy, uint8_t health);

}  // namespace Pet
