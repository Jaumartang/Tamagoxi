import serial, sys, time

port = '/dev/cu.wchusbserial1410'
cmds = sys.argv[1:] if len(sys.argv) > 1 else ['petpause']

s = serial.Serial(port, 115200, timeout=0.2)
time.sleep(0.2)
# Reiniciem la placa (si esta en mode enllac, la consola va a 921600 i no ens
# entendriem). El pols RTS es el mateix que faig servir per llegir el boot.
s.setDTR(False)
s.setRTS(True)
time.sleep(0.15)
s.setRTS(False)

# La placa engega l'enllac tota sola als 4 s: si li escrivim alguna cosa
# durant aquella estona, es queda en mode consola.
buf = ''
t0 = time.time()
while time.time() - t0 < 14:
    s.write(b'x\n')
    s.flush()
    time.sleep(0.7)
    d = s.read(4096)
    if d:
        buf += d.decode('utf-8', 'replace')

# Ara les comandes de debo (primer un salt net, per si ha quedat un tros
# de linia dels caracters de prova).
s.write(b'\n')
s.flush()
time.sleep(0.4)
for c in cmds:
    s.write((c + '\n').encode())
    s.flush()
    time.sleep(3.0)
    d = s.read(8192)
    if d:
        buf += d.decode('utf-8', 'replace')
s.close()

for line in buf.splitlines():
    if any(k in line for k in ('LINK', 'WEB', 'mascota', 'UI', 'SD', 'meteo',
                               'unknown', 'no entenc', 'desconeguda', 'Desconeg')):
        print(line)
