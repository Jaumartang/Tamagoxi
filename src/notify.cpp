#include "notify.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {

constexpr const char* kNamespace = "tg_msg";
/* Com a molt, un avis automatic cada 5 minuts (per no empipar). */
constexpr uint32_t kAlertMinMs = 5u * 60u * 1000u;

volatile bool gEnabled = false;
char          gToken[110];
char          gRecipients[Notify::kMaxRecipients][24];
volatile uint8_t gCount = 0;

char          gOutbox[Notify::kTextMax + 1];
volatile bool gPending    = false;
uint32_t      gLastAlertMs = 0;
char          gLastResult[64] = "cap enviament";
volatile uint32_t gSent = 0;

void save()
{
    Preferences p;
    if (!p.begin(kNamespace, false)) {
        return;
    }
    p.putBool("on", gEnabled);
    p.putString("token", gToken);
    char buf[Notify::kMaxRecipients * 25] = {0};
    for (uint8_t i = 0; i < gCount; ++i) {
        if (i > 0) {
            strlcat(buf, ",", sizeof(buf));
        }
        strlcat(buf, gRecipients[i], sizeof(buf));
    }
    p.putString("rcpts", buf);
    p.end();
}

/* Escapa el text per poder-lo enviar dins una URL (espais, accents, &, ...). */
void urlEncode(const char* in, char* out, size_t outLen)
{
    static const char* kHex = "0123456789ABCDEF";
    size_t n = 0;
    for (const char* p = in; *p != '\0' && n + 4 < outLen; ++p) {
        const char c = *p;
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                          || (c >= '0' && c <= '9') || c == '-' || c == '_'
                          || c == '.' || c == '~';
        if (safe) {
            out[n++] = c;
        } else {
            out[n++] = '%';
            out[n++] = kHex[(static_cast<uint8_t>(c) >> 4) & 0x0F];
            out[n++] = kHex[static_cast<uint8_t>(c) & 0x0F];
        }
    }
    out[n] = '\0';
}

/* Un POST a Telegram: /bot<token>/sendMessage amb chat_id i text. */
bool sendToOne(const char* chatId, const char* text)
{
    char enc[Notify::kTextMax * 3 + 1];
    urlEncode(text, enc, sizeof(enc));

    char url[200];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", gToken);

    WiFiClientSecure client;
    client.setInsecure();          /* sense validacio de certificat (com la meteo) */

    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(client, url)) {
        strlcpy(gLastResult, "no puc obrir la connexio", sizeof(gLastResult));
        return false;
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    char body[Notify::kTextMax * 3 + 40];
    snprintf(body, sizeof(body), "chat_id=%s&text=%s", chatId, enc);
    const int code = http.POST(reinterpret_cast<uint8_t*>(body), strlen(body));
    http.end();

    if (code == 200) {
        snprintf(gLastResult, sizeof(gLastResult), "enviat a %s", chatId);
        Serial.printf("[MSG] enviat a %s\n", chatId);
        return true;
    }
    const char* why = (code == 401) ? "token incorrecte"
                      : ((code == 400) ? "numero o chat id incorrecte"
                                       : ((code < 0) ? "sense xarxa" : "resposta rara"));
    snprintf(gLastResult, sizeof(gLastResult), "error %d (%s)", code, why);
    Serial.printf("[MSG] error %d en enviar a %s (%s)\n", code, chatId, why);
    return false;
}

}  // namespace

namespace Notify {

void begin()
{
    Preferences p;
    if (!p.begin(kNamespace, true)) {
        Serial.println(F("[MSG] sense configuracio (cal 'msg token ...' i 'msg add ...')"));
        return;
    }
    gEnabled = p.getBool("on", false);
    strlcpy(gToken, p.getString("token", "").c_str(), sizeof(gToken));
    const String rc = p.getString("rcpts", "");
    p.end();

    char buf[sizeof(rc) + 1];
    strlcpy(buf, rc.c_str(), sizeof(buf));
    char* tok = strtok(buf, ",");
    while (tok != nullptr && gCount < kMaxRecipients) {
        if (tok[0] != '\0') {
            strlcpy(gRecipients[gCount], tok, sizeof(gRecipients[0]));
            ++gCount;
        }
        tok = strtok(nullptr, ",");
    }

    Serial.printf("[MSG] %u destinataris, %s%s\n", static_cast<unsigned>(gCount),
                  gEnabled ? "activat" : "aturat",
                  (gToken[0] == '\0') ? " (sense token)" : "");
    for (uint8_t i = 0; i < gCount; ++i) {
        Serial.printf("  %u. %s\n", static_cast<unsigned>(i), gRecipients[i]);
    }
}

bool enabled()
{
    return gEnabled;
}

void setEnabled(bool on)
{
    gEnabled = on;
    save();
    Serial.printf("[MSG] %s\n", on ? "activat" : "aturat");
}

const char* token()
{
    return gToken;
}

bool setToken(const char* t)
{
    if (t == nullptr) {
        return false;
    }
    strlcpy(gToken, t, sizeof(gToken));
    save();
    Serial.printf("[MSG] token desat (%u caracters)\n", static_cast<unsigned>(strlen(gToken)));
    return true;
}

uint8_t recipientCount()
{
    return gCount;
}

const char* recipient(uint8_t index)
{
    return (index < gCount) ? gRecipients[index] : "";
}

bool addRecipient(const char* number)
{
    if (number == nullptr || number[0] == '\0' || gCount >= kMaxRecipients) {
        return false;
    }
    /* Nomes digits (i un guio al davant, per als chat id negatius). */
    char clean[24];
    size_t n = 0;
    for (const char* p = number; *p != '\0' && n + 1 < sizeof(clean); ++p) {
        if ((*p >= '0' && *p <= '9') || (*p == '-' && n == 0)) {
            clean[n++] = *p;
        }
    }
    clean[n] = '\0';
    if (n == 0) {
        return false;
    }
    for (uint8_t i = 0; i < gCount; ++i) {
        if (strcmp(gRecipients[i], clean) == 0) {
            return true;                       /* ja hi era */
        }
    }
    strlcpy(gRecipients[gCount], clean, sizeof(gRecipients[0]));
    ++gCount;
    save();
    Serial.printf("[MSG] numero afegit: %s (%u en total)\n", clean,
                  static_cast<unsigned>(gCount));
    return true;
}

bool removeRecipient(uint8_t index)
{
    if (index >= gCount) {
        return false;
    }
    for (uint8_t i = index; i + 1 < gCount; ++i) {
        strlcpy(gRecipients[i], gRecipients[i + 1], sizeof(gRecipients[0]));
    }
    --gCount;
    save();
    return true;
}

void clearAll()
{
    gCount = 0;
    save();
}

void send(const char* text)
{
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    strlcpy(gOutbox, text, sizeof(gOutbox));
    gPending = true;
    Serial.printf("[MSG] a la cua: %s\n", gOutbox);
}

void alert(const char* text)
{
    const uint32_t now = millis();
    if (gLastAlertMs != 0 && (now - gLastAlertMs) < kAlertMinMs) {
        return;                                 /* fa poc que n'hem enviat un */
    }
    gLastAlertMs = now;
    send(text);
}

void loop()
{
    if (!gPending) {
        return;
    }
    if (!gEnabled || gToken[0] == '\0' || gCount == 0) {
        gPending = false;
        strlcpy(gLastResult, "sense configuracio", sizeof(gLastResult));
        Serial.println(F("[MSG] no envio: falta el token o els numeros ('msg on')"));
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return;                                 /* ho tornarem a provar */
    }

    gPending = false;
    for (uint8_t i = 0; i < gCount; ++i) {
        if (sendToOne(gRecipients[i], gOutbox)) {
            ++gSent;
        }
        yield();
    }
}

bool busy()
{
    return gPending;
}

const char* lastResult()
{
    return gLastResult;
}

uint32_t sentCount()
{
    return gSent;
}

void printStatus()
{
    Serial.printf("[MSG] %s, token=%s, %u numeros, enviats=%lu, ultim: %s\n",
                  gEnabled ? "activat" : "aturat",
                  (gToken[0] != '\0') ? "si" : "NO",
                  static_cast<unsigned>(gCount),
                  static_cast<unsigned long>(gSent), gLastResult);
    for (uint8_t i = 0; i < gCount; ++i) {
        Serial.printf("  %u. %s\n", static_cast<unsigned>(i), gRecipients[i]);
    }
}

}  // namespace Notify

