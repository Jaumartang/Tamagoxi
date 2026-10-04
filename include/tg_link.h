#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * tg_link.h - Protocol de l'enllac entre les dues plaques del Tamagoxi.
 *
 *   Tamagoxi (pantalla)      Xip amic (periferics: Bluetooth + audio)
 *   GPIO 32 (TX) ──────────► GPIO 16 (RX)
 *   GPIO 34 (RX) ◄────────── GPIO 17 (TX)
 *   GND ──────────────────── GND
 *
 * Els missatges son LINIES DE TEXT acabades en '\n' amb un checksum. Es facil
 * de seguir des del monitor serie i una linia malmesa es descarta sola:
 *
 *   #12|PING|*4F          <- "#" sequencia "|" comanda "|" args "*" checksum
 *
 * El checksum es la suma (mod 256) de tot el que hi ha entre '#' i '*',
 * escrita en dos digits hexadecimals.
 */

namespace TgLink {

constexpr uint32_t kBaud   = 921600;   /* bauds de l'enllac */
constexpr size_t   kMaxArg = 160;      /* mida maxima dels arguments */

/* Suma (mod 256) dels caracters: el checksum del marc. */
inline uint8_t checksum(const char* s, size_t n)
{
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c = static_cast<uint8_t>(c + static_cast<uint8_t>(s[i]));
    }
    return c;
}

/* Construeix "#<seq>|<cmd>|<arg>*HH\n" dins 'out'. Torna la llargada. */
inline int buildFrame(char* out, size_t outSize, uint8_t seq, const char* cmd, const char* arg)
{
    char body[256];
    const int n = snprintf(body, sizeof(body), "%u|%s|%s", static_cast<unsigned>(seq),
                           cmd, (arg != nullptr) ? arg : "");
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(body)) {
        return 0;
    }
    const uint8_t cs = checksum(body, static_cast<size_t>(n));
    const int total = snprintf(out, outSize, "#%s*%02X\n", body, static_cast<unsigned>(cs));
    return (total > 0 && static_cast<size_t>(total) < outSize) ? total : 0;
}

/* Desmunta una linia (sense el '\n'): comprova el checksum i separa
 * sequencia, comanda i arguments. Torna true si es valida. */
inline bool parseFrame(char* line, uint8_t* seq, char* cmd, size_t cmdSize,
                       char* arg, size_t argSize)
{
    if (line == nullptr || line[0] != '#') {
        return false;
    }
    char* star = strrchr(line, '*');
    if (star == nullptr || strlen(star + 1) < 2) {
        return false;
    }
    const uint8_t want = static_cast<uint8_t>(strtoul(star + 1, nullptr, 16));
    *star = '\0';                                   /* tallam el checksum */
    if (checksum(line + 1, strlen(line + 1)) != want) {
        return false;
    }

    char* body = line + 1;
    char* bar  = strchr(body, '|');                 /* final de la sequencia */
    if (bar == nullptr) {
        return false;
    }
    *bar = '\0';
    if (seq != nullptr) {
        *seq = static_cast<uint8_t>(strtoul(body, nullptr, 10));
    }

    char* c = bar + 1;
    char* a = strchr(c, '|');
    if (a != nullptr) {
        *a = '\0';
        ++a;
    }
    strlcpy(cmd, c, cmdSize);
    strlcpy(arg, (a != nullptr) ? a : "", argSize);
    return true;
}

}  // namespace TgLink
