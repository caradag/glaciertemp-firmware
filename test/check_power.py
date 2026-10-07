#!/usr/bin/env python3
"""Comprueba que todo camino que lee A0..A3 enciende y apaga la alimentacion conmutada.

Por que sobre el fuente y no en el banco: los caminos que leen el header son cinco
--la medicion, LIVE, el informe I, el comando A0 y la calibracion A01/A02-- y dos de
ellos (el despachador y el bucle de LIVE) no son funciones extraibles. Olvidar el
encendido en uno no da ningun error: ese comando lee un sensor APAGADO y devuelve un
valor plausible, unos 0 V. Y olvidar el apagado deja el sensor gastando milivatios
toda la noche, que es justo lo que PIN_POWER existe para evitar.

Que se comprueba:

  * la lectura cruda de un pin del header, getRawAnalog(analogPinNumber(...)), solo
    aparece en getRawAnalogPin() y getAnalogMv(), y getAnalogMv() solo se llama desde
    readAnalogChannels(): no hay otro camino al ADC del header;
  * getRawAnalogPin() y readAnalogChannels() esperan la estabilizacion ANTES de leer
    y apagan DESPUES;
  * takeMeasurement() enciende antes de leer los sensores I2C, que es lo que permite
    solapar la espera con trabajo que se hace igual;
  * LIVE retiene la alimentacion al empezar y la suelta antes de sus mensajes de cierre,
    sin ningun return entre medias que se salte la liberacion;
  * goToSleep() apaga siempre, pase lo que pase despierto.

Se valida a si mismo: con --self-test rompe el fuente de varias formas en memoria y
exige que cada rotura se detecte.

Uso: ./check_power.py [--self-test]
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SKETCH = HERE.parent / "GlacierTemp_1_cell_v02_claude"


def cargar():
    return {p.name: p.read_text(encoding="utf-8", errors="replace")
            for p in sorted(SKETCH.glob("*.ino"))}


def cuerpo(texto, firma):
    """El cuerpo de la funcion cuya cabecera contiene `firma`, por conteo de llaves."""
    i = texto.find(firma)
    if i < 0:
        return None
    j = texto.index("{", i)
    nivel = 0
    for k in range(j, len(texto)):
        if texto[k] == "{":
            nivel += 1
        elif texto[k] == "}":
            nivel -= 1
            if nivel == 0:
                return texto[j:k + 1]
    return None


def orden(texto, *marcas):
    """True si las marcas aparecen en ese orden dentro de `texto`."""
    pos = -1
    for m in marcas:
        p = texto.find(m, pos + 1)
        if p < 0:
            return False
        pos = p
    return True


def revisar(fuentes):
    bad = []
    todo = "\n".join(fuentes.values())
    power = fuentes.get("Power.ino", "")
    eeprom = fuentes.get("EEPROM.ino", "")

    # 1. Un solo camino al ADC del header.
    lectura = "getRawAnalog(analogPinNumber("
    permitidas = ("int getRawAnalogPin(", "int getAnalogMv(")
    for nombre, texto in fuentes.items():
        for m in re.finditer(re.escape(lectura), texto):
            dentro = [f for f in permitidas
                      if (c := cuerpo(texto, f)) and texto.find(c) <= m.start() < texto.find(c) + len(c)]
            if not dentro:
                linea = texto.count("\n", 0, m.start()) + 1
                bad.append(f"{nombre}:{linea}: lectura del header fuera de getRawAnalogPin/getAnalogMv")
    rac = cuerpo(power, "void readAnalogChannels(")
    for nombre, texto in fuentes.items():
        for m in re.finditer(r"getAnalogMv\(", texto):
            if texto[max(0, m.start() - 4):m.start()] == "int ":
                continue                          # la definicion
            if not (rac and texto is power and power.find(rac) <= m.start() < power.find(rac) + len(rac)):
                linea = texto.count("\n", 0, m.start()) + 1
                bad.append(f"{nombre}:{linea}: getAnalogMv() llamado fuera de readAnalogChannels()")

    # 2. Las dos funciones de lectura: encender/esperar antes, apagar despues.
    for firma, lee in (("int getRawAnalogPin(", lectura), ("void readAnalogChannels(", "getAnalogMv(")):
        c = cuerpo(power, firma)
        if c is None:
            bad.append(f"no se encuentra {firma}")
            continue
        if not orden(c, "analogPowerSettle()", lee, "analogPowerOff()"):
            bad.append(f"{firma.strip('(')}: falta esperar la estabilizacion antes de leer "
                       f"o apagar despues")

    # 3. La medicion enciende al principio.
    # Los sensores se leen en readSensors(), que empieza por los I2C.
    tm = cuerpo(eeprom, "bool takeMeasurement(")
    rs = cuerpo(eeprom, "int readSensors(")
    if tm is None or not orden(tm, "analogPowerOn()", "readSensors()"):
        bad.append("takeMeasurement(): analogPowerOn() tiene que ir antes de readSensors()")
    if rs is None or not orden(rs, "getTempAndRH()", "getBatteryVoltage()", "readAnalogChannels()"):
        bad.append("readSensors(): I2C, luego bateria (referencia INTERNA), luego analogicos")

    # 3b. CONT retiene y suelta, y apaga el calentador, despues del bucle.
    cont = fuentes.get("Continuous.ino", "")
    cc = cuerpo(cont, "void contCapture(")
    if cc is None or not orden(cc, "analogPowerHoldOn()", "while(true)", "hdcHeater(false)",
                                "analogPowerRelease()", "CONT end"):
        bad.append("contCapture(): retener antes del bucle; calentador y alimentacion fuera despues")

    # 4. LIVE retiene y suelta, sin return entre medias.
    i = eeprom.find("analogPowerHoldOn()")
    j = eeprom.find("analogPowerRelease()")
    k = eeprom.find('F("LIVE end\\n")')
    if i < 0 or j < 0 or k < 0 or not (i < j < k):
        bad.append("LIVE: analogPowerHoldOn() al empezar y analogPowerRelease() antes del cierre")
    elif re.search(r"\breturn\b", eeprom[i:j]):
        bad.append("LIVE: hay un return entre la retencion y la liberacion de la alimentacion")

    # 5. Dormir siempre apaga.
    gs = cuerpo(power, "void goToSleep(")
    if gs is None or not orden(gs, "analogPowerOff()", "LowPower.powerDown"):
        bad.append("goToSleep(): tiene que apagar la alimentacion antes de dormir")

    if "analogPowerOn" not in todo:
        bad.append("no hay alimentacion conmutada en el fuente")
    return bad


def self_test():
    """Cada rotura tiene que detectarse; si alguna pasa, el comprobador no sirve."""
    base = cargar()
    roturas = {
        "sin espera en getRawAnalogPin": ("Power.ino", "  analogPowerSettle();   // the reference", "  //x"),
        "sin apagar en readAnalogChannels": ("Power.ino", "  analogPinsEnd();\n#if PIN_POWER_MASK\n  analogPowerOff();\n#endif\n}",
                                             "  analogPinsEnd();\n}"),
        "medicion sin encender al principio": ("EEPROM.ino", "  analogPowerOn();\n#endif\n  memSendControlByte(POWER_UP);",
                                               "#endif\n  memSendControlByte(POWER_UP);"),
        "LIVE sin soltar": ("EEPROM.ino", "  analogPowerRelease();", "  //x"),
        "dormir sin apagar": ("Power.ino", "  analogPowerHold=0;\n  analogPowerOff();", "  analogPowerHold=0;"),
        "lectura cruda en otro sitio": ("Display.ino", "void ", "int pirata(){ return getRawAnalog(analogPinNumber(0)); }\nvoid "),
    }
    fallos = []
    if revisar(base):
        fallos.append("el fuente sin romper ya da errores")
    for nombre, (fichero, viejo, nuevo) in roturas.items():
        f = dict(base)
        if viejo not in f[fichero]:
            fallos.append(f"rotura '{nombre}': no se encontro el texto a romper")
            continue
        f[fichero] = f[fichero].replace(viejo, nuevo, 1)
        if not revisar(f):
            fallos.append(f"rotura '{nombre}' NO detectada")
    for x in fallos:
        print("FALLO:", x)
    print("self-test:", "OK" if not fallos else f"{len(fallos)} fallo(s)")
    return 0 if not fallos else 1


def main():
    if "--self-test" in sys.argv:
        return self_test()
    bad = revisar(cargar())
    for b in bad:
        print("ERROR:", b)
    print("check_power:", "OK" if not bad else f"{len(bad)} problema(s)")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
