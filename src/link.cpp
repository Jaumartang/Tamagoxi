#include "link.h"

#include <Arduino.h>

#include "tg_config.h"
#include "tg_link.h"

namespace {

constexpr size_t kLineMax = 240;

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

}  // namespace

namespace Link {

void begin()
{
    gLen      = 0;
    gLastSeen = 0;
    gLastPing = 0;
    Serial1.begin(TgLink::kBaud, SERIAL_8N1, kRxPin, kTxPin);
    Serial.printf("[LINK] enllac obert a %u bauds (TX %d -> 16, RX %d <- 17)\n",
                  static_cast<unsigned>(TgLink::kBaud), kTxPin, kRxPin);
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
    const size_t wrote = Serial1.write(reinterpret_cast<const uint8_t*>(frame),
                                       static_cast<size_t>(n));
    Serial1.flush();
    if (wrote == static_cast<size_t>(n)) {
        ++gSent;
        return true;
    }
    noteError("no s'ha pogut escriure a l'enllac");
    return false;
}

void update()
{
    while (Serial1.available() > 0) {
        const int ch = Serial1.read();
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

    /* Avisam nomes quan l'enllac apareix o desapareix (per no omplir el log). */
    static bool wasOnline = false;
    const bool nowOnline = (gLastSeen != 0) && ((millis() - gLastSeen) < 3000);
    if (nowOnline != wasOnline) {
        wasOnline = nowOnline;
        if (nowOnline) {
            Serial.printf("[LINK] el xip amic ha contestat: ENLLAC VIU ('%s')\n", gLastMsg);
        } else {
            Serial.println(F("[LINK] s'ha perdut l'enllac amb el xip amic"));
        }
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
