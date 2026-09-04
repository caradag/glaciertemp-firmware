// Banco de pruebas en el PC del volcado LOGB y de la cabecera INFO.
//
// Ejecuta el codigo REAL del firmware (extraido en cada compilacion por
// extract.py) contra una memoria flash sintetica, y escribe en un fichero los
// bytes exactos que saldrian por la linea serie. Eso permite comprobar contra
// el simulador y contra el lector de la app que los tres hablan el mismo
// idioma, sin placa y sin telefono.
#include "Arduino.h"

std::vector<unsigned char> g_wire;
std::vector<std::pair<size_t,unsigned long>> g_baudChanges;
FakeSerial Serial;
FakeSPI SPI;
unsigned long millis(){ return 0; }
void delay(unsigned long){}
void digitalWrite(int,int){}

char* ultoa(unsigned long v, char* s, int base){ sprintf(s, "%lu", v); return s; }

long int10Pow(byte power);
#include "lightOStream.h"
long intPow(int power,int base){ long o=1; for(int i=0;i<power;i++) o*=base; return o; }
long int10Pow(byte power){ return intPow(power,10); }

// El mismo tamano que el firmware real: el stream descarta en silencio lo que
// no cabe antes del salto de linea, asi que un banco con un buffer mas grande
// no veria ese fallo.
static char outBuf[127];
lightOStream out(outBuf, sizeof(outBuf));
void ln(){ out << NL; }

// --- entorno simulado de la placa -----------------------------------------
#define LOG_SIGNATURE_ADDR 179
#define BYTES_PER_SAMPLE 12
#define POWER_UP 0xAB
#define STATUS2_QE   0x02
#define STATUS1_SRP0 0x80
#define STATUS2_SRP1 0x01
#define STATUS1_BP_MASK 0x3C
#define BAUDRATE 115200
// SECTOR_SIZE, MAX_SECTORS, FIRMWARE_VERSION y PROTOCOL_VERSION se extraen del
// sketch: copiarlos aqui haria que el banco aprobara una cabecera que anuncia
// una version o un tamano de flash que la placa no tiene.

static std::vector<unsigned char> g_flash;
static uint32_t g_count = 0;
static uint16_t g_sig = 0x100F;

// Estado de alimentacion de la flash, modelado porque una lectura con el chip apagado
// devuelve 0xFF y eso ya causo un aviso falso de registro bloqueado.
bool g_flashPowered = false;
void memSendControlByte(byte b){ if(b==POWER_UP) g_flashPowered = true; }
void flashPowerDown(){ g_flashPowered = false; }

// Valores realistas de una pieza sana: sin proteccion, con QE puesto.
byte memReadStatus(){  return g_flashPowered ? 0x00 : 0xFF; }
byte memReadStatus2(){ return g_flashPowered ? 0x02 : 0xFF; }
uint32_t getCount(){ return g_count; }
uint16_t getUInt(int){ return g_sig; }
void readBytesFromFlash(uint32_t addr, byte* buf, uint32_t len){
  for(uint32_t i=0;i<len;i++){
    buf[i] = (addr+i < g_flash.size()) ? g_flash[addr+i] : 0xFF;
  }
}

#include "extracted.h"

int main(int argc, char** argv){
  // Log sintetico reproducible; el contenido concreto da igual, lo que se
  // comprueba es el encuadre.
  g_count = (argc>1) ? strtoul(argv[1], nullptr, 10) : 100;
  g_flash.resize((size_t)g_count * BYTES_PER_SAMPLE);
  for(size_t i=0;i<g_flash.size();i++) g_flash[i] = (unsigned char)((i*7 + 13) & 0xFF);

  unsigned long from = (argc>2) ? strtoul(argv[2], nullptr, 10) : 0;
  unsigned long to   = (argc>3) ? strtoul(argv[3], nullptr, 10) : g_count-1;

  // Vector estandar del CRC-16/CCITT-FALSE: "123456789" -> 0x29B1.
  const char* v = "123456789";
  uint16_t c = crc16Ccitt((const byte*)v, 9, 0xFFFF);
  fprintf(stderr, "CRC(\"123456789\") = 0x%04X %s\n", c, c==0x29B1 ? "OK" : "FALLA");

  unsigned long fast = (argc>5) ? strtoul(argv[5], nullptr, 10) : 0;

  // Reproduce el arranque: la flash queda encendida tras detectar la memoria, se imprime el
  // identificador y a continuacion se leen los registros de estado. Esa secuencia es la que
  // se rompio, y por eso se ejecuta entera.
  memSendControlByte(POWER_UP);
  printBoardIdLine();
  fprintf(stderr, "flash encendida tras imprimir el ID: %s\n",
          g_flashPowered ? "SI" : "NO -- los avisos de estado saldrian falsos");
  flashReportStatus();
  printVersion();
  printMetadata();
  dumpLogBinary(from, to, fast);

  // Los cambios de velocidad se emiten aparte para que el comprobador los verifique.
  FILE* bf = fopen("baud.txt", "w");
  for(auto& c : g_baudChanges) fprintf(bf, "%zu %lu\n", c.first, c.second);
  fclose(bf);

  FILE* f = fopen(argc>4 ? argv[4] : "wire.bin", "wb");
  fwrite(g_wire.data(), 1, g_wire.size(), f);
  fclose(f);
  fprintf(stderr, "%zu bytes en la linea\n", g_wire.size());
  return c==0x29B1 ? 0 : 1;
}
