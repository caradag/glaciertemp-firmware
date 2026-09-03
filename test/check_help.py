#!/usr/bin/env python3
"""Comprueba que el texto de ayuda cabe en la EEPROM y se alinea bien.

El initializer avisa del desbordamiento en tiempo de ejecucion, cuando ya hay una
placa conectada. Aqui se ve al editar. Ademas reproduce el bucle de printHelp()
para poder LEER como quedara la ayuda sin quemar nada.
"""
import re, sys, pathlib

HERE = pathlib.Path(__file__).resolve().parent
INIT = HERE.parent / "GlacierTemp_1_cell_initializer" / "GlacierTemp_1_cell_initializer.ino"
MAIN = HERE.parent / "GlacierTemp_1_cell_v02_claude" / "GlacierTemp_1_cell_v02_claude.ino"

EEPROM_SIZE = 1024

addr = int(re.search(r'#define\s+HELP_TEXT\s+(\d+)', MAIN.read_text(encoding='utf-8')).group(1))

src = INIT.read_text(encoding='utf-8')
m = re.search(r'char\s+helpStr\[\]\s*=\s*((?:\s*"(?:[^"\\]|\\.)*")+)\s*;', src)
if not m:
    sys.exit("no se encontro helpStr en el initializer")
text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
text = text.encode().decode('unicode_escape')

size = len(text) + 1                      # sizeof incluye el terminador
avail = EEPROM_SIZE - addr

print(f"HELP_TEXT en {addr}, disponible {avail} B, ayuda {size} B, "
      f"libres {avail - size} B")

# Reproduce el bucle de printHelp(): rellena hasta 10 contando el '$', que no se
# imprime, de modo que las descripciones arrancan en la columna 9, y nunca deja
# menos de un espacio.
print("-" * 46)
wide = []
for line in text.split("\n"):
    if "$" in line:
        cmd, desc = line.split("$", 1)
        pad = 10 - (len(cmd) + 1)
        if pad < 1:
            wide.append(cmd)
            pad = 1
        print(cmd + " " * pad + desc)
    else:
        print(line)
print("-" * 46)

fail = 0
if size > avail:
    print(f"FALLA no cabe: sobran {size - avail} B"); fail = 1
else:
    print("OK   cabe en la EEPROM")
if wide:
    # No es un fallo: printHelp garantiza el espacio. Solo rompe la columna.
    print(f"aviso  rompen la alineacion por ser de 9 o mas caracteres: {', '.join(wide)}")
else:
    print("OK   toda descripcion arranca en la columna 9")
sys.exit(fail)
