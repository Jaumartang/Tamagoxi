#include "audio.h"

#include <Arduino.h>
#include <SD.h>
#include <driver/i2s.h>
#include <math.h>

#include "tg_config.h"
#include "net.h"
#include "pins.h"
#include "sprite_renderer.h"

#include <esp_gap_bt_api.h>
#include <esp_a2dp_api.h>

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#if AUDIO_BT
#include "AudioTools/Communication/A2DPStream.h"
/* El callback de descobriment de la biblioteca: ens hi encadenem per poder
 * llistar els dispositius que es troben sense trencar-li res. */
extern "C" void ccall_app_gap_callback(esp_bt_gap_cb_event_t event,
                                       esp_bt_gap_cb_param_t* param);
#endif

namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;

Audio::Output gOutput = Audio::Output::Dac;
uint8_t       gVolume = 60;
bool          gDacReady = false;
uint32_t      gDacRate = 0;

void dacWrite(const uint16_t* samples, size_t count);
void outWrite(const uint16_t* samples, size_t count);
void dacSetRate(uint32_t rate);
bool dacActive();

volatile uint32_t gPcmSamples = 0;   /* mostres per canal reproduides */
uint32_t          gPcmRate    = AUDIO_SAMPLE_RATE;

/*
 * Sink cap al DAC intern. El decodificador d'audio-tools hi escriu el PCM ja
 * descodificat; nosaltres el passem a l'I2S del DAC intern (GPIO26).
 */
class DacSink : public audio_tools::AudioStream {
  public:
    bool begin() override
    {
        return true;
    }

    void setAudioInfo(audio_tools::AudioInfo info) override
    {
        if (info.channels > 0 && info.channels != gChannels) {
            Serial.printf("[AUDIO] format: %u canals, %u bits\n", info.channels,
                          info.bits_per_sample);
        }
        if (info.sample_rate > 0 && info.sample_rate != gPcmRate) {
            Serial.printf("[AUDIO] mostra: %lu Hz (era %lu)\n",
                          static_cast<unsigned long>(info.sample_rate),
                          static_cast<unsigned long>(gPcmRate));
        }
        gChannels = (info.channels > 0) ? info.channels : 2;
        const uint32_t rate = info.sample_rate ? info.sample_rate : AUDIO_SAMPLE_RATE;
        gPcmRate = rate;
        if (rate != gDacRate) {
            dacSetRate(rate);
        }
    }

    size_t write(const uint8_t* data, size_t len) override
    {
        if (len == 0) {
            return 0;
        }
        const uint16_t* src = reinterpret_cast<const uint16_t*>(data);
        const size_t samples = len / 2;
        gPcmSamples += (gChannels == 1) ? samples : (samples / 2);

        if (gChannels == 1) {
            /* Mono -> estereo (el DAC va en estereo i l'amplificador es mono). */
            constexpr size_t kFrames = 128;
            uint16_t buf[kFrames * 2];
            size_t done = 0;
            while (done < samples) {
                const size_t n = (samples - done > kFrames) ? kFrames : (samples - done);
                for (size_t i = 0; i < n; ++i) {
                    const uint16_t v = src[done + i];
                    buf[2 * i] = v;
                    buf[2 * i + 1] = v;
                }
                outWrite(buf, n * 2);
                done += n;
            }
            return len;
        }

        /* Estereo: passem el bloc tal qual (fins a 1024 mostres). */
        constexpr size_t kMaxSamples = 2048;
        size_t done = 0;
        if (samples <= kMaxSamples) {
            outWrite(src, samples);
            return len;
        }
        while (done < samples) {
            const size_t n = (samples - done > kMaxSamples) ? kMaxSamples : (samples - done);
            outWrite(src + done, n);
            done += n;
        }
        return len;
    }

    int availableForWrite() override
    {
        return 4096;
    }

  private:
    size_t gChannels = 2;
};

/* --- Sortida DAC interna (GPIO26 -> amplificador de la placa) -------------- */

bool dacBegin(uint32_t sampleRate)
{
    if (gDacReady) {
        return true;
    }

    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX |
                                       I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = sampleRate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = 0;

    if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
        Serial.println(F("[AUDIO] no s'ha pogut instal·lar el driver I2S"));
        return false;
    }
    /* GPIO26 es el DAC-L (canal esquerre). */
    i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
    i2s_set_sample_rates(kPort, sampleRate);
    i2s_zero_dma_buffer(kPort);
    gDacReady = true;
    gDacRate  = sampleRate;
    Serial.printf("[AUDIO] DAC intern a punt (GPIO%d, %lu Hz)\n", PIN_SPEAKER,
                  static_cast<unsigned long>(sampleRate));
    return true;
}

void dacStop()
{
    if (!gDacReady) {
        return;
    }
    i2s_zero_dma_buffer(kPort);
    i2s_driver_uninstall(kPort);
    gDacReady = false;
    gDacRate  = 0;
}

bool dacActive()
{
    return gDacReady;
}

/* Canvia la frequencia de mostreig sobre la marxa (segons el fitxer). */
void dacSetRate(uint32_t rate)
{
    if (!gDacReady || rate == gDacRate || rate == 0) {
        return;
    }
    i2s_set_sample_rates(kPort, rate);
    gDacRate = rate;
}

/* Escriu un bloc de mostres de 16 bits (estereo) al DAC intern. */
void dacWrite(const uint16_t* samples, size_t count)
{
    size_t written = 0;
    size_t todo = count * sizeof(uint16_t);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(samples);
    while (todo > 0) {
        if (i2s_write(kPort, p, todo, &written, portMAX_DELAY) != ESP_OK) {
            break;
        }
        p += written;
        todo -= written;
    }
}

/* Genera un to sinusoidal. Omple 'frames' frames estereo (2 mostres cadascun).
 * Al DAC intern li cal el valor de 8 bits als 8 bits ALTS de la mostra. */
void fillTone(uint16_t* buf, size_t frames, float freq, uint32_t rate, float amp,
              double& phase)
{
    const double step = 2.0 * M_PI * static_cast<double>(freq) / static_cast<double>(rate);
    for (size_t i = 0; i < frames; ++i) {
        const double s = sin(phase);
        phase += step;
        if (phase > 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
        }
        int v = 128 + static_cast<int>(amp * 127.0 * s);
        if (v < 0) {
            v = 0;
        }
        if (v > 255) {
            v = 255;
        }
        const uint16_t sample = static_cast<uint16_t>(v << 8);
        buf[2 * i] = sample;
        buf[2 * i + 1] = sample;
    }
}

/* --- Reproductor (playlist de /music a la SD) ------------------------------ */

File                            gFile;
DacSink                         gDacSink;
audio_tools::MP3DecoderHelix    gDecMp3;
audio_tools::WAVDecoder         gDecWav;
audio_tools::EncodedAudioStream gDec(&gDacSink, &gDecMp3);

Audio::Track  gTracks[Audio::kMaxTracks];
Audio::Status gSt = {};

volatile bool    gPlayReq  = false;
volatile bool    gStopReq  = false;
volatile bool    gNextReq  = false;
volatile bool    gPrevReq  = false;
volatile bool    gPauseReq = false;
volatile uint8_t gReqIndex = 0;
volatile uint8_t gTrackCount = 0;
TaskHandle_t     gTask     = nullptr;

/* --- Bluetooth (A2DP) ------------------------------------------------------ */

#if AUDIO_BT
audio_tools::A2DPStream gA2dp;
#endif
volatile Audio::BtMode   gBtMode = Audio::BtMode::Off;
bool                     gBtStarted = false;

bool btBegin(audio_tools::RxTxMode rxtx, Audio::BtMode mode)
{
#if AUDIO_BT
    if (gBtStarted) {
        return true;
    }

    /* Aquest xip no te PSRAM i la pila Bluetooth en demana ~90 kB. Per fer-hi
     * lloc: (1) la mascota deixa anar els seus buffers grossos i (2) s'atura el
     * WiFi, que tambe allibera memoria i, a mes, la radio no pot atendre'l
     * mentre fa Bluetooth. */
    SpriteRenderer::suspend();
    Net::setEnabled(false);
    vTaskDelay(600 / portTICK_PERIOD_MS);

    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 70000) {
        Serial.printf("[BT] memoria insuficient (%u kB lliures, en calen ~70): "
                      "no encenc el Bluetooth\n",
                      static_cast<unsigned>(freeHeap / 1024));
        SpriteRenderer::resume();
        Net::setEnabled(true);
        return false;
    }

    auto cfg = gA2dp.defaultConfig(rxtx);
    cfg.name  = "Tamagoxi";
    cfg.wait_for_connection = false;
    if (!gA2dp.begin(cfg)) {
        Serial.println(F("[BT] no s'ha pogut iniciar el Bluetooth"));
        SpriteRenderer::resume();
        Net::setEnabled(true);
        return false;
    }
    gBtStarted = true;
    Serial.printf("[BT] \"%s\" a punt (%s, heap %u kB): %s\n", cfg.name,
                  (mode == Audio::BtMode::Source) ? "emissor" : "altaveu",
                  static_cast<unsigned>(ESP.getFreeHeap() / 1024),
                  (mode == Audio::BtMode::Source)
                      ? "emparella-hi uns auriculars i posa musica"
                      : "emparella-hi el mobil i envia-hi musica");
    return true;
#else
    Serial.println(F("[BT] Bluetooth no compilat en aquesta versio (-D AUDIO_BT=1)"));
    return false;
#endif
}

/* Encamina un bloc de PCM estereo de 16 bits cap a la sortida activa: els
 * auriculars (o altaveu) Bluetooth si estem en mode emissor, o el DAC de la
 * placa (GPIO26 -> amplificador). */
void outWrite(const uint16_t* samples, size_t count)
{
    if (count == 0) {
        return;
    }
#if AUDIO_BT
    if (gBtMode == Audio::BtMode::Source && gBtStarted && gA2dp.isConnected()) {
        gA2dp.write(reinterpret_cast<const uint8_t*>(samples), count * 2u);
        return;
    }
#endif
    dacWrite(samples, count);
}

/* --- Cerca de dispositius Bluetooth (auriculars, altaveus) ----------------- */

namespace {

Audio::BtDevice  gDevices[Audio::kBtMaxDevices];
volatile uint8_t gDeviceCount = 0;
volatile bool    gScanning    = false;
bool             gGapChained  = false;
int8_t           gConnectedIdx = -1;
uint32_t         gConnectTryMs = 0;    /* quan s'ha demanat l'ultim enllac */

/* Treu el nom, el senyal i la classe d'un aparell trobat i, si es d'audio
 * (auriculars, altaveu...), el desa a la llista. */
void collectDevice(esp_bt_gap_cb_param_t* param)
{
    const esp_bt_gap_dev_prop_t* propName = nullptr;
    const esp_bt_gap_dev_prop_t* propRssi = nullptr;
    const esp_bt_gap_dev_prop_t* propCod  = nullptr;
    for (int i = 0; i < param->disc_res.num_prop; ++i) {
        const esp_bt_gap_dev_prop_t* p = &param->disc_res.prop[i];
        if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            propName = p;
        } else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI) {
            propRssi = p;
        } else if (p->type == ESP_BT_GAP_DEV_PROP_COD) {
            propCod = p;
        }
    }

    /* Nomes aparells d'audio: classe major 0x04 (Audio/Video). */
    if (propCod == nullptr) {
        return;
    }
    const uint32_t cod = *reinterpret_cast<const uint32_t*>(propCod->val);
    if (((cod >> 8) & 0x1Fu) != 0x04u) {
        return;
    }

    for (uint8_t i = 0; i < gDeviceCount; ++i) {          /* ja el teniem? */
        if (memcmp(gDevices[i].addr, param->disc_res.bda, 6) == 0) {
            return;
        }
    }
    if (gDeviceCount >= Audio::kBtMaxDevices) {
        return;
    }

    Audio::BtDevice& d = gDevices[gDeviceCount];
    if (propName != nullptr && propName->len > 0) {
        const size_t n = (propName->len < static_cast<int>(Audio::kBtNameMax - 1))
                             ? static_cast<size_t>(propName->len)
                             : (Audio::kBtNameMax - 1);
        memcpy(d.name, propName->val, n);
        d.name[n] = '\0';
    } else {
        /* Sense nom (alguns auriculars no el donen a la primera): l'adreça. */
        const uint8_t* a = param->disc_res.bda;
        snprintf(d.name, sizeof(d.name), "Aparell %02X:%02X:%02X", a[3], a[4], a[5]);
    }
    memcpy(d.addr, param->disc_res.bda, 6);
    d.rssi = (propRssi != nullptr) ? *reinterpret_cast<const int8_t*>(propRssi->val) : 0;
    d.connected = false;
    ++gDeviceCount;
    Serial.printf("[BT] trobat: %s (%d dBm)\n", d.name, static_cast<int>(d.rssi));

    /* L'EIR no sempre porta el nom: el demanem (com fa el mobil). */
    if (propName == nullptr) {
        esp_bt_gap_read_remote_name(d.addr);
    }
}

/* El nom ha arribat: el posem a la fila que li toca. */
void setDeviceName(const uint8_t* bda, const char* nm)
{
    if (nm == nullptr || nm[0] == '\0') {
        return;
    }
    for (uint8_t i = 0; i < gDeviceCount; ++i) {
        if (memcmp(gDevices[i].addr, bda, 6) == 0) {
            /* El buffer de l'IDF pot no estar tancat amb '\0': copiem amb mida. */
            const size_t n = strnlen(nm, Audio::kBtNameMax - 1);
            memcpy(gDevices[i].name, nm, n);
            gDevices[i].name[n] = '\0';
            Serial.printf("[BT] nom: %s\n", gDevices[i].name);
            return;
        }
    }
}

/* Ens posem al davant del callback de la biblioteca i li ho passem tot. */
void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param)
{
    if (event == ESP_BT_GAP_DISC_RES_EVT) {
        collectDevice(param);
    } else if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT) {
        setDeviceName(param->read_rmt_name.bda,
                      reinterpret_cast<const char*>(param->read_rmt_name.rmt_name));
    } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT
               && param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        gScanning = false;
        Serial.printf("[BT] cerca acabada: %u aparells\n",
                      static_cast<unsigned>(gDeviceCount));
    }
    ccall_app_gap_callback(event, param);        /* la biblioteca, com sempre */
}

}  // namespace

bool endsWith(const char* name, const char* ext)
{
    const size_t n = strlen(name);
    const size_t e = strlen(ext);
    return (n > e) && (strcasecmp(name + n - e, ext) == 0);
}

bool isPlayable(const char* name)
{
    return endsWith(name, ".mp3") || endsWith(name, ".wav");
}

/* Escriu silenci mentre esta en pausa (i mira si ha de continuar). */
void silenceWhilePaused()
{
    constexpr size_t kFrames = 128;
    const uint16_t zero[kFrames * 2] = {0};
    while (gSt.paused && !gStopReq && !gPlayReq && !gNextReq && !gPrevReq) {
        if (!dacActive()) {
            return;                      /* amb Bluetooth no cal omplir res */
        }
        dacWrite(zero, kFrames * 2);
    }
}

void playFile(uint8_t index)
{
    if (index >= gTrackCount) {
        return;
    }
    if (gOutput != Audio::Output::Dac && gBtMode != Audio::BtMode::Source) {
        Serial.println(F("[AUDIO] activa el Bluetooth (bt source) o passa a la sortida de placa"));
        return;
    }
    if (!dacBegin(AUDIO_SAMPLE_RATE)) {
        return;
    }

    char path[Audio::kNameMax + 16];   /* que cap nom sencer quedi tallat */
    snprintf(path, sizeof(path), "/music/%s", gTracks[index].name);
    gFile = SD.open(path, FILE_READ);
    if (!gFile) {
        Serial.printf("[AUDIO] no s'ha pogut obrir %s\n", path);
        return;
    }

    const bool mp3 = endsWith(gTracks[index].name, ".mp3");
    gDec.setDecoder(mp3 ? static_cast<audio_tools::AudioDecoder*>(&gDecMp3)
                        : static_cast<audio_tools::AudioDecoder*>(&gDecWav));
    gDec.begin();

    gPcmSamples  = 0;
    gSt.playing  = true;
    gSt.paused   = false;
    gSt.index    = index;
    gSt.percent  = 0;
    gSt.elapsedSec = 0;
    gSt.totalSec   = 0;
    strlcpy(gSt.name, gTracks[index].name, sizeof(gSt.name));
    Serial.printf("[AUDIO] sonant: %s (%s, %u B)\n", gTracks[index].name,
                  mp3 ? "mp3" : "wav", static_cast<unsigned>(gTracks[index].bytes));

    uint8_t buf[1024];
    while (true) {
        if (gStopReq) {
            gStopReq = false;
            break;
        }
        if (gNextReq || gPrevReq) {
            const bool fwd = gNextReq;
            gNextReq = false;
            gPrevReq = false;
            const uint8_t n = gTrackCount ? gTrackCount : 1;
            const uint8_t nxt = fwd ? static_cast<uint8_t>((index + 1) % n)
                                    : static_cast<uint8_t>((index + n - 1) % n);
            gFile.close();
            gSt.playing = false;
            playFile(nxt);
            return;
        }
        if (gPauseReq) {
            gPauseReq = false;
            gSt.paused = true;
            silenceWhilePaused();
            gSt.paused = false;
            continue;
        }

        const size_t n = gFile.read(buf, sizeof(buf));
        if (n == 0) {
            break;                       /* fi del fitxer */
        }
        gDec.write(buf, n);

        const audio_tools::AudioInfo info = gDec.audioInfo();
        if (info.channels > 0) {
            gDacSink.setAudioInfo(info);
        }

        const uint32_t pos = gFile.position();
        const uint32_t total = gTracks[index].bytes;
        gSt.percent = total ? static_cast<uint8_t>((pos * 100u) / total) : 0;
        if (gSt.percent > 100) {
            gSt.percent = 100;
        }
        gSt.elapsedSec = gPcmSamples / (gPcmRate ? gPcmRate : AUDIO_SAMPLE_RATE);
        if (gSt.percent >= 3 && gSt.elapsedSec > 1) {
            gSt.totalSec = static_cast<uint32_t>(
                static_cast<uint64_t>(gSt.elapsedSec) * 100u / gSt.percent);
        }
    }

    gFile.close();
    gDec.end();
    gSt.playing = false;
    gSt.paused  = false;
    gSt.percent = 0;
    gPcmSamples = 0;
    Serial.println(F("[AUDIO] fi del tema"));
}

void playerTask(void*)
{
    uint8_t btBuf[512];
    for (;;) {
        if (gPlayReq) {
            gPlayReq = false;
            playFile(gReqIndex);
            gSt.playing = false;
        }

        /* Mode altaveu Bluetooth: el mobil hi envia la musica (Spotify, radio...)
         * i nosaltres la traiem pel DAC de la placa. */
#if AUDIO_BT
        if (gBtMode == Audio::BtMode::Sink && gBtStarted) {
            if (gA2dp.available() > 0) {
                const size_t n = gA2dp.readBytes(btBuf, sizeof(btBuf));
                if (n > 0) {
                    if (!dacActive()) {
                        dacBegin(AUDIO_SAMPLE_RATE);
                    }
                    audio_tools::AudioInfo info;
                    info.sample_rate     = 44100;
                    info.channels        = 2;
                    info.bits_per_sample = 16;
                    gDacSink.setAudioInfo(info);
                    gDacSink.write(btBuf, n);
                }
            }
        }
#endif

        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

}  // namespace

namespace Audio {

/* --- Cerca i enllacament de dispositius (auriculars, altaveus) ------------- */

void btStartScan()
{
#if AUDIO_BT
    if (!gBtStarted) {
        Serial.println(F("[BT] primer encen el Bluetooth (bt source)"));
        return;
    }
    if (!gGapChained) {
        esp_bt_gap_register_callback(gapCallback);
        gGapChained = true;
    }
    gDeviceCount = 0;
    gScanning    = true;
    const esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 0x08, 0);
    Serial.printf("[BT] cercant aparells d'audio... (%s)\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        gScanning = false;
    }
#else
    Serial.println(F("[BT] Bluetooth no compilat en aquesta versio"));
#endif
}

bool btScanning()
{
    return gScanning;
}

uint8_t btDeviceCount()
{
    return gDeviceCount;
}

const BtDevice* btDevice(uint8_t index)
{
    return (index < gDeviceCount) ? &gDevices[index] : nullptr;
}

bool btConnect(uint8_t index)
{
#if AUDIO_BT
    if (!gBtStarted || index >= gDeviceCount) {
        return false;
    }
    /* Si encara s'esta cercant, ho aturam: la cerca i l'enllac no es poden fer
     * alhora i la biblioteca es queixaria. */
    if (gScanning) {
        esp_bt_gap_cancel_discovery();
        gScanning = false;
    }
    esp_bd_addr_t addr;
    memcpy(addr, gDevices[index].addr, 6);
    const esp_err_t err = esp_a2d_source_connect(addr);
    Serial.printf("[BT] enllacant amb %s -> %s\n", gDevices[index].name,
                  esp_err_to_name(err));
    if (gConnectedIdx >= 0 && gConnectedIdx < static_cast<int8_t>(gDeviceCount)) {
        gDevices[gConnectedIdx].connected = false;
    }
    if (err == ESP_OK) {
        gConnectedIdx = static_cast<int8_t>(index);
        gConnectTryMs = millis();
    } else {
        gConnectedIdx = -1;
    }
    return err == ESP_OK;
#else
    (void)index;
    return false;
#endif
}

/* Aparell amb qui s'ha demanat enllacar (-1 si cap). */
int8_t btTargetIndex()
{
    return gConnectedIdx;
}

/* L'enllac s'ha demanat pero el dispositiu no ha respost? */
bool btConnectFailed()
{
    if (gConnectedIdx < 0 || gA2dp.isConnected()) {
        return false;
    }
    /* La biblioteca contesta de seguida si el dispositiu no accepta: si al cap
     * de vuit segons no hi ha enllac, donam l'intent per perdut. */
    return (millis() - gConnectTryMs) > 8000;
}

void btDisconnect()
{
#if AUDIO_BT
    if (gBtStarted && gConnectedIdx >= 0) {
        esp_bd_addr_t addr;
        memcpy(addr, gDevices[gConnectedIdx].addr, 6);
        esp_a2d_source_disconnect(addr);
        gDevices[gConnectedIdx].connected = false;
        gConnectedIdx = -1;
        Serial.println(F("[BT] enllac desfet"));
    }
#endif
}

void begin()
{
    /* Els decodificadors han d'informar el sink del format del fitxer (freq. de
     * mostreig i canals), si no el DAC aniria sempre a 44,1 kHz estereo. */
    gDecMp3.addNotifyAudioChange(gDacSink);
    gDecWav.addNotifyAudioChange(gDacSink);

    Serial.printf("[AUDIO] volum %u, sortida %s\n", static_cast<unsigned>(gVolume),
                  (gOutput == Output::Dac) ? "DAC (placa)" : "Bluetooth");
    xTaskCreatePinnedToCore(playerTask, "audio", AUDIO_TASK_STACK, nullptr, 2, &gTask, 0);
}

/* --- Llista de cançons ----------------------------------------------------- */

void scan()
{
    gTrackCount = 0;
    gSt.count   = 0;

    File dir = SD.open("/music");
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        Serial.println(F("[AUDIO] no hi ha la carpeta /music a la SD"));
        return;
    }

    while (gTrackCount < kMaxTracks) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        if (!entry.isDirectory()) {
            const char* nm = entry.name();
            const char* slash = strrchr(nm, '/');
            const char* base = (slash != nullptr) ? (slash + 1) : nm;
            if (base[0] != '.' && isPlayable(base)) {
                if (strlen(base) >= kNameMax) {
                    /* El nom no ens hi cap: desat tallat no es podria tornar a
                     * obrir (es perdria l'extensio). Val mes avisar i saltar-lo. */
                    Serial.printf("[AUDIO] nom massa llarg (%u caracters), el salto: %.24s...\n",
                                  static_cast<unsigned>(strlen(base)), base);
                    entry.close();
                    continue;
                }
                strlcpy(gTracks[gTrackCount].name, base, kNameMax);
                gTracks[gTrackCount].bytes = entry.size();
                ++gTrackCount;
            }
        }
        entry.close();
    }
    dir.close();

    /* Ordenem per nom (bombolla; en son poques). */
    for (uint8_t a = 0; a + 1 < gTrackCount; ++a) {
        for (uint8_t b = 0; b + 1 < gTrackCount - a; ++b) {
            if (strcasecmp(gTracks[b].name, gTracks[b + 1].name) > 0) {
                const Track tmp = gTracks[b];
                gTracks[b]      = gTracks[b + 1];
                gTracks[b + 1]  = tmp;
            }
        }
    }

    gSt.count = gTrackCount;
    Serial.printf("[AUDIO] %u cancons a /music\n", static_cast<unsigned>(gTrackCount));
    for (uint8_t i = 0; i < gTrackCount; ++i) {
        Serial.printf("   %2u. %s (%u kB)\n", static_cast<unsigned>(i + 1), gTracks[i].name,
                      static_cast<unsigned>(gTracks[i].bytes / 1024));
    }
}

const Track* track(uint8_t index)
{
    return (index < gTrackCount) ? &gTracks[index] : nullptr;
}

const Status& status()
{
    return gSt;
}

bool isPlaying()
{
    return gSt.playing;
}

void play(uint8_t index)
{
    if (index < gTrackCount) {
        gReqIndex = index;
        gPlayReq  = true;
    }
}

void togglePause()
{
    if (gSt.playing) {
        gPauseReq = true;
    }
}

void stop()
{
    if (gSt.playing) {
        gStopReq = true;
    }
}

void next()
{
    if (gSt.playing) {
        gNextReq = true;
    }
}

void previous()
{
    if (gSt.playing) {
        gPrevReq = true;
    }
}

/* --- WAV de prova (per validar el reproductor sense ordinador) ------------- */

namespace {

void wr16(File& f, uint16_t v)
{
    const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
    f.write(b, 2);
}

void wr32(File& f, uint32_t v)
{
    const uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                          static_cast<uint8_t>((v >> 16) & 0xFF),
                          static_cast<uint8_t>((v >> 24) & 0xFF)};
    f.write(b, 4);
}

}  // namespace

bool writeTestWav(const char* name, uint16_t seconds)
{
    if (seconds == 0) {
        seconds = 8;
    }
    if (!SD.exists("/music") && !SD.mkdir("/music")) {
        Serial.println(F("[AUDIO] no s'ha pogut crear /music"));
        return false;
    }

    char path[Audio::kNameMax + 16];   /* que cap nom sencer quedi tallat */
    snprintf(path, sizeof(path), "/music/%s", name);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        return false;
    }

    const uint32_t rate     = 22050;
    const uint16_t channels = 1;
    const uint16_t bits     = 16;
    const uint32_t total    = rate * seconds;
    const uint32_t dataBytes = total * 2u;
    const uint32_t t0 = millis();

    f.write(reinterpret_cast<const uint8_t*>("RIFF"), 4);
    wr32(f, 36 + dataBytes);
    f.write(reinterpret_cast<const uint8_t*>("WAVE"), 4);
    f.write(reinterpret_cast<const uint8_t*>("fmt "), 4);
    wr32(f, 16);
    wr16(f, 1);                                   /* PCM */
    wr16(f, channels);
    wr32(f, rate);
    wr32(f, rate * channels * bits / 8u);         /* bytes per segon */
    wr16(f, channels * bits / 8u);                /* alineacio de bloc */
    wr16(f, bits);
    f.write(reinterpret_cast<const uint8_t*>("data"), 4);
    wr32(f, dataBytes);

    /* Melodia de prova: 8 notes que pugen i baixen, mig segon cada una. */
    const uint16_t notes[8] = {523, 587, 659, 784, 880, 784, 659, 587};
    const uint32_t noteSamples = rate / 2;
    uint8_t buf[256];
    uint16_t bi = 0;
    uint32_t written = 0;
    for (uint32_t s = 0; s < total; ++s) {
        const uint32_t note = (s / noteSamples) % 8;
        const uint32_t inNote = s % noteSamples;
        const float env = 1.0f - (static_cast<float>(inNote) / noteSamples) * 0.6f;
        const double ph = 2.0 * M_PI * notes[note] * static_cast<double>(inNote) / rate;
        const int16_t v = static_cast<int16_t>(sin(ph) * 12000.0 * env);
        buf[bi++] = static_cast<uint8_t>(v & 0xFF);
        buf[bi++] = static_cast<uint8_t>((v >> 8) & 0xFF);
        if (bi >= sizeof(buf)) {
            f.write(buf, bi);
            written += bi;
            bi = 0;
        }
    }
    if (bi > 0) {
        f.write(buf, bi);
        written += bi;
    }
    f.close();
    Serial.printf("[AUDIO] WAV de prova: %s (%u B) en %lu ms\n", path,
                  static_cast<unsigned>(written),
                  static_cast<unsigned long>(millis() - t0));
    return true;
}

void setOutput(Output out)
{
    if (out == gOutput) {
        return;
    }
    if (out == Output::Dac) {
        dacBegin(AUDIO_SAMPLE_RATE);
    } else {
        dacStop();
    }
    gOutput = out;
    Serial.printf("[AUDIO] sortida -> %s\n", (out == Output::Dac) ? "DAC (placa)" : "Bluetooth");
}

Output output()
{
    return gOutput;
}

void setVolume(uint8_t v)
{
    gVolume = (v > 100) ? 100 : v;
}

uint8_t volume()
{
    return gVolume;
}

bool beep(uint16_t freqHz, uint16_t ms, uint8_t vol)
{
    if (freqHz < 20) {
        freqHz = 20;
    }
    if (vol > 100) {
        vol = 100;
    }

    Serial.printf("[AUDIO] to %u Hz, %u ms, volum %u (sortida %s)\n",
                  static_cast<unsigned>(freqHz), static_cast<unsigned>(ms),
                  static_cast<unsigned>(vol),
                  (gOutput == Output::Dac) ? "DAC" : "BT");

    if (gOutput != Output::Dac) {
        Serial.println(F("[AUDIO] la sortida Bluetooth encara no esta implementada"));
        return false;
    }
    if (!dacBegin(AUDIO_SAMPLE_RATE)) {
        return false;
    }

    constexpr size_t kFrames = 128;
    uint16_t buf[kFrames * 2];
    const float amp = 0.9f * (static_cast<float>(vol) / 100.0f);
    const uint32_t total = static_cast<uint32_t>(AUDIO_SAMPLE_RATE) * ms / 1000u;

    double phase = 0.0;
    uint32_t done = 0;
    const uint32_t t0 = millis();
    while (done < total) {
        const size_t frames = (total - done >= kFrames) ? kFrames : (total - done);
        fillTone(buf, frames, static_cast<float>(freqHz), AUDIO_SAMPLE_RATE, amp, phase);
        dacWrite(buf, frames * 2);
        done += frames;
    }
    i2s_zero_dma_buffer(kPort);
    Serial.printf("[AUDIO] to enviat en %lu ms (esperat ~%u ms)\n",
                  static_cast<unsigned long>(millis() - t0), static_cast<unsigned>(ms));
    return true;
}

void printStatus()
{
    Serial.printf("[AUDIO] sortida=%s volum=%u dac=%s\n",
                  (gOutput == Output::Dac) ? "DAC (placa)" : "Bluetooth",
                  static_cast<unsigned>(gVolume), gDacReady ? "iniciat" : "aturat");
    Serial.printf("[BT] mode=%s iniciat=%d connectat=%d\n",
                  (gBtMode == BtMode::Sink) ? "altaveu" :
                  ((gBtMode == BtMode::Source) ? "emissor" : "apagat"),
                  gBtStarted ? 1 : 0, btConnected() ? 1 : 0);
}

const char* btName()
{
    return "Tamagoxi";
}

bool btConnected()
{
#if AUDIO_BT
    return gBtStarted && gA2dp.isConnected();
#else
    return false;
#endif
}

BtMode btMode()
{
    return gBtMode;
}

void btSetMode(BtMode mode)
{
    if (mode == gBtMode) {
        return;
    }
    if (mode != BtMode::Off) {
        /* Emissor (source): enviem la nostra musica a uns auriculars o altaveu
         * Bluetooth. Altaveu (sink): el mobil hi envia la seva musica. */
        const audio_tools::RxTxMode rxtx =
            (mode == BtMode::Source) ? audio_tools::TX_MODE : audio_tools::RX_MODE;
        if (!btBegin(rxtx, mode)) {
            gBtMode = BtMode::Off;
            return;
        }
    } else {
#if AUDIO_BT
        if (gBtStarted) {
            gA2dp.end();
            gBtStarted = false;
        }
#endif
        /* La memoria que reserva la pila Bluetooth NOMES es recupera reiniciant
         * (l'end() no la torna). Com que els buffers de la mascota s'han hagut
         * d'alliberar per encabir-hi el Bluetooth, reiniciem per tenir-la de nou
         * i no deixar la consola a mitges. */
        Serial.println(F("[BT] reiniciant per recuperar la mascota..."));
        Serial.flush();
        vTaskDelay(300 / portTICK_PERIOD_MS);
        ESP.restart();
    }
    gBtMode = mode;
    Serial.printf("[BT] mode -> %s\n",
                  (mode == BtMode::Sink) ? "altaveu (sink: el mobil hi envia musica)"
                                         : ((mode == BtMode::Source)
                                                ? "emissor (source: musica cap als auriculars)"
                                                : "apagat"));
}

}  // namespace Audio
