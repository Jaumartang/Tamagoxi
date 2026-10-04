#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * notify.h - Enviar missatges al mobil (Fase 9).
 *
 * L'ESP32 no pot enviar SMS sense un modul GSM, pero si que pot enviar
 * missatges per internet. Fem servir **Telegram**: cada destinatari es un
 * NUMERO (el "chat id"), tal com demanava la Noa, i el missatge arriba com una
 * notificacio al mobil dels pares.
 *
 * Per fer-ho servir cal, un cop:
 *   1. Crear un bot amb @BotFather (Telegram) i copiar-ne el token.
 *      -> 'msg token 123456:ABC...'
 *   2. Escriure un "hola" al bot des del mobil i mirar el chat id
 *      (https://api.telegram.org/bot<token>/getUpdates).
 *      -> 'msg add 123456789'
 *   3. 'msg on'
 *
 * Tot es desa a la NVS. Els enviaments els fa la tasca de xarxa (mai bloquegen
 * la mascota) i es poden fer des del bucle principal amb send()/alert().
 */

namespace Notify {

constexpr uint8_t kMaxRecipients = 6;
constexpr size_t  kTextMax       = 200;

/* Carrega la configuracio de la NVS (es crida un cop a l'arrencada). */
void begin();

/* --- Configuracio (es desa a la NVS) -------------------------------------- */
bool        enabled();
void        setEnabled(bool on);
const char* token();
bool        setToken(const char* token);
uint8_t     recipientCount();
const char* recipient(uint8_t index);
bool        addRecipient(const char* number);   /* nomes digits (- opcional) */
bool        removeRecipient(uint8_t index);
void        clearAll();

/* --- Enviament ------------------------------------------------------------ */
/* Envia un missatge a tots els destinataris (no bloqueja: es posa a la cua). */
void send(const char* text);

/* Com send(), pero amb limit de frequencia: per als avisos de la mascota, per
 * no empipar (com a molt un cada 5 minuts). */
void alert(const char* text);

/* Ho crida sovint la tasca de xarxa: es qui fa l'enviament de veritat. */
void loop();
bool busy();                     /* hi ha un missatge esperant */
const char* lastResult();        /* com ha anat l'ultim enviament */
uint32_t    sentCount();
void        printStatus();

/* --- Missatges que ENS arriben (del mobil al Tamagoxi) -------------------- */

constexpr uint8_t kMaxInbox = 5;
constexpr size_t  kInboxTextMax = 120;

struct InboxMsg {
    char     from[24];       /* numero (chat id) de qui l'ha enviat */
    char     text[kInboxTextMax];
    uint32_t ms;
};

uint8_t         inboxCount();
const InboxMsg* inbox(uint8_t index);   /* 0 = el mes nou */
/* Cert si n'hi ha algun de nou, i els marca com a llegits (per la finestreta). */
bool            takeNew();
/* Forca una comprovacio ara (la fa igualment la tasca de xarxa cada 10 s). */
void            pollNow();
uint32_t        receivedCount();

/* Nomes per provar la finestreta sense cap bot: entra un missatge com si
 * hagues arribat del mobil. */
void            inject(const char* from, const char* text);

}  // namespace Notify
