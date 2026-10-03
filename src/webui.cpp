#include "webui.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "tg_config.h"

namespace {

WebServer gServer(80);
bool      gActive      = false;
uint32_t  gRequests    = 0;
char      gNotice[160] = {0};    /* misstage per ensenyar a la pagina */
File      gUpload;               /* fitxer que s'esta rebent */
char      gUploadPath[128] = {0};
uint32_t  gUploadBytes = 0;
volatile bool gUploaded = false;   /* s'ha pujat algu (per refrescar la musica) */
volatile bool gTestReq  = false;   /* autoprova demanada */
volatile bool gTestDump = false;   /* ... i que tambe envii la pagina */

/* --- Carpetes on es pot pujar --------------------------------------------- */
struct Folder {
    const char* path;    /* ruta dins la SD */
    const char* label;   /* nom per a la pagina */
};

const Folder kFolders[] = {
    {"/music",       "Musica (MP3 / WAV)"},
    {"/backgrounds", "Fons de pantalla (.bin)"},
    {"/pets",        "Mascotes (.bin)"},
    {"/",            "Arrel de la targeta"},
};
constexpr uint8_t kFolderCount = sizeof(kFolders) / sizeof(kFolders[0]);
/* Memoria minima per arrencar el servidor (es menja Strings i buffers). */
constexpr uint32_t kMinFreeHeap = 26000u;

/* Escapa el text per poder-lo posar dins l'HTML. */
String esc(const char* s)
{
    String out;
    if (s == nullptr) {
        return out;
    }
    for (const char* p = s; *p != '\0'; ++p) {
        switch (*p) {
            case '&':  out += F("&amp;");  break;
            case '<':  out += F("&lt;");   break;
            case '>':  out += F("&gt;");   break;
            case '"':  out += F("&quot;"); break;
            case '\'': out += F("&#39;");  break;
            default:   out += *p;          break;
        }
    }
    return out;
}

/* Passa %XX a bytes (els navegadors envien els accents aixi). */
void urlDecode(const String& in, char* out, size_t outLen)
{
    size_t n = 0;
    for (size_t i = 0; i < in.length() && n + 1 < outLen; ++i) {
        char c = in[i];
        if (c == '%' && i + 2 < in.length()) {
            const char hi = in[i + 1];
            const char lo = in[i + 2];
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') { return h - '0'; }
                if (h >= 'a' && h <= 'f') { return h - 'a' + 10; }
                if (h >= 'A' && h <= 'F') { return h - 'A' + 10; }
                return -1;
            };
            const int vh = hex(hi);
            const int vl = hex(lo);
            if (vh >= 0 && vl >= 0) {
                out[n++] = static_cast<char>((vh << 4) | vl);
                i += 2;
                continue;
            }
        }
        if (c == '+') {
            c = ' ';
        }
        out[n++] = c;
    }
    out[n] = '\0';
}

/* Neteja el nom pujat: ens quedem NOMES el nom (sense carpetes, sense "..") i
 * traiem els caracters que donen problemes (es conserven els accents UTF-8). */
void sanitizeName(const String& in, char* out, size_t outLen)
{
    char tmp[128];
    urlDecode(in, tmp, sizeof(tmp));

    size_t n = 0;
    for (size_t i = 0; tmp[i] != '\0' && n + 1 < outLen; ++i) {
        const char c = tmp[i];
        if (c == '\\' || c == '/') {     /* talla el cami: nomes el nom */
            n = 0;
            continue;
        }
        const uint8_t u = static_cast<uint8_t>(c);
        const bool safe = (u >= 0x80)                       /* UTF-8 (accents) */
                          || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
                          || (c >= 'a' && c <= 'z') || c == '.' || c == '_'
                          || c == '-' || c == ' ' || c == '(' || c == ')';
        if (safe) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
}


/* --- Pagina (CSS rosa/lila, estilo mobil) --------------------------------- */
const char kHead[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=ca><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Tamagoxi</title><style>
*{box-sizing:border-box}
body{margin:0;padding:16px;font-family:system-ui,-apple-system,sans-serif;
  background:linear-gradient(165deg,#ffe3f4,#ece2ff 55%,#ddf1ff);color:#4a2b56;
  min-height:100vh}
h1{font-size:1.45rem;margin:.1rem 0 1rem;text-align:center}
.card{background:#fff;border-radius:18px;padding:15px;margin:0 auto 13px;
  max-width:540px;box-shadow:0 6px 18px rgba(150,90,180,.16)}
label{font-weight:700;font-size:.88rem;display:block;margin:10px 0 5px}
select,input[type=file]{width:100%;padding:10px;border-radius:12px;
  border:2px solid #eedcf8;background:#fdf8ff;font-size:.95rem;color:#4a2b56}
button{background:linear-gradient(135deg,#f7a8d8,#b79cf0);border:0;color:#fff;
  font-weight:800;font-size:1rem;padding:13px;border-radius:14px;width:100%;
  margin-top:14px;cursor:pointer}
button:active{transform:scale(.98)}
ul{list-style:none;margin:0;padding:0}
li{display:flex;align-items:center;gap:8px;padding:9px 2px;
  border-bottom:1px solid #f5eafc;font-size:.92rem}
li span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li small{color:#ad8fc2;font-size:.8rem}
a.del{background:#ffe1ea;color:#c2335f;border-radius:10px;padding:6px 9px;
  text-decoration:none;font-weight:800;font-size:.82rem}
.ok{background:#e8ffec;border-left:6px solid #5ec26a;padding:10px 12px;
  border-radius:10px;margin-bottom:8px;font-size:.92rem}
.info{font-size:.88rem;color:#7b5a8f}
.note{font-size:.8rem;color:#ad8fc2;text-align:center;margin:6px auto 24px;
  max-width:540px}
em{font-style:normal;font-weight:700;color:#8f6bb0}
</style></head><body><h1>&#128009; Tamagoxi</h1>
)HTML";

const char kFoot[] PROGMEM = R"HTML(
<p class=note>Servidor web de la placa. Arriba-hi la targeta SD per WiFi: musica,
fons i mascotes. Un cop pujats, toca <em>music scan</em> perque sonin.</p>
</body></html>)HTML";

/* Construeix la pagina: estat (WiFi + SD), formulari de pujada i llista. */
String buildPage(const String& folder)
{
    String p;
    p.reserve(2600);
    p += FPSTR(kHead);

    /* Estat: WiFi i espai a la SD. */
    p += F("<div class=card><label>Estat</label><div class=info>");
    if (WiFi.status() == WL_CONNECTED) {
        p += F("WiFi <em>");
        p += esc(WiFi.SSID().c_str());
        p += F("</em> &middot; IP <em>");
        p += WiFi.localIP().toString();
        p += F("</em>");
    } else {
        p += F("Punt d'acces <em>Tamagoxi</em> &middot; IP <em>");
        p += WiFi.softAPIP().toString();
        p += F("</em>");
    }
    const uint32_t totalMb = static_cast<uint32_t>(SD.totalBytes() / 1048576u);
    const uint32_t usedMb  = static_cast<uint32_t>(SD.usedBytes() / 1048576u);
    p += F("<br>Targeta SD: <em>");
    p += String(usedMb);
    p += F(" MB</em> usats de <em>");
    p += String(totalMb);
    p += F(" MB</em> &middot; memoria de la placa: <em>");
    p += String(ESP.getFreeHeap() / 1024u);
    p += F(" kB</em></div></div>");

    if (gNotice[0] != '\0') {
        p += F("<div class=card><div class=ok>");
        p += esc(gNotice);
        p += F("</div></div>");
    }

    /* Formulari de pujada. */
    p += F("<div class=card><label>Pujar fitxers a la SD</label>"
           "<form method=POST action=/upload enctype=multipart/form-data>"
           "<label>Carpeta de desti</label><select name=folder>");
    for (uint8_t i = 0; i < kFolderCount; ++i) {
        p += F("<option value=\"");
        p += kFolders[i].path;
        p += F("\"");
        if (folder == kFolders[i].path) {
            p += F(" selected");
        }
        p += F(">");
        p += kFolders[i].label;
        p += F("</option>");
    }
    p += F("</select><label>Fitxers (pots triar-ne uns quants)</label>"
           "<input type=file name=file multiple>"
           "<button type=submit>Pujar a la SD &#128190;</button></form></div>");

    /* Llista de fitxers de la carpeta. */
    const Folder* cur = &kFolders[0];
    for (uint8_t i = 0; i < kFolderCount; ++i) {
        if (folder == kFolders[i].path) {
            cur = &kFolders[i];
            break;
        }
    }
    p += F("<div class=card><label>");
    p += cur->label;
    p += F(" &middot; ");
    p += cur->path;
    p += F("</label><ul>");

    File dir = SD.open(cur->path);
    uint16_t count = 0;
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f && count < 200) {
            const char* nm = f.name();
            const char* base = strrchr(nm, '/');
            base = (base != nullptr) ? base + 1 : nm;
            if (!f.isDirectory() && base[0] != '.') {
                String path = String(cur->path);
                if (path != "/") {
                    path += '/';
                }
                path += base;
                p += F("<li><span>");
                p += esc(base);
                p += F("</span><small>");
                p += String(static_cast<uint32_t>(f.size() / 1024u));
                p += F(" kB</small><a class=del href=\"/del?p=");
                p += esc(path.c_str());
                p += F("\">Esborrar</a></li>");
                ++count;
            }
            f = dir.openNextFile();
        }
        if (count == 0) {
            p += F("<li><span>Encara no hi ha cap fitxer aqui.</span></li>");
        }
    } else {
        p += F("<li><span>No s'ha pogut obrir la carpeta.</span></li>");
    }
    dir.close();
    p += F("</ul></div>");
    p += FPSTR(kFoot);
    return p;
}

/* --- Peticions HTTP -------------------------------------------------------- */

void sendPage(const String& folder)
{
    gServer.sendHeader("Cache-Control", "no-store");
    gServer.send(200, "text/html; charset=utf-8", buildPage(folder));
}

String currentFolder()
{
    if (gServer.hasArg("f")) {
        return gServer.arg("f");
    }
    if (gServer.hasArg("folder")) {
        return gServer.arg("folder");
    }
    return String(kFolders[0].path);
}

bool knownFolder(const String& folder)
{
    for (uint8_t i = 0; i < kFolderCount; ++i) {
        if (folder == kFolders[i].path) {
            return true;
        }
    }
    return false;
}

void handleRoot()
{
    ++gRequests;
    sendPage(currentFolder());
}

void handleNotFound()
{
    ++gRequests;
    gServer.sendHeader("Location", "/", true);
    gServer.send(302, "text/plain", "");
}

/* Rep un fitxer a trossos i els va escrivint a la SD (sense passar per RAM). */
void handleUpload()
{
    const HTTPUpload& up = gServer.upload();

    if (up.status == UPLOAD_FILE_START) {
        char name[96];
        sanitizeName(up.filename, name, sizeof(name));
        String folder = currentFolder();
        if (!knownFolder(folder)) {
            folder = kFolders[0].path;      /* per seguretat, mai fora d'aqui */
        }
        String path = folder;
        if (path != "/") {
            path += '/';
        }
        path += name;
        strlcpy(gUploadPath, path.c_str(), sizeof(gUploadPath));
        gUploadBytes = 0;

        gUpload = SD.open(path.c_str(), FILE_WRITE);
        if (!gUpload) {
            snprintf(gNotice, sizeof(gNotice), "No s'ha pogut crear %s (targeta plena?)",
                     path.c_str());
            Serial.printf("[WEB] error creant %s\n", path.c_str());
        } else {
            Serial.printf("[WEB] rebent %s ...\n", path.c_str());
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (gUpload) {
            gUpload.write(up.buf, up.currentSize);
            gUploadBytes += up.currentSize;
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (gUpload) {
            gUpload.close();
            snprintf(gNotice, sizeof(gNotice),
                     "Pujat %s (%.1f MB) a %s. Toca 'music scan' per veure'l.",
                     up.filename.c_str(), static_cast<double>(gUploadBytes) / 1048576.0,
                     gUploadPath);
            Serial.printf("[WEB] pujat %s (%u B)\n", gUploadPath,
                          static_cast<unsigned>(gUploadBytes));
            gUploaded = true;
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (gUpload) {
            gUpload.close();
        }
        snprintf(gNotice, sizeof(gNotice), "Pujada cancel·lada");
        Serial.println(F("[WEB] pujada cancel·lada"));
    }
    yield();
}

/* En acabar la peticio, tornem a la pagina de la carpeta. */
void handleUploadDone()
{
    ++gRequests;
    const String folder = currentFolder();
    gServer.sendHeader("Location", "/?f=" + folder, true);
    gServer.send(302, "text/plain", "");
}

/* Esborra un fitxer de la SD (nomes dins les carpetes permeses). */
void handleDelete()
{
    ++gRequests;
    const String p = gServer.arg("p");

    bool ok = p.startsWith("/") && p.indexOf("..") < 0 && p.length() > 1;
    if (ok && !knownFolder("/")) {
        /* (l'arrel sempre hi es; el filtre de ".." es el que protegeix) */
    }
    String folder = kFolders[0].path;
    if (p.startsWith("/music/"))         { folder = "/music"; }
    else if (p.startsWith("/backgrounds/")) { folder = "/backgrounds"; }
    else if (p.startsWith("/pets/"))        { folder = "/pets"; }
    else if (p.lastIndexOf('/') <= 0)       { folder = "/"; }

    if (ok && SD.remove(p.c_str())) {
        snprintf(gNotice, sizeof(gNotice), "Esborrat %s", p.c_str());
        Serial.printf("[WEB] esborrat %s\n", p.c_str());
    } else if (ok) {
        snprintf(gNotice, sizeof(gNotice), "No s'ha pogut esborrar %s", p.c_str());
        Serial.printf("[WEB] no s'ha pogut esborrar %s\n", p.c_str());
    } else {
        snprintf(gNotice, sizeof(gNotice), "Ruta no permesa");
    }

    gServer.sendHeader("Location", "/?f=" + folder, true);
    gServer.send(302, "text/plain", "");

}

}  // namespace (auxiliars)

namespace WebUI {

/* (declarat abans perque dumpPage/loop tambe els fan servir) */
String httpSelf(const String& req, uint32_t timeoutMs);
bool   selfTest();
void   dumpPage();

bool begin()
{
    if (gActive) {
        return true;
    }
    if (WiFi.getMode() == WIFI_OFF) {
        Serial.println(F("[WEB] cal el WiFi engegat (comanda 'wifi' o 'ap')"));
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        Serial.println(F("[WEB] no hi ha targeta SD: no puc obrir el servidor"));
        return false;
    }
    if (ESP.getFreeHeap() < kMinFreeHeap) {
        Serial.printf("[WEB] memoria insuficient (%u kB, en calen %u): no arrenco\n",
                      static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                      static_cast<unsigned>(kMinFreeHeap / 1024));
        return false;
    }

    static bool routesReady = false;
    if (!routesReady) {
        gServer.on("/", HTTP_GET, handleRoot);
        gServer.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
        gServer.on("/del", HTTP_GET, handleDelete);
        gServer.onNotFound(handleNotFound);
        routesReady = true;
    }

    gServer.begin();
    gActive = true;
    gNotice[0] = '\0';

    /* Nom amigable: http://tamagoxi.local */
    if (MDNS.begin("tamagoxi")) {
        MDNS.addService("http", "tcp", 80);
    }

    Serial.printf("[WEB] servidor a punt: http://%s/  (memoria %u kB)\n",
                  (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str()
                                                  : WiFi.softAPIP().toString().c_str(),
                  static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    return true;
}

void stop()
{
    if (!gActive) {
        return;
    }
    gServer.stop();
    MDNS.end();
    gActive = false;
    Serial.println(F("[WEB] servidor web aturat"));
}

bool active()
{
    return gActive;
}

void loop()
{
    if (!gActive) {
        return;
    }
    if (gTestReq) {
        gTestReq = false;
        const bool dump = gTestDump;
        gTestDump = false;
        selfTest();
        if (dump) {
            dumpPage();
        }
    }
    gServer.handleClient();
}

uint32_t requests()
{
    return gRequests;
}

bool takeUploaded()
{
    const bool v = gUploaded;
    gUploaded = false;
    return v;
}

/* Demana una autoprova: la fa la tasca de xarxa dins loop() (si es fes des d'una
 * altra tasca, el socket de l'autopeticio es queda encallat i la placa reinicia). */
void requestSelfTest(bool dump)
{
    gTestDump = dump;
    gTestReq  = true;
    Serial.println(F("[WEB] autoprova programada; la fa la tasca de xarxa"));
}

/* Envia la pagina pel port serie (per revisar-la des de l'ordinador). */
void dumpPage()
{
    const String r = httpSelf(String("GET / HTTP/1.0\r\nHost: tamagoxi\r\n\r\n"), 4000);
    const int i = r.indexOf("\r\n\r\n");
    Serial.println(F("----- inici de la pagina web -----"));
    Serial.println(i > 0 ? r.substring(i + 4) : r);
    Serial.println(F("----- fi de la pagina web -----"));
}

/* Mig HTTP fet a ma: envia una peticio a nosaltres mateixos i torna la resposta.
 * Prova primer el loopback (127.0.0.1) i despres la nostra propia IP. */
String httpSelf(const String& req, uint32_t timeoutMs)
{
    for (int intent = 0; intent < 2; ++intent) {
        WiFiClient c;
        const IPAddress ip = (intent == 0)
                                 ? IPAddress(127, 0, 0, 1)
                                 : ((WiFi.status() == WL_CONNECTED) ? WiFi.localIP()
                                                                    : WiFi.softAPIP());
        if (!c.connect(ip, 80)) {
            continue;
        }
        Serial.printf("[WEB] autoprova connectant a %s\n", ip.toString().c_str());
        c.print(req);

        String resp;
        resp.reserve(3000);
        const uint32_t t0 = millis();
        while (millis() - t0 < timeoutMs) {
            /* Som a la tasca de xarxa: mentre esperem la resposta, atenem la
             * peticio que acabem d'enviar nosaltres mateixos. */
            gServer.handleClient();
            while (c.available() > 0) {
                resp += static_cast<char>(c.read());
            }
            if (!c.connected() && c.available() == 0 && resp.length() > 0) {
                break;
            }
            if (resp.endsWith("</html>") || resp.indexOf("HTTP/1.1 302") > 0
                || resp.indexOf("HTTP/1.1 200") > 0) {
                break;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        c.stop();
        return resp;
    }
    return String("ERROR: no s'ha pogut connectar (ni 127.0.0.1 ni la IP local)");
}

bool selfTest()
{
    if (!gActive) {
        Serial.println(F("[WEB] el servidor no esta en marxa (web on / ap / wifi)"));
        return false;
    }
    bool allOk = true;

    /* 1) GET / : la pagina principal. */
    const String r1 = httpSelf(String("GET / HTTP/1.0\r\nHost: tamagoxi\r\n\r\n"), 4000);
    const bool okGet  = r1.indexOf(" 200 ") > 0 || r1.startsWith("HTTP/1.");
    const bool okHtml = r1.indexOf("<title>Tamagoxi") > 0;
    const bool okForm = r1.indexOf("Pujar fitxers a la SD") > 0;
    allOk = allOk && okGet && okHtml && okForm;
    Serial.printf("[WEB] GET /  -> resposta=%s html=%s formulari=%s (%u B)\n",
                  okGet ? "si" : "NO", okHtml ? "si" : "NO", okForm ? "si" : "NO",
                  static_cast<unsigned>(r1.length()));

    /* 2) POST /upload : pugem un fitxer de prova i mirem que sigui a la SD. */
    const char* kTest = "/music/prova_web.txt";
    SD.remove(kTest);
    const String text = F("Prova de pujada pel servidor web de la Tamagoxi.\n");
    String body = F("--TGBOUND\r\nContent-Disposition: form-data; name=\"folder\"\r\n\r\n"
                    "/music\r\n--TGBOUND\r\nContent-Disposition: form-data; name=\"file\";"
                    " filename=\"prova_web.txt\"\r\nContent-Type: text/plain\r\n\r\n");
    body += text;
    body += F("\r\n--TGBOUND--\r\n");
    String req = F("POST /upload HTTP/1.0\r\nHost: tamagoxi\r\n"
                   "Content-Type: multipart/form-data; boundary=TGBOUND\r\nContent-Length: ");
    req += String(body.length());
    req += F("\r\n\r\n");
    req += body;

    const String r2 = httpSelf(req, 8000);
    File f = SD.open(kTest, FILE_READ);
    const bool okFile = static_cast<bool>(f);
    if (okFile) {
        f.close();
        SD.remove(kTest);
    }
    allOk = allOk && okFile;
    Serial.printf("[WEB] POST /upload -> fitxer a la SD: %s (%s)\n",
                  okFile ? "SI" : "NO", okFile ? "esborrat despres" : "no hi es");

    if (allOk) {
        Serial.println(F("[WEB] autoprova correcta: el servidor i la pujada funcionen"));
    } else {
        Serial.println(F("[WEB] autoprova FALLIDA (mira els missatges de dalt)"));
    }
    return allOk;
}

void printStatus()
{
    Serial.printf("[WEB] servidor=%s peticions=%lu memoria=%u kB  adreca=http://%s/\n",
                  gActive ? "en marxa" : "aturat",
                  static_cast<unsigned long>(gRequests),
                  static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                  (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str()
                                                  : WiFi.softAPIP().toString().c_str());
}

}  // namespace WebUI
