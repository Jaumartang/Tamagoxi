#pragma once

#include <stdint.h>

/*
 * net.h - Xarxa (Fase 6): WiFi + NTP + meteo (Open-Meteo), en una tasca propia
 * del FreeRTOS al nucli 0.
 *
 * IMPORTANT: tota la feina de xarxa viu en una tasca apart, de manera que el
 * bucle principal (que anima la mascota i atén el tactil) mai no queda
 * bloquejat per un escaneig de WiFi, una sincronitzacio NTP o un GET HTTPS.
 * Aquest modul nomes exposa l'estat; el bucle el llegeix quan li va be.
 *
 * Credencials: es desen a la NVS amb la comanda 'wifi <ssid> <contrasenya>' o,
 * si no n'hi ha, es fan servir les macros de include/secrets.h.
 */

namespace Net {

enum class State : uint8_t {
    NoCredentials,  /* cap ssid desat ni definit a secrets.h */
    Connecting,
    Connected,
    Failed,
};

struct Weather {
    bool    valid;          /* hi ha dades d'aquesta sessio */
    int8_t  temperature;    /* graus C (arrodonit) */
    int16_t code;           /* codi WMO (0 = clar, 95 = tempesta...) */
    int16_t windKmh;
    bool    isDay;
    uint8_t bgIndex;        /* fons weather_NN suggerit per aquest temps */
};

/* Arrenca la tasca de xarxa. No bloqueja (torna de seguida). */
void begin();

State       state();
bool        connected();
bool        timeSynced();       /* l'hora local ja es valida */
uint32_t    epoch();            /* segons des de 1970 (0 si no sincronitzat) */
unsigned    weatherAgeSec();    /* edat de les dades de meteo (segons) */
const Weather& weather();

/* Credencials guardades a la NVS (es fan servir al seguent intent). */
bool        hasCredentials();
bool        setCredentials(const char* ssid, const char* pass);
bool        clearCredentials();
const char* ssid();

/* Ubicacio per la meteo (es desa a la NVS). */
void  setLocation(float lat, float lon);
float latitude();
float longitude();

/* Demana refrescar la meteo a la seguent volta de la tasca. */
void requestRefresh();

/* Nom del fons suggerit ('weather_NN') o "" si encara no hi ha dades. */
const char* backgroundName();

/* Volcat d'estat per consola. */
void printStatus();

}  // namespace Net
