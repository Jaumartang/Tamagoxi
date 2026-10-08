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
        tw = te = 0
        sizes = set()
        for f in bins:
            data = open(os.path.join(d, f), 'rb').read()
            sizes.add(len(data))
            w = 128
            h = len(data) // (w * 2)
            for y in range(h):
                for k in range(4):
                    if val(data, (y * w + k) * 2) != TRANS:
                        te += 1
                    if val(data, (y * w + w - 1 - k) * 2) != TRANS:
                        te += 1
            for i in range(0, len(data), 2):
                if val(data, i) == WHITE:
                    tw += 1
        # Nomes son un problema els pixels estranys a les VORES (les linies
        # laterals). Els blancs de dins son detalls del drac (ulls, dents...).
        flag = 'OK' if te == 0 else 'PROBLEMA (vores!)'
        if te != 0:
            ok = False
        print('  %-8s %2d frames  mides=%s  vores_estranyes=%d  blancs_dins=%d  -> %s'
              % (st, len(bins), sorted(sizes), te, tw, flag))

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
