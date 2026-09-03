#!/usr/bin/env python3
"""Compara byte a byte el volcado LOGB del firmware con el del simulador.

El simulador es lo unico contra lo que se prueba la app Android, asi que si
firmware y simulador divergen, la app pasa todas sus pruebas y luego falla
contra la placa. Esta comparacion es lo que ata los dos extremos.
"""
import subprocess, sys, struct, pathlib, importlib.util

HERE = pathlib.Path(__file__).resolve().parent
SIM = HERE.parent.parent / "android-app" / "tools" / "fake_glaciertemp.py"

spec = importlib.util.spec_from_file_location("sim", SIM)
sim = importlib.util.module_from_spec(spec); spec.loader.exec_module(sim)

REC, BLOCK = 12, sim.BLOCK

def synth(count):
    return bytes(((i*7 + 13) & 0xFF) for i in range(count*REC))

def expected(count, a, b):
    """Lo que emitiria el simulador, construido con SU propio crc16."""
    payload = synth(count)[a*REC:(b+1)*REC]
    n = (len(payload) + BLOCK - 1)//BLOCK
    head = (f"LOGB begin sig=0x100F rec={REC} from={a} to={b} "
            f"blocks={n} blocksize={BLOCK}\n").encode()
    body = b""
    for i in range(n):
        c = payload[i*BLOCK:(i+1)*BLOCK]
        body += b"\xAA\x55" + struct.pack("<HH", i, len(c)) + c + struct.pack("<H", sim.crc16(c))
    return head + body + b"LOGB end\n"

def run(count, a, b):
    out = HERE / "wire.bin"
    subprocess.run([str(HERE/"logb_host"), str(count), str(a), str(b), str(out)],
                   check=True, capture_output=True)
    d = out.read_bytes()
    return d.split(b"\n", 1)[1]           # descarta la cabecera INFO

CASES = [
    (100, 0,  99, "log completo, ultimo bloque parcial"),
    (100, 0,   0, "un solo registro"),
    (100, 50, 99, "rango, el caso habitual"),
    ( 64, 0,  63, "768 B: tres bloques exactos"),
    (  1, 0,   0, "un registro en todo el log"),
    (300, 0, 299, "3600 B, catorce bloques"),
    (100, 0, 999, "tope por encima del count: se recorta a count-1"),
]

bad = 0
for count, a, b, desc in CASES:
    got = run(count, a, b)
    exp = expected(count, a, min(b, count-1))
    ok = got == exp
    bad += not ok
    print(f"{'OK  ' if ok else 'FALLA'} {desc}")
    if not ok:
        for i,(x,y) in enumerate(zip(got, exp)):
            if x != y:
                print(f"      primer byte distinto en {i}: firmware {x:02X}, simulador {y:02X}")
                break
        else:
            print(f"      longitudes {len(got)} vs {len(exp)}")

# El log vacio no tiene equivalente en el simulador porque este siempre tiene datos.
got = run(0, 0, 0)
ok = got == b"LOGB empty\n" or b"LOGB empty" in got
bad += not ok
print(f"{'OK  ' if ok else 'FALLA'} log vacio -> LOGB empty")

print("todo en verde" if not bad else f"{bad} casos fallan")
sys.exit(1 if bad else 0)
