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


# Volcado acotado para las pruebas de flujo: lo que se mide es el sobrepaso en bytes, y
# para eso no hacen falta los veintitres megas del volcado completo.
HEX_BYTES = 20000


def run(count, xoff_at, hex_bytes=HEX_BYTES, fast=0):
    subprocess.run([str(HERE / "logb_host"), str(count), "0", "0", str(HERE / "flow.bin"),
                    "0", "600", str(xoff_at), str(hex_bytes), str(fast)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    lineas = (HERE / "flow.txt").read_text().strip().split("\n")
    a, b, c, total = (int(x) for x in lineas[0].split())
    # Cada linea siguiente es un cambio de velocidad: posicion en la linea y baudios.
    cambios = [tuple(int(x) for x in l.split()) for l in lineas[1:] if l.strip()]
    return a, b, c, total, cambios


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
        pedido, atendido, reanudado, total, _ = run(count, xoff_at)
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

    # --- el volcado SIN argumento tiene que ser la memoria entera -------------
    #
    # LOGH es la via de recuperacion para cuando el firmware no puede leer su propio log:
    # con el contador corrompido o la firma cambiada, nSamples es justamente el dato del que
    # no hay que fiarse. Volcar "los registros grabados" dejaria fuera lo que se rescata.
    run(100, 10**9, hex_bytes=0)          # xoff imposible de alcanzar: no frena
    texto = (HERE / "flow.bin").read_bytes().decode("latin-1")
    m = re.search(r"bytes:\s*(\d+)\s*of\s*(\d+)", texto)
    if not m:
        print("FALLA: la cabecera de LOGH no dice cuantos bytes vuelca")
        bad += 1
    elif m.group(1) != m.group(2):
        print(f"FALLA: vuelca {m.group(1)} de {m.group(2)} bytes; deberia volcar la memoria entera")
        bad += 1
    else:
        print(f"OK   sin argumento vuelca la memoria entera: {m.group(1)} bytes")

    # Y con un contador de CERO registros tambien: es el caso que mas importa, porque un
    # contador a cero es sintoma tipico de la averia que justifica este comando.
    run(0, 10**9, hex_bytes=0)
    texto = (HERE / "flow.bin").read_bytes().decode("latin-1")
    m = re.search(r"bytes:\s*(\d+)\s*of\s*(\d+)", texto)
    if not m or m.group(1) != m.group(2) or m.group(1) == "0":
        print("FALLA: con el contador a cero deja de volcar; es justo cuando hace falta")
        bad += 1
    else:
        print(f"OK   con el contador a cero sigue volcando los {m.group(1)} bytes")

    # --- volcado rapido: donde cae exactamente el cambio de velocidad ---------
    #
    # Un cambio un registro antes o despues no se ve mirando los datos: el receptor leeria
    # basura justo en el borde. Se comprueba la POSICION, como hace check_baud.py con LOGB.
    _, _, _, total, cambios = run(100, 10**9, hex_bytes=20000, fast=230400)
    texto = (HERE / "flow.bin").read_bytes().decode("latin-1")
    # rfind y no find: el PREAMBULO menciona el registro de fin de fichero en su texto
    # ("Keep from the first ':' to :00000001FF"), asi que buscar hacia adelante encuentra la
    # mencion y no el registro. Lo mismo con el primer registro de datos, que hay que buscar
    # despues de esa linea y no desde el principio.
    eof = texto.rfind(":00000001FF")
    tras_preambulo = texto.find("Intel HEX follows")
    fin_preambulo = texto.find("\n", tras_preambulo) if tras_preambulo >= 0 else 0

    if len(cambios) != 2:
        print(f"FALLA: se esperaban dos cambios de velocidad y hubo {len(cambios)}: {cambios}")
        bad += 1
    else:
        (pos_sube, baud_sube), (pos_baja, baud_baja) = cambios
        primer_registro = texto.find(":", fin_preambulo)
        errs = []
        if baud_sube != 230400:
            errs.append(f"sube a {baud_sube} en vez de a 230400")
        if baud_baja == 230400:
            errs.append("no vuelve a la velocidad normal")
        # La subida tiene que caer ANTES del primer registro Intel HEX y DESPUES de la
        # cabecera de texto: si cayera antes, la cabecera saldria ilegible.
        if not (0 < pos_sube <= primer_registro):
            errs.append(f"sube en {pos_sube}, fuera de la cabecera ({primer_registro})")
        # Y la bajada, DESPUES del fin de fichero: ese registro es la senal de vuelta.
        if not (eof < pos_baja <= eof + len(":00000001FF") + 2):
            errs.append(f"baja en {pos_baja}, y el fin de fichero esta en {eof}")
        if errs:
            print("FALLA volcado rapido: " + "; ".join(errs))
            bad += 1
        else:
            print(f"OK   sube a {baud_sube} tras la cabecera ({pos_sube}) y vuelve a "
                  f"{baud_baja} justo tras el fin de fichero ({pos_baja})")

        # La cabecera tiene que anunciar la velocidad, o el receptor no sabria cambiar.
        if "fast:" not in texto:
            print("FALLA: la cabecera no anuncia la velocidad rapida")
            bad += 1
        else:
            print("OK   la cabecera anuncia la velocidad con fast:")

    # Sin pedirlo, no se toca la velocidad.
    _, _, _, _, cambios = run(100, 10**9, hex_bytes=20000, fast=0)
    if cambios:
        print(f"FALLA: sin pedirlo cambia la velocidad: {cambios}")
        bad += 1
    else:
        print("OK   sin pedirlo no se toca la velocidad")

    # Un volcado vacio tampoco puede dejar la linea en la velocidad rapida.
    _, _, _, _, cambios = run(100, 10**9, hex_bytes=0, fast=230400)
    if cambios and cambios[-1][1] == 230400:
        print("FALLA: la linea se queda en la velocidad rapida")
        bad += 1
    else:
        print("OK   la linea nunca se queda en la velocidad rapida")

    print("todo en verde" if not bad else f"{bad} casos fallan")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
