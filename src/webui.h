#pragma once

#include <stdint.h>

/*
 * webui.h - Pujada de fitxers a la SD des del navegador (Fase 8).
 *
 * Un servidor web minim (port 80) que viu dins la tasca de xarxa: llista els
 * fitxers de /music (i de les altres carpetes), en deixa esborrar i permet
 * pujar-n'hi de nous amb el mobil o l'ordinador.
 *
 *   - Amb el WiFi connectat:  http://tamagoxi.local  (o la IP de 'net')
 *   - En mode punt d'acces (comanda 'ap'): http://192.168.4.1
 *
 * Requereix la SD muntada i el WiFi en marxa. Comprova la memoria abans
 * d'arrencar: aquest xip va just i val mes dir que no que no pas petar.
 */

namespace WebUI {

/* Arrenca el servidor. Retorna fals si no hi ha WiFi o falta memoria. */
bool begin();
void stop();
bool active();

/* Processa les peticions pendents: cridar sovint (des de la tasca de xarxa). */
void loop();

uint32_t requests();
void     printStatus();

/* Cert un cop despres d'haver pujat un fitxer (per refrescar la llista de
 * cancons des del bucle principal, que es qui mana sobre el reproductor). */
bool takeUploaded();

/* Autoprova: es visita a si mateixa (GET /) i puja un fitxer de prova per
 * POST /upload, comprovant que arriba a la SD. La fa la mateixa tasca de xarxa
 * (no es pot cridar des d'una altra tasca: el socket s'hi encalla). */
void requestSelfTest(bool dumpPage);

}  // namespace WebUI
