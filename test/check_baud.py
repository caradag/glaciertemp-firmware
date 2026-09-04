#!/usr/bin/env python3
"""Comprueba que el cambio de velocidad del volcado rapido cae donde debe.

Lo que hay que verificar no son los bytes --eso ya lo cubre check_wire.py-- sino QUE bytes
viajaron a QUE velocidad. Un cambio un byte antes de tiempo deja el ultimo caracter de la
cabecera ilegible; uno tarde, el primer byte del primer bloque. Ninguno de los dos se ve
mirando el volcado.
"""
import subprocess, sys, pathlib

HERE = pathlib.Path(__file__).resolve().parent
NORMAL, FAST = 115200, 230400


def run(count, a, b, fast):
    subprocess.run([str(HERE / "logb_host"), str(count), str(a), str(b),
                    str(HERE / "wire.bin"), str(fast)], check=True, capture_output=True)
    wire = (HERE / "wire.bin").read_bytes()
    changes = [tuple(map(int, l.split()))
               for l in (HERE / "baud.txt").read_text().split("\n") if l.strip()]
    return wire, changes


def check(name, cond, detail=""):
    print(f"{'OK  ' if cond else 'FALLA'} {name}" + (f"   {detail}" if not cond else ""))
    return 0 if cond else 1


bad = 0

# --- volcado rapido ---------------------------------------------------------------
wire, changes = run(100, 0, 99, FAST)
first_block = wire.index(b"\xAA\x55")
end_marker = wire.rindex(b"LOGB end")

bad += check("cambia a la velocidad rapida y vuelve, una vez cada cosa",
             [c[1] for c in changes] == [FAST, NORMAL], str(changes))
bad += check("la subida ocurre justo donde empieza el primer bloque",
             changes[0][0] == first_block, f"cambio en {changes[0][0]}, bloque en {first_block}")
bad += check("la bajada ocurre justo antes de anunciar el cierre",
             changes[1][0] == end_marker, f"cambio en {changes[1][0]}, cierre en {end_marker}")
bad += check("la cabecera declara la velocidad que va a usar",
             b" fast=230400" in wire[:first_block])
# Todo el texto tiene que quedar fuera del tramo rapido, o se leeria como basura.
bad += check("la cabecera entera viaja a la velocidad normal",
             changes[0][0] >= first_block)
bad += check("el cierre entero viaja a la velocidad normal",
             changes[1][0] <= end_marker)

# --- sin velocidad rapida: nada debe cambiar --------------------------------------
wire, changes = run(100, 0, 99, 0)
bad += check("sin pedirlo no se toca la velocidad", changes == [], str(changes))
bad += check("y la cabecera no anuncia ninguna", b"fast=" not in wire.split(b"\n")[1])

# --- un log vacio sale antes de tocar la velocidad --------------------------------
wire, changes = run(0, 0, 0, FAST)
bad += check("un log vacio no deja la linea en la velocidad rapida",
             changes == [] or changes[-1][1] == NORMAL, str(changes))

print("todo en verde" if not bad else f"{bad} comprobaciones fallan")
sys.exit(1 if bad else 0)
