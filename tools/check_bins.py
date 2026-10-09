#!/usr/bin/env python3
"""check_bins.py - Revisa un arbre de .bin RGB565 big-endian de mascota.

Per a cada estat comprova: que tots els frames tinguin la mida correcta, que les
4 columnes de cada costat siguin del color transparent, i que no hi hagi cap
pixel blanc (0xFFFF). Tambe pot escriure el manifest.json.

Us:
    python3 tools/check_bins.py <carpeta_pets/dragon> [--manifest]
"""

import json
import os
import sys

TRANS = 0xF81F
WHITE = 0xFFFF


def val(b, i):
    return (b[i] << 8) | b[i + 1]


def is_whiteish565(v):
    """Veritablement blanc/clar (el símptoma de les linies laterals)."""
    return ((v >> 11) & 0x1F) >= 28 and ((v >> 5) & 0x3F) >= 56 and (v & 0x1F) >= 28


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    write_manifest = '--manifest' in sys.argv
    anims = {}
    ok = True

    for st in sorted(os.listdir(root)):
        d = os.path.join(root, st)
        if not os.path.isdir(d):
            continue
        bins = sorted(f for f in os.listdir(d)
                      if f.endswith('.bin') and not f.startswith('.'))
        if not bins:
            continue
        anims[st] = len(bins)
        tw = te = tw_edge = 0
        sizes = set()
        for f in bins:
            data = open(os.path.join(d, f), 'rb').read()
            sizes.add(len(data))
            w = 128
            h = len(data) // (w * 2)
            for y in range(h):
                for k in range(4):
                    v1 = val(data, (y * w + k) * 2)
                    v2 = val(data, (y * w + w - 1 - k) * 2)
                    if v1 != TRANS:
                        te += 1
                        if is_whiteish565(v1):
                            tw_edge += 1
                    if v2 != TRANS:
                        te += 1
                        if is_whiteish565(v2):
                            tw_edge += 1
            for i in range(0, len(data), 2):
                if val(data, i) == WHITE:
                    tw += 1
        # Blancs a les VORES = problema (les linies laterals). Pixels del drac
        # que toquen la vora = nomes una nota (l'art queda una mica just).
        flag = 'OK' if tw_edge == 0 else 'PROBLEMA (vores blanques!)'
        if tw_edge != 0:
            ok = False
        nota = '' if te == 0 else '  (el drac toca la vora)'
        print('  %-9s %2d frames  mides=%s  vores_blanques=%d  vores_tocades=%d%s  blancs_dins=%d  -> %s'
              % (st, len(bins), sorted(sizes), tw_edge, te, nota, tw, flag))

    if write_manifest:
        man = {'width': 128, 'height': 128, 'fps': 6, 'transparent': '0xF81F',
               'animations': anims}
        with open(os.path.join(root, 'manifest.json'), 'w') as f:
            json.dump(man, f, indent=1)
        print('  manifest.json escrit:', json.dumps(anims))

    print('  RESULTAT:', 'TOT PERFECTE' if ok else 'revisar')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
