#pragma once

#include <stdint.h>

/*
 * games.h - Jocs didactics per a la Noa (Fase 10).
 *
 * Es un sistema ESCALABLE: cada joc es una entrada de la taula kGames amb tres
 * funcions (comencar, bucle i tocs). Per afegir-ne un de nou, nomes cal escriure
 * les tres funcions i afegir una linia a la taula (vegeu games.cpp).
 *
 * Tots els jocs comparteixen:
 *   - Tocs GRANS (pensats per a un dit de 6 anys).
 *   - Reforc immediat: verd si encerta, vermell si no, i un soet.
 *   - Recompenses: cada encert dona una ESTRELLA que es desa a la NVS i fa
 *     mes feliç el drac; cada 10 estrelles surt una celebracio.
 *   - Cap penalitzacio: si s'equivoca, es pot tornar a provar sense por.
 *
 * Requereix: Display, Ui (colors) i Audio (sons). Es fa servir des del panell
 * "Jocs" del menu de dalt.
 */

namespace Games {

/* Un joc del sistema. Per afegir-ne un de nou NOMES cal una funcio que ompli la
 * pregunta (vegeu games.cpp) i una linia mes a la taula. */
struct Definition {
    const char* name;        /* nom que surt al menu (curt!) */
    const char* symbol;      /* que dibuixem a la icona (p.ex. "+", "A", "abc") */
    void (*generate)();      /* omple la pregunta que es pregunta ara */
};

void begin();                 /* carrega estrelles i encerts de la NVS */

/* --- Menu de jocs (el pinta el panell de Jocs) ---------------------------- */
uint8_t     count();
const Definition& game(uint8_t index);
void        open(uint8_t index);    /* comenca el joc N (pinta la pantalla) */
void        close();                /* torna al menu de jocs */
bool        isOpen();
uint8_t     current();

/* --- Bucle i tocs (els crida el panell de Jocs) --------------------------- */
void loop(uint32_t nowMs);
bool handleTap(int16_t x, int16_t y);

/* --- Recompenses ---------------------------------------------------------- */
uint16_t stars();                   /* estrelles guanyades en total */
uint16_t starsThisSession();
uint8_t  rightOf(uint8_t index);    /* encerts del joc N */
uint16_t level();                   /* nivell del drac (per estrelles) */
void     resetProgress();           /* comenca de zero */

void printStatus();

}  // namespace Games
