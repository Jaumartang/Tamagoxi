#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * audio.h - So (Fase 7): reproduccio de musica i sortida d'audio.
 *
 * Sortides possibles:
 *   - DAC  : el DAC intern de l'ESP32 (GPIO26) connectat a l'amplificador
 *            FM8002B de la placa i, d'alla, a l'altaveu.
 *   - BT   : emissor Bluetooth A2DP (enviar la musica a un altaveu Bluetooth).
 *
 * Tot el so es treballa en una tasca propia perque la mascota no s'encalli.
 */

namespace Audio {

enum class Output : uint8_t { Dac, Bluetooth };

/* Bluetooth: la placa pot fer d'altaveu (sink: el mobil hi envia la musica) o
 * d'emissor (source: enviar la musica de la SD/radio a un altaveu Bluetooth). */
enum class BtMode : uint8_t { Off, Sink, Source };
void    btSetMode(BtMode mode);
BtMode  btMode();
bool    btConnected();
/* Aparell amb qui s'ha demanat enllacar (-1 si cap) i si l'intent ha quedat
 * sense resposta (la biblioteca ho diu de seguida; donam 8 s de marge). */
int8_t  btTargetIndex();
bool    btConnectFailed();
const char* btName();          /* nom amb que ens veuen els altres */
const char* btPeer();          /* dispositiu connectat ("" si cap) */

/* --- Cerca i enllacament de dispositius (auriculars, altaveus) ------------- */

constexpr uint8_t kBtMaxDevices = 8;
constexpr size_t  kBtNameMax    = 32;

struct BtDevice {
    char     name[kBtNameMax];
    uint8_t  addr[6];
    int8_t   rssi;
    bool     connected;      /* es el dispositiu enllacat ara */
};

/* Comenca una cerca (dura uns 10 s) i la fa la pila Bluetooth. */
void btStartScan();
bool btScanning();
uint8_t btDeviceCount();
const BtDevice* btDevice(uint8_t index);

/* Enllaca amb el dispositiu (l'index es de la llista de la cerca). */
bool btConnect(uint8_t index);
/* oblida l'enllac actual */
void btDisconnect();

void begin();

/* Sortida activa (es pot canviar en calent). */
void setOutput(Output out);
Output output();

/* Prova de maquinari: to de 'freqHz' durant 'ms' amb el volum indicat (0-100). */
bool beep(uint16_t freqHz, uint16_t ms, uint8_t volume);

/* Cambra de so: llista de cançons, estat i control del reproductor. */

constexpr uint8_t kMaxTracks = 64;
/* Els noms dels fitxers de la SD venen de la FAT (fins a 255 caracters). Amb 48
 * n'hi havia prou per a "prova.wav", pero qualsevol canco amb titol llarg es
 * tallava i despres no es podia tornar a obrir (es perdia l'extensio). */
constexpr size_t  kNameMax   = 128;

struct Track {
    char     name[kNameMax];
    uint32_t bytes;
};

struct Status {
    bool     playing;
    bool     paused;
    uint8_t  count;          /* cançons trobades a /music */
    uint8_t  index;          /* canço en curs */
    char     name[kNameMax];
    uint32_t elapsedSec;
    uint32_t totalSec;
    uint8_t  percent;
};

void scan();
const Track* track(uint8_t index);
const Status& status();
bool isPlaying();

void play(uint8_t index);
void togglePause();
void stop();
void next();
void previous();

/* Crea un WAV de prova a /music (per validar el reproductor sense ordinador). */
bool writeTestWav(const char* name, uint16_t seconds);

/* Volum 0-100 (es desa). */
void setVolume(uint8_t volume);
uint8_t volume();

void printStatus();

}  // namespace Audio
