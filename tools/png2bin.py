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
import re
import sys

from PIL import Image

TRANS = 0xF81F          # magenta: el color transparent que enten el firmware
MARGIN = 6              # px d'aire minim que volem al voltant del drac


def is_magenta(r, g, b):
    return r > 200 and g < 70 and b > 200


def is_whiteish(r, g, b):
    return r > 235 and g > 235 and b > 235


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def inset_if_touches_edge(im, margin=MARGIN):
    """Si el sprite toca la vora del marc, l'encongeix i el recentra perque
    quedi 'margin' px d'aire al voltant (aixi no queda mai tallat)."""
    alpha = im.getchannel('A')
    bbox = alpha.getbbox()
    if bbox is None:
        return im, False
    x0, y0, x1, y1 = bbox
    w, h = im.size
    if x0 >= margin and y0 >= margin and x1 <= w - margin and y1 <= h - margin:
        return im, False                    # ja hi ha prou aire ✓

    # Encongeix el sprite perque el seu quadre capiga dins el marc - marge.
    bw = x1 - x0
    bh = y1 - y0
    fit = min((w - 2 * margin) / bw, (h - 2 * margin) / bh, 1.0)
    nw = max(1, int(bw * fit))
    nh = max(1, int(bh * fit))
    crop = im.crop(bbox).resize((nw, nh), Image.LANCZOS)
    out = Image.new('RGBA', (w, h), (255, 0, 255, 0))
    out.paste(crop, ((w - nw) // 2, (h - nh) // 2), crop)
    return out, True


def convert_file(path, out_path):
    """Retorna (w, h, pixels_de_fons_trets, si_s_ha_retocat)."""
    im = Image.open(path).convert('RGBA')
    im, inset = inset_if_touches_edge(im)
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
    return w, h, sum(bg), inset


def frame_index(name):
    """Si el fitxer es "NN.png" o "<prefix>_NN.png", retorna l'index N;
    si no, -1 (aixi ignora muntatges, brossa del Mac i qualsevol altre)."""
    stem, ext = os.path.splitext(name)
    if ext.lower() != '.png' or name.startswith('.'):
        return -1
    if stem.isdigit():
        return int(stem)
    match = re.search(r'_(\d+)$', stem)
    return int(match.group(1)) if match else -1


def convert_dir(src, dst):
    """Converteix una carpeta de PNG NN.png a dst/NN.bin. Retorna 0 si va be."""
    os.makedirs(dst, exist_ok=True)
    pngs = sorted((f for f in os.listdir(src) if frame_index(f) >= 0),
                  key=frame_index)
    if not pngs:
        print('  (cap PNG NN.png o <nom>_NN.png a %s)' % src)
        return 1

    w = h = 0
    inset_count = 0
    for i, name in enumerate(pngs):
        w, h, removed, inset = convert_file(os.path.join(src, name),
                                            os.path.join(dst, '%02d.bin' % i))
        if inset:
            inset_count += 1
        print('    %s -> %02d.bin  (fons tret: %d px%s)' % (name, i, removed,
              ', retocat' if inset else ''))
    print('  %u frames  %dx%d  ->  %s%s' % (len(pngs), w, h, dst,
          '' if inset_count == 0 else f'  ({inset_count} retocats)'))
    return 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src = sys.argv[1]
    dst = sys.argv[2]

    # Si la carpeta no te PNG pero si subcarpetes (IDLE/, HAPPY/...), les
    # convertim totes: aixi n'hi ha prou amb "png2bin.py Assets Assets/bin".
    if not any(frame_index(f) >= 0 for f in os.listdir(src)):
        subs = [d for d in sorted(os.listdir(src))
                if os.path.isdir(os.path.join(src, d))]
        if not subs:
            print('  (ni PNG ni subcarpetes a %s)' % src)
            return 1
        rc = 0
        for st in subs:
            if not any(frame_index(f) >= 0
                       for f in os.listdir(os.path.join(src, st))):
                continue                 # p.ex. la propia carpeta de sortida
            print('==', st)
            rc |= convert_dir(os.path.join(src, st), os.path.join(dst, st))
        return rc
    return convert_dir(src, dst)


if __name__ == '__main__':
    sys.exit(main())
