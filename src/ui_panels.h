#pragma once

#include <stdint.h>

/*
 * ui_panels.h - Menus i panells modals (Fase 6.5).
 *
 *   TopMenu  - desplegable que baixa de la barra de dalt (boto de ratlles del
 *              HUD): WiFi · Musica · Bluetooth · Jocs · Ajustos · Sobre.
 *   Wifi     - panell a pantalla completa: estat de la connexio, llista de
 *              xarxes trobades (senyal, cadenat) i teclat en pantalla per
 *              escriure la contrasenya.
 *   Bluetooth- encendre el so pels auriculars (emissor) o fer d'altaveu, i
 *              apagar-lo. Tambe es pot triar si el so surt per la placa.
 *   Settings - tema, fons i brillantor.
 *   About    - informacio del sistema.
 *   Games    - jocs (encara per fer).
 *
 * Mentre hi ha un panell obert, la mascota queda congelada (no s'anima) perque
 * res no repinti a sobre del panell. En tancar, el cridador restaura la
 * pantalla (vegeu closePanel() a main.cpp).
 */

namespace Panels {

enum class Id : uint8_t { None, TopMenu, Wifi, Music, Bluetooth, Messages, Settings, About, Games, Message };

/* Finestreta d'avis (notificacio): un titol i un text que surten sols. */
void setNotice(const char* title, const char* text);

/* Callbacks que main registra perque els panells puguin aplicar canvis. */
struct Hooks {
    void (*setBackground)(const char* name, bool autoBg);
    bool (*isAuto)(void);
    const char* (*currentName)(void);
    void (*startGfxTest)(void);      /* obre la pantalla de proves de grafics ✓ */
};

void setHooks(const Hooks& hooks);

/* Obre el desplegable de dalt (boto de ratlles del HUD). */
void openTopMenu();

/* Obre un panell a pantalla completa. */
void open(Id id);

/* Tanca el que hi hagi obert. Retorna true si hi havia alguna cosa. */
bool close();

Id          current();
bool        isOpen();
const char* name(Id id);

/* Crida periòdicament mentre hi ha un panell obert (refresca estats). */
void update(uint32_t nowMs);

/* Toca dins del panell actiu. Retorna true si el toc queda consumit. */
bool handleTap(int16_t x, int16_t y);

/* Rectangle que ocupa el desplegable de dalt (per restaurar-hi el fons). */
void topMenuRect(int16_t& x, int16_t& y, int16_t& w, int16_t& h);

}  // namespace Panels
