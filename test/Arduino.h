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

struct FakeSerial {
  void write(const char* s, size_t n){ for(size_t i=0;i<n;i++) g_wire.push_back((unsigned char)s[i]); }
  void write(unsigned char b){ g_wire.push_back(b); }
  void write(const byte* b, size_t n){ for(size_t i=0;i<n;i++) g_wire.push_back(b[i]); }
  void flush(){}
  int available(){ return 0; }
  int peek(){ return -1; }
  int read(){ return -1; }
};
extern FakeSerial Serial;
unsigned long millis();
