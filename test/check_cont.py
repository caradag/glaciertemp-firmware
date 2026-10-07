#!/usr/bin/env python3
"""Comprueba la captura continua (CONT) sin placa.

1. EL REGISTRO v2. packRecord() y las tres lineas de contCapture() que le anteponen
   segundos y milisegundos, extraidas VERBATIM y compiladas con los anchos del AVR,
   arman un registro con valores conocidos. Se decodifica con layout() de
   decode_logh.py --el mismo contrato que sigue la app-- y tiene que devolver lo que
   se puso: segundos, milisegundos y cada canal en su sitio.
2. EL CONTADOR. CONT no suma en las ranuras: mueve la referencia del ultimo reset
   (COUNT_RESET = total - n). Con getCount()/getFullCount() extraidos y la linea de
   contCapture() que graba la referencia, contra una EEPROM simulada: tras RC con un
   historial de 2005 registros, una captura de 8500 --mas que el historial, el caso
   que depende de la aritmetica sin signo-- da getCount()==8500, las ranuras no
   cambian, y despues de otro RC y un addCount() normal vuelve a contar desde 1.
3. LOGB E INFO SOBRE UN LOG CONT. El banco de logb_host con el log marcado como CONT
   tiene que anunciar la firma de version 2 y el tamano de registro con los 2 bytes de
   milisegundos, que es de lo que la app saca la disposicion.

Uso: ./check_cont.py   (despues de build.sh)
"""
import importlib.util
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True   # no dejar __pycache__ junto al sketch
HERE = pathlib.Path(__file__).resolve().parent
SKETCH = HERE.parent / "GlacierTemp_1_cell_v02_claude"
MAIN = "GlacierTemp_1_cell_v02_claude.ino"

DEFINES = ["LOG_VOLTAGE", "LOG_HDC_TEMP", "LOG_HDC_RH", "LOG_TMP119", "LOG_DS18B20",
           "LOG_A0", "LOG_A1", "LOG_A2", "LOG_A3", "OFF_TIME", "OFF_VOLTAGE",
           "OFF_HDC_TEMP", "OFF_HDC_RH", "OFF_TMP119", "OFF_DS18B20", "OFF_A0", "OFF_A1",
           "OFF_A2", "OFF_A3", "BYTES_PER_SAMPLE", "CONT_MS_BYTES",
           "BYTES_PER_SAMPLE_CONT", "COUNT_ADDR", "COUNTERS_SLOTS", "COUNT_RESET_ADDR"]

SHIM = r"""
#include <cstdio>
#include <cstdint>
#include <cstring>
typedef uint8_t byte;
#define F(x) x
struct Out { Out& operator<<(const char* s){ printf("OUT:%s", s); return *this; } } out;
static uint8_t eeprom[1024];
struct EE {
  template<class T> const T& put(int a, const T& v){ memcpy(eeprom+a, &v, sizeof(T)); return v; }
} EEPROM;
uint32_t getULong(int a){ uint32_t v; memcpy(&v, eeprom+a, 4); return v; }
int16_t currentTemp=-1234, currentRH=873, currentTempHA=-567;
int16_t currentTempDS[8]={2101,2202,2303,2404,2505,2606,2707,2808};
int16_t currentAnalog[4]={1500,1600,1700,1800};
uint32_t currentTime=0;
"""

MAIN_TEST = r"""
uint32_t slot(int i){ return getULong(COUNT_ADDR+4*i); }
uint32_t suma(){ uint32_t s=0; for(int i=0;i<COUNTERS_SLOTS;i++) s+=slot(i); return s; }
int main(){
  // --- registro v2 ---
  byte buf[BYTES_PER_SAMPLE_CONT];
  memset(buf, 0xEE, sizeof buf);
  int16_t battMv=1503;
  uint16_t ms=45;
  currentTime=844634700UL;
  @@PACK@@
  printf("REC");
  for(unsigned i=0;i<sizeof buf;i++) printf(" %02X", buf[i]);
  printf("\n");
  // --- contador ---
  memset(eeprom, 0, sizeof eeprom);
  EEPROM.put(COUNT_ADDR, (uint32_t)2005);                // historial
  EEPROM.put(COUNT_RESET_ADDR, getFullCount());          // RC
  printf("C0 %u\n", getCount());
  {
    uint32_t total=getFullCount();
    uint32_t n=8500;
    @@COMMIT@@
  }
  printf("C1 %u %u\n", getCount(), suma());
  EEPROM.put(COUNT_RESET_ADDR, getFullCount());          // RC
  addCount();
  printf("C2 %u %u\n", getCount(), suma());
  return 0;
}
"""


def extraer(bad):
    r = subprocess.run([sys.executable, str(HERE / "extract.py"), str(SKETCH),
                        MAIN + ":" + ",".join("#" + d for d in DEFINES),
                        "EEPROM.ino:packRecord,getFullCount,getCount,addCount"],
                       capture_output=True, text=True)
    if r.returncode:
        bad.append("extract.py: " + (r.stderr or r.stdout).strip())
        return None
    cont = (SKETCH / "Continuous.ino").read_text(encoding="utf-8")
    m = re.search(r"^\s*packRecord\(buf\+CONT_MS_BYTES.*?memcpy\(buf\+4, &ms, 2\);", cont,
                  re.M | re.S)
    if not m:
        bad.append("Continuous.ino: no se encuentra el armado del registro (packRecord + memcpy)")
        return None
    c = re.search(r"^\s*EEPROM\.put\(COUNT_RESET_ADDR, total-n\);", cont, re.M)
    if not c:
        bad.append("Continuous.ino: no se encuentra la linea que graba COUNT_RESET")
        return None
    return r.stdout, m.group(0), c.group(0)


def decoder():
    spec = importlib.util.spec_from_file_location("decode_logh", SKETCH / "decode_logh.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def flags():
    src = (SKETCH / MAIN).read_text(encoding="utf-8")
    val = {}
    for d in ("LOG_VOLTAGE", "LOG_HDC_TEMP", "LOG_HDC_RH", "LOG_TMP119", "LOG_DS18B20",
              "LOG_A0", "LOG_A1", "LOG_A2", "LOG_A3"):
        val[d] = int(re.search(r"^#define\s+" + d + r"\s+(\d+)", src, re.M).group(1))
    return val


def registro(bad, salida):
    f = flags()
    sig = 0x2000
    for d, b in (("LOG_VOLTAGE", 1), ("LOG_HDC_TEMP", 2), ("LOG_HDC_RH", 4), ("LOG_TMP119", 8),
                 ("LOG_A0", 0x20), ("LOG_A1", 0x40), ("LOG_A2", 0x80), ("LOG_A3", 0x100)):
        if f[d]:
            sig |= b
    if f["LOG_DS18B20"]:
        sig |= 0x10 | ((f["LOG_DS18B20"] - 1) << 9)
    m = re.search(r"^REC((?: [0-9A-F]{2})+)$", salida, re.M)
    if not m:
        bad.append("el banco no imprimio el registro")
        return
    rec = bytes(int(x, 16) for x in m.group(1).split())
    fields, size, ts = decoder().layout(sig)
    if (size, ts) != (len(rec), 6):
        bad.append(f"layout(0x{sig:04X}) da {size} B con marca de {ts}; el firmware arma {len(rec)} B")
        return
    if int.from_bytes(rec[0:4], "little") != 844634700:
        bad.append("los segundos no estan en los bytes 0..3")
    if int.from_bytes(rec[4:6], "little") != 45:
        bad.append("los milisegundos no estan en los bytes 4..5")
    esperado = {"Volt": 1503, "Temp": -1234, "RH": 873, "HAtemp": -567,
                "A0": 1500, "A1": 1600, "A2": 1700, "A3": 1800}
    for i in range(8):
        esperado[f"DS{i}"] = 2101 + 101 * i
    for i, (name, _, _) in enumerate(fields):
        v = int.from_bytes(rec[ts + 2 * i:ts + 2 + 2 * i], "little", signed=True)
        if v != esperado[name]:
            bad.append(f"campo {name}: el decodificador lee {v}, se grabo {esperado[name]}")
    if 0xEE in rec[len(rec) - 1:]:
        bad.append("el registro no se lleno hasta el final")


def contador(bad, salida):
    c0 = re.search(r"^C0 (\d+)$", salida, re.M)
    c1 = re.search(r"^C1 (\d+) (\d+)$", salida, re.M)
    c2 = re.search(r"^C2 (\d+) (\d+)$", salida, re.M)
    if not (c0 and c1 and c2):
        bad.append("el banco no imprimio las pruebas del contador")
        return
    if c0.group(1) != "0":
        bad.append(f"tras RC getCount()={c0.group(1)}")
    if c1.groups() != ("8500", "2005"):
        bad.append(f"tras CONT de 8500: getCount()={c1.group(1)}, ranuras suman {c1.group(2)} "
                   f"(esperado 8500 y 2005 sin tocar)")
    if c2.groups() != ("1", "2006"):
        bad.append(f"RC y un registro normal: getCount()={c2.group(1)}, suma {c2.group(2)} "
                   f"(esperado 1 y 2006)")


def logb(bad):
    with tempfile.TemporaryDirectory() as d:
        wire = pathlib.Path(d) / "w.bin"
        env = dict(os.environ, LOGB_CONT="1")
        subprocess.run([str(HERE / "logb_host"), "3", "0", "2", str(wire)],
                       capture_output=True, env=env, cwd=HERE)
        txt = wire.read_bytes().decode("latin-1")
    info = re.search(r"INFO .*?sig=0x([0-9A-F]{4}) rec=(\d+)", txt)
    head = re.search(r"LOGB begin sig=0x([0-9A-F]{4}) rec=(\d+)", txt)
    if not info or not head:
        bad.append("logb_host no emitio INFO o la cabecera de LOGB")
        return
    for nombre, m in (("INFO", info), ("LOGB", head)):
        if m.group(1) != "200F" or m.group(2) != "14":
            bad.append(f"{nombre} sobre un log CONT: sig=0x{m.group(1)} rec={m.group(2)} "
                       f"(esperado 0x200F y 14)")


def main():
    bad = []
    ex = extraer(bad)
    if ex:
        codigo, pack, commit = ex
        cpp = SHIM + codigo + MAIN_TEST.replace("@@PACK@@", pack).replace("@@COMMIT@@", commit)
        with tempfile.TemporaryDirectory() as d:
            src = pathlib.Path(d) / "cont.cpp"
            exe = pathlib.Path(d) / "cont"
            src.write_text(cpp)
            c = subprocess.run(["g++", "-std=c++14", "-w", "-o", str(exe), str(src)],
                               capture_output=True, text=True)
            if c.returncode:
                bad.append("no compila en el PC:\n" + c.stderr[:1500])
            else:
                salida = subprocess.run([str(exe)], capture_output=True, text=True).stdout
                registro(bad, salida)
                contador(bad, salida)
    logb(bad)
    for b in bad:
        print("ERROR:", b)
    print("check_cont:", "OK" if not bad else f"{len(bad)} problema(s)")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
