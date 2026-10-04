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

/* Pins de l'enllac en aquesta placa (la pantalla). */
constexpr int kTxPin = 32;   /* envia cap al xip amic (el seu 16) */
constexpr int kRxPin = 34;   /* rep del xip amic (el seu 17); nomes entrada */

void begin();                   /* obre la UART (cridar un cop al setup) */
void update();                  /* llegeix i contesta: cridar sovint des del bucle */

bool online();                  /* el xip amic ens ha parlat fa poc? */
uint32_t lastSeenMs();          /* fa quants ms que no en sabem res */
const char* lastMessage();      /* ultim LOG que ens ha enviat */

bool send(const char* cmd, const char* arg = nullptr);

void printStatus();             /* resum per la consola */
uint32_t sentCount();
uint32_t recvCount();
uint32_t badCount();
const char* lastError();

}  // namespace Link
