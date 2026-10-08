#!/usr/bin/env python3
"""png2bin.py - Converteix PNG (RGBA) a .bin RGB565 big-endian per a la Tamagoxi.

Pren una carpeta amb els PNG d'un estat (00.png, 01.png...) i escriu 00.bin,
01.bin... amb el fons transparent 0xF81F.

Com troba el fons:
  1. Usa el canal alfa del PNG si en te (alpha < 128 -> transparent).
  2. Tambe treu el fons (magenta o blanc) que TOQUI LA VORA, amb un flood fill:
     aixi un fons blanc desapareix sencer sense menjar-se els blancs interiors
     del drac (per exemple una brillantor a l'ull).

Us:
    python3 tools/png2bin.py <carpeta_png> <carpeta_sortida>
"""

import os
import sys

from PIL import Image

TRANS = 0xF81F          # magenta: el color transparent que enten el firmware


def is_magenta(r, g, b):
    return r > 200 and g < 70 and b > 200


def is_whiteish(r, g, b):
    return r > 235 and g > 235 and b > 235


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert_file(path, out_path):
    """Retorna (w, h, pixels_de_fons_trets)."""
    im = Image.open(path).convert('RGBA')
    w, h = im.size
    px = im.load()

    # --- Flood fill des de tota la vora, a traves dels pixels de fons ---
    bg = bytearray(w * h)
    stack = []
    for x in range(w):
        stack.append((x, 0))
        stack.append((x, h - 1))
    for y in range(h):
        stack.append((0, y))
        stack.append((w - 1, y))

    while stack:
        x, y = stack.pop()
        i = y * w + x
        if bg[i]:
            continue
        r, g, b, a = px[x, y]
        if not (a < 128 or is_magenta(r, g, b) or is_whiteish(r, g, b)):
            continue
        bg[i] = 1
        if x > 0:
            stack.append((x - 1, y))
        if x < w - 1:
            stack.append((x + 1, y))
        if y > 0:
            stack.append((x, y - 1))
        if y < h - 1:
            stack.append((x, y + 1))

    out = bytearray(w * h * 2)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            r, g, b, a = px[x, y]
            if bg[i] or a < 128:
                v = TRANS
            else:
                v = rgb565(r, g, b)
            out[i * 2] = (v >> 8) & 0xFF        # big-endian ✓
            out[i * 2 + 1] = v & 0xFF
    with open(out_path, 'wb') as f:
        f.write(out)
    return w, h, sum(bg)


def is_frame(name):
    """Nomes "NN.png" (tot digits): ignora els muntatges i la brossa del Mac."""
    stem, ext = os.path.splitext(name)
    return ext.lower() == '.png' and stem.isdigit() and not name.startswith('.')


def convert_dir(src, dst):
    """Converteix una carpeta de PNG NN.png a dst/NN.bin. Retorna 0 si va be."""
    os.makedirs(dst, exist_ok=True)
    pngs = sorted((f for f in os.listdir(src) if is_frame(f)),
                  key=lambda s: int(os.path.splitext(s)[0]))
    if not pngs:
        print('  (cap PNG NN.png a %s)' % src)
        return 1

    w = h = 0
    for i, name in enumerate(pngs):
        w, h, removed = convert_file(os.path.join(src, name),
                                     os.path.join(dst, '%02d.bin' % i))
        print('    %s -> %02d.bin  (fons tret: %d px)' % (name, i, removed))
    print('  %u frames  %dx%d  ->  %s' % (len(pngs), w, h, dst))
    return 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src = sys.argv[1]
    dst = sys.argv[2]

    # Si la carpeta no te PNG pero si subcarpetes (IDLE/, HAPPY/...), les
    # convertim totes: aixi n'hi ha prou amb "png2bin.py Assets Assets/bin".
    if not any(is_frame(f) for f in os.listdir(src)):
        subs = [d for d in sorted(os.listdir(src))
                if os.path.isdir(os.path.join(src, d))]
        if not subs:
            print('  (ni PNG ni subcarpetes a %s)' % src)
            return 1
        rc = 0
        for st in subs:
            print('==', st)
            rc |= convert_dir(os.path.join(src, st), os.path.join(dst, st))
        return rc
    return convert_dir(src, dst)


if __name__ == '__main__':
    sys.exit(main())
