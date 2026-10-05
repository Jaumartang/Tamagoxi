#include "link.h"

#include <Arduino.h>
#include <SD.h>

#include "tg_config.h"
#include "tg_link.h"

namespace {

constexpr size_t kLineMax = 1200;   /* tambe hi passen els trossos de firmware */

char     gLine[kLineMax];
size_t   gLen = 0;
uint32_t gLastSeen = 0;
char     gLastMsg[80] = "";
char     gLastError[48] = "";
uint8_t  gSeq = 0;
uint32_t gSent = 0;
uint32_t gRecv = 0;
uint32_t gBad = 0;
uint32_t gLastPing = 0;
bool     gWasOnline = false;
bool     gActive = false;      /* l'enllac nomes funciona si l'han engegat */

void noteError(const char* text)
{
    strlcpy(gLastError, text, sizeof(gLastError));
}

/* Tracta una linia sencera (ja sense el '\n'). */
void handleLine(char* line)
{
    uint8_t seq = 0;
    char    cmd[24];
    char    arg[TgLink::kMaxArg];
    if (!TgLink::parseFrame(line, &seq, cmd, sizeof(cmd), arg, sizeof(arg))) {
        ++gBad;
        noteError("linia descartada (checksum o format)");
        Serial.printf("[LINK] descartada: %s\n", line);
        return;
    }
    ++gRecv;
    gLastSeen = millis();

    if (strcmp(cmd, "PONG") == 0 || strcmp(cmd, "HELLO") == 0) {
        if (arg[0] != '\0') {
            strlcpy(gLastMsg, arg, sizeof(gLastMsg));
        }
    } else if (strcmp(cmd, "LOG") == 0) {
        strlcpy(gLastMsg, arg, sizeof(gLastMsg));
        Serial.printf("[AMI] %s\n", arg);
    } else {
        Serial.printf("[LINK] comanda desconeguda: %s\n", cmd);
    }
}

/* Espera una resposta d'aquesta comanda (per l'OTA, que va sincronitzada). */
bool waitFor(const char* want, uint32_t ms)
{
    const uint32_t deadline = millis() + ms;
    while (static_cast<int32_t>(deadline - millis()) > 0) {
        while (Serial.available() > 0) {
            const int ch = Serial.read();
            if (ch < 0) {
                break;
            }
            if (ch == '\n' || ch == '\r') {
                if (gLen > 0) {
                    gLine[gLen] = '\0';
                    uint8_t seq = 0;
                    char    c[24];
                    static char a[TgLink::kMaxArg];
                    const bool ok = TgLink::parseFrame(gLine, &seq, c, sizeof(c), a, sizeof(a));
                    gLen = 0;
                    if (ok) {
                        gLastSeen = millis();
                        ++gRecv;
                        if (strcmp(c, want) == 0) {
                            return true;
                        }
                    } else {
                        ++gBad;
                    }
                }
                continue;
            }
            if (gLen + 1 < kLineMax) {
                gLine[gLen++] = static_cast<char>(ch);
            } else {
                gLen = 0;
                ++gBad;
            }
        }
        delay(2);
    }
    return false;
}

}  // namespace

namespace Link {

void begin()
{
    gLen      = 0;
    gLastSeen = 0;
    gLastPing = 0;
    gWasOnline = false;
    /* L'enllac va per UART0, els MATEIXOS pins del monitor serie: a partir
     * d'aqui la consola d'aquesta placa queda muda fins que es reiniciï. */
    Serial.begin(TgLink::kBaud, SERIAL_8N1, kRxPin, kTxPin);
    gActive = true;
    send("HELLO", "pantalla a 921600");
    Serial.flush();
}

bool active()
{
    return gActive;
}

bool send(const char* cmd, const char* arg)
{
    char frame[280];
    const int n = TgLink::buildFrame(frame, sizeof(frame), static_cast<uint8_t>(++gSeq),
                                     cmd, arg);
    if (n <= 0) {
        noteError("marc massa llarg");
        return false;
    }
    const size_t wrote = Serial.write(reinterpret_cast<const uint8_t*>(frame),
                                      static_cast<size_t>(n));
    Serial.flush();
    if (wrote == static_cast<size_t>(n)) {
        ++gSent;
        return true;
    }
    noteError("no s'ha pogut escriure a l'enllac");
    return false;
}

bool sendFirmware(const char* path)
{
    if (!gActive) {
        begin();                  /* l'enllac ocupa l'UART0 d'aquesta placa */
    }
    File f = SD.open(path, FILE_READ);
    if (!f) {
        noteError("no s'ha pogut obrir el fitxer del firmware");
        return false;
    }
    const size_t size = f.size();
    Serial.printf("[OTA] enviant %s (%u kB) al xip amic...\n", path,
                  static_cast<unsigned>(size / 1024));
    Serial.flush();

    char arg[24];
    snprintf(arg, sizeof(arg), "%u", static_cast<unsigned>(size));
    send("OTA", arg);
    if (!waitFor("OTAOK", 5000)) {
        f.close();
        noteError("el xip amic no ha acceptat l'OTA");
        return false;
    }

    static uint8_t chunk[500];
    static char    hex[1004];
    size_t sent = 0;
    while (sent < size) {
        const size_t n = f.read(chunk, sizeof(chunk));
        if (n == 0) {
            break;
        }
        for (size_t i = 0; i < n; ++i) {
            snprintf(hex + i * 2, 3, "%02X", chunk[i]);
        }
        bool ok = false;
        for (int attempt = 0; attempt < 4 && !ok; ++attempt) {
            send("OTAD", hex);
            ok = waitFor("OTAOK", 2500);
        }
        if (!ok) {
            f.close();
            noteError("l'enllac s'ha tallat a mig enviament");
            return false;
        }
        sent += n;
        if ((sent % 40960) < n) {         /* cada 40 kB, per no omplir el log */
            Serial.printf("[OTA] %u/%u kB\n", static_cast<unsigned>(sent / 1024),
                          static_cast<unsigned>(size / 1024));
        }
    }
    f.close();

    send("OTAE", "fi");
    const bool done = waitFor("OTAOK", 8000);
    noteError(done ? "" : "sense confirmacio final de l'OTA");
    return done;
}

void update()
{
    if (!gActive) {
        return;                   /* si no l'han engegat, no tocam el port */
    }
    while (Serial.available() > 0) {
        const int ch = Serial.read();
        if (ch < 0) {
            break;
        }
        if (ch == '\n' || ch == '\r') {
            if (gLen > 0) {
                gLine[gLen] = '\0';
                handleLine(gLine);
                gLen = 0;
            }
            continue;
        }
        if (gLen + 1 < kLineMax) {
            gLine[gLen++] = static_cast<char>(ch);
        } else {
            gLen = 0;
            ++gBad;
            noteError("linia massa llarga");
        }
    }

    /* Un cop per segon preguntam si el xip amic hi es. */
    const uint32_t now = millis();
    if (now - gLastPing >= 1000) {
        gLastPing = now;
        send("PING", nullptr);
    }

    /* Cada 5 s enviem les nostres estadistiques: aixi, mirant el log de l'amic,
     * es veu si realment ens arriben les seves respostes. */
    static uint32_t lastStat = 0;
    if (now - lastStat >= 5000) {
        lastStat = now;
        char st[64];
        snprintf(st, sizeof(st), "rebuts=%u enviats=%u dolents=%u viu=%d",
                 static_cast<unsigned>(gRecv), static_cast<unsigned>(gSent),
                 static_cast<unsigned>(gBad), online() ? 1 : 0);
        send("STAT", st);
    }

    /* Avisam nomes quan l'enllac apareix o desapareix (per no omplir el log). */
    const bool nowOnline = (gLastSeen != 0) && ((millis() - gLastSeen) < 3000);
    if (nowOnline != gWasOnline) {
        gWasOnline = nowOnline;
        if (nowOnline) {
            char txt[96];
            snprintf(txt, sizeof(txt), "enllac viu! l'amic diu: %s", gLastMsg);
            send("LOG", txt);
        } else {
            send("LOG", "s'ha perdut l'enllac amb el xip amic");
        }
        Serial.flush();
    }
}

bool online()
{
    return gLastSeen != 0 && (millis() - gLastSeen) < 3000;
}

uint32_t lastSeenMs()
{
    return (gLastSeen == 0) ? 0 : (millis() - gLastSeen);
}

const char* lastMessage()
{
    return gLastMsg;
}

uint32_t sentCount() { return gSent; }
uint32_t recvCount() { return gRecv; }
uint32_t badCount()  { return gBad; }
const char* lastError() { return gLastError; }

void printStatus()
{
    Serial.printf("[LINK] %s | enviats %u | rebuts %u | dolents %u\n",
                  online() ? "ENLLAC VIU" : "sense enllac (el xip amic no respon)",
                  static_cast<unsigned>(gSent), static_cast<unsigned>(gRecv),
                  static_cast<unsigned>(gBad));
    if (gLastSeen != 0) {
        Serial.printf("[LINK] darrera resposta: fa %u ms | '%s'\n",
                      static_cast<unsigned>(millis() - gLastSeen), gLastMsg);
    }
    if (gLastError[0] != '\0') {
        Serial.printf("[LINK] darrer problema: %s\n", gLastError);
    }
    Serial.printf("[LINK] placa amiga? connecta-la i comprova que faci PONG\n");
}

}  // namespace Link
