#include "audio.h"

#include <Arduino.h>
#include <SD.h>
#include <driver/i2s.h>
#include <math.h>

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"

#include "tg_config.h"
#include "pins.h"

namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;

Audio::Output gOutput = Audio::Output::Dac;
uint8_t       gVolume = 60;
bool          gDacReady = false;
uint32_t      gDacRate = 0;

void dacWrite(const uint16_t* samples, size_t count);
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
                dacWrite(buf, n * 2);
                done += n;
            }
            return len;
        }

        /* Estereo: passem el bloc tal qual (fins a 1024 mostres). */
        constexpr size_t kMaxSamples = 2048;
        size_t done = 0;
        if (samples <= kMaxSamples) {
            dacWrite(src, samples);
            return len;
        }
        while (done < samples) {
            const size_t n = (samples - done > kMaxSamples) ? kMaxSamples : (samples - done);
            dacWrite(src + done, n);
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
    if (gOutput != Audio::Output::Dac) {
        Serial.println(F("[AUDIO] la sortida Bluetooth encara no esta implementada"));
        return;
    }
    if (!dacBegin(AUDIO_SAMPLE_RATE)) {
        return;
    }

    char path[64];
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
    for (;;) {
        if (gPlayReq) {
            gPlayReq = false;
            playFile(gReqIndex);
            gSt.playing = false;
        }
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}

}  // namespace

namespace Audio {

void begin()
{
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

    char path[64];
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
}

}  // namespace Audio
