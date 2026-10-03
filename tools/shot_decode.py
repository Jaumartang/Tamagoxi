#!/usr/bin/env python3
"""
shot_decode.py - Captura de pantalla del TAMAGOTXI pel port serie.

La consola del firmware te la comanda "shot [x y [w h]]", que llegeix del
panell el que hi ha PINTAT de veritat (TFT_eSPI readRect; el LCD te el MISO al
GPIO 12) i ho treu en hexadecimal:

    SHOT BEGIN <x> <y> <w> <h>
    SHOT <fila> <hex...>
    SHOT END <w> <h>

Aquest script envia la comanda, llegeix la resposta i desa un PNG amb els
pixels tal com surten a la pantalla. Serveix per depurar composicions d'escena
(fons directe + LVGL) sense haver de fer fotos.

Us:
  python3 tools/shot_decode.py PORT [DESTI.png] [opcions]

Opcions:
  --region X Y W H   captura nomes aquesta regio (per defecte: pantalla sencera)
  --baud N           velocitat de la captura (per defecte 460800)
  --scale N          escala el PNG N cops amb veins (per defecte 1)
  --keep-baud        no torna a 115200 al final

Exemples:
  python3 tools/shot_decode.py /dev/cu.wchusbserial1410 /tmp/pantalla.png
  python3 tools/shot_decode.py PORT /tmp/hud.png --region 0 0 320 140 --scale 2

Requisits: pyserial i Pillow  (pip3 install pyserial Pillow)
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit("Falta pyserial. Instal·la amb:  pip3 install pyserial")

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("Falta Pillow. Instal·la amb:  pip3 install Pillow")

BASE_BAUD = 115200


class Device:
    """Port serie amb la consola del firmware (mateix protocol que sd_upload)."""

    def __init__(self, port, baud, keep_baud=False):
        self.keep_baud = keep_baud
        self.baud = BASE_BAUD
        self.ser = serial.Serial(port, BASE_BAUD, timeout=0.05)
        time.sleep(0.2)
        self.drain()
        if baud != BASE_BAUD:
            self.set_baud(baud)

    def drain(self):
        self.ser.reset_input_buffer()
        time.sleep(0.1)
        self.ser.reset_input_buffer()

    def write(self, text):
        self.ser.write(text.encode("ascii"))
        self.ser.flush()

    def set_baud(self, baud):
        # "BAUD OK" arriba a la velocitat actual; nomes despres el dispositiu
        # canvia, aixi que esperem la confirmacio abans de reobrir el port.
        self.write(f"baud {baud}\n")
        buf = b""
        deadline = time.time() + 3.0
        while time.time() < deadline and b"BAUD OK" not in buf:
            buf += self.ser.read(256)
        if b"BAUD OK" not in buf:
            raise RuntimeError("el dispositiu no ha confirmat el canvi de baud")
        time.sleep(0.3)
        self.ser.baudrate = baud
        self.baud = baud

    def close(self, restore=True):
        if restore and not self.keep_baud and self.baud != BASE_BAUD:
            try:
                self.set_baud(BASE_BAUD)
            except Exception as exc:  # noqa: BLE001 - informem i prou
                print(f"  (avís: no s'ha pogut tornar a 115200: {exc})")
        self.ser.close()

    def shot(self, region=None, timeout=240.0):
        """Demana una captura. Retorna (x, y, w, h, [(fila, bytes), ...])."""
        cmd = "shot" if region is None else "shot %d %d %d %d" % tuple(region)
        self.drain()
        self.write(cmd + "\n")

        x = y = w = h = 0
        started = False
        bands = {}
        deadline = time.time() + timeout
        last_data = time.time()

        while time.time() < deadline:
            raw = self.ser.readline()
            if not raw:
                if started and time.time() - last_data > 10.0:
                    print("  (avís: la captura s'ha tallat)")
                    break
                continue
            last_data = time.time()
            text = raw.decode("ascii", "replace").strip()

            if text.startswith("[CMD]"):
                print("  " + text)
                continue
            if text.startswith("SHOT BEGIN"):
                parts = text.split()
                x, y, w, h = (int(v) for v in parts[2:6])
                started = True
                print(f"  captura de {w}x{h} pixels a ({x},{y}) ...")
                continue
            if text.startswith("SHOT END"):
                break
            if text.startswith("SHOT ") and started:
                parts = text.split()
                if len(parts) < 3:
                    continue
                row = int(parts[1])
                if row not in bands:
                    bands[row] = bytearray()
                bands[row] += bytes.fromhex(parts[2])

        if not started:
            raise RuntimeError("el dispositiu no ha respost a 'shot'")
        return x, y, w, h, [(row, bytes(bands[row])) for row in sorted(bands)]


def to_image(w, h, bands):
    """Construeix la imatge a partir de les franges (RGB565 big-endian)."""
    img = Image.new("RGB", (w, h), (0, 0, 0))
    px = img.load()
    rows_done = 0
    for row, data in bands:
        pixels = len(data) // 2
        for i in range(pixels):
            v = (data[2 * i] << 8) | data[2 * i + 1]
            cy = row + i // w
            if cy >= h:
                break
            px[i % w, cy] = (((v >> 11) & 0x1F) * 255 // 31,
                             ((v >> 5) & 0x3F) * 255 // 63,
                             (v & 0x1F) * 255 // 31)
        rows_done = max(rows_done, row + (pixels // w if w else 0))
    return img, rows_done


def main():
    ap = argparse.ArgumentParser(description="Captura la pantalla del TAMAGOTXI pel port serie.")
    ap.add_argument("port", help="p.ex. /dev/cu.wchusbserial1410")
    ap.add_argument("dest", nargs="?", default="pantalla.png", help="PNG de sortida")
    ap.add_argument("--region", type=int, nargs=4, metavar=("X", "Y", "W", "H"),
                    help="captura nomes aquesta regio")
    ap.add_argument("--baud", type=int, default=460800,
                    help="115200 | 230400 | 460800 | 921600")
    ap.add_argument("--scale", type=int, default=1, help="escala del PNG (veins)")
    ap.add_argument("--keep-baud", action="store_true", help="no torna a 115200 al final")
    args = ap.parse_args()

    dev = Device(args.port, args.baud, keep_baud=args.keep_baud)
    try:
        t0 = time.time()
        x, y, w, h, bands = dev.shot(args.region)
        img, rows_done = to_image(w, h, bands)
        colors = img.getcolors(maxcolors=1 << 24) or []
        print(f"  {len(bands)} franges, {rows_done} files, {len(colors)} colors, "
              f"{time.time() - t0:.1f} s")
        if len(colors) <= 2:
            print("  AVIS: gairebe tot el mateix color -> el panell potser no retorna "
                  "dades (MISO no connectat?) o la pantalla es en blanc.")
        if args.scale > 1:
            img = img.resize((img.width * args.scale, img.height * args.scale),
                             Image.NEAREST)
        img.save(args.dest)
        print(f"  desat {args.dest} ({img.width}x{img.height})")
    finally:
        dev.close()


if __name__ == "__main__":
    main()
