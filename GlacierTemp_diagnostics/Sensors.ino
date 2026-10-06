//======================= HDC1080, TMP119, DS18B20 ===========================

//------------------------------- HDC1080 ------------------------------------
bool hdcReadReg(byte reg, uint16_t &v){
  Wire.beginTransmission(HDC1080_ADDR);
  Wire.write(reg);
  if(Wire.endTransmission()!=0) return false;
  if(Wire.requestFrom((uint8_t)HDC1080_ADDR, (uint8_t)2)!=2) return false;
  v=(uint16_t)Wire.read()<<8;
  v|=Wire.read();
  return true;
}

bool hdcWriteReg(byte reg, uint16_t v){
  Wire.beginTransmission(HDC1080_ADDR);
  Wire.write(reg);
  Wire.write(v>>8);
  Wire.write(v&0xFF);
  return Wire.endTransmission()==0;
}

// One temperature + RH conversion, as the logger takes it (register 0x00,
// both 14-bit, 15 ms). Either pointer may be NULL.
bool hdcRead(float* t, float* rh){
  Wire.beginTransmission(HDC1080_ADDR);
  Wire.write(0x00);
  if(Wire.endTransmission()!=0) return false;
  delay(15);
  if(Wire.requestFrom((uint8_t)HDC1080_ADDR, (uint8_t)4)!=4) return false;
  uint16_t rt=(uint16_t)Wire.read()<<8; rt|=Wire.read();
  uint16_t rr=(uint16_t)Wire.read()<<8; rr|=Wire.read();
  if(t)  *t =rt/65536.0*165.0-40.0;
  if(rh) *rh=rr/65536.0*100.0;
  return true;
}

#if WITH_SENSORS
void testHdc(int n){
  section(F("HDC1080 TEMPERATURE / RH"));
  if(n<2) n=2;
  if(n>500) n=500;
  uint16_t man, dev, cfg, s1, s2, s3;
  if(!hdcReadReg(0xFE, man)){ result(R_FAIL, F("HDC1080 does not answer at 0x40")); return; }
  hdcReadReg(0xFF, dev);
  label(F("Manufacturer/device ID")); P("0x"); hex4(man); P(" / 0x"); hex4(dev); Serial.println();
  result((man==0x5449 && dev==0x1050) ? R_PASS : R_FAIL, F("IDs are Texas Instruments HDC1080 (5449/1050)"));
  if(hdcReadReg(0xFB, s1) && hdcReadReg(0xFC, s2) && hdcReadReg(0xFD, s3)){
    label(F("Serial number")); hex4(s1); hex4(s2); hex2(s3>>8); Serial.println();
  }
  hdcReadReg(0x02, cfg);
  label(F("Configuration 02h")); P("0x"); hex4(cfg);
  if(cfg & 0x1000) P("  sequential T+RH"); else P("  T or RH");
  if(cfg & 0x2000) P("  HEATER ON");
  if(cfg & 0x0800) P("  BTST: VDD<2.8V");
  Serial.println();
  if(!(cfg & 0x1000)) result(R_FAIL, F("not in sequential mode: the logger's 4-byte read gets T only"));
  if(cfg & 0x2000)    result(R_WARN, F("heater left on: readings run warm and it draws current"));
  if(cfg & 0x0800)    result(R_FAIL, F("the sensor reports its supply below 2.8 V"));

  Stat T, H, dT, dH; T.clear(); H.clear(); dT.clear(); dH.clear();
  float pt=0, ph=0; byte fallos=0;
  for(int i=0;i<n;i++){
    float t, h;
    if(!hdcRead(&t, &h)){ fallos++; continue; }
    if(T.n){ dT.add(t-pt); dH.add(h-ph); }
    T.add(t); H.add(h); pt=t; ph=h;
    delay(250);
  }
  P("  "); Serial.print(T.n); P(" readings, 250 ms apart");
  if(fallos){ P(", "); Serial.print(fallos); P(" failed"); }
  Serial.println();
  printStatLine(F("Temperature C"), T, dT, 3);
  printStatLine(F("Humidity %RH"), H, dH, 2);
  if(fallos) result(R_FAIL, F("some reads failed: check the I2C wiring"));
  if(T.n && (T.mean<-40 || T.mean>60)) result(R_WARN, F("temperature out of the expected range"));
  if(H.n && (H.mean<=0.5 || H.mean>=99.9)) result(R_WARN, F("RH pinned at an end: sensor saturated or wet"));
  if(T.n && !fallos) result(R_PASS, F("HDC1080 reads consistently"));
}

// mean, sd, min..max, and the noise from successive differences: sd(diff)/sqrt2
// is the sample-to-sample noise with any slow drift removed, which the plain sd
// is not when the room is warming up during the test.
void printStatLine(const __FlashStringHelper* name, const Stat& s, const Stat& d, byte dec){
  label(name);
  P("mean "); Serial.print(s.mean, dec);
  P("  sd "); Serial.print(s.sd(), dec+1);
  P("  noise "); Serial.print(d.sd()/1.41421, dec+1);
  P("  range "); Serial.print(s.mn, dec); P(".."); Serial.println(s.mx, dec);
}

void testHdcHeater(){
  section(F("HDC1080 HEATER"));
  uint16_t cfg;
  if(!hdcReadReg(0x02, cfg)){ result(R_FAIL, F("HDC1080 does not answer")); return; }
  float t0, h0, t1, h1;
  hdcRead(&t0, &h0);
  PL("  Heater on for 15 s (it only heats while converting, so it converts flat out)...");
  hdcWriteReg(0x02, cfg | 0x2000);
  unsigned long ini=millis();
  while(millis()-ini < 15000) hdcRead(&t1, &h1);
  hdcWriteReg(0x02, cfg & ~0x2000);
  label(F("Temperature before/after")); Serial.print(t0, 2); P(" -> "); Serial.print(t1, 2); PL(" C");
  label(F("Humidity before/after")); Serial.print(h0, 1); P(" -> "); Serial.print(h1, 1); PL(" %");
  if(t1-t0 > 0.3) result(R_PASS, F("temperature rose with the heater: the sensor responds"));
  else            result(R_WARN, F("no clear rise: weak heater, strong air flow, or a sensor that does not respond"));
  PL("  Heater off. Readings take a minute or two to return to ambient.");
}

#endif // WITH_SENSORS

//-------------------------------- TMP119 ------------------------------------
bool tmpWrite(byte reg, uint16_t v){
  Wire.beginTransmission(TMP119_ADDR);
  Wire.write(reg);
  Wire.write(v>>8);
  Wire.write(v&0xFF);
  return Wire.endTransmission()==0;
}

bool tmpRead(byte reg, uint16_t &v){
  Wire.beginTransmission(TMP119_ADDR);
  Wire.write(reg);
  if(Wire.endTransmission()!=0) return false;
  if(Wire.requestFrom((uint8_t)TMP119_ADDR, (uint8_t)2)!=2) return false;
  v=(uint16_t)Wire.read()<<8;
  v|=Wire.read();
  return true;
}

// One one-shot conversion with the given configuration word, as the logger
// takes it: write the config, poll Data_Ready (bit 13), read. Returns the
// temperature in C and the conversion time in ms. Either pointer may be NULL.
bool tmpOneShot(uint16_t cfgWord, float* tC, unsigned long* ms){
  if(!tmpWrite(TMP119_CONFIG_REG, cfgWord)) return false;
  unsigned long t0=millis();
  uint16_t cfg;
  while(true){
    delay(1);
    if(!tmpRead(TMP119_CONFIG_REG, cfg)) return false;
    if(cfg & 0x2000) break;
    if(millis()-t0 > 1500) return false;
  }
  unsigned long dt=millis()-t0;
  uint16_t raw;
  if(!tmpRead(TMP119_TEMP_REG, raw)) return false;
  if(tC) *tC=(int16_t)raw*0.0078125;
  if(ms) *ms=dt;
  return true;
}

#if WITH_SENSORS
void testTmp(int n){
  section(F("TMP119 HIGH ACCURACY TEMPERATURE"));
  if(n<2) n=2;
  if(n>500) n=500;
  uint16_t id, cfg, off, e1, e2, e3;
  if(!tmpRead(0x0F, id)){ result(R_FAIL, F("TMP119 does not answer at 0x48")); return; }
  label(F("Device ID 0Fh")); P("0x"); hex4(id); P("  (rev "); Serial.print(id>>12); PL(")");
  if((id & 0x0FFF)==0x0117) result(R_PASS, F("TMP11x family ID (TMP119 reads 0x2117)"));
  else                      result(R_FAIL, F("unexpected device ID: not a TMP117/TMP119"));
  tmpRead(TMP119_CONFIG_REG, cfg);
  label(F("Configuration 01h")); P("0x"); hex4(cfg);
  byte mod=(cfg>>10)&3;
  if(mod==0) P("  MOD=00 continuous"); else if(mod==1) P("  MOD=01 shutdown"); else if(mod==3) P("  MOD=11 one-shot"); else P("  MOD=10");
  P("  AVG="); Serial.print((cfg>>5)&3); Serial.println();
  if(mod==0) result(R_WARN, F("free-running (~16 uA): the logger parks it in shutdown"));
  if(tmpRead(0x07, off)){
    label(F("Temperature offset 07h")); Serial.print((int16_t)off*0.0078125, 4); PL(" C");
    if(off) result(R_WARN, F("a temperature offset is programmed: every reading is shifted by it"));
  }
  if(tmpRead(0x05, e1) && tmpRead(0x06, e2) && tmpRead(0x08, e3)){
    label(F("EEPROM 1/2/3")); hex4(e1); P(" "); hex4(e2); P(" "); hex4(e3); PL("  (unique ID / user data)");
  }

  Stat T, D, C; T.clear(); D.clear(); C.clear();
  float prev=0; byte fallos=0;
  for(int i=0;i<n;i++){
    float t; unsigned long ms;
    if(!tmpOneShot(0x0C20, &t, &ms)){ fallos++; continue; }
    if(T.n) D.add(t-prev);
    T.add(t); C.add(ms); prev=t;
  }
  tmpWrite(TMP119_CONFIG_REG, TMP119_SHUTDOWN);
  P("  "); Serial.print(T.n); P(" one-shot readings with 8 averages (the logger's default)");
  if(fallos){ P(", "); Serial.print(fallos); P(" failed"); }
  Serial.println();
  printStatLine(F("Temperature C"), T, D, 4);
  label(F("Conversion time")); Serial.print(C.mean, 1); P(" ms (");
  Serial.print(C.mn, 0); P(".."); Serial.print(C.mx, 0); PL(")  ~125 expected");
  if(fallos) result(R_FAIL, F("some conversions failed or timed out"));
  else       result(R_PASS, F("TMP119 one-shot conversions work"));
  uint16_t fin; tmpRead(TMP119_CONFIG_REG, fin);
  result(((fin>>10)&3)!=0 ? R_PASS : R_FAIL, F("left in shutdown/one-shot, not free-running"));
}

// Noise against averaging. The four settings are taken INTERLEAVED, one of
// each per round, so that the room drifting during the test affects all four
// alike instead of whichever happened to run last. Two noise figures:
//   sd     plain standard deviation, which includes any drift;
//   noise  sd of successive differences / sqrt(2): sample-to-sample noise with
//          slow drift removed. This is the one to compare.
// The cost of each setting is its conversion time: the logger stays awake for
// all of it, so it goes straight into the battery budget.
void testTmpAveraging(int rondas){
  section(F("TMP119 NOISE vs AVERAGING"));
  if(rondas<5) rondas=5;
  if(rondas>200) rondas=200;
  if(!i2cPresent(TMP119_ADDR)){ result(R_FAIL, F("TMP119 does not answer")); return; }
  const uint16_t modos[4]={0x0C00, 0x0C20, 0x0C40, 0x0C60};
  const byte medias[4]={0, 8, 32, 64};
  Stat T[4], D[4], C[4];
  float prev[4];
  for(byte m=0;m<4;m++){ T[m].clear(); D[m].clear(); C[m].clear(); }
  P("  "); Serial.print(rondas); P(" rounds of 0, 8, 32 and 64 averages, about ");
  Serial.print(rondas*165/100); PL(" s. Keep the board still and out of drafts.");
  byte fallos=0;
  for(int r=0;r<rondas;r++){
    for(byte m=0;m<4;m++){
      float t; unsigned long ms;
      if(!tmpOneShot(modos[m], &t, &ms)){ fallos++; continue; }
      if(T[m].n) D[m].add(t-prev[m]);
      T[m].add(t); C[m].add(ms); prev[m]=t;
    }
    if((r+1)%10==0){ P("  ... "); Serial.print(r+1); Serial.println(); }
  }
  tmpWrite(TMP119_CONFIG_REG, TMP119_SHUTDOWN);
  PL("\n  avg  config  conv ms   mean C     sd mC   noise mC   p-p mC");
  for(byte m=0;m<4;m++){
    P("  "); if(medias[m]<10) P(" "); Serial.print(medias[m]);
    P("   0x"); hex4(modos[m]);
    P("  "); pad(C[m].mean, 7, 1);
    P("  "); pad(T[m].mean, 8, 4);
    P("  "); pad(T[m].sd()*1000, 7, 2);
    P("  "); pad(D[m].sd()/1.41421*1000, 8, 2);
    P("  "); pad((T[m].mx-T[m].mn)*1000, 7, 1);
    Serial.println();
  }
  if(fallos){ P("  "); Serial.print(fallos); PL(" conversions failed."); result(R_WARN, F("some conversions failed")); }
  float n8=D[1].sd(), n0=D[0].sd();
  if(n8>0){
    label(F("Noise ratio 0/8 avg")); Serial.print(n0/n8, 2); PL("  (sqrt(8)=2.83 if the noise were white)");
  }
  PL("  One LSB is 7.8 mC. Averaging lowers NOISE only, not the +-0.1 C accuracy.");
  PL("  Pick the fewest averages whose noise is below what the data needs:");
  PL("  each step up multiplies the awake time of every measurement by about 4.");
  result(R_INFO, F("compare the 'noise' column; TMP119_AVERAGING in the logger selects the setting"));
}

// A number right-aligned in w characters. Counts the digits itself and lets
// Serial.print() do the formatting: dtostrf() would pull in ~700 bytes of its
// own float formatter next to the one Serial.print() already uses.
void pad(float v, byte w, byte dec){
  long e=(long)fabs(v);
  byte len=1;
  while(e>=10){ e/=10; len++; }
  if(v<0) len++;
  if(dec) len+=dec+1;
  for(byte i=len;i<w;i++) Serial.print(' ');
  Serial.print(v, dec);
}

#endif // WITH_SENSORS

#if WITH_BOARD
//-------------------------------- DS18B20 -----------------------------------
void testDs18b20(){
  section(F("1-WIRE BUS (DS18B20)"));
  pinMode(ONE_WIRE_PIN, INPUT);
  digitalWrite(ONE_WIRE_PIN, LOW);
  delay(2);
  bool alto=digitalRead(ONE_WIRE_PIN);
  label(F("Idle bus level")); if(alto) PL("high"); else PL("LOW");
  if(!alto){
    result(R_INFO, F("bus low with no pull-up: no 4.7k pull-up fitted, or nothing connected"));
    pinMode(ONE_WIRE_PIN, INPUT); digitalWrite(ONE_WIRE_PIN, LOW);
    return;
  }
  result(R_PASS, F("bus pulled up (external 4.7k present)"));
  OneWire ow(ONE_WIRE_PIN);
  if(!ow.reset()){ result(R_INFO, F("no presence pulse: no sensor on the bus")); return; }
  byte rom[8], n=0;
  ow.reset_search();
  while(ow.search(rom)){
    n++;
    P("  Sensor "); Serial.print(n); P(": ");
    for(byte i=0;i<8;i++) hex2(rom[i]);
    bool crcOk=OneWire::crc8(rom,7)==rom[7];
    if(crcOk) P("  CRC ok"); else P("  CRC BAD");
    if(rom[0]==0x28) P("  DS18B20");
    else if(rom[0]==0x10) P("  DS18S20 (NOT supported by the logger)");
    else P("  unknown family");
    Serial.println();
    if(!crcOk) result(R_FAIL, F("ROM CRC error: noisy bus, long cable or weak pull-up"));
  }
  label(F("Sensors found")); Serial.println(n);
  if(!n){ result(R_WARN, F("presence pulse but the search found nothing")); return; }
  ow.reset(); ow.write(0xCC); ow.write(0xB4);
  bool parasito=!ow.read_bit();
  if(parasito) result(R_WARN, F("a sensor is parasite-powered: the logger does not support it"));
  ow.reset(); ow.write(0xCC); ow.write(0x44);
  delay(800);
  ow.reset_search();
  byte i=0;
  while(ow.search(rom)){
    i++;
    byte sp[9];
    ow.reset(); ow.select(rom); ow.write(0xBE);
    for(byte k=0;k<9;k++) sp[k]=ow.read();
    bool ok=OneWire::crc8(sp,8)==sp[8];
    P("  Sensor "); Serial.print(i); P(": ");
    if(!ok){ PL("scratchpad CRC BAD"); result(R_FAIL, F("scratchpad CRC error")); continue; }
    int16_t raw=(sp[1]<<8)|sp[0];
    Serial.print(raw/16.0, 4); P(" C   resolution "); Serial.print(9+((sp[4]>>5)&3)); P(" bit");
    if(raw==0x0550) P("  (85.000 C power-on value: conversion did not run)");
    Serial.println();
  }
  result(R_PASS, F("1-Wire sensors read"));
}
#endif // WITH_BOARD
