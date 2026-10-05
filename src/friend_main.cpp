/*
 * Xip amic del Tamagoxi - la segona placa (ESP32 DevKit v4).
 *
 * Fa de periferics: Bluetooth i audio (de moment, sap parlar per l'enllac).
 *
 * Cablejat amb la placa de la pantalla (connector UART de 4 fils):
 *   el seu TX (IO1) -> aquesta RX (IO3)
 *   la seva RX (IO3) <- aquesta TX (IO1)
 *   GND             -> GND          (NO connectis el 5V!)
 *
 * Com que son els mateixos pins del port USB, la consola nomes funciona els
 * primers 2,5 segons d'engegar: despres la placa passa a l'enllac (921600).
 * Per tornar a veure la consola, reinicia-la.
 *
 * Compilar i pujar:   pio run -e friend -t upload
 */

#include <Arduino.h>
#include <Update.h>

#include "tg_link.h"

constexpr int    kTxPin   = 17;       /* connector lliure de la placa: IO17 (TX) */
constexpr int    kRxPin   = 16;       /* i IO16 (RX) -> la consola USB queda lliure ✓ */
constexpr int    kLedPin  = 2;        /* LED blau de la placa */
constexpr size_t kLineMax = 1200;   /* tambe hi passen els trossos de firmware */

char     gLine[kLineMax];
size_t   gLen = 0;
uint8_t  gSeq = 0;
uint32_t gRecv = 0;
uint32_t gSent = 0;
uint32_t gBad = 0;
uint32_t gLastBlink = 0;
uint32_t gLastHello = 0;
bool     gLedOn = false;
size_t   gOtaTotal = 0;
size_t   gOtaDone = 0;

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
    /* Ho diem tambe pel USB (aixo no va per l'enllac): aixi es veu la conversa. */
    if (strcmp(cmd, "OTAD") != 0) {
        Serial.printf("[AMI] -> %s %s\n", cmd, (arg != nullptr) ? arg : "");
    }
}

/* Valor d'un digit hexadecimal (-1 si no ho es). */
int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Tracta una linia sencera vinguda de la pantalla. */
void handleLine(char* line)
{
    uint8_t pseq = 0;
    char    cmd[24];
    char    arg[TgLink::kMaxArg];
    if (!TgLink::parseFrame(line, &pseq, cmd, sizeof(cmd), arg, sizeof(arg))) {
        ++gBad;
        return;                       /* soroll o linia malmesa: la ignorem */
    }
    ++gRecv;
    if (strcmp(cmd, "OTAD") != 0) {
        Serial.printf("[AMI] <- %s %s\n", cmd, arg);
    }

    if (strcmp(cmd, "OTA") == 0) {
        /* La pantalla ens vol enviar un firmware nou: hi reservam el lloc. */
        const size_t size = strtoul(arg, nullptr, 10);
        const bool ok = (size > 1000) && Update.begin(size, U_FLASH);
        gOtaTotal = size;
        gOtaDone  = 0;
        Serial.printf("[AMI] OTA de %u bytes -> %s\n", static_cast<unsigned>(size),
                      ok ? "llest" : Update.errorString());
        reply("OTAOK", ok ? "llest" : "error");
    } else if (strcmp(cmd, "OTAD") == 0) {
        static uint8_t buf[600];
        const size_t alen = strlen(arg);
        size_t w = 0;
        for (size_t i = 0; i + 1 < alen && w < sizeof(buf); i += 2) {
            const int hi = hexVal(arg[i]);
            const int lo = hexVal(arg[i + 1]);
            if (hi < 0 || lo < 0) {
                break;
            }
            buf[w++] = static_cast<uint8_t>((hi << 4) | lo);
        }
        const size_t written = Update.write(buf, w);
        gOtaDone += w;
        if (gOtaTotal > 0 && (gOtaDone % 32768) < w) {
            Serial.printf("[AMI] OTA: %u/%u kB (%u%%)\n",
                          static_cast<unsigned>(gOtaDone / 1024),
                          static_cast<unsigned>(gOtaTotal / 1024),
                          static_cast<unsigned>((gOtaDone * 100) / gOtaTotal));
        }
        if (written != w) {
            Serial.printf("[AMI] OTA error escrivint: %s\n", Update.errorString());
            reply("OTAOK", "error");
        } else {
            reply("OTAOK", "ok");
        }
    } else if (strcmp(cmd, "OTAE") == 0) {
        const bool ok = Update.end(true);
        Serial.printf("[AMI] OTA %s\n", ok ? "completada: reiniciant!" : Update.errorString());
        reply("OTAOK", ok ? "fet" : "error");
        delay(600);
        if (ok) {
            ESP.restart();
        }
    } else if (strcmp(cmd, "PING") == 0) {
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
    Serial.println(F("Enllac: IO17 (TX) -> RX de la pantalla, IO16 (RX) <- TX de la pantalla"));
    Serial.println(F("Nomes 3 fils: TX, RX i GND (no connectis el 5V)"));

    pinMode(kLedPin, OUTPUT);
    digitalWrite(kLedPin, HIGH);        /* el LED de la placa sol ser actiu baix */

    /* L'enllac va per la UART2 (IO16/IO17): la consola USB queda lliure ✓ */
    Serial2.begin(TgLink::kBaud, SERIAL_8N1, kRxPin, kTxPin);
    Serial.printf("[AMI] enllac obert a %u bauds (TX %d, RX %d)\n",
                  static_cast<unsigned>(TgLink::kBaud), kTxPin, kRxPin);
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
        reply("HELLO", "amic a punt");
    }
}
