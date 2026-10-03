#include "net.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <time.h>

#include "tg_config.h"

#if __has_include(<secrets.h>)
#include <secrets.h>
#define TG_HAVE_SECRETS 1
#endif

namespace {

constexpr const char* kNamespace = "tg_net";
constexpr const char* kPlaceholderSsid = "el-teu-ssid";   /* el de secrets.example.h */

SemaphoreHandle_t gMutex = nullptr;
TaskHandle_t      gTask  = nullptr;

/* --- Estat compartit (es llegeix des del bucle principal) ----------------- */
volatile Net::State gState       = Net::State::NoCredentials;
volatile bool       gTimeSynced  = false;
volatile uint32_t   gEpoch       = 0;
volatile bool       gRefresh     = false;
volatile uint32_t   gWeatherMs   = 0;      /* millis() de les dades valides */
uint32_t            gLastTryMs   = 0;

Net::Weather gWeather = {false, 0, 0, 0, true, 0};

/* --- Escaneig de xarxes (el fa la tasca; la UI nomes llegeix) -------------- */
constexpr uint8_t kMaxAps = 16;
Net::Ap         gAps[kMaxAps];
volatile uint8_t gApCount     = 0;
volatile bool    gScanRequest = false;
volatile bool    gScanRunning = false;
volatile bool    gScanDone    = false;

/* --- Configuracio (protegida pel mutex) ----------------------------------- */
char  gSsid[33] = {0};
char  gPass[65] = {0};
float gLat = NET_DEFAULT_LATITUDE;
float gLon = NET_DEFAULT_LONGITUDE;

void lock()
{
    if (gMutex != nullptr) {
        xSemaphoreTake(gMutex, portMAX_DELAY);
    }
}

void unlock()
{
    if (gMutex != nullptr) {
        xSemaphoreGive(gMutex);
    }
}

bool credentialsCopy(char* ssid, size_t ssidLen, char* pass, size_t passLen)
{
    lock();
    strlcpy(ssid, gSsid, ssidLen);
    strlcpy(pass, gPass, passLen);
    unlock();
    return ssid[0] != '\0';
}

/*
 * Mapeig temps (codi WMO) + hora + vent -> fons weather_NN.
 *   00 sol · 01 nit · 02 nit estelada · 03 ennuvolat · 04 pluja · 05 neu
 *   06 fred · 07 calor · 08 vent · 09 arc de sant Marti · 10 tempesta
 *   11 boira · 12 sortida de sol · 13 posta de sol · 14 lluna plena · 15 aurores
 */
uint8_t mapWeatherToBg(int code, bool isDay, float temp, float wind, int hour, int prevCode)
{
    /* Fenomens extrems primer: sempre guanyen. */
    if (code == 95 || code == 96 || code == 99) {
        return 10;                                   /* tempesta */
    }
    if (code == 45 || code == 48) {
        return 11;                                   /* boira */
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return 5;                                    /* neu */
    }
    if (code == 56 || code == 57 || code == 66 || code == 67) {
        return 6;                                    /* pluja gelada */
    }
    if ((code >= 51 && code <= 65) || (code >= 80 && code <= 82)) {
        return 4;                                    /* pluja */
    }
    if (code == 3) {
        return isDay ? 3 : 1;                        /* ennuvolat (dia o nit) */
    }

    /* Cel clar o poc ennuvolat. */
    if (isDay) {
        const bool wasWet = (prevCode >= 51 && prevCode <= 67) ||
                            (prevCode >= 80 && prevCode <= 82);
        if (wasWet && code <= 2) {
            return 9;                                /* arc de sant Marti */
        }
        if (temp <= 3.0f) {
            return 6;                                /* fred */
        }
        if (temp >= 32.0f) {
            return 7;                                /* calor */
        }
        if (wind >= 30.0f) {
            return 8;                                /* vent */
        }
        if (hour >= 5 && hour < 8) {
            return 12;                               /* sortida de sol */
        }
        if (hour >= 18 && hour < 21) {
            return 13;                               /* posta de sol */
        }
        return 0;                                    /* dia assolellat */
    }

    /* Nit. */
    if (temp <= 2.0f) {
        return 15;                                   /* aurores (nit freda i clara) */
    }
    if (wind >= 30.0f) {
        return 8;                                    /* vent */
    }
    return (code == 0) ? 2 : 1;                      /* 2 = estelada, 1 = nit */
}

}  // namespace

namespace {

/* --- Feina de xarxa (nomes des de la tasca) -------------------------------- */

bool connectWifi()
{
    char ssid[33];
    char pass[65];
    if (!credentialsCopy(ssid, sizeof(ssid), pass, sizeof(pass))) {
        gState = Net::State::NoCredentials;
        return false;
    }

    gState = Net::State::Connecting;
    Serial.printf("[NET] connectant a \"%s\"...\n", ssid);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);

    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < NET_WIFI_TIMEOUT_MS) {
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    if (WiFi.status() != WL_CONNECTED) {
        gState = Net::State::Failed;
        Serial.printf("[NET] no s'ha pogut connectar (estat %d)\n",
                      static_cast<int>(WiFi.status()));
        return false;
    }

    gState = Net::State::Connected;
    Serial.printf("[NET] connectat: IP %s  RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(), static_cast<int>(WiFi.RSSI()));
    return true;
}

bool syncTime()
{
    configTzTime(NET_TIMEZONE, "pool.ntp.org", "time.google.com");

    const uint32_t t0 = millis();
    struct tm now {};
    while ((millis() - t0) < 15000) {
        if (getLocalTime(&now, 0)) {
            gEpoch      = static_cast<uint32_t>(time(nullptr));
            gTimeSynced = true;
            Serial.printf("[NET] hora: %02d:%02d:%02d (TZ %s)\n", now.tm_hour,
                          now.tm_min, now.tm_sec, NET_TIMEZONE);
            return true;
        }
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
    Serial.println(F("[NET] NTP: sense resposta"));
    return false;
}

bool fetchWeather()
{
    float lat = 0.0f;
    float lon = 0.0f;
    lock();
    lat = gLat;
    lon = gLon;
    unlock();

    char url[224];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,wind_speed_10m,is_day&timezone=auto",
             static_cast<double>(lat), static_cast<double>(lon));

    WiFiClientSecure client;
    client.setInsecure();          /* sense validacio de certificat: nomes lectura publica */

    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setUserAgent("Tamagoxi/2.0");
    if (!http.begin(client, url)) {
        Serial.println(F("[NET] meteo: no s'ha pogut iniciar la peticio"));
        return false;
    }

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("[NET] meteo: HTTP %d\n", status);
        http.end();
        return false;
    }

    /* Filtre: nomes ens interessa el bloc 'current'. */
    JsonDocument filter;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["weather_code"]   = true;
    filter["current"]["wind_speed_10m"] = true;
    filter["current"]["is_day"]         = true;

    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[NET] meteo: JSON invalid (%s)\n", err.c_str());
        return false;
    }

    const JsonObject current = doc["current"];
    if (current.isNull()) {
        Serial.println(F("[NET] meteo: sense bloc 'current'"));
        return false;
    }

    const float temp  = current["temperature_2m"] | 20.0f;
    const int   code  = current["weather_code"] | 0;
    const float wind  = current["wind_speed_10m"] | 0.0f;
    const bool  isDay = (current["is_day"] | 1) != 0;

    int hour = 12;
    struct tm now {};
    if (getLocalTime(&now, 0)) {
        hour = now.tm_hour;
    }

    const uint8_t bg = mapWeatherToBg(code, isDay, temp, wind, hour, gWeather.code);

    lock();
    gWeather.valid       = true;
    gWeather.temperature = static_cast<int8_t>(lroundf(temp));
    gWeather.code        = static_cast<int16_t>(code);
    gWeather.windKmh     = static_cast<int16_t>(lroundf(wind));
    gWeather.isDay       = isDay;
    gWeather.bgIndex     = bg;
    unlock();
    gWeatherMs = millis();

    Serial.printf("[NET] meteo: %d C  codi %d  vent %d km/h  %s -> weather_%02u\n",
                  static_cast<int>(lroundf(temp)), code, static_cast<int>(lroundf(wind)),
                  isDay ? "dia" : "nit", static_cast<unsigned>(bg));
    return true;
}

void doScan()
{
    gScanRunning = true;
    gScanDone    = false;
    Serial.println(F("[NET] escanejant xarxes WiFi..."));
    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    const int16_t found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);

    lock();
    gApCount = 0;
    for (int16_t i = 0; i < found && gApCount < kMaxAps; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0 || ssid.length() >= sizeof(gAps[0].ssid)) {
            continue;                       /* oculta o massa llarga */
        }
        bool dup = false;
        for (uint8_t j = 0; j < gApCount; ++j) {
            if (strcmp(gAps[j].ssid, ssid.c_str()) == 0) {
                dup = true;                 /* repetida: ens quedem la mes forta */
                const int8_t r = static_cast<int8_t>(WiFi.RSSI(i));
                if (r > gAps[j].rssi) {
                    gAps[j].rssi = r;
                }
                break;
            }
        }
        if (dup) {
            continue;
        }
        strlcpy(gAps[gApCount].ssid, ssid.c_str(), sizeof(gAps[0].ssid));
        gAps[gApCount].rssi   = static_cast<int8_t>(WiFi.RSSI(i));
        gAps[gApCount].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        ++gApCount;
    }
    /* Ordenem de mes forta a mes feble (bombolla; son poques). */
    for (uint8_t a = 0; a + 1 < gApCount; ++a) {
        for (uint8_t b = 0; b + 1 < gApCount - a; ++b) {
            if (gAps[b].rssi < gAps[b + 1].rssi) {
                const Net::Ap tmp = gAps[b];
                gAps[b]     = gAps[b + 1];
                gAps[b + 1] = tmp;
            }
        }
    }
    unlock();

    WiFi.scanDelete();
    gScanRunning = false;
    gScanDone    = true;
    Serial.printf("[NET] %u xarxes WiFi\n", static_cast<unsigned>(gApCount));
}

void netTask(void*)
{
    Serial.println(F("[NET] tasca de xarxa en marxa (nucli 0)"));
    for (;;) {
        if (gScanRequest) {
            gScanRequest = false;
            doScan();
        }

        if (WiFi.status() != WL_CONNECTED) {
            if (!connectWifi()) {
                gTimeSynced = false;
                /* Dormim, pero la comanda 'wifi' pot despertar-nos a l'instant. */
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15000));
                continue;
            }
        }

        if (!gTimeSynced) {
            syncTime();
        }

        const uint32_t now = millis();
        const bool periodDue = gWeather.valid && (now - gWeatherMs) >= NET_WEATHER_PERIOD_MS;
        const bool retryDue  = !gWeather.valid && (now - gLastTryMs) >= NET_WEATHER_RETRY_MS;
        if (gRefresh || periodDue || retryDue) {
            gRefresh   = false;
            gLastTryMs = now;
            fetchWeather();
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

}  // namespace

namespace Net {

void begin()
{
    gMutex = xSemaphoreCreateMutex();

    Preferences prefs;
    if (prefs.begin(kNamespace, /*readOnly=*/true)) {
        const String ssid = prefs.getString("ssid", "");
        const String pass = prefs.getString("pass", "");
        gLat = prefs.getFloat("lat", NET_DEFAULT_LATITUDE);
        gLon = prefs.getFloat("lon", NET_DEFAULT_LONGITUDE);
        prefs.end();
        if (ssid.length() > 0 && ssid.length() < sizeof(gSsid)) {
            strlcpy(gSsid, ssid.c_str(), sizeof(gSsid));
            strlcpy(gPass, pass.c_str(), sizeof(gPass));
        }
    }

#ifdef TG_HAVE_SECRETS
    if (gSsid[0] == '\0' && strcmp(SECRET_WIFI_SSID, kPlaceholderSsid) != 0) {
        strlcpy(gSsid, SECRET_WIFI_SSID, sizeof(gSsid));
        strlcpy(gPass, SECRET_WIFI_PASSWORD, sizeof(gPass));
    }
#endif

    Serial.printf("[NET] ubicacio %.4f, %.4f | credencials: %s\n",
                  static_cast<double>(gLat), static_cast<double>(gLon),
                  gSsid[0] != '\0' ? "si" : "NO (fes 'wifi <ssid> <contrasenya>')");
    if (gSsid[0] == '\0') {
        Serial.println(F("[NET] sense WiFi: el rellotge i la meteo no funcionaran"));
    }

    xTaskCreatePinnedToCore(netTask, "net", NET_TASK_STACK, nullptr, 1, &gTask, 0);
}

State state()
{
    return gState;
}

bool connected()
{
    return gState == State::Connected;
}

bool timeSynced()
{
    return gTimeSynced;
}

uint32_t epoch()
{
    return gEpoch;
}

unsigned weatherAgeSec()
{
    if (!gWeather.valid) {
        return 0;
    }
    return (millis() - gWeatherMs) / 1000u;
}

const Weather& weather()
{
    return gWeather;
}

bool hasCredentials()
{
    lock();
    const bool has = gSsid[0] != '\0';
    unlock();
    return has;
}

const char* ssid()
{
    static char copy[sizeof(gSsid)];
    lock();
    strlcpy(copy, gSsid, sizeof(copy));
    unlock();
    return copy;
}

bool setCredentials(const char* ssidIn, const char* passIn)
{
    if (ssidIn == nullptr || ssidIn[0] == '\0') {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        return false;
    }
    prefs.putString("ssid", ssidIn);
    prefs.putString("pass", passIn != nullptr ? passIn : "");
    prefs.end();

    lock();
    strlcpy(gSsid, ssidIn, sizeof(gSsid));
    strlcpy(gPass, passIn != nullptr ? passIn : "", sizeof(gPass));
    unlock();

    WiFi.disconnect(true);       /* perque la tasca torni a connectar amb les noves dades */
    gRefresh = true;
    if (gTask != nullptr) {
        xTaskNotifyGive(gTask);  /* despertar la tasca ara mateix */
    }
    return true;
}

bool clearCredentials()
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
        return false;
    }
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();

    lock();
    gSsid[0] = '\0';
    gPass[0] = '\0';
    unlock();

    gState = State::NoCredentials;
    WiFi.disconnect(true);
    if (gTask != nullptr) {
        xTaskNotifyGive(gTask);
    }
    return true;
}

void setLocation(float lat, float lon)
{
    Preferences prefs;
    if (prefs.begin(kNamespace, /*readOnly=*/false)) {
        prefs.putFloat("lat", lat);
        prefs.putFloat("lon", lon);
        prefs.end();
    }
    lock();
    gLat = lat;
    gLon = lon;
    unlock();
}

float latitude()
{
    return gLat;
}

float longitude()
{
    return gLon;
}

void requestRefresh()
{
    gRefresh = true;
}

void startScan()
{
    gScanDone    = false;
    gScanRunning = true;
    gScanRequest = true;
    if (gTask != nullptr) {
        xTaskNotifyGive(gTask);
    }
}

bool scanRunning()
{
    return gScanRunning;
}

bool scanDone()
{
    return gScanDone;
}

uint8_t scanCount()
{
    return gApCount;
}

Ap scanAp(uint8_t index)
{
    Ap out = {};
    lock();
    if (index < gApCount) {
        out = gAps[index];
    }
    unlock();
    return out;
}

const char* backgroundName()
{
    static char name[16];
    if (!gWeather.valid) {
        return "";
    }
    snprintf(name, sizeof(name), "weather_%02u", static_cast<unsigned>(gWeather.bgIndex));
    return name;
}

void printStatus()
{
    const char* st = "?";
    switch (gState) {
        case State::NoCredentials: st = "sense credencials"; break;
        case State::Connecting:    st = "connectant";        break;
        case State::Connected:     st = "connectat";         break;
        case State::Failed:        st = "error";             break;
    }

    Serial.printf("[NET] wifi: %s  ssid=\"%s\"  IP=%s  RSSI=%d dBm\n", st, ssid(),
                  connected() ? WiFi.localIP().toString().c_str() : "-",
                  connected() ? static_cast<int>(WiFi.RSSI()) : 0);
    Serial.printf("[NET] hora: %s", gTimeSynced ? "sincronitzada" : "NO sincronitzada");
    if (gTimeSynced) {
        struct tm now {};
        if (getLocalTime(&now, 0)) {
            Serial.printf(" (%02d:%02d:%02d)", now.tm_hour, now.tm_min, now.tm_sec);
        }
        Serial.printf("  epoch=%lu", static_cast<unsigned long>(gEpoch));
    }
    Serial.printf("\n[NET] ubicacio: %.4f, %.4f\n", static_cast<double>(latitude()),
                  static_cast<double>(longitude()));

    if (gWeather.valid) {
        const float temp = static_cast<float>(gWeather.temperature);
        const float wind = static_cast<float>(gWeather.windKmh);
        Serial.printf("[NET] meteo: %d C  codi %d  vent %d km/h  %s  -> %s (fa %u s)\n",
                      static_cast<int>(temp), static_cast<int>(gWeather.code),
                      static_cast<int>(wind), gWeather.isDay ? "dia" : "nit",
                      backgroundName(), weatherAgeSec());
    } else {
        Serial.println(F("[NET] meteo: sense dades encara"));
    }
}

}  // namespace Net
