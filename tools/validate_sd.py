#!/usr/bin/env python3
"""validate_sd.py - Comprova l'estructura i les mides de la targeta SD del
Tamagoxi v2 des del PC, ABANS de ficar-la a la placa.

Comprovacions:
  - /backgrounds/manifest.json  (width, height, files[])
  - /pets/<nom>/manifest.json   (width, height, fps, transparent, animations)
  - Cada .bin te exactament width*height*2 bytes (RGB565, sense capcalera).

Us:
    python3 tools/validate_sd.py /ruta/a/tamagochi_sd
Si no es passa cap ruta, prova ~/Downloads/tamagochi_sd.

Surt amb codi 0 si tot esta be, 1 si hi ha errors.
"""
import json
import os
import sys

BG_BYTES_DEFAULT = 320 * 480 * 2      # 307200
FRAME_BYTES_DEFAULT = 128 * 128 * 2   # 32768


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def check_backgrounds(root, errors, warn, report):
    man_path = os.path.join(root, "backgrounds", "manifest.json")
    if not os.path.isfile(man_path):
        errors.append(f"Falta {man_path}")
        return
    man = load_manifest(man_path)
    w, h = man.get("width"), man.get("height")
    expected = (w * h * 2) if (w and h) else BG_BYTES_DEFAULT
    files = man.get("files") or []
    report.append(f"[backgrounds] {len(files)} fons  {w}x{h}  ({expected} B)")
    for name in files:
        bin_path = os.path.join(root, "backgrounds", f"{name}.bin")
        if not os.path.isfile(bin_path):
            errors.append(f"Falta {bin_path}")
            continue
        size = os.path.getsize(bin_path)
        if size != expected:
            errors.append(f"{name}.bin = {size} B (esperat {expected})")
    report.append(f"[backgrounds] mides correctes: "
                  f"{sum(1 for n in files if os.path.isfile(os.path.join(root,'backgrounds',n+'.bin')) and os.path.getsize(os.path.join(root,'backgrounds',n+'.bin'))==expected)}/{len(files)}")


def check_pets(root, errors, warn, report):
    pets_dir = os.path.join(root, "pets")
    if not os.path.isdir(pets_dir):
        errors.append(f"Falta la carpeta {pets_dir}")
        return
    pets = sorted(d for d in os.listdir(pets_dir)
                  if os.path.isdir(os.path.join(pets_dir, d)))
    if not pets:
        errors.append(f"Cap mascota dins {pets_dir}")
        return
    for pet in pets:
        man_path = os.path.join(pets_dir, pet, "manifest.json")
        if not os.path.isfile(man_path):
            errors.append(f"Falta {man_path}")
            continue
        man = load_manifest(man_path)
        w, h = man.get("width"), man.get("height")
        fps = man.get("fps", "?")
        transparent = man.get("transparent", "?")
        expected = (w * h * 2) if (w and h) else FRAME_BYTES_DEFAULT
        anims = man.get("animations") or {}
        report.append(f"[pets/{pet}] {w}x{h}  fps={fps}  transparent={transparent}  "
                      f"{len(anims)} animacions  ({expected} B/frame)")
        total_ok = total = 0
        for anim, frames in anims.items():
            anim_dir = os.path.join(pets_dir, pet, anim)
            if not os.path.isdir(anim_dir):
                errors.append(f"Falta la carpeta {anim_dir}")
                continue
            for i in range(int(frames)):
                frame_path = os.path.join(anim_dir, f"{i:02d}.bin")
                total += 1
                if not os.path.isfile(frame_path):
                    errors.append(f"Falta {frame_path}")
                    continue
                size = os.path.getsize(frame_path)
                if size != expected:
                    errors.append(f"{frame_path} = {size} B (esperat {expected})")
                else:
                    total_ok += 1
        report.append(f"[pets/{pet}] frames correctes: {total_ok}/{total}")


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Downloads/tamagochi_sd")
    if not os.path.isdir(root):
        print(f"ERROR: no existeix la carpeta {root}", file=sys.stderr)
        return 1

    errors, warn, report = [], [], []
    print(f"Validant la targeta SD: {root}\n")
    check_backgrounds(root, errors, warn, report)
    check_pets(root, errors, warn, report)

    print("\n".join(report))
    for w in warn:
        print(f"AVIS: {w}")
    print()
    if errors:
        print(f"RESULTAT: {len(errors)} ERROR(S)")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("RESULTAT: OK - estructura i mides correctes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
