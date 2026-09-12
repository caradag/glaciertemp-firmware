#!/usr/bin/env python3
"""Comprueba que un volcado se puede CANCELAR desde el anfitrion.

Parar de leer no para de emitir. El anfitrion que abandona una descarga larga dejaba a
la placa volcando megabytes contra un enlace que ya no lee, y esa cola se colaba
despues como si fuera la respuesta de los comandos siguientes: uno escribia VER y
recibia bloques del volcado anterior durante minutos.

El byte 0x18 --CAN de ASCII, el convencional para "cancela"-- para el volcado. Se
distingue de XOFF a proposito: XOFF dice "espera, no doy abasto" y CAN dice "ya no lo
quiero". Reutilizar XOFF para las dos cosas obligaria a adivinar cual es por cuanto
tarda en llegar el XON.

Lo que se comprueba aqui:

  * el volcado se PARA, y antes del final;
  * se cierra con un texto DISTINTO del normal, porque el anfitrion necesita saber si
    lo que tiene esta completo o a medias;
  * el Intel HEX cancelado NO lleva registro de fin de fichero: ponerlo seria firmar
    como completo algo que no lo esta;
  * cancelar durante la PAUSA tambien vale, que es cuando el anfitrion se da cuenta de
    que no va a poder con el resto.

Uso: ./check_cancel.py
"""
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent


def run(count=2000, hex_bytes=40000, cancel_hex=-1, cancel_logb=-1, xoff=10**9):
    subprocess.run([str(HERE / "logb_host"), str(count), "0", str(count - 1),
                    str(HERE / "c.bin"), "0", "600", str(xoff), str(hex_bytes), "0",
                    str(cancel_hex), str(cancel_logb)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return (HERE / "c.bin").read_bytes().decode("latin-1")


def registros_eof(texto):
    """Registros de fin de fichero REALES, no la mencion del preambulo.

    El preambulo dice "Keep from the first ':' to :00000001FF", asi que buscar la cadena
    a secas siempre la encuentra. Solo cuenta una linea que sea exactamente eso.
    """
    return sum(1 for l in texto.split("\n") if l.strip().upper() == ":00000001FF")


def main():
    bad = 0

    # --- volcado binario -----------------------------------------------------
    completo = run()
    parcial = run(cancel_logb=3000)
    pos = [int(x) for x in (HERE / "cancel_logb.txt").read_text().split()]

    if "LOGB end" not in completo:
        print("FALLA: el volcado sin cancelar no se cierra con LOGB end"); bad += 1
    else:
        print("OK   sin cancelar se cierra con LOGB end")

    if "LOGB aborted" not in parcial:
        print("FALLA: el volcado cancelado no lo dice"); bad += 1
    elif "LOGB end" in parcial.split("LOGB aborted")[0]:
        print("FALLA: dice LOGB end y LOGB aborted a la vez"); bad += 1
    else:
        print(f"OK   el volcado cancelado se cierra con LOGB aborted "
              f"(pedido en {pos[0]}, atendido en {pos[1]})")

    if len(parcial) >= len(completo):
        print(f"FALLA: cancelado ({len(parcial)} B) no es mas corto que completo "
              f"({len(completo)} B): no paro"); bad += 1
    else:
        print(f"OK   paro antes del final: {len(parcial)} B frente a {len(completo)}")

    # --- volcado Intel HEX ---------------------------------------------------
    hex_completo = run()
    if registros_eof(hex_completo) != 1:
        print(f"FALLA: el volcado completo tiene {registros_eof(hex_completo)} registros "
              f"de fin de fichero, se esperaba 1"); bad += 1
    else:
        print("OK   el volcado completo lleva su registro de fin de fichero")

    hex_parcial = run(cancel_hex=30000)
    if "LOGH aborted" not in hex_parcial:
        print("FALLA: el volcado Intel HEX cancelado no lo dice"); bad += 1
    else:
        print("OK   el volcado Intel HEX cancelado se cierra con LOGH aborted")

    n = registros_eof(hex_parcial)
    if n != 0:
        print(f"FALLA: el volcado cancelado lleva {n} registros de fin de fichero; "
              f"firmaria como completo algo que no lo esta"); bad += 1
    else:
        print("OK   el volcado cancelado NO lleva registro de fin de fichero")

    # --- cancelar durante la pausa -------------------------------------------
    # XOFF primero y CAN despues, los dos pendientes: el banco entrega el CAN antes porque
    # si el receptor pide las dos cosas, lo que quiere es parar.
    durante_pausa = run(xoff=28000, cancel_hex=28000)
    if "LOGH aborted" not in durante_pausa:
        print("FALLA: cancelar con una pausa pedida a la vez no para el volcado"); bad += 1
    else:
        print("OK   cancelar mientras se pide la pausa tambien para el volcado")

    print("todo en verde" if not bad else f"{bad} casos fallan")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
