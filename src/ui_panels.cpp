#include "ui_panels.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>

#include "audio.h"
#include "tg_config.h"
#include "display.h"
#include "led.h"
#include "net.h"
#include "pet.h"
#include "sd_assets.h"
#include "storage.h"
#include "ui.h"

namespace {

TFT_eSPI& tft()
{
    return Display::driver();
}

constexpr uint16_t kText     = TFT_WHITE;
constexpr uint16_t kDimText  = 0x9CD3;
constexpr uint16_t kGood     = 0x07E0;
constexpr uint16_t kBad      = 0xF800;
constexpr uint16_t kWarn     = 0xFE60;

/* --- Desplegable de dalt -------------------------------------------------- */
constexpr int kMenuX = SCREEN_W - 212;
constexpr int kMenuY = UI_HUD_H;
constexpr int kMenuW = 208;
constexpr int kRowH  = 44;
constexpr int kRowGap = 4;
constexpr int kMenuRows = 6;
constexpr int kMenuH = 4 + kMenuRows * kRowH + (kMenuRows - 1) * kRowGap + 4;   /* 244 */

/* --- Panell de musica ------------------------------------------------------ */
constexpr int kNpY       = 44;      /* fitxa "sonant ara" */
constexpr int kProgY     = 102;     /* barra de progrés */
constexpr int kTransY    = 120;     /* transport */
constexpr int kTransH    = 54;
constexpr int kVolY      = 180;     /* volum */
constexpr int kVolH      = 44;
constexpr int kMListTop  = 236;     /* llista de cançons */
constexpr int kMListRows = 4;
constexpr int kMRowH     = 44;
constexpr int kMBtnY     = 414;
constexpr int kMBtnH     = 60;

/* --- Panells a pantalla completa ------------------------------------------ */
constexpr int kTitleH = 40;
constexpr int kCloseW = 46;

/* --- Teclat --------------------------------------------------------------- */
constexpr int kKeyH  = 44;
constexpr int kKeyGapY = 4;
constexpr int kKeyW  = 29;
constexpr int kKeyGapX = 2;
constexpr int kKbLeft = 6;
constexpr int kKbTop = 288;

enum : uint8_t { K_CHAR = 0, K_SHIFT, K_BACK, K_SPACE, K_MODE, K_OK };

struct Key {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint8_t type;
    char    ch;
    char    label[10];
};

Key     gKeys[44];
uint8_t gKeyCount = 0;

constexpr uint8_t kPassMax = 63;

Panels::Id gId = Panels::Id::None;

enum class WifiView : uint8_t { List, Password, Connecting, Result };
WifiView gWifiView = WifiView::List;

char     gSsid[33]    = {0};
char     gPass[kPassMax + 1] = {0};
uint8_t  gPassLen     = 0;
bool     gPassShown   = true;
bool     gShift       = false;
bool     gSymbols     = false;
uint8_t  gListTop     = 0;
uint8_t  gMusicTop    = 0;
uint8_t  gSelected    = 0;
uint32_t gStartedMs   = 0;
bool     gConnected   = false;
char     gMsg[48]     = {0};
uint8_t  gTargetBright = 100;

/* --- Ajudes de dibuix ------------------------------------------------------ */

uint16_t rssiColor(int8_t rssi)
{
    if (rssi >= -60) {
        return kGood;
    }
    if (rssi >= -72) {
        return kWarn;
    }
    return kBad;
}

uint8_t rssiBars(int8_t rssi)
{
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -65) {
        return 3;
    }
    if (rssi >= -75) {
        return 2;
    }
    return 1;
}

/* Barres de senyal (4). 'n' = barres plenes. */
void drawBars(int x, int y, uint8_t n, uint16_t col, uint16_t bg)
{
    TFT_eSPI& t = tft();
    for (uint8_t i = 0; i < 4; ++i) {
        const int bh = 5 + i * 4;
        const int bx = x + i * 7;
        const uint16_t c = (i < n) ? col : bg;
        t.fillRect(bx, y - bh, 5, bh, c);
    }
}

/* Cadenat petit (xarxa amb contrasenya). */
void drawLock(int x, int y, uint16_t col)
{
    TFT_eSPI& t = tft();
    t.drawCircle(x, y - 3, 3, col);            /* arc del dalt */
    t.fillRect(x - 4, y - 1, 9, 8, col);       /* cos */
    t.fillRect(x - 1, y + 1, 2, 3, Ui::colorPanelBg());
}

}  // namespace

namespace {

/* --- Elements comuns ------------------------------------------------------- */

void drawTitleBar(const char* title)
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, kTitleH, Ui::colorPanelBg());
    t.drawLine(0, kTitleH - 1, SCREEN_W - 1, kTitleH - 1, Ui::colorPanelEdge());

    t.setTextDatum(ML_DATUM);
    t.setTextFont(4);
    t.setTextColor(kText, Ui::colorPanelBg());

    /* El titol no pot trepitjar el boto de tancar. */
    char shown[26];
    strlcpy(shown, title, sizeof(shown));
    const int maxW = SCREEN_W - kCloseW - 20;
    while (t.textWidth(shown) > maxW && strlen(shown) > 3) {
        shown[strlen(shown) - 1] = '\0';
    }
    t.drawString(shown, 10, kTitleH / 2);

    const int bx = SCREEN_W - kCloseW;
    t.fillRoundRect(bx + 4, 4, kCloseW - 8, kTitleH - 8, 6, Ui::colorClose());
    const int cx = bx + kCloseW / 2;
    const int cy = kTitleH / 2;
    t.drawLine(cx - 8, cy - 8, cx + 8, cy + 8, kText);
    t.drawLine(cx - 8, cy + 8, cx + 8, cy - 8, kText);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t col,
                bool pressed = false)
{
    TFT_eSPI& t = tft();
    const uint16_t c = pressed ? kText : col;
    /* Text clar o fosc segons com de clar es el fons, perque sempre es llegeixi. */
    const uint16_t r = static_cast<uint16_t>(((c >> 11) & 0x1F) * 255 / 31);
    const uint16_t g = static_cast<uint16_t>(((c >> 5) & 0x3F) * 255 / 63);
    const uint16_t b = static_cast<uint16_t>((c & 0x1F) * 255 / 31);
    const uint16_t lum = static_cast<uint16_t>((r * 299 + g * 587 + b * 114) / 1000);
    const uint16_t tc = pressed ? col : ((lum > 120) ? TFT_BLACK : kText);
    t.fillRoundRect(x, y, w, h, 8, c);
    t.drawRoundRect(x, y, w, h, 8, Ui::colorPanelEdge());
    t.setTextDatum(MC_DATUM);
    t.setTextFont(2);
    t.setTextColor(tc, c);
    t.drawString(label, x + w / 2, y + h / 2);
}

void drawSmallText(int x, int y, const char* txt, uint16_t col)
{
    TFT_eSPI& t = tft();
    t.setTextDatum(ML_DATUM);
    t.setTextFont(1);
    t.setTextColor(col, Ui::colorPanelBg());
    t.drawString(txt, x, y);
}

/* --- Icones dels botons del desplegable ----------------------------------- */

void iconWifi(int cx, int cy, bool on)
{
    drawBars(cx - 12, cy + 9, on ? 4 : 1, kText, Ui::colorRow(0));
}

void iconGame(int cx, int cy)
{
    TFT_eSPI& t = tft();
    t.fillRoundRect(cx - 14, cy - 8, 28, 16, 7, kText);
    t.fillCircle(cx - 6, cy, 2, Ui::colorRow(1));
    t.fillCircle(cx + 6, cy, 2, Ui::colorRow(1));
}

void iconMusic(int cx, int cy)
{
    TFT_eSPI& t = tft();
    t.fillRect(cx - 4, cy - 11, 3, 18, kText);
    t.fillRect(cx + 7, cy - 14, 3, 18, kText);
    t.fillRect(cx - 4, cy - 11, 14, 3, kText);
    t.fillCircle(cx - 7, cy + 7, 4, kText);
    t.fillCircle(cx + 4, cy + 4, 4, kText);
}

void iconSliders(int cx, int cy)
{
    TFT_eSPI& t = tft();
    for (int i = -1; i <= 1; ++i) {
        t.drawLine(cx - 11, cy + i * 6, cx + 11, cy + i * 6, kText);
        t.fillCircle(cx + ((i == 0) ? 5 : -4), cy + i * 6, 3, kText);
    }
}

void iconInfo(int cx, int cy)
{
    TFT_eSPI& t = tft();
    t.drawCircle(cx, cy, 11, kText);
    t.fillRect(cx - 1, cy - 5, 3, 3, kText);
    t.fillRect(cx - 1, cy - 1, 3, 7, kText);
}

/* Runa del Bluetooth (barra central + dues diagonals). */
void iconBluetooth(int cx, int cy)
{
    TFT_eSPI& t = tft();
    constexpr int h = 8;
    constexpr int d = 5;
    t.drawLine(cx, cy - h, cx, cy + h, kText);
    t.drawLine(cx - d, cy - d, cx + d, cy + d, kText);
    t.drawLine(cx - d, cy + d, cx + d, cy - d, kText);
}

/* --- Declaracions dels panells -------------------------------------------- */

void showWifi();
void showMusic();
void showBluetooth();
void drawBluetooth();
void saveBtSnapshot();
bool btChanged();
void drawMusic();
void showSettings();
void showAbout();
void showGames();
void drawWifi();
void drawWifiPassword();
void drawWifiList();
void drawWifiStrip();
void drawWifiBottom();
void drawKeyboard();
void drawKey(uint8_t index, bool pressed);
void buildKeyboard();
void drawPassField();
void drawWifiMessage();

/* --- Desplegable de dalt --------------------------------------------------- */

void drawTopMenu()
{
    TFT_eSPI& t = tft();
    t.fillRoundRect(kMenuX, kMenuY, kMenuW, kMenuH, 10, Ui::colorPanelBg());
    t.drawRoundRect(kMenuX, kMenuY, kMenuW, kMenuH, 10, Ui::colorPanelEdge());

    const char* labels[kMenuRows] = {"WiFi", "Musica", "Bluetooth", "Jocs", "Ajustos", "Sobre"};
    for (int i = 0; i < kMenuRows; ++i) {
        const int x = kMenuX + 4;
        const int y = kMenuY + 4 + i * (kRowH + kRowGap);
        const int w = kMenuW - 8;
        const uint16_t col = Ui::colorRow(static_cast<uint8_t>(i));
        t.fillRoundRect(x, y, w, kRowH, 8, col);

        const int icx = x + 24;
        const int icy = y + kRowH / 2;
        switch (i) {
            case 0: iconWifi(icx, icy, Net::connected()); break;
            case 1: iconMusic(icx, icy); break;
            case 2: iconBluetooth(icx, icy); break;
            case 3: iconGame(icx, icy); break;
            case 4: iconSliders(icx, icy); break;
            default: iconInfo(icx, icy); break;
        }

        /* Segona linia d'estat (WiFi i Bluetooth). */
        const char* sub = nullptr;
        if (i == 0) {
            sub = Net::connected() ? "connectat"
                                   : (Net::hasCredentials() ? "desconnectat" : "sense configurar");
        } else if (i == 2) {
            switch (Audio::btMode()) {
                case Audio::BtMode::Source: sub = "auriculars"; break;
                case Audio::BtMode::Sink:   sub = "altaveu";    break;
                default:                    sub = "apagat";     break;
            }
        }

        t.setTextDatum(ML_DATUM);
        t.setTextFont(2);
        t.setTextColor(kText, col);
        t.drawString(labels[i], x + 46, (sub != nullptr) ? (icy - 7) : icy);

        if (sub != nullptr) {
            t.setTextFont(1);
            t.setTextColor(kText, col);
            t.drawString(sub, x + 46, icy + 9);
        }
    }
}

/* --- Panell de WiFi: llista de xarxes -------------------------------------- */

constexpr int kListTop  = 84;
constexpr int kListRowH = 44;
constexpr int kListRows = 7;
constexpr int kListW    = 296;
constexpr int kBtnY     = 410;
constexpr int kBtnH     = 60;

void drawWifiStrip()
{
    TFT_eSPI& t = tft();
    const int y = kTitleH;
    const int h = kListTop - kTitleH;
    t.fillRect(0, y, SCREEN_W, h, Ui::colorTrack());
    t.drawLine(0, kListTop - 1, SCREEN_W - 1, kListTop - 1, Ui::colorPanelEdge());

    const bool on = Net::connected();
    drawBars(16, y + h - 8, on ? 4 : 1, on ? kGood : kBad, Ui::colorTrack());

    t.setTextDatum(ML_DATUM);
    t.setTextFont(2);
    t.setTextColor(kText, Ui::colorTrack());
    if (on) {
        t.drawString(Net::ssid(), 46, y + 12);
        t.setTextFont(1);
        t.drawString(WiFi.localIP().toString().c_str(), 46, y + 28);
    } else {
        t.drawString(Net::hasCredentials() ? "Desconnectat" : "Sense WiFi configurat", 46, y + 11);
        t.setTextFont(1);
        t.drawString("Tria una xarxa de la llista", 46, y + 28);
    }
}

void drawWifiList()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, kListTop, SCREEN_W, SCREEN_H - kListTop, Ui::colorPanelBg());

    const uint8_t n = Net::scanCount();

    if (Net::scanRunning()) {
        t.setTextDatum(MC_DATUM);
        t.setTextFont(2);
        t.setTextColor(kWarn, Ui::colorPanelBg());
        t.drawString("Cercant xarxes...", SCREEN_W / 2, kListTop + 70);
    } else if (n == 0) {
        t.setTextDatum(MC_DATUM);
        t.setTextFont(2);
        t.setTextColor(kDimText, Ui::colorPanelBg());
        t.drawString("Cap xarxa trobada", SCREEN_W / 2, kListTop + 50);
        t.drawString("Prem Cerca xarxes", SCREEN_W / 2, kListTop + 80);
    }

    for (uint8_t i = 0; i < kListRows; ++i) {
        const uint8_t idx = static_cast<uint8_t>(gListTop + i);
        if (idx >= n) {
            break;
        }
        const Net::Ap ap = Net::scanAp(idx);
        const int x = 4;
        const int y = kListTop + i * kListRowH;
        const uint16_t col = (idx == gSelected) ? Ui::colorAccent() : Ui::colorTrack();

        t.fillRoundRect(x, y + 1, kListW, kListRowH - 4, 7, col);
        drawBars(x + 14, y + kListRowH / 2 + 8, rssiBars(ap.rssi), rssiColor(ap.rssi), col);

        t.setTextDatum(ML_DATUM);
        t.setTextFont(2);
        t.setTextColor(kText, col);
        char label[26];
        strlcpy(label, ap.ssid, sizeof(label));
        while (t.textWidth(label) > 190 && strlen(label) > 4) {
            label[strlen(label) - 1] = '\0';
        }
        t.drawString(label, x + 50, y + kListRowH / 2);

        if (ap.secure) {
            drawLock(x + kListW - 18, y + kListRowH / 2, kText);
        } else {
            t.setTextFont(1);
            t.drawString("oberta", x + kListW - 44, y + kListRowH / 2);
        }
    }

    /* Fletxes de desplacament (nomes si calen). */
    if (n > kListRows) {
        const int ax = SCREEN_W - 16;
        t.fillRoundRect(ax - 7, kListTop + 4, 20, 40, 5, Ui::colorTrack());
        t.fillTriangle(ax, kListTop + 13, ax - 6, kListTop + 25, ax + 6, kListTop + 25, kText);
        const int by = kListTop + kListRows * kListRowH - 44;
        t.fillRoundRect(ax - 7, by, 20, 40, 5, Ui::colorTrack());
        t.fillTriangle(ax, by + 27, ax - 6, by + 15, ax + 6, by + 15, kText);
    }
}

void drawWifiBottom()
{
    drawButton(8, kBtnY, 216, kBtnH, Net::scanRunning() ? "Cercant..." : "Cerca xarxes",
               Ui::colorRow(1));
    drawButton(232, kBtnY, 80, kBtnH, "Tanca", Ui::colorClose());
}

void drawWifi()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, SCREEN_H, Ui::colorPanelBg());

    if (gWifiView == WifiView::List) {
        drawTitleBar("WiFi");
        drawWifiStrip();
        drawWifiList();
        drawWifiBottom();
        return;
    }
    drawWifiPassword();
}

/* --- Teclat en pantalla ---------------------------------------------------- */

void addKey(int x, int y, int w, int h, uint8_t type, char ch, const char* label)
{
    if (gKeyCount >= sizeof(gKeys) / sizeof(gKeys[0])) {
        return;
    }
    Key& k = gKeys[gKeyCount++];
    k.x     = static_cast<int16_t>(x);
    k.y     = static_cast<int16_t>(y);
    k.w     = static_cast<int16_t>(w);
    k.h     = static_cast<int16_t>(h);
    k.type  = type;
    k.ch    = ch;
    k.label[0] = '\0';
    if (label != nullptr) {
        strlcpy(k.label, label, sizeof(k.label));
    } else if (ch != 0) {
        k.label[0] = ch;
        k.label[1] = '\0';
    }
}

void buildKeyboard()
{
    gKeyCount = 0;
    const char* rows[3];
    if (gSymbols) {
        rows[0] = "1234567890";
        rows[1] = "-_=+.,;:";
        rows[2] = "/!?@#&%*$";
    } else if (gShift) {
        rows[0] = "QWERTYUIOP";
        rows[1] = "ASDFGHJKL";
        rows[2] = "ZXCVBNM";
    } else {
        rows[0] = "qwertyuiop";
        rows[1] = "asdfghjkl";
        rows[2] = "zxcvbnm";
    }

    const int y0 = kKbTop;
    const int y1 = y0 + kKeyH + kKeyGapY;
    const int y2 = y1 + kKeyH + kKeyGapY;
    const int y3 = y2 + kKeyH + kKeyGapY;

    for (int i = 0; rows[0][i] != '\0'; ++i) {
        addKey(kKbLeft + i * (kKeyW + kKeyGapX), y0, kKeyW, kKeyH, K_CHAR, rows[0][i], nullptr);
    }
    for (int i = 0; rows[1][i] != '\0'; ++i) {
        addKey(kKbLeft + 16 + i * (kKeyW + kKeyGapX), y1, kKeyW, kKeyH, K_CHAR, rows[1][i],
               nullptr);
    }
    if (gSymbols) {
        for (int i = 0; rows[2][i] != '\0'; ++i) {
            addKey(kKbLeft + i * (kKeyW + kKeyGapX), y2, kKeyW, kKeyH, K_CHAR, rows[2][i],
                   nullptr);
        }
        addKey(kKbLeft + 9 * (kKeyW + kKeyGapX), y2, kKeyW, kKeyH, K_BACK, 0, "<-");
    } else {
        addKey(kKbLeft, y2, 45, kKeyH, K_SHIFT, 0, "Maj");
        for (int i = 0; rows[2][i] != '\0'; ++i) {
            addKey(kKbLeft + 47 + i * (kKeyW + kKeyGapX), y2, kKeyW, kKeyH, K_CHAR, rows[2][i],
                   nullptr);
        }
        addKey(kKbLeft + 47 + 7 * (kKeyW + kKeyGapX), y2, 45, kKeyH, K_BACK, 0, "<-");
    }

    addKey(kKbLeft, y3, 46, kKeyH, K_MODE, 0, gSymbols ? "ABC" : "123");
    addKey(kKbLeft + 48, y3, 164, kKeyH, K_SPACE, ' ', "Espai");
    addKey(kKbLeft + 214, y3, 94, kKeyH, K_OK, 0, "Connecta");
}

void drawKey(uint8_t index, bool pressed)
{
    const Key& k = gKeys[index];
    uint16_t col = Ui::colorTrack();
    if (k.type == K_OK) {
        col = kGood;
    } else if (k.type == K_SHIFT || k.type == K_MODE || k.type == K_BACK) {
        col = gShift ? Ui::colorRow(3) : 0x5AAB;
    } else if (k.type == K_SPACE) {
        col = 0x5AAB;
    }
    if (pressed) {
        col = kText;
    }

    TFT_eSPI& t = tft();
    t.fillRoundRect(k.x, k.y, k.w, k.h, 6, col);
    t.setTextDatum(MC_DATUM);
    t.setTextColor(pressed ? 0x0000 : kText, col);
    t.setTextFont((k.label[1] == '\0') ? 4 : 1);
    t.drawString(k.label, k.x + k.w / 2, k.y + k.h / 2);
    t.setTextFont(2);
}

void drawKeyboard()
{
    buildKeyboard();
    for (uint8_t i = 0; i < gKeyCount; ++i) {
        drawKey(i, false);
    }
}

void drawPassField()
{
    TFT_eSPI& t = tft();
    const int x = 8;
    const int y = 62;
    const int w = SCREEN_W - 16;
    const int h = 42;

    t.fillRoundRect(x, y, w, h, 8, 0x18C3);
    t.drawRoundRect(x, y, w, h, 8, Ui::colorPanelEdge());

    char shown[kPassMax + 1];
    if (gPassShown) {
        strlcpy(shown, gPass, sizeof(shown));
    } else {
        for (uint8_t i = 0; i < gPassLen; ++i) {
            shown[i] = '*';
        }
        shown[gPassLen] = '\0';
    }

    t.setTextDatum(ML_DATUM);
    t.setTextFont(2);
    t.setTextColor(kText, 0x18C3);
    char* p = shown;
    while (t.textWidth(p) > (w - 20) && strlen(p) > 3) {
        ++p;
    }
    t.drawString(p, x + 8, y + h / 2);

    if (gPassLen > 0) {
        t.fillRect(x + 8 + t.textWidth(p) + 2, y + 10, 2, h - 20, kText);
    } else {
        t.setTextColor(kDimText, 0x18C3);
        t.drawString("(buida)", x + 8, y + h / 2);
    }
}

void drawWifiMessage()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 152, SCREEN_W, kKbTop - 152, Ui::colorPanelBg());
    t.setTextDatum(MC_DATUM);

    if (gWifiView == WifiView::Connecting) {
        t.setTextFont(2);
        t.setTextColor(kWarn, Ui::colorPanelBg());
        char buf[44];
        snprintf(buf, sizeof(buf), "Connectant a %s...", gSsid);
        t.drawString(buf, SCREEN_W / 2, 180);
        t.setTextFont(1);
        t.setTextColor(kDimText, Ui::colorPanelBg());
        t.drawString(gMsg, SCREEN_W / 2, 212);

        /* Barra de progres (fins a 20 s). */
        const int bx = 40;
        const int bw = SCREEN_W - 80;
        int pct = static_cast<int>((millis() - gStartedMs) * 100u / 20000u);
        if (pct > 100) { pct = 100; }
        t.fillRoundRect(bx, 232, bw, 12, 6, Ui::colorTrack());
        const int fw = (bw * pct) / 100;
        if (fw > 4) {
            t.fillRoundRect(bx, 232, fw, 12, 6, kWarn);
        }
        return;
    }
    if (gWifiView == WifiView::Result) {
        t.setTextFont(2);
        t.setTextColor(gConnected ? kGood : kBad, Ui::colorPanelBg());
        t.drawString(gConnected ? "Connectat!" : "No s'ha pogut connectar", SCREEN_W / 2, 185);
        t.setTextFont(1);
        t.setTextColor(kDimText, Ui::colorPanelBg());
        t.drawString(gMsg, SCREEN_W / 2, 215);
        t.drawString(gConnected ? "Prem Tanca per tornar" : "Revisa la contrasenya",
                     SCREEN_W / 2, 235);
        return;
    }

    t.setTextFont(1);
    t.setTextColor(kDimText, Ui::colorPanelBg());
    t.drawString(gMsg, SCREEN_W / 2, 175);
    t.drawString("Maj = majuscules   123 = numeros i simbols", SCREEN_W / 2, 200);
}

void drawWifiPassword()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, SCREEN_H, Ui::colorPanelBg());

    drawTitleBar(gSsid);
    drawSmallText(10, 52, "Contrasenya", kDimText);
    drawPassField();

    drawButton(8, 108, 96, 40, gPassShown ? "Tapa" : "Mostra", Ui::colorRow(2));
    drawButton(112, 108, 96, 40, "Esborra", Ui::colorRow(3));
    drawButton(216, 108, 96, 40, "Torna", Ui::colorRow(0));

    drawWifiMessage();
    drawKeyboard();
}

}  // namespace

namespace {

Panels::Hooks gHooks = {};

/* --- Ajustos, Sobre i Jocs ------------------------------------------------ */

int gSceneIndex = -1;      /* -1 = automatic (segons la meteo) */

int bgIndexOf(const char* name)
{
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    for (uint8_t i = 0; i < bgs.count; ++i) {
        if (strcmp(bgs.names[i], name) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void drawSettings()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, SCREEN_H, Ui::colorPanelBg());
    drawTitleBar("Ajustos");

    /* Tema de colors. */
    drawSmallText(10, 52, "Tema de colors", kDimText);
    const uint8_t tc = Ui::themeCount();
    const int tw = (SCREEN_W - 16 - (tc - 1) * 4) / tc;
    for (uint8_t i = 0; i < tc; ++i) {
        const int x = 8 + i * (tw + 4);
        drawButton(x, 64, tw, 42, Ui::themeName(i),
                   (i == Ui::theme()) ? kGood : Ui::colorRow(i));
    }

    /* Fons de la pantalla. */
    drawSmallText(10, 118, "Fons de la pantalla", kDimText);
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    char name[40];
    if (gSceneIndex < 0) {
        strlcpy(name, "Automatic (meteo)", sizeof(name));
    } else if (gSceneIndex < bgs.count) {
        strlcpy(name, bgs.names[gSceneIndex], sizeof(name));
    } else {
        strlcpy(name, "?", sizeof(name));
    }
    drawButton(8, 130, 52, 54, "<", Ui::colorRow(3));
    drawButton(SCREEN_W - 60, 130, 52, 54, ">", Ui::colorRow(3));
    t.fillRoundRect(64, 130, SCREEN_W - 128, 54, 8, Ui::colorTrack());
    t.setTextDatum(MC_DATUM);
    t.setTextFont(name[0] == 'A' ? 2 : 4);
    t.setTextColor(kText, Ui::colorTrack());
    t.drawString(name, SCREEN_W / 2, 130 + 27);

    drawSmallText(10, 196, "Prem < o > per triar. Es desa sol.", kDimText);
    char info[48];
    snprintf(info, sizeof(info), "%u fons disponibles a la targeta",
             static_cast<unsigned>(bgs.count));
    drawSmallText(10, 214, info, kDimText);

    drawButton(8, 410, 304, 60, "Tanca", Ui::colorClose());
}

void drawAbout()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, SCREEN_H, Ui::colorPanelBg());
    drawTitleBar("Sobre");

    t.setTextDatum(ML_DATUM);
    int y = 60;

    t.setTextFont(4);
    t.setTextColor(kText, Ui::colorPanelBg());
    t.drawString("Tamagoxi v2", 12, y);
    y += 30;
    t.setTextFont(1);
    t.setTextColor(kDimText, Ui::colorPanelBg());
    t.drawString("El dragó de la Noa", 12, y);
    y += 26;

    struct Line { const char* label; char value[40]; };
    Line lines[8];
    int n = 0;

    snprintf(lines[n].value, sizeof(lines[n].value), "%s rev%d", ESP.getChipModel(),
             ESP.getChipRevision());
    lines[n++].label = "Xip";
    snprintf(lines[n].value, sizeof(lines[n].value), "%u kB lliures",
             static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    lines[n++].label = "Memoria";
    const uint32_t up = millis() / 1000;
    snprintf(lines[n].value, sizeof(lines[n].value), "%luh %02lum", 
             static_cast<unsigned long>(up / 3600), static_cast<unsigned long>((up / 60) % 60));
    lines[n++].label = "Encesa";
    const Pet::Needs& nd = Pet::needs();
    snprintf(lines[n].value, sizeof(lines[n].value), "%u/%u/%u/%u", nd.food, nd.happiness,
             nd.energy, nd.health);
    lines[n++].label = "Menjar/fel/ener/salut";
    snprintf(lines[n].value, sizeof(lines[n].value), "%s", Net::connected() ? Net::ssid() : "-");
    lines[n++].label = "WiFi";
    if (Net::connected()) {
        snprintf(lines[n].value, sizeof(lines[n].value), "%d dBm",
                 static_cast<int>(WiFi.RSSI()));
        lines[n++].label = "Senyal";
    }
    if (Net::weather().valid) {
        snprintf(lines[n].value, sizeof(lines[n].value), "%d C  codi %d  %s",
                 Net::weather().temperature, Net::weather().code,
                 Net::weather().isDay ? "dia" : "nit");
        lines[n++].label = "Meteo";
    }

    t.setTextFont(2);
    for (int i = 0; i < n; ++i) {
        t.setTextColor(kDimText, Ui::colorPanelBg());
        t.drawString(lines[i].label, 12, y + 8);
        t.setTextColor(kText, Ui::colorPanelBg());
        t.setTextDatum(MR_DATUM);
        t.drawString(lines[i].value, SCREEN_W - 12, y + 8);
        t.setTextDatum(ML_DATUM);
        y += 30;
    }

    drawButton(8, 410, 304, 60, "Tanca", Ui::colorClose());
}

/* --- Panell de Bluetooth --------------------------------------------------- */

constexpr int kBtScanY    = 104;
constexpr int kBtScanH    = 46;
constexpr int kBtListY    = 158;
constexpr int kBtRowH2    = 47;
constexpr int kBtListRows = 4;
constexpr int kBtBtnY1    = 350;
constexpr int kBtBtnY2    = 412;
constexpr int kBtBtnH     = 56;

void drawBluetooth()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, SCREEN_H, Ui::colorPanelBg());
    drawTitleBar("Bluetooth");

    const Audio::BtMode mode = Audio::btMode();
    const bool on = (mode != Audio::BtMode::Off);

    /* Mode i estat, en dues linies. */
    t.setTextDatum(ML_DATUM);
    t.setTextFont(4);
    t.setTextColor(kText, Ui::colorPanelBg());
    t.drawString((mode == Audio::BtMode::Source) ? "Auriculars"
                 : ((mode == Audio::BtMode::Sink) ? "Altaveu" : "Apagat"),
                 12, 58);

    const Audio::BtDevice* peer = nullptr;
    if (on) {
        for (uint8_t i = 0; i < Audio::btDeviceCount(); ++i) {
            const Audio::BtDevice* d = Audio::btDevice(i);
            if (d != nullptr && d->connected) {
                peer = d;
                break;
            }
        }
    }
    char st[64];
    if (!on) {
        strlcpy(st, "Encen \"Auriculars\" per sentir-hi la musica", sizeof(st));
    } else if (Audio::btConnected()) {
        snprintf(st, sizeof(st), "enllacat amb %s", (peer != nullptr) ? peer->name : "?");
    } else {
        strlcpy(st, "cap aparell enllacat", sizeof(st));
    }
    t.setTextFont(2);
    t.setTextColor(kDimText, Ui::colorPanelBg());
    t.drawString(st, 12, 84);

    /* Boto d'escanejar. */
    char lbl[40];
    if (Audio::btScanning()) {
        snprintf(lbl, sizeof(lbl), "Cercant... (%u)",
                 static_cast<unsigned>(Audio::btDeviceCount()));
    } else {
        strlcpy(lbl, "Escaneja aparells", sizeof(lbl));
    }
    drawButton(8, kBtScanY, SCREEN_W - 16, kBtScanH, lbl,
               Audio::btScanning() ? kGood : Ui::colorRow(0));

    /* Llista dels aparells trobats (toca'n un per enllacar-hi). */
    const uint8_t count = Audio::btDeviceCount();
    for (uint8_t i = 0; i < kBtListRows; ++i) {
        const int ry = kBtListY + i * kBtRowH2;
        if (i >= count) {
            if (i == 0) {
                t.setTextDatum(ML_DATUM);
                t.setTextFont(1);
                t.setTextColor(kDimText, Ui::colorPanelBg());
                t.drawString(Audio::btScanning()
                                 ? "Cercant aparells d'audio a prop..."
                                 : "Toca \"Escaneja\" amb els auriculars encesos",
                             16, ry + 14);
            }
            continue;
        }
        const Audio::BtDevice* d = Audio::btDevice(i);
        if (d == nullptr) {
            continue;
        }
        const uint16_t bg = d->connected ? kGood : Ui::colorTrack();
        const int rh = kBtRowH2 - 6;
        t.fillRoundRect(8, ry, SCREEN_W - 16, rh, 8, bg);
        t.setTextDatum(ML_DATUM);
        t.setTextFont(2);
        t.setTextColor(kText, bg);
        t.drawString(d->name, 16, ry + rh / 2 - 7);
        t.setTextFont(1);
        t.drawString(d->connected ? "enllacat" : "toca per enllacar", 16, ry + rh / 2 + 9);
        char rssi[12];
        snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(d->rssi));
        t.setTextDatum(MR_DATUM);
        t.setTextFont(2);
        t.drawString(rssi, SCREEN_W - 16, ry + rh / 2);
        t.setTextDatum(ML_DATUM);
    }

    /* Botons. */
    drawButton(8, kBtBtnY1, 148, kBtBtnH, "Auriculars",
               (mode == Audio::BtMode::Source) ? kGood : Ui::colorRow(1));
    drawButton(164, kBtBtnY1, 148, kBtBtnH, "Altaveu",
               (mode == Audio::BtMode::Sink) ? kGood : Ui::colorRow(2));
    drawButton(8, kBtBtnY2, 148, kBtBtnH, on ? "Apagar" : "So a la placa",
               on ? Ui::colorClose() : Ui::colorRow(3));
    drawButton(164, kBtBtnY2, 148, kBtBtnH, "Tanca", Ui::colorClose());

    saveBtSnapshot();       /* per no repintar-ho si no canvia res */
}

/* Estat de la darrera pintada: si no canvia res, no cal repintar (abans el
 * panell es repintava cada 2 s i es veia parpellejar). */
struct BtSnapshot {
    uint8_t mode;
    bool    connected;
    bool    scanning;
    uint8_t count;
    char    names[Audio::kBtMaxDevices][Audio::kBtNameMax];
    int8_t  rssi[Audio::kBtMaxDevices];
};

BtSnapshot gBtSnap = {};

void saveBtSnapshot()
{
    gBtSnap.mode      = static_cast<uint8_t>(Audio::btMode());
    gBtSnap.connected = Audio::btConnected();
    gBtSnap.scanning  = Audio::btScanning();
    gBtSnap.count     = Audio::btDeviceCount();
    for (uint8_t i = 0; i < gBtSnap.count && i < Audio::kBtMaxDevices; ++i) {
        const Audio::BtDevice* d = Audio::btDevice(i);
        if (d == nullptr) {
            continue;
        }
        strlcpy(gBtSnap.names[i], d->name, Audio::kBtNameMax);
        gBtSnap.rssi[i] = d->rssi;
    }
}

bool btChanged()
{
    if (gBtSnap.mode != static_cast<uint8_t>(Audio::btMode())
        || gBtSnap.connected != Audio::btConnected()
        || gBtSnap.scanning != Audio::btScanning()
        || gBtSnap.count != Audio::btDeviceCount()) {
        return true;
    }
    for (uint8_t i = 0; i < gBtSnap.count && i < Audio::kBtMaxDevices; ++i) {
        const Audio::BtDevice* d = Audio::btDevice(i);
        if (d == nullptr) {
            return true;
        }
        if (gBtSnap.rssi[i] != d->rssi
            || strncmp(gBtSnap.names[i], d->name, Audio::kBtNameMax) != 0) {
            return true;
        }
    }
    return false;
}

void drawGames()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, SCREEN_H, Ui::colorPanelBg());
    drawTitleBar("Jocs");

    t.setTextDatum(MC_DATUM);
    t.setTextFont(4);
    t.setTextColor(kText, Ui::colorPanelBg());
    t.drawString("Encara no hi ha jocs", SCREEN_W / 2, 120);

    t.setTextFont(2);
    t.setTextColor(kDimText, Ui::colorPanelBg());
    t.drawString("Aviat en vindran:", SCREEN_W / 2, 180);

    t.setTextFont(2);
    t.setTextColor(kText, Ui::colorPanelBg());
    t.drawString("· Memoria d'animals", SCREEN_W / 2, 220);
    t.drawString("· Endevina el numero", SCREEN_W / 2, 252);
    t.drawString("· El laberint del drac", SCREEN_W / 2, 284);

    t.setTextFont(1);
    t.setTextColor(kDimText, Ui::colorPanelBg());
    t.drawString("Mentrestant, cuida el drago!", SCREEN_W / 2, 340);

    drawButton(8, 410, 304, 60, "Tanca", Ui::colorClose());
}

bool gListDrawn = false;
uint32_t gTimer = 0;

void applyScene(const SdAssets::Backgrounds& bgs)
{
    if (gHooks.setBackground == nullptr) {
        return;
    }
    if (gSceneIndex < 0) {
        gHooks.setBackground(nullptr, true);
    } else if (gSceneIndex < static_cast<int>(bgs.count)) {
        gHooks.setBackground(bgs.names[gSceneIndex], false);
    }
    drawSettings();
}

void startConnect()
{
    if (gSsid[0] == '\0') {
        return;
    }
    gWifiView  = WifiView::Connecting;
    gConnected = false;
    gStartedMs = millis();
    strlcpy(gMsg, "Espera uns segons...", sizeof(gMsg));
    Net::setCredentials(gSsid, gPass);
    drawWifiPassword();
}

void openPasswordFor(uint8_t idx)
{
    const Net::Ap ap = Net::scanAp(idx);
    strlcpy(gSsid, ap.ssid, sizeof(gSsid));
    gPass[0]        = '\0';
    gPassLen        = 0;
    gShift          = false;
    gSymbols        = false;
    gPassShown      = true;
    gStartedMs      = millis();

    if (!ap.secure) {
        strlcpy(gMsg, "Xarxa oberta: connectant...", sizeof(gMsg));
        gWifiView = WifiView::Connecting;
        Net::setCredentials(gSsid, "");
        drawWifiPassword();
        return;
    }
    strlcpy(gMsg, "Escriu la contrasenya de la xarxa", sizeof(gMsg));
    gWifiView = WifiView::Password;
    drawWifiPassword();
}

void handleKey(uint8_t index)
{
    const Key k = gKeys[index];
    const bool wasShift = gShift;

    drawKey(index, true);          /* retroaccio visual de la premuda */
    delay(35);

    switch (k.type) {
        case K_CHAR:
            if (gPassLen < kPassMax) {
                gPass[gPassLen++] = k.ch;
                gPass[gPassLen]   = '\0';
            }
            if (gShift) {
                gShift = false;    /* una majuscula i torna a minuscules */
            }
            break;
        case K_BACK:
            if (gPassLen > 0) {
                gPass[--gPassLen] = '\0';
            }
            break;
        case K_SPACE:
            if (gPassLen < kPassMax) {
                gPass[gPassLen++] = ' ';
                gPass[gPassLen]   = '\0';
            }
            break;
        case K_SHIFT:
            gShift = !gShift;
            break;
        case K_MODE:
            gSymbols = !gSymbols;
            gShift   = false;
            break;
        case K_OK:
            startConnect();
            return;
        default:
            break;
    }

    if (k.type == K_SHIFT || k.type == K_MODE || wasShift != gShift) {
        drawKeyboard();
    } else {
        drawKey(index, false);
    }
    drawPassField();
}

/* --- Panell de musica ------------------------------------------------------ */

void drawNowPlaying()
{
    TFT_eSPI& t = tft();
    const Audio::Status& st = Audio::status();

    t.fillRoundRect(8, kNpY, SCREEN_W - 16, 52, 8, Ui::colorTrack());

    char name[30];
    if (st.name[0] != '\0') {
        strlcpy(name, st.name, sizeof(name));
    } else {
        strlcpy(name, "Cap canco triada", sizeof(name));
    }
    t.setTextDatum(ML_DATUM);
    t.setTextFont(2);
    t.setTextColor(kText, Ui::colorTrack());
    while (t.textWidth(name) > 200 && strlen(name) > 4) {
        name[strlen(name) - 1] = '\0';
    }
    t.drawString(name, 16, kNpY + 17);

    char times[28];
    snprintf(times, sizeof(times), "%lu:%02lu / %lu:%02lu",
             static_cast<unsigned long>(st.elapsedSec / 60),
             static_cast<unsigned long>(st.elapsedSec % 60),
             static_cast<unsigned long>(st.totalSec / 60),
             static_cast<unsigned long>(st.totalSec % 60));
    t.setTextFont(1);
    t.setTextColor(kDimText, Ui::colorTrack());
    t.drawString(times, 16, kNpY + 37);

    t.setTextDatum(MR_DATUM);
    t.setTextColor(st.playing ? (st.paused ? kWarn : kGood) : kDimText, Ui::colorTrack());
    t.drawString(st.paused ? "en pausa" : (st.playing ? "sonant" : "aturat"),
                 SCREEN_W - 16, kNpY + 37);
    t.setTextDatum(ML_DATUM);

    const int x = 8;
    const int w = SCREEN_W - 16;
    t.fillRoundRect(x, kProgY, w, 10, 5, Ui::colorTrack());
    const int fw = (w * st.percent) / 100;
    if (fw > 4) {
        t.fillRoundRect(x, kProgY, fw, 10, 5, kGood);
    }
}

void drawTransport()
{
    TFT_eSPI& t = tft();
    const Audio::Status& st = Audio::status();
    const int w = 96;
    const int y = kTransY;
    const int cy = y + kTransH / 2;

    drawButton(8, y, w, kTransH, "", Ui::colorRow(0));
    {
        const int cx = 8 + w / 2;
        t.fillTriangle(cx - 4, cy, cx + 6, cy - 9, cx + 6, cy + 9, kText);
        t.fillRect(cx - 11, cy - 9, 4, 18, kText);
    }

    drawButton(112, y, w, kTransH, "", (st.playing && !st.paused) ? kGood : Ui::colorRow(1));
    {
        const int cx = 112 + w / 2;
        if (st.playing && !st.paused) {
            t.fillRect(cx - 8, cy - 10, 6, 20, kText);
            t.fillRect(cx + 2, cy - 10, 6, 20, kText);
        } else {
            t.fillTriangle(cx - 7, cy - 11, cx - 7, cy + 11, cx + 10, cy, kText);
        }
    }

    drawButton(216, y, w, kTransH, "", Ui::colorRow(2));
    {
        const int cx = 216 + w / 2;
        t.fillTriangle(cx + 4, cy, cx - 6, cy - 9, cx - 6, cy + 9, kText);
        t.fillRect(cx + 7, cy - 9, 4, 18, kText);
    }
}

void drawVolumeRow()
{
    TFT_eSPI& t = tft();
    drawButton(8, kVolY, 60, kVolH, "-", Ui::colorRow(3));
    drawButton(252, kVolY, 60, kVolH, "+", Ui::colorRow(3));

    const uint8_t v = Audio::volume();
    const int bx = 76;
    const int bw = 168;
    const int by = kVolY + kVolH / 2 - 6;

    t.fillRoundRect(bx, by, bw, 12, 6, Ui::colorTrack());
    const int fw = (bw * v) / 100;
    if (fw > 4) {
        t.fillRoundRect(bx, by, fw, 12, 6, Ui::colorAccent());
    }
    t.setTextDatum(MC_DATUM);
    t.setTextFont(1);
    t.setTextColor(kText, Ui::colorTrack());
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(v));
    t.drawString(buf, bx + bw / 2, by + 6);
    t.setTextDatum(ML_DATUM);
    t.setTextColor(kDimText, Ui::colorPanelBg());
    t.drawString("Volum", bx, kVolY + 2);
}

void drawMusicList()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, kMListTop, SCREEN_W, kMBtnY - kMListTop, Ui::colorPanelBg());

    const Audio::Status& st = Audio::status();
    if (st.count == 0) {
        t.setTextDatum(MC_DATUM);
        t.setTextFont(2);
        t.setTextColor(kDimText, Ui::colorPanelBg());
        t.drawString("No hi ha cancons", SCREEN_W / 2, kMListTop + 40);
        t.setTextFont(1);
        t.drawString("Posa fitxers .mp3 a la carpeta /music", SCREEN_W / 2, kMListTop + 70);
        t.drawString("de la targeta SD", SCREEN_W / 2, kMListTop + 88);
        return;
    }

    for (uint8_t i = 0; i < kMListRows; ++i) {
        const uint8_t idx = static_cast<uint8_t>(gMusicTop + i);
        if (idx >= st.count) {
            break;
        }
        const Audio::Track* tr = Audio::track(idx);
        if (tr == nullptr) {
            break;
        }
        const int y = kMListTop + i * kMRowH;
        const bool cur = (idx == st.index) && st.playing;
        const uint16_t col = cur ? kGood : Ui::colorTrack();
        t.fillRoundRect(4, y + 1, 296, kMRowH - 4, 7, col);

        char nm[30];
        strlcpy(nm, tr->name, sizeof(nm));
        t.setTextDatum(ML_DATUM);
        t.setTextFont(2);
        t.setTextColor(kText, col);
        while (t.textWidth(nm) > 240 && strlen(nm) > 4) {
            nm[strlen(nm) - 1] = '\0';
        }
        t.drawString(nm, 16, y + kMRowH / 2);
    }

    if (st.count > kMListRows) {
        const int ax = SCREEN_W - 16;
        t.fillRoundRect(ax - 7, kMListTop + 2, 20, 38, 5, Ui::colorRow(3));
        t.fillTriangle(ax, kMListTop + 11, ax - 6, kMListTop + 23, ax + 6, kMListTop + 23, kText);
        const int by = kMListTop + kMListRows * kMRowH - 40;
        t.fillRoundRect(ax - 7, by, 20, 38, 5, Ui::colorRow(3));
        t.fillTriangle(ax, by + 27, ax - 6, by + 15, ax + 6, by + 15, kText);
    }
}

void drawMusic()
{
    TFT_eSPI& t = tft();
    t.fillRect(0, 0, SCREEN_W, SCREEN_H, Ui::colorPanelBg());
    drawTitleBar("Musica");
    drawNowPlaying();
    drawTransport();
    drawVolumeRow();
    drawMusicList();
    drawButton(8, kMBtnY, 148, kMBtnH, "Actualitza", Ui::colorRow(3));
    drawButton(164, kMBtnY, 148, kMBtnH, "Tanca", Ui::colorClose());
}

void showMusic()
{
    gId = Panels::Id::Music;
    gMusicTop = 0;
    Audio::scan();
    drawMusic();
}

}  // namespace

namespace Panels {

void setHooks(const Hooks& hooks)
{
    gHooks = hooks;
}

const char* name(Id id)
{
    switch (id) {
        case Id::TopMenu:  return "menu";
        case Id::Wifi:     return "wifi";
        case Id::Music:    return "musica";
        case Id::Bluetooth: return "bluetooth";
        case Id::Settings: return "ajustos";
        case Id::About:    return "sobre";
        case Id::Games:    return "jocs";
        default:           return "cap";
    }
}

Id current()
{
    return gId;
}

bool isOpen()
{
    return gId != Id::None;
}

void showWifi()
{
    gId       = Id::Wifi;
    gWifiView = WifiView::List;
    gSelected = 0;
    gListTop  = 0;
    gListDrawn = false;
    gMsg[0]   = '\0';
    Net::startScan();
    drawWifi();
}

void showSettings()
{
    gId = Id::Settings;
    const bool autoBg = (gHooks.isAuto != nullptr) && gHooks.isAuto();
    const char* cur = (gHooks.currentName != nullptr) ? gHooks.currentName() : "";
    gSceneIndex = autoBg ? -1 : bgIndexOf(cur);
    Serial.printf("[UI] ajustos: auto=%d nom=\"%s\" index=%d\n", autoBg ? 1 : 0, cur,
                  gSceneIndex);
    drawSettings();
}

void showAbout()
{
    gId = Id::About;
    drawAbout();
}

void showBluetooth()
{
    gId  = Id::Bluetooth;
    gTimer = millis();
    Serial.println(F("[UI] panell de Bluetooth"));
    drawBluetooth();
}

void showGames()
{
    gId = Id::Games;
    drawGames();
}

void openTopMenu()
{
    gId = Id::TopMenu;
    Serial.println(F("[UI] desplegable de dalt obert"));
    drawTopMenu();
}

void open(Id id)
{
    Serial.printf("[UI] obrint panell: %s\n", name(id));
    switch (id) {
        case Id::TopMenu:  openTopMenu(); break;
        case Id::Wifi:     showWifi(); break;
        case Id::Music:    showMusic(); break;
        case Id::Bluetooth: showBluetooth(); break;
        case Id::Settings: showSettings(); break;
        case Id::About:    showAbout(); break;
        case Id::Games:    showGames(); break;
        default:           break;
    }
}

bool close()
{
    if (gId == Id::None) {
        return false;
    }
    gId = Id::None;
    return true;
}

void topMenuRect(int16_t& x, int16_t& y, int16_t& w, int16_t& h)
{
    x = kMenuX;
    y = kMenuY;
    w = kMenuW;
    h = kMenuH;
}

void update(uint32_t nowMs)
{
    if (gId == Id::Bluetooth) {
        /* Refresca NOMES si ha canviat alguna cosa (si no, parpellejava). */
        const uint32_t period = Audio::btScanning() ? 600u : 2000u;
        if (nowMs - gTimer >= period) {
            gTimer = nowMs;
            if (btChanged()) {
                drawBluetooth();
            }
        }
        return;
    }

    if (gId == Id::Music) {
        if (nowMs - gTimer >= 500) {
            gTimer = nowMs;
            drawNowPlaying();
            drawTransport();
            drawMusicList();
        }
        return;
    }

    if (gId != Id::Wifi) {
        return;
    }

    if (gWifiView == WifiView::List) {
        if (Net::scanRunning()) {
            if (nowMs - gTimer >= 400) {
                gTimer = nowMs;
                drawWifiList();
            }
        } else if (!gListDrawn) {
            gListDrawn = true;
            drawWifiList();
        }
        return;
    }

    if (gWifiView == WifiView::Connecting) {
        if (Net::connected()) {
            gWifiView  = WifiView::Result;
            gConnected = true;
            snprintf(gMsg, sizeof(gMsg), "IP %s", WiFi.localIP().toString().c_str());
            drawWifiPassword();
        } else if (Net::state() == Net::State::Failed) {
            gWifiView  = WifiView::Result;
            gConnected = false;
            strlcpy(gMsg, "Comprova la contrasenya i torna-ho a provar", sizeof(gMsg));
            drawWifiPassword();
        } else if (nowMs - gTimer >= 300) {
            gTimer = nowMs;
            drawWifiMessage();      /* barra de progres */
        }
    }
}

bool handleTap(int16_t x, int16_t y)
{
    if (gId == Id::None) {
        return true;
    }
    Serial.printf("[UI] toc %d,%d -> panell %s\n", static_cast<int>(x), static_cast<int>(y),
                  name(gId));

    /* --- Desplegable de dalt --- */
    if (gId == Id::TopMenu) {
        for (int i = 0; i < kMenuRows; ++i) {
            const int ry = kMenuY + 4 + i * (kRowH + kRowGap);
            if (x >= kMenuX && x < kMenuX + kMenuW && y >= ry && y < ry + kRowH) {
                switch (i) {
                    case 0: showWifi(); break;
                    case 1: showMusic(); break;
                    case 2: showBluetooth(); break;
                    case 3: showGames(); break;
                    case 4: showSettings(); break;
                    default: showAbout(); break;
                }
                return true;
            }
        }
        return false;               /* fora del desplegable: es tanca */
    }

    /* --- Bluetooth --- */
    if (gId == Id::Bluetooth) {
        /* Boto d'escanejar: encen l'emissor si cal i cerca aparells d'audio. */
        if (y >= kBtScanY && y < kBtScanY + kBtScanH) {
            if (Audio::btMode() != Audio::BtMode::Source) {
                Audio::btSetMode(Audio::BtMode::Source);
                Audio::setOutput(Audio::Output::Bluetooth);
            }
            if (Audio::btMode() == Audio::BtMode::Source) {
                Audio::btStartScan();
            }
            drawBluetooth();
            return true;
        }
        /* Llista d'aparells trobats: un toc enllaca amb aquell. */
        if (y >= kBtListY && y < kBtListY + kBtListRows * kBtRowH2) {
            const int row = (y - kBtListY) / kBtRowH2;
            if (row >= 0 && row < static_cast<int>(Audio::btDeviceCount())) {
                Audio::btConnect(static_cast<uint8_t>(row));
                drawBluetooth();
            }
            return true;
        }
        if (y >= kBtBtnY1 && y < kBtBtnY1 + kBtBtnH) {
            if (x >= 8 && x < 156) {          /* auriculars (emissor) */
                Audio::btSetMode(Audio::BtMode::Source);
                if (Audio::btMode() == Audio::BtMode::Source) {
                    Audio::setOutput(Audio::Output::Bluetooth);
                }
                drawBluetooth();
            } else if (x >= 164 && x < 312) { /* altaveu (rebre) */
                Audio::btSetMode(Audio::BtMode::Sink);
                if (Audio::btMode() == Audio::BtMode::Sink) {
                    Audio::setOutput(Audio::Output::Bluetooth);
                }
                drawBluetooth();
            }
            return true;
        }
        if (y >= kBtBtnY2 && y < kBtBtnY2 + kBtBtnH) {
            if (x >= 8 && x < 156) {
                if (Audio::btMode() != Audio::BtMode::Off) {
                    Audio::btSetMode(Audio::BtMode::Off);   /* reinicia la placa */
                    return true;
                }
                Audio::setOutput(Audio::Output::Dac);
                drawBluetooth();
            } else if (x >= 164 && x < 312) {
                return false;            /* Tanca */
            }
            return true;
        }
        return true;
    }

    /* Boto de tancar de la capçalera (panells a pantalla completa). */
    if (y < kTitleH && x >= SCREEN_W - kCloseW) {
        return false;
    }

    /* --- WiFi --- */
    if (gId == Id::Wifi) {
        if (gWifiView == WifiView::Connecting) {
            return true;            /* res a fer mentre connecta */
        }
        if (gWifiView == WifiView::Result) {
            return false;           /* qualsevol toc torna a casa */
        }
        if (gWifiView == WifiView::Password) {
            if (y >= 108 && y < 148) {
                if (x >= 8 && x < 104) {
                    gPassShown = !gPassShown;
                    drawButton(8, 108, 96, 40, gPassShown ? "Tapa" : "Mostra", Ui::colorRow(2));
                    drawPassField();
                } else if (x >= 112 && x < 208) {
                    gPass[0] = '\0';
                    gPassLen = 0;
                    drawPassField();
                } else if (x >= 216 && x < 312) {
                    gWifiView = WifiView::List;
                    drawWifi();
                }
                return true;
            }
            for (uint8_t i = 0; i < gKeyCount; ++i) {
                const Key& k = gKeys[i];
                if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) {
                    handleKey(i);
                    return true;
                }
            }
            return true;
        }

        /* Llista de xarxes. */
        if (y >= kListTop && y < kListTop + kListRows * kListRowH) {
            const uint8_t row = static_cast<uint8_t>((y - kListTop) / kListRowH);
            const uint8_t idx = static_cast<uint8_t>(gListTop + row);
            if (idx < Net::scanCount()) {
                gSelected = idx;
                openPasswordFor(idx);
            }
            return true;
        }
        if (Net::scanCount() > kListRows && x >= SCREEN_W - 30 && y >= kListTop &&
            y < kListTop + kListRows * kListRowH) {
            if (y < kListTop + (kListRows * kListRowH) / 2) {
                if (gListTop > 0) {
                    --gListTop;
                }
            } else if (gListTop + kListRows < Net::scanCount()) {
                ++gListTop;
            }
            drawWifiList();
            return true;
        }
        if (y >= kBtnY && y <= kBtnY + kBtnH) {
            if (x < 232) {
                gListDrawn = false;
                gTimer = millis();
                Net::startScan();
                drawWifiBottom();
                drawWifiList();
                return true;
            }
            return false;           /* Tanca */
        }
        return true;
    }

    /* --- Musica --- */
    if (gId == Id::Music) {
        const Audio::Status& st = Audio::status();
        if (y >= kTransY && y < kTransY + kTransH) {
            if (x >= 8 && x < 104) {
                Audio::previous();
            } else if (x >= 112 && x < 208) {
                Audio::togglePause();
            } else if (x >= 216 && x < 312) {
                Audio::next();
            }
            drawMusic();
            return true;
        }
        if (y >= kVolY && y < kVolY + kVolH) {
            int v = Audio::volume();
            if (x < 76) {
                v = (v >= 10) ? (v - 10) : 0;
                Audio::setVolume(static_cast<uint8_t>(v));
                drawVolumeRow();
            } else if (x >= 244) {
                v = (v <= 90) ? (v + 10) : 100;
                Audio::setVolume(static_cast<uint8_t>(v));
                drawVolumeRow();
            }
            return true;
        }
        if (y >= kMListTop && y < kMListTop + kMListRows * kMRowH) {
            if (st.count > kMListRows && x >= SCREEN_W - 30) {
                if (y < kMListTop + (kMListRows * kMRowH) / 2) {
                    gMusicTop = (gMusicTop > 0) ? (gMusicTop - 1) : 0;
                } else if (gMusicTop + kMListRows < st.count) {
                    ++gMusicTop;
                }
                drawMusicList();
                return true;
            }
            const uint8_t row = static_cast<uint8_t>((y - kMListTop) / kMRowH);
            const uint8_t idx = static_cast<uint8_t>(gMusicTop + row);
            if (idx < st.count) {
                Audio::play(idx);
                drawMusic();
            }
            return true;
        }
        if (y >= kMBtnY && y < kMBtnY + kMBtnH) {
            if (x < 160) {
                Audio::scan();
                drawMusic();
                return true;
            }
            return false;           /* Tanca */
        }
        return true;
    }

    /* --- Ajustos --- */
    if (gId == Id::Settings) {
        if (y >= 64 && y < 106) {
            const uint8_t tc = Ui::themeCount();
            const int tw = (SCREEN_W - 16 - (tc - 1) * 4) / tc;
            for (uint8_t i = 0; i < tc; ++i) {
                const int bx = 8 + i * (tw + 4);
                if (x >= bx && x < bx + tw) {
                    Ui::setTheme(i);
                    Storage::saveUiTheme(i);
                    drawSettings();
                    return true;
                }
            }
            return true;
        }
        if (y >= 130 && y < 184) {
            const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
            const int count = static_cast<int>(bgs.count);
            if (count == 0) {
                return true;
            }
            if (x < 64) {
                gSceneIndex = (gSceneIndex <= -1) ? (count - 1) : (gSceneIndex - 1);
                applyScene(bgs);
            } else if (x >= SCREEN_W - 64) {
                gSceneIndex = (gSceneIndex + 1 > count - 1) ? -1 : (gSceneIndex + 1);
                applyScene(bgs);
            }
            return true;
        }
        if (y >= 410) {
            return false;           /* Tanca */
        }
        return true;
    }

    /* --- Sobre i Jocs: boto Tanca de baix --- */
    if (y >= 410 && y < 480) {
        return false;
    }
    return true;
}

}  // namespace Panels
