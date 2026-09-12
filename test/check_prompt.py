#!/usr/bin/env python3
"""Comprueba que los VOLCADOS no piden el prompt "Waiting commands".

Por que esto no es un test del banco: la decision vive en el despachador de comandos,
dentro del loop() principal, que no es una funcion extraible como dumpLogBinary o
displayHistoryHex. Lo que se puede comprobar sin montar medio firmware es el fuente, y
resulta que es justo donde esta el riesgo: la linea `hiddenCommand=true;` es una sola,
no tiene efecto visible en nada que el banco mida, y desaparece sin que nadie lo note.

Que se comprueba:

  * el prompt sigue estando guardado por `if (!hiddenCommand)` -- si alguien quita esa
    guarda, las dos lineas de abajo dejan de servir para nada;
  * las ramas de LOGB y de LOGH lo ponen;
  * y no lo pone ninguna otra salvo ER, que es la que ya lo usaba para descartar los
    errores que escupe el HM-10 antes de conectar. Ponerlo de mas es peor que de menos:
    dejaria la consola sin prompt despues de un comando escrito a mano.

Uso: ./check_prompt.py
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SKETCH = HERE.parent / "GlacierTemp_1_cell_v02_claude" / "GlacierTemp_1_cell_v02_claude.ino"

# Ramas que SI deben silenciar el prompt, y por que.
SILENCIOSAS = {
    "logb": "el volcado se cierra con LOGB end o LOGB aborted",
    "logh": "el volcado se cierra con el registro de fin de fichero",
    "ER":   "son errores del modulo Bluetooth, no un comando del usuario",
}


def ramas(texto):
    """Parte el despachador en (nombre de la rama, cuerpo) por cada `else if`."""
    # El nombre es el literal que compara strcasecmp/strncasecmp en la condicion.
    partes = re.split(r"\}else if\(", texto)
    out = []
    for p in partes[1:]:
        m = re.match(r"!strn?casecmp\(\"([^\"]+)\"", p)
        if not m:
            continue
        out.append((m.group(1), p))
    return out


def main():
    src = SKETCH.read_text(encoding="utf-8", errors="replace")
    bad = []

    if not re.search(r"if\s*\(\s*!hiddenCommand\s*\)\s*\{\s*(//[^\n]*\n\s*)*"
                     r"(//[^\n]*\n\s*)*[^}]*printWaitingCommands", src):
        bad.append("el prompt ya no esta guardado por if(!hiddenCommand): "
                   "silenciarlo desde una rama no tiene ningun efecto")

    vistas = {}
    for nombre, cuerpo in ramas(src):
        vistas[nombre] = "hiddenCommand=true" in cuerpo.replace(" ", "")

    for nombre, motivo in SILENCIOSAS.items():
        if nombre not in vistas:
            bad.append(f"no encuentro la rama {nombre} en el despachador")
        elif not vistas[nombre]:
            bad.append(f"{nombre} vuelve a pedir el prompt ({motivo})")
        else:
            print(f"OK   {nombre} no pide el prompt: {motivo}")

    for nombre, silencia in sorted(vistas.items()):
        if silencia and nombre not in SILENCIOSAS:
            bad.append(f"{nombre} silencia el prompt y no deberia: un comando escrito a "
                       f"mano deja la consola sin saber si sigue escuchando")

    print("todo en verde" if not bad else "\n".join("FALLA " + b for b in bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
