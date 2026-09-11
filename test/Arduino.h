// Sustituto minimo de Arduino.h para compilar en el PC las partes del firmware
// que solo dependen del stream de salida. Existe para poder VER el texto exacto
// que produce el firmware sin quemarlo en una placa: el stream lightOStream
// inserta separadores por su cuenta, asi que la unica forma fiable de saber que
// linea sale es ejecutarla.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

typedef uint8_t byte;
class __FlashStringHelper;
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))
#define PGM_P const char*
#define pgm_read_byte(p) (*(p))
#define PROGMEM

// Todo lo que el firmware emite, en orden y sin perder los bytes crudos: el
// volcado binario y el texto comparten la misma linea serie, y precisamente lo
// que hay que comprobar es como quedan entrelazados.
extern std::vector<unsigned char> g_wire;

// Cada llamada a begin() queda registrada junto con la posicion de la linea en la que
// ocurrio: asi el banco puede comprobar QUE bytes viajaron a QUE velocidad, que es
// justamente lo que hay que verificar del volcado rapido.
extern std::vector<std::pair<size_t,unsigned long>> g_baudChanges;

// Inyeccion de control de flujo. El receptor pide la pausa en una posicion concreta de la
// linea y el banco anota EN QUE posicion la atendio el firmware: la diferencia es el
// sobrepaso, que es exactamente lo que hay que medir. Un volcado que no comprueba nunca
// tiene sobrepaso infinito y no se distingue mirando los datos.
extern long g_xoffAfter;     // posicion a partir de la cual hay un XOFF esperando; -1 nunca
extern bool g_xoffDone;      // ya lo leyo el firmware
extern bool g_xonDone;       // ya se le entrego el XON que lo reanuda
extern long g_xoffSeenAt;    // posicion de la linea cuando lo leyo
extern long g_xonSeenAt;     // posicion cuando reanudo

struct FakeSerial {
  void begin(unsigned long baud){ g_baudChanges.push_back({g_wire.size(), baud}); }
  void end(){}
  void write(const char* s, size_t n){ for(size_t i=0;i<n;i++) g_wire.push_back((unsigned char)s[i]); }
  void write(unsigned char b){ g_wire.push_back(b); }
  void write(const byte* b, size_t n){ for(size_t i=0;i<n;i++) g_wire.push_back(b[i]); }
  void flush(){}

  bool xoffPending(){
    return g_xoffAfter>=0 && !g_xoffDone && (long)g_wire.size()>=g_xoffAfter;
  }
  bool xonPending(){ return g_xoffDone && !g_xonDone; }

  int available(){ return (xoffPending() || xonPending()) ? 1 : 0; }
  int peek(){ return xoffPending() ? 0x13 : (xonPending() ? 0x11 : -1); }
  int read(){
    if(xoffPending()){ g_xoffDone=true; g_xoffSeenAt=(long)g_wire.size(); return 0x13; }
    if(xonPending()){ g_xonDone=true; g_xonSeenAt=(long)g_wire.size(); return 0x11; }
    return -1;
  }
};
extern FakeSerial Serial;
unsigned long millis();
extern bool g_flashPowered;

// SPI simulado con lo justo para el opcode 0x4B (Read Unique ID): devuelve un patron fijo
// cuando la flash esta encendida y 0xFF cuando no, igual que un chip que no contesta.
struct FakeSPI {
  int idx = -1;
  byte transfer(byte b){
    if(!g_flashPowered) return 0xFF;
    if(b == 0x4B){ idx = 0; return 0xFF; }
    if(idx >= 0){
      int i = idx++;
      if(i < 4) return 0xFF;              // los cuatro bytes de relleno
      byte k = (byte)(i - 4);
      return (byte)(0x10*k + k);          // 00 11 22 ... 77
    }
    return 0xFF;
  }
};
extern FakeSPI SPI;
// La flash del banco: leer su estado apagada devuelve 0xFF, como el chip real.
void digitalWrite(int,int);
#define FLASH_MEMORY_CS 0
#define LOW 0
#define HIGH 1
void delay(unsigned long ms);
