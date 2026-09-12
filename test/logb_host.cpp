// Banco de pruebas en el PC del volcado LOGB y de la cabecera INFO.
//
// Ejecuta el codigo REAL del firmware (extraido en cada compilacion por
// extract.py) contra una memoria flash sintetica, y escribe en un fichero los
// bytes exactos que saldrian por la linea serie. Eso permite comprobar contra
// el simulador y contra el lector de la app que los tres hablan el mismo
// idioma, sin placa y sin telefono.
#include "Arduino.h"
// Constantes del sketch, extraidas: hacen falta antes que las funciones.
#include "extracted_defs.h"

std::vector<unsigned char> g_wire;
std::vector<std::pair<size_t,unsigned long>> g_baudChanges;
FakeSerial Serial;
long g_xoffAfter = -1;
long g_cancelAfter = -1;
bool g_cancelDone = false;
long g_cancelSeenAt = -1;
bool g_xoffDone = false;
bool g_xonDone = false;
long g_xoffSeenAt = -1;
long g_xonSeenAt = -1;
FakeSPI SPI;
unsigned long millis(){ return 0; }
void delay(unsigned long){}
void digitalWrite(int,int){}

char* ultoa(unsigned long v, char* s, int base){ sprintf(s, "%lu", v); return s; }

long int10Pow(byte power);
#include "lightOStream.h"
long intPow(int power,int base){ long o=1; for(int i=0;i<power;i++) o*=base; return o; }
long int10Pow(byte power){ return intPow(power,10); }

// El mismo tamano que el firmware real, EXTRAIDO y no copiado. Un banco con un buffer
// mas grande no veria el fallo, y uno con el tamano copiado a mano dejaria de verlo en
// cuanto alguien cambiara el del sketch: la linea solo se emite al recibir el '\n', asi
// que un buffer que se llena antes se come esa linea y todas las siguientes.
static char outBuf[OUT_BUFFER_SIZE];
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
#define ASTERISK_BAR '\xD0'
#define COMPILATION_TIME 840748759UL
unsigned long currentTime = 800000000UL;   // un reloj sin ajustar, anterior a la compilacion
// Basta con que imprima algo estable: lo que se comprueba aqui es el ENCUADRE del aviso.
void displayUnixTime(unsigned long t){ out << NOSPACER << t << NORMALTEXT; }
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

// Entorno de printMemoryLifetime. El intervalo se pasa por linea de ordenes para poder
// probar los casos extremos --1 s y 86.400 s con la memoria vacia-- que son justo los que
// desbordarian si el calculo se hiciera de la forma evidente.
unsigned long measureInterval = 600;
void getCurrentTime(){}

// Entorno de displayHistoryHex. El banco usa el mismo tamano de registro para el log
// almacenado y para esta compilacion, asi que no hay desajuste que simular.
bool logFormatMismatch = false;

// La bandera de cancelacion del volcado. No se extrae porque extract.py saca funciones y
// constantes, no variables globales; declararla aqui es seguro porque si el firmware la
// renombra el banco deja de COMPILAR, que es el tipo de fallo que se ve enseguida.
bool dumpAborted = false;
#define MAX_RECORD_BYTES 12
#define LOG_SIGNATURE 0x100F
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
  reportClockNotSet();

  memSendControlByte(POWER_UP);
  printBoardIdLine();
  fprintf(stderr, "flash encendida tras imprimir el ID: %s\n",
          g_flashPowered ? "SI" : "NO -- los avisos de estado saldrian falsos");
  flashReportStatus();
  // El comando ID, para comprobar que da la MISMA linea que el arranque: es lo que una
  // persona teclea cuando quiere el identificador, y antes daba solo los 16 digitos.
  printBoardIdStandalone();
  printVersion();
  printMetadata();
  // argv[11] cancela durante el volcado BINARIO; argv[10], durante el Intel HEX. Van
  // separados porque los dos volcados corren uno detras del otro en este banco, y una sola
  // posicion siempre caeria en el primero.
  if(argc>11){
    g_cancelAfter = strtol(argv[11], nullptr, 10);
  }
  dumpLogBinary(from, to, fast);
  {
    FILE* bf2 = fopen("cancel_logb.txt", "w");
    fprintf(bf2, "%ld %ld %zu\n", g_cancelAfter, g_cancelSeenAt, g_wire.size());
    fclose(bf2);
  }
  // Se reinicia el estado para que la fase de Intel HEX empiece limpia.
  g_cancelAfter = -1; g_cancelDone = false; g_cancelSeenAt = -1;

  // Volcado Intel HEX con inyeccion de XOFF: argv[7] es la posicion de la linea a partir
  // de la cual el receptor pide la pausa. Sin ese argumento no se ejecuta, para no alterar
  // la comparacion byte a byte de check_wire.py contra el simulador.
  if(argc>7){
    g_xoffAfter = strtol(argv[7], nullptr, 10);
    // argv[8] acota el volcado. Sin el se vuelcan los ocho megas enteros, que es el
    // comportamiento real; con el, las pruebas de control de flujo no tienen que mover
    // veintitres megas por cada caso para comprobar un sobrepaso de medio kilobyte.
    unsigned long hexBytes = (argc>8) ? strtoul(argv[8], nullptr, 10) : 0;
    unsigned long hexFast  = (argc>9) ? strtoul(argv[9], nullptr, 10) : 0;
    if(argc>10) g_cancelAfter = strtol(argv[10], nullptr, 10);
    g_baudChanges.clear();          // solo interesan los del volcado crudo
    displayHistoryHex(hexBytes, hexFast);
    FILE* ff = fopen("flow.txt", "w");
    fprintf(ff, "%ld %ld %ld %zu\n", g_xoffAfter, g_xoffSeenAt, g_xonSeenAt, g_wire.size());
    for(auto& c : g_baudChanges) fprintf(ff, "%zu %lu\n", c.first, c.second);
    fclose(ff);
    FILE* cf = fopen("cancel.txt", "w");
    fprintf(cf, "%ld %ld %zu\n", g_cancelAfter, g_cancelSeenAt, g_wire.size());
    fclose(cf);
  }

  // Solo si se pide un intervalo: check_wire.py compara byte a byte contra el simulador,
  // y estas dos lineas pertenecen a la respuesta de INT, no a la secuencia de arranque.
  if(argc>6){
    measureInterval = strtoul(argv[6], nullptr, 10);
    printMemoryLifetime();
  }

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
