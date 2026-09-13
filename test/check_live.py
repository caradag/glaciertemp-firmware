#!/usr/bin/env python3
"""Comprueba que LIVE ensena las MISMAS columnas que el log, en el mismo orden.

Por que importa tanto: la app no aprende un formato nuevo para LIVE. Toma los
nombres de las columnas de la firma del log --que ya conoce por INFO-- y los
aplica por posicion a los valores que llegan. Si las dos cadenas de campos del
firmware se desalinean, la app no da ningun error: ensena la humedad bajo la
etiqueta de temperatura, con su unidad y sus decimales, y parece un dato.

Son dos cadenas de #if que hay que cambiar a la vez, en dos funciones distintas
y a cuatrocientas lineas una de otra. Ese es exactamente el cambio que se olvida
a medias, y el compilador no puede verlo.

Que se comprueba:

  * printLiveSample y la fila de displayHistory emiten los mismos campos, bajo
    los mismos #if, en el mismo orden y con los mismos decimales;
  * printLogHeader lleva los mismos #if en el mismo orden, para que los nombres
    caigan sobre sus valores;
  * LIVE no escribe en la flash ni toca el contador: es para mirar, y guardar
    esas lecturas ensuciaria el registro con datos fuera de cadencia.

Uso: ./check_live.py
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent / "GlacierTemp_1_cell_v02_claude" / "EEPROM.ino"


def cuerpo(texto, firma):
    """El cuerpo de una funcion, contando llaves."""
    i = texto.index(firma)
    i = texto.index("{", i)
    n, j = 0, i
    while True:
        if texto[j] == "{":
            n += 1
        elif texto[j] == "}":
            n -= 1
            if n == 0:
                return texto[i:j]
        j += 1


def campos(texto):
    """(guarda, decimales) por cada logField, en orden de aparicion."""
    guardas = []
    out = []
    for linea in texto.split("\n"):
        t = linea.strip()
        if t.startswith("#if"):
            guardas.append(t[3:].strip().lstrip("def ").strip())
        elif t.startswith("#endif"):
            if guardas:
                guardas.pop()
        elif t.startswith("#else"):
            # Lo que sigue al #else NO es el campo: es el apano para cuando el
            # canal esta apagado. Se marca para que no cuente como columna.
            if guardas:
                guardas[-1] = None
        m = re.search(r"logField\(\s*[^,]+,\s*[^,]+,\s*(\d+)\s*,", t)
        if m and (not guardas or guardas[-1] is not None):
            out.append((guardas[-1] if guardas else "", int(m.group(1))))
    return out


def cabeceras(texto):
    guardas = []
    out = []
    for linea in texto.split("\n"):
        t = linea.strip()
        if t.startswith("#if"):
            guardas.append(t[3:].strip().lstrip("def ").strip())
        elif t.startswith("#endif"):
            if guardas:
                guardas.pop()
        if re.search(r"logHeader(Ram)?\(", t):
            out.append(guardas[-1] if guardas else "")
    return out


def main():
    s = SRC.read_text(encoding="utf-8")
    bad = []

    live = cuerpo(s, "void printLiveSample(")
    hist = cuerpo(s, "void displayHistory(byte mode)")
    head = cuerpo(s, "void printLogHeader(bool compact)")

    cl, ch = campos(live), campos(hist)
    if cl != ch:
        bad.append("LIVE y el log no emiten los mismos campos:\n"
                   f"       LIVE: {cl}\n"
                   f"       log : {ch}")
    else:
        print(f"OK   {len(cl)} campos, mismos #if y mismos decimales en LIVE y en el log")

    gh = cabeceras(head)
    gc = [g for g, _ in cl]
    if gh != gc:
        bad.append("los nombres de columna no siguen a los valores:\n"
                   f"       cabecera: {gh}\n"
                   f"       valores : {gc}")
    else:
        print(f"OK   {len(gh)} nombres de columna alineados con sus valores")

    vivo = cuerpo(s, "void liveData(unsigned long periodMs)") + live
    escrituras = [x for x in ("flashWrite", "addCount(", "EEPROM.put") if x in vivo]
    for prohibido in escrituras:
        bad.append(f"LIVE usa {prohibido}: tiene que mirar, no registrar")
    if not escrituras:
        print("OK   LIVE no escribe nada: ni flash, ni contador, ni EEPROM")

    # --- Que la linea QUEPA en el buffer de salida, con todos los canales encendidos ---
    #
    # Esto ya paso una vez con INFO: el buffer no trunca la linea larga, se la come entera
    # --y arrastra a las siguientes, porque una linea solo sale cuando llega su '\n'--.
    # Aqui el peligro es mayor porque la configuracion de canales de ESTA compilacion no es
    # la peor: una placa con las ocho sondas DS18B20 y los cuatro analogicos emite una linea
    # mucho mas larga, y no se descubriria hasta tenerla delante.
    defs = (HERE / "extracted_defs.h")
    m = re.search(r"^#define\s+OUT_BUFFER_SIZE\s+(\d+)", defs.read_text(encoding="utf-8"), re.M)
    if not m:
        bad.append("no encuentro OUT_BUFFER_SIZE; ejecuta build.sh")
    else:
        buffer = int(m.group(1))
        # Un campo es una coma mas el valor. El valor sale de un entero de 16 bits con
        # signo, asi que lo mas ancho es "-327.67": signo, cinco digitos y el punto.
        ANCHO_CAMPO = 1 + 1 + 5 + 1
        MAX_DS = 8                     # lo que cabe en los bits DS del LOG_SIGNATURE
        canales = len([g for g, _ in cl if not g.startswith("LOG_DS")]) + MAX_DS
        peor = len("LIVE ") + len("2026-09-13 10:22:31") + canales * ANCHO_CAMPO + 1
        if peor > buffer:
            bad.append(f"la linea LIVE mas larga son {peor} B y el buffer tiene {buffer}: "
                       f"con todos los canales encendidos la linea no saldria, y se "
                       f"llevaria por delante las siguientes")
        else:
            print(f"OK   la peor linea LIVE ({canales} canales) son {peor} B "
                  f"de {buffer}: sobran {buffer - peor}")

    print("todo en verde" if not bad else "\n".join("FALLA " + b for b in bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
