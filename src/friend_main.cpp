/*
 * Xip amic del Tamagoxi - la segona placa (ESP32 DevKit v4).
 *
 * Fa de periferics: Bluetooth i audio (de moment, sap parlar per l'enllac).
 *
 * Cablejat amb la placa de la pantalla:
 *   aquesta TX (GPIO 17) -> GPIO 34 de la pantalla
 *   aquesta RX (GPIO 16) <- GPIO 32 de la pantalla
 *   GND                  -> GND
 *
 * Compilar i pujar:   pio run -e friend -t upload
 * Monitor:            pio device monitor -e friend
 */

#include <Arduino.h>

#include "tg_link.h"

constexpr int    kTxPin   = 17;
constexpr int    kRxPin   = 16;
constexpr int    kLedPin  = 2;        /* LED blau de la placa */
constexpr size_t kLineMax = 240;

char     gLine[kLineMax];
size_t   gLen = 0;
uint8_t  gSeq = 0;
uint32_t gRecv = 0;
uint32_t gSent = 0;
uint32_t gBad = 0;
uint32_t gLastBlink = 0;
uint32_t gLastHello = 0;
bool     gLedOn = false;

/* Envia una resposta a la placa de la pantalla. */
void reply(const char* cmd, const char* arg)
{
    char frame[280];
    const int n = TgLink::buildFrame(frame, sizeof(frame), static_cast<uint8_t>(++gSeq),
                                     cmd, arg);
    if (n <= 0) {
        return;
    }
    Serial2.write(reinterpret_cast<const uint8_t*>(frame), static_cast<size_t>(n));
    Serial2.flush();
    ++gSent;
    Serial.printf("[AMI] -> %s %s\n", cmd, (arg != nullptr) ? arg : "");
}

/* Tracta una linia sencera vinguda de la pantalla. */
void handleLine(char* line)
{
    uint8_t pseq = 0;
    char    cmd[24];
    char    arg[TgLink::kMaxArg];
    if (!TgLink::parseFrame(line, &pseq, cmd, sizeof(cmd), arg, sizeof(arg))) {
        ++gBad;
        Serial.printf("[AMI] descartada: %s\n", line);
        return;
    }
    ++gRecv;
    Serial.printf("[AMI] <- %s %s\n", cmd, arg);

    if (strcmp(cmd, "PING") == 0) {
        char up[40];
        snprintf(up, sizeof(up), "amic viu %lu s", static_cast<unsigned long>(millis() / 1000));
        reply("PONG", up);
    } else if (strcmp(cmd, "LOG") == 0) {
        /* La pantalla ens manava un text: li tornam per provar l'anada i tornada. */
        reply("LOG", arg);
    } else if (strcmp(cmd, "HELLO") == 0) {
        reply("LOG", "hola Tamagoxi, soc el xip amic!");
    } else if (strcmp(cmd, "PLAY") == 0 || strcmp(cmd, "STOP") == 0 ||
               strcmp(cmd, "VOL") == 0) {
        /* Aqui hi anira el Bluetooth i l'audio: encara no hi es. */
        Serial.printf("[AMI] audio pendent: %s %s\n", cmd, arg);
        reply("LOG", "audio encara pendent");
    } else {
        reply("LOG", "no entenc aquesta comanda");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("=== Xip amic del Tamagoxi ==="));
    Serial.printf("Enllac: TX %d -> GPIO 34 de la pantalla, RX %d <- GPIO 32\n", kTxPin, kRxPin);

    pinMode(kLedPin, OUTPUT);
    digitalWrite(kLedPin, HIGH);        /* el LED de la placa sol ser actiu baix */

    Serial2.begin(TgLink::kBaud, SERIAL_8N1, kRxPin, kTxPin);
    Serial.printf("[AMI] enllac obert a %u bauds\n", static_cast<unsigned>(TgLink::kBaud));

    reply("HELLO", "amic a punt");
    gLastHello = millis();
}

void loop()
{
    while (Serial2.available() > 0) {
        const int ch = Serial2.read();
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
        }
    }

    /* El LED batega: es veu que la placa es viva encara que no hi hagi pantalla. */
    const uint32_t now = millis();
    if (now - gLastBlink >= 500) {
        gLastBlink = now;
        gLedOn = !gLedOn;
        digitalWrite(kLedPin, gLedOn ? LOW : HIGH);
    }

    /* Cada 10 s ens presentam una altra vegada, per si la pantalla ha arrencat mes tard. */
    if (now - gLastHello >= 10000) {
        gLastHello = now;
        if (gRecv == 0) {
            Serial.println(F("[AMI] encara no m'ha parlat la pantalla (revisa els cables)"));
        }
        reply("HELLO", "amic a punt");
    }
}
