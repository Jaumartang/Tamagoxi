#include "bg_renderer.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

#include "tg_config.h"
#include "display.h"
#include "sd_assets.h"

namespace {

uint8_t* gBand = nullptr;
BgRenderer::Status gStatus;
constexpr size_t kPathLen = 64;

}  // namespace

namespace BgRenderer {

bool begin()
{
    if (gBand != nullptr) {
        return true;
    }
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    if (bgs.width == 0 || bgs.height == 0) {
        Serial.println(F("[BG] mides de fons desconegudes (cal sd_assets.begin)"));
        return false;
    }

    const size_t bytes = static_cast<size_t>(bgs.width) * BG_BAND_LINES * 2u;
    gBand = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_DMA));
    if (gBand == nullptr) {
        Serial.printf("[BG] no s'ha pogut reservar la franja (%u B)\n",
                      static_cast<unsigned>(bytes));
        return false;
    }

    gStatus.bandLines = BG_BAND_LINES;
    gStatus.width = bgs.width;
    gStatus.height = bgs.height;
    Serial.printf("[BG] buffer de franja: %u files = %u B (fons %ux%u)\n",
                  static_cast<unsigned>(BG_BAND_LINES),
                  static_cast<unsigned>(bytes),
                  static_cast<unsigned>(bgs.width),
                  static_cast<unsigned>(bgs.height));
    return true;
}

void end()
{
    heap_caps_free(gBand);
    gBand = nullptr;
}

uint32_t drawFull(const char* name)
{
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    const uint16_t W = bgs.width;
    const uint16_t H = bgs.height;

    gStatus.lastOk = false;
    gStatus.lastMs = 0;
    gStatus.lastBytes = 0;
    strlcpy(gStatus.lastName, name, sizeof(gStatus.lastName));

    if (gBand == nullptr || W == 0 || H == 0) {
        return 0;
    }

    char path[kPathLen];
    snprintf(path, sizeof(path), "/backgrounds/%s.bin", name);
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[BG] no s'ha pogut obrir %s\n", path);
        return 0;
    }
    const uint32_t expected = static_cast<uint32_t>(W) * H * 2u;
    if (static_cast<uint32_t>(f.size()) != expected) {
        Serial.printf("[BG] mida incorrecta a %s\n", path);
        f.close();
        return 0;
    }

    TFT_eSPI& t = Display::driver();
    /* Els bytes de la SD ja venen en big-endian (com els vol la pantalla). Ens
     * assegurem de NO dur el byte-swap activat que pugui haver deixat el dibuix
     * de la mascota; si no, el fons sortiria amb els colors intercanviats. */
    t.setSwapBytes(false);
    const uint32_t t0 = millis();

    /* Una sola finestra per a tot el fons: cada franja hi va consecutivament. */
    t.startWrite();
    t.setAddrWindow(0, 0, W, H);

    bool ok = true;
    uint32_t done = 0;
    for (uint16_t y = 0; y < H; y += BG_BAND_LINES) {
        uint16_t lines = BG_BAND_LINES;
        if (static_cast<uint32_t>(y) + lines > H) {
            lines = static_cast<uint16_t>(H - y);
        }
        const size_t bandBytes = static_cast<size_t>(W) * lines * 2u;
        const size_t got = f.read(gBand, bandBytes);
        if (got != bandBytes) {
            ok = false;
            break;
        }
        t.pushPixels(reinterpret_cast<uint16_t*>(gBand), static_cast<uint32_t>(W) * lines);
        done += bandBytes;
    }

    t.endWrite();
    f.close();

    gStatus.lastMs = millis() - t0;
    gStatus.lastBytes = done;
    gStatus.lastOk = ok;
    return gStatus.lastMs;
}

uint32_t drawRegion(const char* name, int x, int y, int w, int h)
{
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    const uint16_t W = bgs.width;
    const uint16_t H = bgs.height;

    gStatus.lastOk = false;
    gStatus.lastMs = 0;

    if (gBand == nullptr || W == 0 || H == 0) {
        return 0;
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > static_cast<int>(W)) { w = static_cast<int>(W) - x; }
    if (y + h > static_cast<int>(H)) { h = static_cast<int>(H) - y; }
    if (w <= 0 || h <= 0) {
        return 0;
    }

    char path[kPathLen];
    snprintf(path, sizeof(path), "/backgrounds/%s.bin", name);
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[BG] no s'ha pogut obrir %s\n", path);
        return 0;
    }
    if (static_cast<uint32_t>(f.size()) != static_cast<uint32_t>(W) * H * 2u) {
        Serial.printf("[BG] mida incorrecta a %s\n", path);
        f.close();
        return 0;
    }

    const size_t rowBytes = static_cast<size_t>(w) * 2u;
    if (rowBytes * BG_BAND_LINES > static_cast<size_t>(W) * BG_BAND_LINES * 2u) {
        f.close();
        return 0;
    }

    TFT_eSPI& t = Display::driver();
    t.setSwapBytes(false);      /* els bytes de la SD son big-endian ✓ */
    const uint32_t t0 = millis();
    bool ok = true;

    t.startWrite();
    for (int r = 0; r < h && ok; r += BG_BAND_LINES) {
        const int lines = (h - r > BG_BAND_LINES) ? BG_BAND_LINES : (h - r);
        uint8_t* p = gBand;
        for (int i = 0; i < lines; ++i) {
            const uint32_t off =
                (static_cast<uint32_t>(y + r + i) * W + static_cast<uint32_t>(x)) * 2u;
            if (!f.seek(off) || f.read(p, rowBytes) != rowBytes) {
                ok = false;
                break;
            }
            p += rowBytes;
        }
        if (!ok) {
            break;
        }
        t.setAddrWindow(x, y + r, w, lines);
        t.pushPixels(reinterpret_cast<uint16_t*>(gBand),
                     static_cast<uint32_t>(w) * static_cast<uint32_t>(lines));
    }
    t.endWrite();
    f.close();

    gStatus.lastMs = millis() - t0;
    gStatus.lastOk = ok;
    return gStatus.lastMs;
}

bool bench(const char* name)
{
    if (gBand == nullptr) {
        return false;
    }
    const uint16_t W = gStatus.width;
    const uint16_t H = gStatus.height;
    const size_t bandBytes = static_cast<size_t>(W) * BG_BAND_LINES * 2u;

    char path[kPathLen];
    snprintf(path, sizeof(path), "/backgrounds/%s.bin", name);

    /* 1) Nomes lectura de la SD (descartant les dades). */
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("[BENCH] no s'ha pogut obrir %s\n", path);
        return false;
    }
    uint32_t total = 0;
    uint32_t t0 = millis();
    while (true) {
        const size_t got = f.read(gBand, bandBytes);
        if (got == 0) {
            break;
        }
        total += static_cast<uint32_t>(got);
    }
    const uint32_t readMs = millis() - t0;
    f.close();

    /* 2) Nomes push a pantalla (mateix buffer, sense SD). */
    TFT_eSPI& t = Display::driver();
    t0 = millis();
    t.startWrite();
    t.setAddrWindow(0, 0, W, H);
    for (uint16_t y = 0; y < H; y += BG_BAND_LINES) {
        uint16_t lines = BG_BAND_LINES;
        if (static_cast<uint32_t>(y) + lines > H) {
            lines = static_cast<uint16_t>(H - y);
        }
        t.pushPixels(reinterpret_cast<uint16_t*>(gBand), static_cast<uint32_t>(W) * lines);
    }
    t.endWrite();
    const uint32_t pushMs = millis() - t0;

    const double readMBs = readMs ? (static_cast<double>(total) / readMs / 1000.0) : 0.0;
    const double pushMBs = pushMs
        ? (static_cast<double>(W) * H * 2.0 / pushMs / 1000.0) : 0.0;
    Serial.printf("[BENCH] %s: read=%lu ms (%u B, %.2f MB/s)  push=%lu ms (%.2f MB/s) "
                  "=> combinat ~%lu ms\n",
                  name, static_cast<unsigned long>(readMs), static_cast<unsigned>(total),
                  readMBs, static_cast<unsigned long>(pushMs), pushMBs,
                  static_cast<unsigned long>(readMs + pushMs));
    return true;
}

const Status& status()
{
    return gStatus;
}

/* --- Lectura per franges (per fer barreges translucides) ------------------ */

namespace {
File     gStrip;                 /* fitxer obert mentre es llegeix per franges */
uint16_t gStripW = 0;
uint16_t gStripH = 0;
}  // namespace

bool beginStrip(const char* name)
{
    endStrip();
    const SdAssets::Backgrounds& bgs = SdAssets::backgrounds();
    gStripW = bgs.width;
    gStripH = bgs.height;
    if (name == nullptr || name[0] == '\0' || gStripW == 0 || gStripH == 0) {
        return false;
    }
    char path[kPathLen];
    snprintf(path, sizeof(path), "/backgrounds/%s.bin", name);
    gStrip = SD.open(path, FILE_READ);
    if (!gStrip) {
        Serial.printf("[BG] franges: no s'ha pogut obrir %s\n", path);
        return false;
    }
    return true;
}

bool readStripRow(int y, int x, int w, uint8_t* out)
{
    if (!gStrip || x < 0 || w <= 0) {
        return false;
    }
    if (x + w > static_cast<int>(gStripW)) {
        return false;
    }
    /* Si el fons es mes baix que la pantalla (pack de 320x400), repetim la
     * darrera fila: aixi les targetes de vidre tambe funcionen abaix de tot. */
    if (y >= static_cast<int>(gStripH)) {
        y = static_cast<int>(gStripH) - 1;
    }
    if (y < 0) {
        return false;
    }
    const uint32_t off = (static_cast<uint32_t>(y) * gStripW + static_cast<uint32_t>(x)) * 2u;
    if (!gStrip.seek(off)) {
        return false;
    }
    const size_t bytes = static_cast<size_t>(w) * 2u;
    return gStrip.read(out, bytes) == bytes;
}

void endStrip()
{
    if (gStrip) {
        gStrip.close();
    }
}

}  // namespace BgRenderer
