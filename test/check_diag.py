#!/usr/bin/env python3
"""Comprueba el firmware de diagnostico contra el logger, sin placa.

Dos cosas:

1. LAS CONSTANTES COMPARTIDAS. El diagnostico no puede incluir las pestanas del logger
   (un sketch no ve las de otro), asi que copia los pines, las direcciones I2C y el mapa
   de la EEPROM. Si el logger cambia uno y el diagnostico no, el diagnostico prueba otra
   placa y lo dice con toda seguridad. Aqui se comparan los dos fuentes, valor a valor.

2. LAS FUNCIONES PURAS, extraidas VERBATIM del fuente y compiladas en el PC con los
   anchos de tipo del AVR (int de 16 bits): el lector de argumentos de la consola, la
   mascara de pines ("123" -> A1|A2|A3), los dias desde 2000 (contra el calendario de
   Python), el tamano de registro que implica una firma de log (4 bytes de hora mas 2
   por canal, segun la definicion de los bits del logger) y la alineacion de numeros.

Uso: ./check_diag.py
"""
import datetime
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
DIAG = ROOT / "GlacierTemp_diagnostics"
LOGGER = ROOT / "GlacierTemp_1_cell_v02_claude" / "GlacierTemp_1_cell_v02_claude.ino"

# Mismo nombre en los dos fuentes, mismo valor.
COMPARTIDAS = [
    "WAKEUP_PIN", "ONE_WIRE_PIN", "MEM_POWER", "GREEN_LED", "FLASH_MEMORY_CS",
    "BLUETOOTH_SATUS_PIN", "LED_PIN", "BATT_VOLTAGE_PIN", "HDC1080_ADDR", "TMP119_ADDR",
    "CLOCK_ADDRESS", "TMP119_TEMP_REG", "TMP119_CONFIG_REG", "TMP119_SHUTDOWN",
    "BATT_SAMPLES", "ADC_REF_SETTLE_MS", "SECTOR_SIZE", "MAX_SECTORS", "BAUDRATE",
    "COUNT_ADDR", "COUNTERS_SLOTS", "COUNT_RESET_ADDR", "RESET_TIME_ADDR",
    "REFERENCE_VOLTAGE_1", "REFERENCE_VOLTAGE_COUNT_1", "REFERENCE_VOLTAGE_2",
    "REFERENCE_VOLTAGE_COUNT_2", "ANALOG_CAL_ADDR", "LOG_SIGNATURE_ADDR",
    "PIN_POWER_A0", "PIN_POWER_A1", "PIN_POWER_A2", "PIN_POWER_A3",
]


def defines(texto):
    out = {}
    for m in re.finditer(r"^#define\s+([A-Z0-9_]+)\s+([^\s/]+)", texto, re.M):
        out.setdefault(m.group(1), m.group(2))
    return out


def comparar(bad):
    log = defines(LOGGER.read_text(encoding="utf-8", errors="replace"))
    dia = defines((DIAG / "GlacierTemp_diagnostics.ino").read_text(encoding="utf-8", errors="replace"))
    # Las direcciones de INT y TZN no son #define en el logger: vienen de varAddr[].
    varaddr = re.search(r"int varAddr\[\]=\{(\d+),\d+,(\d+)", LOGGER.read_text(errors="replace"))
    for nombre in COMPARTIDAS:
        if nombre not in log:
            bad.append(f"{nombre}: no esta en el logger")
        elif nombre not in dia:
            bad.append(f"{nombre}: no esta en el diagnostico")
        elif log[nombre] != dia[nombre]:
            bad.append(f"{nombre}: logger {log[nombre]} != diagnostico {dia[nombre]}")
    if varaddr:
        if dia.get("INTERVAL_EE_ADDR") != varaddr.group(1):
            bad.append(f"INTERVAL_EE_ADDR {dia.get('INTERVAL_EE_ADDR')} != varAddr INT {varaddr.group(1)}")
        if dia.get("TIMEZONE_EE_ADDR") != varaddr.group(2):
            bad.append(f"TIMEZONE_EE_ADDR {dia.get('TIMEZONE_EE_ADDR')} != varAddr TZN {varaddr.group(2)}")
    else:
        bad.append("no se encuentra varAddr[] en el logger")


SHIM = r"""
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>
typedef uint8_t byte;
std::string g_out;
struct S {
  void print(char c){ g_out+=c; }
  void print(const char* s){ g_out+=s; }
  void print(double v, int d){ char b[64]; snprintf(b,sizeof b,"%.*f",d,v); g_out+=b; }
} Serial;
"""

MAIN = r"""
int fallos=0;
#define CHECK(c, msg) do{ if(!(c)){ printf("FALLO: %s\n", msg); fallos++; } }while(0)
int main(){
  CHECK(argNum("SETTLE", 1, 7)==7, "sin argumento: el valor por defecto");
  CHECK(argNum("SETTLE 2", 1, 7)==2, "primer argumento");
  CHECK(argNum("TMPAVG  45", 1, 30)==45, "espacios dobles");
  CHECK(argNum("STEP 0 123", 2, 9)==123, "segundo argumento");
  CHECK(argNum("SLEEP -5", 1, 30)==-5, "negativo");
  CHECK(argNum("SLEEP X", 1, 30)==30, "no numerico: el valor por defecto");
  CHECK(argMask("SETTLE 0 123", 2, 0)==0x0E, "123 -> A1|A2|A3");
  CHECK(argMask("SETTLE 0 1", 2, 0)==0x02, "1 -> A1");
  CHECK(argMask("SETTLE 0", 2, 0x0E)==0x0E, "sin mascara: la de por defecto");
  CHECK(argMask("SETTLE 0 9", 2, 0x0E)==0x0E, "digito fuera de 0..3: la de por defecto");
  CHECK(is("TMP 5","TMP") && !is("TMPAVG","TMP") && is("TMPAVG 3","TMPAVG"), "is() distingue TMP de TMPAVG");
  CHECK(is("RTC","RTC") && !is("RTCSQW","RTC"), "is() distingue RTC de RTCSQW");
  @@DIAS@@
  @@RECORD@@
  g_out=""; pad(3.14159f, 8, 2); CHECK(g_out=="    3.14", ("pad positivo: '"+g_out+"'").c_str());
  g_out=""; pad(-12.5f, 7, 1);   CHECK(g_out=="  -12.5", ("pad negativo: '"+g_out+"'").c_str());
  g_out=""; pad(1234.0f, 4, 0);  CHECK(g_out=="1234", ("pad justo: '"+g_out+"'").c_str());
  g_out=""; pad(0.0f, 5, 0);     CHECK(g_out=="    0", ("pad cero: '"+g_out+"'").c_str());
  printf("%d fallo(s)\n", fallos);
  return fallos ? 1 : 0;
}
"""


def funciones(bad):
    extractor = HERE / "extract.py"
    r = subprocess.run([sys.executable, str(extractor), str(DIAG),
                        "GlacierTemp_diagnostics.ino:argNum,argMask,is",
                        "Rtc.ino:daysSince2000",
                        "Board.ino:recordBytes",
                        "Sensors.ino:pad"],
                       capture_output=True, text=True)
    if r.returncode:
        bad.append("extract.py: " + (r.stderr or r.stdout).strip())
        return
    # Dias desde 2000 contra el calendario de Python, en fechas que cruzan bisiestos.
    casos = [(2000, 1, 1), (2000, 2, 29), (2000, 3, 1), (2001, 1, 1), (2004, 3, 1),
             (2024, 11, 26), (2026, 10, 6), (2099, 12, 31)]
    base = datetime.date(2000, 1, 1)
    dias = "\n  ".join(
        f'CHECK(daysSince2000({y},{m},{d})=={(datetime.date(y, m, d) - base).days}L, "{y}-{m}-{d}");'
        for y, m, d in casos)
    # Tamano de registro: 4 de la hora + 2 por canal, para varias firmas.
    firmas = {0x101F: 4 + 2 * 5, 0x1027: 4 + 2 * 4, 0x100F: 4 + 2 * 4, 0x200F: 6 + 2 * 4,
              0x1000 | 0x1F | (2 << 9) | 0x20: 4 + 2 * (4 + 3 + 1)}
    rec = "\n  ".join(f'CHECK(recordBytes(0x{s:04X})=={n}, "firma 0x{s:04X}");' for s, n in firmas.items())
    cpp = SHIM + r.stdout + MAIN.replace("@@DIAS@@", dias).replace("@@RECORD@@", rec)
    with tempfile.TemporaryDirectory() as d:
        src = pathlib.Path(d) / "diag_host.cpp"
        exe = pathlib.Path(d) / "diag_host"
        src.write_text(cpp)
        c = subprocess.run(["g++", "-std=c++14", "-w", "-o", str(exe), str(src)],
                           capture_output=True, text=True)
        if c.returncode:
            bad.append("no compila en el PC:\n" + c.stderr[:1500])
            return
        e = subprocess.run([str(exe)], capture_output=True, text=True)
        if e.returncode:
            bad.append("funciones puras:\n" + e.stdout.strip())


def main():
    bad = []
    comparar(bad)
    funciones(bad)
    for b in bad:
        print("ERROR:", b)
    print("check_diag:", "OK" if not bad else f"{len(bad)} problema(s)")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
