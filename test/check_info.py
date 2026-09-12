#!/usr/bin/env python3
"""Comprueba la linea de arranque y la cabecera INFO en la salida del banco.

Cubre tres cosas que ninguna revision a ojo detecta:

1. El identificador corto tiene el formato "GT001-XXXXXX" y sus seis digitos son
   los seis ultimos del identificador completo.
2. El identificador COMPLETO de 64 bits sigue apareciendo en la linea de arranque.
   Es la que se lee con la placa delante cuando algo va mal, y es la unica forma de
   desempatar si dos placas compartieran los seis digitos cortos.
3. Ninguna linea se acerca al limite del buffer de salida. Este es el fallo
   silencioso de verdad: la linea solo se emite cuando el stream recibe el '\\n', asi
   que si el buffer se llena antes, ese '\\n' no cabe, la linea no se vacia nunca y
   arrastra a TODAS las siguientes. Al alargar el identificador corto, INFO se paso
   por un byte y se llevo por delante tambien la cabecera de LOGB.

Uso: ./check_info.py [wire.bin]
"""
import re
import subprocess
import sys
import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def buffer_size():
    """El tamano real del buffer, leido del sketch. Copiarlo aqui seria repetir el
    error que este comprobador existe para impedir."""
    src = (HERE / "extracted_defs.h").read_text(encoding="utf-8")
    m = re.search(r"^#define\s+OUT_BUFFER_SIZE\s+(\d+)", src, re.M)
    if not m:
        sys.exit("no encuentro OUT_BUFFER_SIZE en extracted_defs.h; ejecuta build.sh")
    return int(m.group(1))


def main():
    # Escenario propio y fichero propio: los demas comprobadores dejan wire.bin con el
    # ultimo caso que probaron --uno de ellos con el log vacio, que no emite cabecera de
    # LOGB-- y leer ese resto daba un fallo que no existia.
    wire = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "info.bin"
    subprocess.run([str(HERE / "logb_host"), "100", "0", "9", str(wire)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    text = wire.read_bytes().decode("latin-1")
    fails = []

    # --- 1 y 2: la linea de arranque ---------------------------------------
    m = re.search(r"Board ID: (\S+)\s+\(full ([0-9A-F]+)\)", text)
    if not m:
        fails.append("no aparece la linea 'Board ID: ... (full ...)' del arranque")
    else:
        short, full = m.group(1), m.group(2)
        if len(full) != 16:
            fails.append(f"el identificador completo tiene {len(full)} digitos, no 16")
        if not re.fullmatch(r"[A-Z]{2}\d{3}-[0-9A-F]{6}", short):
            fails.append(f"el identificador corto '{short}' no tiene la forma GT001-XXXXXX")
        elif short[-6:] != full[-6:]:
            fails.append(f"los seis digitos de '{short}' no son los ultimos de '{full}'")

    # --- INFO ---------------------------------------------------------------
    i = text.find("INFO ")
    if i < 0:
        fails.append("la cabecera INFO no aparece (posible desbordamiento del buffer)")
    else:
        info = text[i:text.index("\n", i)]
        sid = re.search(r"sid=(\S+)", info)
        proto = re.search(r"proto=(\d+)", info)
        if not sid:
            fails.append("INFO no lleva campo sid=")
        elif m and sid.group(1) != m.group(1):
            fails.append(f"INFO sid={sid.group(1)} no coincide con el arranque {m.group(1)}")
        if not proto:
            fails.append("INFO no lleva campo proto=")
        elif int(proto.group(1)) < 3:
            fails.append(f"INFO anuncia proto={proto.group(1)}, se esperaba 3 o mas")

    # --- el comando ID da la misma linea que el arranque --------------------
    #
    # Antes imprimia solo los 16 digitos del completo. Quien teclea ID en el terminal
    # quiere el corto --el que nombra la placa-- sin perder el completo, y la linea del
    # arranque ya tiene esa forma: no hay razon para que sean dos formatos distintos.
    lineas_id = [l for l in text.split("\n") if l.startswith("Board ID:")]
    if len(lineas_id) < 2:
        fails.append(f"el comando ID no imprime la linea completa: {len(lineas_id)} "
                     f"lineas 'Board ID:' (arranque + comando, se esperaban 2)")
    elif lineas_id[0] != lineas_id[1]:
        fails.append(f"ID y el arranque dan formatos distintos:\n"
                     f"      arranque: {lineas_id[0]}\n"
                     f"      comando:  {lineas_id[1]}")

    if "LOGB begin" not in text:
        fails.append("la cabecera de LOGB no aparece (posible desbordamiento del buffer)")

    # --- 3: margen del buffer en TODAS las lineas de texto ------------------
    size = buffer_size()
    limit = size - 2          # addChar exige _pos+1 < _size, y el '\n' cuenta
    worst = ("", 0)
    for line in text.split("\n"):
        # Solo lineas de texto: los bloques binarios de LOGB no pasan por el buffer.
        if not line or any(ord(c) < 9 or ord(c) > 126 for c in line):
            continue
        if len(line) > worst[1]:
            worst = (line, len(line))
    if worst[1] > limit:
        fails.append(f"linea de {worst[1]} caracteres, por encima del limite {limit}: "
                     f"{worst[0][:60]}...")

    print(f"buffer de salida: {size} bytes (limite util {limit})")
    print(f"linea de texto mas larga: {worst[1]} caracteres  "
          f"-- margen {limit - worst[1]}")
    if worst[1] > limit * 0.9:
        print(f"AVISO: la linea mas larga usa el {100*worst[1]/limit:.0f} % del buffer")

    if fails:
        for f in fails:
            print("FALLA:", f)
        return 1
    print(f"OK: arranque, INFO, comando ID y margen de buffer correctos")
    return 0


if __name__ == "__main__":
    sys.exit(main())
