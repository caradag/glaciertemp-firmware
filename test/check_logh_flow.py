#!/usr/bin/env python3
"""Comprueba el control de flujo durante el volcado LOGH (Intel HEX).

displayHistoryHex no llamaba a flowControlCheck NI UNA VEZ, mientras que
dumpLogBinary lo hace entre bloques y cada 32 bytes. Era una asimetria sin razon:
el volcado mas largo que hace la placa --media hora larga, la memoria entera-- era
justo el unico que no se podia frenar, y es el mas expuesto a desbordar el buffer
de un puente BLE.

Lo que se mide es el SOBREPASO: cuantos bytes sigue emitiendo la placa entre que el
receptor pide la pausa y que la atiende. Sin comprobacion alguna el sobrepaso es
todo el volcado restante, asi que este comprobador falla ruidosamente si alguien
quita la llamada. Mirando los datos no se distingue.

Uso: ./check_logh_flow.py
"""
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
DEFS = HERE / "extracted_defs.h"

# Un registro Intel HEX de 16 bytes de datos ocupa 44 de linea:
#   ':' + 2 (longitud) + 4 (direccion) + 2 (tipo) + 32 (datos) + 2 (checksum) + '\n'
BYTES_POR_REGISTRO = 16
LINEA_POR_REGISTRO = 44


def intervalo_de_comprobacion():
    """Cada cuantos bytes de DATO comprueba el firmware, leido del propio codigo.

    La mascara esta escrita en el sketch como `(a & 0xFF)==0`; sacarla de ahi evita que
    este comprobador siga aprobando un intervalo que ya no es el que se usa.
    """
    src = (HERE.parent / "GlacierTemp_1_cell_v02_claude" / "EEPROM.ino").read_text(encoding="utf-8")
    m = re.search(r"if\(\(a & 0x([0-9A-Fa-f]+)\)==\d+\)\{\s*\n\s*flowControlCheck\(\);", src)
    if not m:
        sys.exit("no encuentro la comprobacion de flujo en displayHistoryHex")
    return int(m.group(1), 16) + 1


def run(count, xoff_at):
    subprocess.run([str(HERE / "logb_host"), str(count), "0", "0", str(HERE / "flow.bin"),
                    "0", "600", str(xoff_at)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    a, b, c, total = (int(x) for x in (HERE / "flow.txt").read_text().split())
    return a, b, c, total


def main():
    datos_por_comprobacion = intervalo_de_comprobacion()
    registros = datos_por_comprobacion // BYTES_POR_REGISTRO
    # Cota del sobrepaso: lo que cabe entre dos comprobaciones, mas un registro de
    # direccion extendida (tipo 04) que puede colarse en medio.
    cota = registros * LINEA_POR_REGISTRO + LINEA_POR_REGISTRO

    print(f"comprueba cada {datos_por_comprobacion} bytes de dato "
          f"({registros} registros, cota de sobrepaso {cota} bytes de linea)")

    bad = 0
    for count, xoff_at, desc in [
        (100, 500,  "pausa al principio del volcado"),
        (100, 3000, "pausa a mitad"),
        (500, 8000, "pausa en un volcado largo"),
        (100, 1,    "pausa en el primer byte"),
    ]:
        pedido, atendido, reanudado, total = run(count, xoff_at)
        errs = []
        if atendido < 0:
            errs.append("el firmware NUNCA leyo el XOFF: no hay control de flujo")
        else:
            sobrepaso = atendido - pedido
            if sobrepaso < 0:
                errs.append(f"sobrepaso negativo ({sobrepaso}), la medida no vale")
            elif sobrepaso > cota:
                errs.append(f"sobrepaso {sobrepaso} por encima de la cota {cota}")
            if reanudado != atendido:
                errs.append(f"emitio {reanudado - atendido} bytes durante la pausa")
        if errs:
            bad += 1
            print(f"FALLA {desc}: " + "; ".join(errs))
        else:
            print(f"OK   {desc}: sobrepaso {atendido - pedido} de {cota}, "
                  f"0 bytes durante la pausa")

    # El volcado tiene que terminar igual: frenar no puede perder datos.
    wire = (HERE / "flow.bin").read_bytes().decode("latin-1")
    if ":00000001FF" not in wire:
        print("FALLA: el volcado no llega al registro de fin de fichero")
        bad += 1
    else:
        print("OK   el volcado termina en :00000001FF pese a la pausa")

    print("todo en verde" if not bad else f"{bad} casos fallan")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
