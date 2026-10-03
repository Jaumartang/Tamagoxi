#!/usr/bin/env python3
"""Envia una sequencia de comandes/tocs pel port serie del Tamagoxi.

Us:  python3 tools/seq.py fitxer.seq [--log]

Cada linia del fitxer:
    tap <x> <y>      injecta un toc
    cmd <comanda>    envia una comanda de consola
    wait <segons>    espera
    log              imprimeix el que s'hagi rebut fins ara
"""
import serial
import sys
import time

PORT = '/dev/cu.wchusbserial1410'


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    with open(path) as fh:
        lines = [ln.split('#')[0].strip() for ln in fh]
    lines = [ln for ln in lines if ln]

    ser = serial.Serial(PORT, 115200, timeout=0.2)
    time.sleep(1.2)
    ser.reset_input_buffer()

    for ln in lines:
        parts = ln.split()
        what = parts[0]
        if what == 'tap':
            ser.write(f'tap {parts[1]} {parts[2]}\n'.encode())
            print(f'>> tap {parts[1]},{parts[2]}')
            time.sleep(1.2)
        elif what == 'cmd':
            ser.write((' '.join(parts[1:]) + '\n').encode())
            print('>> ' + ' '.join(parts[1:]))
            time.sleep(0.8)
        elif what == 'wait':
            time.sleep(float(parts[1]))
        elif what == 'log':
            end = time.time() + 0.8
            buf = b''
            while time.time() < end:
                data = ser.read(8192)
                if data:
                    buf += data
            for out in buf.decode('utf-8', 'replace').splitlines():
                if out.startswith('['):
                    print('   ' + out)

    time.sleep(0.5)
    end = time.time() + 1.2
    buf = b''
    while time.time() < end:
        data = ser.read(8192)
        if data:
            buf += data
    ser.close()
    for out in buf.decode('utf-8', 'replace').splitlines():
        if out.startswith('['):
            print('   ' + out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
