#!/usr/bin/env python3
"""Comprueba el aviso de duracion de memoria (printMemoryLifetime).

Lo que se verifica no es el formato sino la ARITMETICA, porque es donde esta el
peligro: registros libres por intervalo son hasta 6,04e10 segundos, catorce veces
el techo del unsigned long de 32 bits del AVR. El firmware lo esquiva partiendo el
intervalo en minutos y resto; aqui se compara el resultado contra el calculo exacto
hecho en Python, que no tiene ese limite.

Los casos extremos son los que importan: memoria vacia con el intervalo maximo
--el que desborda-- y con el minimo --el que da el numero mas pequeno.

Uso: ./check_memlife.py
"""
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
DEFS = HERE / "extracted_defs.h"

# Tolerancia: el firmware divide dos veces por separado en vez de una, asi que puede
# quedarse un dia corto respecto al exacto. Mas de eso seria un error de verdad.
TOLERANCIA_DIAS = 1


def const(name):
    m = re.search(r"^#define\s+" + name + r"\s+(\d+)", DEFS.read_text(encoding="utf-8"), re.M)
    if not m:
        sys.exit(f"no encuentro {name} en extracted_defs.h; ejecuta build.sh")
    return int(m.group(1))


def run(count, interval):
    out = HERE / "mem.bin"
    subprocess.run([str(HERE / "logb_host"), str(count), "0", "0", str(out), "0", str(interval)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out.read_bytes().decode("latin-1")


def main():
    sector, sectors = const("SECTOR_SIZE"), const("MAX_SECTORS")
    rec = 12                       # BYTES_PER_SAMPLE del banco
    capacidad = sector * (sectors + 1) // rec

    casos = [
        (0, 1,      "intervalo minimo, memoria vacia"),
        (0, 600,    "el intervalo de uso normal"),
        (0, 3600,   "una hora"),
        (0, 67,     "un intervalo que no divide al minuto ni al dia"),
        (0, 86400,  "intervalo maximo: el caso que desborda 32 bits"),
        (capacidad // 2, 600, "memoria a medias"),
        (capacidad, 600,      "memoria llena"),
    ]

    bad = 0
    for count, interval, desc in casos:
        text = run(count, interval)
        m = re.search(r"Memory:\s*(\d+) free records,\s*(\d+) days at\s*(\d+) s", text)
        if not m:
            print(f"FALLA {desc}: no aparece la linea Memory:")
            bad += 1
            continue
        libres, dias, inter = int(m.group(1)), int(m.group(2)), int(m.group(3))

        libres_exacto = max(0, capacidad - count)
        dias_exacto = libres_exacto * interval // 86400

        errs = []
        if libres != libres_exacto:
            errs.append(f"libres {libres} != {libres_exacto}")
        if inter != interval:
            errs.append(f"intervalo {inter} != {interval}")
        if abs(dias - dias_exacto) > TOLERANCIA_DIAS:
            errs.append(f"dias {dias} != {dias_exacto} (exacto)")

        # Memoria llena: tiene que decirlo y no ofrecer fecha.
        if libres_exacto == 0:
            if "Memory is FULL" not in text:
                errs.append("no avisa de que la memoria esta llena")
            if "Full on:" in text:
                errs.append("ofrece fecha de llenado con la memoria ya llena")
        # Horizonte largo: fecha omitida en vez de un tiempo unix desbordado.
        elif dias_exacto > 18250:
            if "more than 50 years" not in text:
                errs.append("no recorta el horizonte a 50 anos")
            if "Full on:" in text:
                errs.append("imprime una fecha que desbordaria el tiempo unix")
        elif "Full on:" not in text:
            errs.append("falta la fecha de llenado")

        if errs:
            bad += 1
            print(f"FALLA {desc}: " + "; ".join(errs))
        else:
            print(f"OK   {desc}: {libres} libres, {dias} dias "
                  f"(exacto {dias_exacto})")

    print("todo en verde" if not bad else f"{bad} casos fallan")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
