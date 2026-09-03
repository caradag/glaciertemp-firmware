#!/usr/bin/env python3
"""Extrae funciones del firmware VERBATIM para compilarlas en el PC.

Copiar el codigo al banco de pruebas lo condena a divergir del original en
cuanto alguien toque uno de los dos. Extraerlo en cada compilacion hace que el
banco pruebe siempre el codigo que de verdad se sube a la placa.
"""
import re, sys, pathlib

def grab(src, name):
    # Busca la definicion (no una llamada) y captura hasta la llave que cierra.
    m = re.search(r'^[A-Za-z_][A-Za-z0-9_ \*]*\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{',
                  src, re.M)
    if not m:
        sys.exit(f"no encontrada: {name}")
    i = src.index('{', m.start())
    depth = 0
    for j in range(i, len(src)):
        if src[j] == '{': depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                return src[m.start():j+1]
    sys.exit(f"llaves sin cerrar en {name}")

def avr_widths(code):
    """Reescribe los tipos a los anchos del AVR.

    En el ATmega328P int son 16 bits y long 32; en el PC son 32 y 64. Sin esto
    el CRC del banco no seria el CRC de la placa: crc16Ccitt hace (crc<<1) y
    confia en que el tipo TRUNQUE a 16 bits. Compilarlo con int de 32 bits da un
    resultado distinto y el banco aprobaria un firmware roto.
    """
    code = re.sub(r'\bunsigned\s+long\b', 'uint32_t', code)
    code = re.sub(r'\bunsigned\s+int\b', 'uint16_t', code)
    code = re.sub(r'\blong\b', 'int32_t', code)
    code = re.sub(r'\bint\b', 'int16_t', code)
    return code

root = pathlib.Path(sys.argv[1])
out = []
for spec in sys.argv[2:]:
    fname, names = spec.split(':')
    src = (root / fname).read_text(encoding='utf-8')
    for n in names.split(','):
        if n.startswith('#'):
            # Una constante copiada al banco es una constante que puede mentir:
            # se saca del original igual que el codigo.
            m = re.search(r'^#define\s+' + re.escape(n[1:]) + r'\b.*$', src, re.M)
            if not m:
                sys.exit(f"no encontrada la constante: {n[1:]}")
            out.append(m.group(0))
        else:
            out.append(f"// --- {fname}: {n} (extraido verbatim) ---")
            out.append(avr_widths(grab(src, n)))
print("\n".join(out))
