#!/usr/bin/env python3
"""Comprueba la calibracion con el par completo: "A01=2>258" y "V1=1.234>512".

La app ajusta una recta a muchas lecturas y manda a la placa dos puntos de ella, con su
cuenta. Si la placa guardara la cuenta que lee EN ESE MOMENTO en vez de la que viene en el
comando --que es lo que hacia el firmware anterior, porque readScaled() se para en el '>'--
la calibracion quedaria mal sin ningun aviso. Por eso se prueba:

  1. setRefVoltage() extraida VERBATIM del fuente y compilada en el PC, con una EEPROM
     y una salida falsas: que guarda el par dado, que rechaza una cuenta fuera de 0..1023
     sin tocar nada, y que la forma corta sigue igual.
  2. Sobre el fuente, que los dos sitios que la llaman NO leen el ADC cuando hay '>'.
     Esa decision vive en el despachador de loop(), que no se puede extraer.

Se valida a si mismo rompiendo el fuente (--selftest).
"""
import re, subprocess, sys, tempfile, pathlib

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent / "GlacierTemp_1_cell_v02_claude"



def grab(src, name):
    m = re.search(r'^[A-Za-z_][A-Za-z0-9_ \*]*\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{',
                  src, re.M)
    if not m:
        sys.exit(f"no encontrada: {name}")
    i = src.index('{', m.start())
    depth = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                return src[m.start():j + 1]
    sys.exit(f"llaves sin cerrar en {name}")


HARNESS = r'''
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <map>
typedef uint8_t byte;
// Anchos del AVR: int de 16 bits. Lo que se guarda en la EEPROM es un int de la placa.
static std::map<int,int16_t> eeprom;
struct FakeEEPROM { void put(int a, int16_t v){ eeprom[a]=v; } } EEPROM;
int16_t getInt(int a){ return eeprom.count(a) ? eeprom[a] : -1; }
struct Out {
  std::string s;
  Out& operator<<(const char* t){ s+=t; return *this; }
  Out& operator<<(char c){ s+=c; return *this; }
  Out& operator<<(int v){ s+=std::to_string(v); return *this; }
  Out& operator<<(long v){ s+=std::to_string(v); return *this; }
} out;
#define F(x) x
#define NL '\n'
%(code)s

static int fallos=0;
static void caso(const char* cmd, int mem, byte eq, int leida,
                 int esperaV, int esperaC, const char* esperaOut){
  eeprom.clear(); eeprom[mem]=111; eeprom[mem+2]=222; out.s.clear();
  char buf[64]; strcpy(buf, cmd);
  setRefVoltage(buf, mem, eq, leida);
  bool ok = getInt(mem)==esperaV && getInt(mem+2)==esperaC && out.s==esperaOut;
  if(!ok){
    printf("FALLO %%-16s guardo %%d/%%d, dijo <%%s>; esperaba %%d/%%d <%%s>\n", cmd,
           getInt(mem), getInt(mem+2), out.s.c_str(), esperaV, esperaC, esperaOut);
    fallos++;
  }
}
int main(){
  // El par completo: la cuenta del comando, no la leida (999).
  caso("A01=2>258",     10, 3, 999, 2000, 258, "2000mV->258\n");
  caso("V1=1.234>512",  10, 2, 999, 1234, 512, "1234mV->512\n");
  caso("A02=-0.5>7",    10, 3, 999, -500, 7,   "-500mV->7\n");
  caso("A01=2.5 > 300", 10, 3, 999, 2500, 300, "2500mV->300\n");
  caso("A01=0>0",       10, 3, 999, 0,    0,   "0mV->0\n");
  caso("A01=1>1023",    10, 3, 999, 1000, 1023,"1000mV->1023\n");
  // La forma corta, como siempre: la cuenta leida ahora.
  caso("A01=2",         10, 3, 345, 2000, 345, "2000mV->345\n");
  caso("V2=3.3",        10, 2, 600, 3300, 600, "3300mV->600\n");
  // Solo consultar: no cambia nada.
  caso("A01",           10, 3, 999, 111,  222, "111mV->222\n");
  // Cuentas imposibles: se rechazan SIN tocar lo guardado.
  caso("A01=2>1024",    10, 3, 999, 111,  222, "Bad count\n");
  caso("A01=2>-3",      10, 3, 999, 111,  222, "Bad count\n");
  caso("A01=2>99999999999", 10, 3, 999, 111, 222, "Bad count\n");
  char a[]="A01=2>5", b[]="A01=2";
  if(!refCountGiven(a) || refCountGiven(b)){ printf("FALLO refCountGiven\n"); fallos++; }
  return fallos ? 1 : 0;
}
'''


def run(power_src, functions_src, main_src):
    code = "\n".join([
        "bool refCountGiven(const char *inputStr);",
        grab(functions_src, "readScaled"),
        grab(power_src, "refCountGiven"),
        grab(power_src, "setRefVoltage"),
    ])
    problemas = []
    with tempfile.TemporaryDirectory() as d:
        cpp = pathlib.Path(d) / "calpair.cpp"
        exe = pathlib.Path(d) / "calpair"
        cpp.write_text(HARNESS % {"code": code})
        r = subprocess.run(["g++", "-std=c++14", "-o", str(exe), str(cpp)],
                           capture_output=True, text=True)
        if r.returncode:
            return [f"no compila:\n{r.stderr}"]
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        if r.returncode:
            problemas.append(r.stdout.strip())

    # Los que llaman no deben leer el ADC con '>': encender un sensor conmutado y esperar
    # 250 ms para tirar la cuenta no rompe la calibracion, pero es justo lo que no se pidio.
    if not re.search(r'setRefVoltage\(inputStr,\s*REFERENCE_VOLTAGE_1[^;]*'
                     r'refCountGiven\(inputStr\)\s*\?\s*0\s*:\s*getRawBatteryVoltage\(\)',
                     main_src):
        problemas.append("V1=/V2= lee la bateria aunque la cuenta venga en el comando")
    if not re.search(r'setRefVoltage\(inputStr,\s*ANALOG_CAL\(pin\)[^;]*'
                     r'refCountGiven\(inputStr\)\s*\?\s*0\s*:\s*getRawAnalogPin\(pin\)',
                     power_src):
        problemas.append("A01=/A02= lee el pin aunque la cuenta venga en el comando")
    return problemas


def main():
    power = (SRC / "Power.ino").read_text(encoding="utf-8")
    funcs = (SRC / "Functions.ino").read_text(encoding="utf-8")
    main_src = (SRC / "GlacierTemp_1_cell_v02_claude.ino").read_text(encoding="utf-8")

    if "--selftest" in sys.argv:
        roturas = {
            "guarda la cuenta leida": power.replace("rawCount=(int)given;", ""),
            "acepta 1024": power.replace("given>1023", "given>1024"),
            "V lee el ADC": None,
            "A lee el pin": power.replace("refCountGiven(inputStr) ? 0 : getRawAnalogPin(pin)",
                                          "getRawAnalogPin(pin)"),
        }
        malos = 0
        for nombre, p in roturas.items():
            m = main_src
            if p is None:
                p = power
                m = main_src.replace("refCountGiven(inputStr) ? 0 : getRawBatteryVoltage()",
                                     "getRawBatteryVoltage()")
            if not run(p, funcs, m):
                print(f"AUTOPRUEBA FALLIDA: no detecta '{nombre}'")
                malos += 1
        print("autoprueba OK" if not malos else f"autoprueba: {malos} sin detectar")
        sys.exit(1 if malos else 0)

    problemas = run(power, funcs, main_src)
    if problemas:
        print("\n".join(problemas))
        sys.exit(1)
    print("check_calpair: OK")


if __name__ == "__main__":
    main()
