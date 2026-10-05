#pragma once

#include <stdint.h>

/*
 * link.h - Enllac amb el xip amic (periferics: Bluetooth i audio).
 *
 * Fa servir un UART de 3 fils a 921600 bauds:
 *   aquesta placa envia pel GPIO 32 i rep pel GPIO 34 (nomes entrada).
 * Les linies son de text amb checksum; mira include/tg_link.h.
 */

namespace Link {

/* Pins de l'enllac en aquesta placa (la pantalla). Son els del CONNECTOR UART
 * de 4 fils (TX/RX/GND): en aquesta placa, UART0 = IO1 (TX) i IO3 (RX). Com que
 * son els mateixos pins del monitor serie, l'enllac NOMES s'activa amb la
 * comanda "link on" i es desactiva reiniciant la placa. */
constexpr int kTxPin = 1;    /* envia cap al xip amic (el seu RX) */
constexpr int kRxPin = 3;    /* rep del xip amic (el seu TX) */

void begin();                   /* obre la UART (cridar un cop al setup) */
void update();                  /* llegeix i contesta: cridar sovint des del bucle */

bool online();                  /* el xip amic ens ha parlat fa poc? */
bool active();                  /* l'enllac ja esta engegat (comanda "link on")? */
uint32_t lastSeenMs();          /* fa quants ms que no en sabem res */
const char* lastMessage();      /* ultim LOG que ens ha enviat */

bool send(const char* cmd, const char* arg = nullptr);

/* Envia un fitxer de la SD al xip amic com a firmware nou (OTA per l'enllac).
 * Bloqueja fins que s'acaba (o falla) i va dient el progres per la consola. */
bool sendFirmware(const char* path);

void printStatus();             /* resum per la consola */
uint32_t sentCount();
uint32_t recvCount();
uint32_t badCount();
const char* lastError();

}  // namespace Link
