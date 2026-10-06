//=========================== I2C BUS AND DS3231 =============================

bool i2cPresent(byte addr){
  Wire.beginTransmission(addr);
  return Wire.endTransmission()==0;
}

byte bcd(byte v){ return (v>>4)*10 + (v&0x0F); }
byte toBcd(byte v){ return ((v/10)<<4) | (v%10); }

byte rtcReadReg(byte reg){
  Wire.beginTransmission(CLOCK_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)CLOCK_ADDRESS, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

bool rtcReadRegs(byte reg, byte* buf, byte n){
  Wire.beginTransmission(CLOCK_ADDRESS);
  Wire.write(reg);
  if(Wire.endTransmission()!=0) return false;
  if(Wire.requestFrom((uint8_t)CLOCK_ADDRESS, n)!=n) return false;
  for(byte i=0;i<n;i++) buf[i]=Wire.read();
  return true;
}

void rtcWriteReg(byte reg, byte v){
  Wire.beginTransmission(CLOCK_ADDRESS);
  Wire.write(reg);
  Wire.write(v);
  Wire.endTransmission();
}

void rtcWriteRegs(byte reg, const byte* v, byte n){
  Wire.beginTransmission(CLOCK_ADDRESS);
  Wire.write(reg);
  for(byte i=0;i<n;i++) Wire.write(v[i]);
  Wire.endTransmission();
}

//------------------------------- I2C SCAN -----------------------------------
void testI2c(){
  section(F("I2C BUS"));
  pinMode(18, INPUT); pinMode(19, INPUT);
  if(!digitalRead(18) || !digitalRead(19)){
    result(R_FAIL, F("SDA or SCL is low with the bus idle: a device holds it, or a pull-up is missing"));
  }else{
    result(R_PASS, F("SDA and SCL idle high (pull-ups present)"));
  }
  Wire.begin();
  P("  Answering:");
  byte otros=0;
  for(byte a=8; a<0x78; a++){
    if(i2cPresent(a)){
      P(" 0x"); hex2(a);
      if(a!=HDC1080_ADDR && a!=TMP119_ADDR && a!=CLOCK_ADDRESS && a!=0x57) otros++;
    }
  }
  Serial.println();
  result(i2cPresent(CLOCK_ADDRESS) ? R_PASS : R_FAIL, F("DS3231 RTC at 0x68"));
  result(i2cPresent(HDC1080_ADDR)  ? R_PASS : R_FAIL, F("HDC1080 temperature/RH at 0x40"));
  result(i2cPresent(TMP119_ADDR)   ? R_PASS : R_FAIL, F("TMP119 temperature at 0x48 (ADD0 to GND)"));
  if(!i2cPresent(TMP119_ADDR)){
    for(byte a=0x49; a<=0x4B; a++){
      if(i2cPresent(a)){
        P("  Something answers at 0x"); hex2(a);
        PL(": a TMP119 with ADD0 not tied to GND (solder bridge or open pin).");
      }
    }
  }
  if(otros) result(R_INFO, F("other devices answer (expected only with add-on boards)"));
}

#if WITH_BOARD
//-------------------------------- DS3231 ------------------------------------
// Date of this sketch's build, as seconds since 2000: the RTC must be later.
long buildSeconds2000(){
  const char* d=__DATE__;   // "Oct  6 2026"
  const char* m="JanFebMarAprMayJunJulAugSepOctNovDec";
  byte mes=1;
  for(byte i=0;i<12;i++) if(!strncmp(d, m+3*i, 3)) mes=i+1;
  int dia=atoi(d+4), anio=atoi(d+7);
  return daysSince2000(anio, mes, dia)*86400L;
}

long daysSince2000(int y, byte m, byte d){
  static const int acum[]={0,31,59,90,120,151,181,212,243,273,304,334};
  long dias=(long)(y-2000)*365 + (y-1997)/4 + acum[m-1] + d-1;
  if(m>2 && y%4==0) dias++;
  return dias;
}

void testRtc(){
  section(F("RTC DS3231"));
  byte t[7];
  if(!rtcReadRegs(0x00, t, 7)){ result(R_FAIL, F("RTC does not answer")); return; }
  int anio=2000+bcd(t[6]);
  byte mes=bcd(t[5]&0x1F), dia=bcd(t[4]&0x3F);
  byte h=bcd(t[2]&0x3F), mi=bcd(t[1]&0x7F), se=bcd(t[0]&0x7F);
  label(F("Time (as stored)"));
  Serial.print(anio); P("-"); if(mes<10) P("0"); Serial.print(mes); P("-"); if(dia<10) P("0"); Serial.print(dia);
  P(" "); if(h<10) P("0"); Serial.print(h); P(":"); if(mi<10) P("0"); Serial.print(mi); P(":"); if(se<10) P("0"); Serial.println(se);
  if(t[2] & 0x40) result(R_WARN, F("RTC in 12-hour mode: the logger assumes 24-hour"));
  long ahora=daysSince2000(anio, mes, dia)*86400L + h*3600L + mi*60L + se;
  if(ahora < buildSeconds2000()-86400L) result(R_FAIL, F("RTC earlier than this sketch's build date: the clock is not set"));
  else                                  result(R_PASS, F("RTC date plausible (not before this build)"));

  // A second read a little over a second later: is it running?
  delay(1100);
  byte t2; rtcReadRegs(0x00, &t2, 1);
  if(t2==t[0]) result(R_FAIL, F("seconds did not advance in 1.1 s: oscillator stopped"));
  else         result(R_PASS, F("clock is running"));

  byte ctrl=rtcReadReg(0x0E), st=rtcReadReg(0x0F);
  label(F("Control 0Eh")); P("0x"); hex2(ctrl);
  if(ctrl&0x80) P(" EOSC(osc off on battery)");
  if(ctrl&0x40) P(" BBSQW");
  if(ctrl&0x04) P(" INTCN"); else P(" SQW-out");
  if(ctrl&0x01) P(" A1IE");
  if(ctrl&0x02) P(" A2IE");
  Serial.println();
  label(F("Status 0Fh")); P("0x"); hex2(st);
  if(st&0x80) P(" OSF");
  if(st&0x08) P(" EN32kHz");
  if(st&0x04) P(" BSY");
  if(st&0x01) P(" A1F");
  if(st&0x02) P(" A2F");
  Serial.println();
  if(st&0x80) result(R_WARN, F("OSF set: the oscillator stopped at some point (power loss), so the time may be wrong"));
  if(st&0x08) result(R_WARN, F("32 kHz output enabled: it costs current and is not used"));
  if(!(ctrl&0x04)) result(R_WARN, F("INTCN clear: INT/SQW is a square wave, alarms cannot wake the logger"));
  if(ctrl&0x40) result(R_WARN, F("BBSQW set: square wave/interrupt also on battery backup"));
  if(st&0x03) result(R_INFO, F("an alarm flag is pending (normal right after a wake-up; the logger clears it)"));

  int8_t aging=(int8_t)rtcReadReg(0x10);
  label(F("Aging offset")); Serial.print(aging); PL(" (about 0.1 ppm per step)");
  byte tt[2]; rtcReadRegs(0x11, tt, 2);
  float temp=(int8_t)tt[0] + (tt[1]>>6)*0.25;
  label(F("RTC temperature")); Serial.print(temp, 2); PL(" C (updated every 64 s, +-3 C)");
}

// The MCU's crystal against the RTC's TCXO: the RTC puts 1.024 kHz on INT/SQW
// and the MCU times 4096 edges with micros(). micros() is built from F_CPU,
// so this measures the real clock against what the build assumes: a crystal
// tolerance shows as tens of ppm, a sketch built for the wrong clock as
// percent. The RTC's own accuracy is +-2 ppm.
void testRtcSqw(){
  section(F("MCU CLOCK vs RTC"));
  if(!i2cPresent(CLOCK_ADDRESS)){ result(R_FAIL, F("no RTC")); return; }
  byte ctrl=rtcReadReg(0x0E);
  rtcWriteReg(0x0E, 0x08);              // INTCN=0, RS=01: 1.024 kHz on INT/SQW
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  bool ok=true;
  unsigned long lim=millis();
  while(digitalRead(WAKEUP_PIN)==LOW)  if(millis()-lim>100){ ok=false; break; }
  while(ok && digitalRead(WAKEUP_PIN)==HIGH) if(millis()-lim>100){ ok=false; break; }
  unsigned long t0=micros();
  for(unsigned int i=0; ok && i<4096; i++){
    lim=millis();
    while(digitalRead(WAKEUP_PIN)==LOW)  if(millis()-lim>50){ ok=false; break; }
    while(ok && digitalRead(WAKEUP_PIN)==HIGH) if(millis()-lim>50){ ok=false; break; }
  }
  unsigned long dt=micros()-t0;
  rtcWriteReg(0x0E, ctrl);
  rtcWriteReg(0x0F, rtcReadReg(0x0F) & ~0x03);
  if(!ok){ result(R_FAIL, F("no square wave on INT/SQW: the line to D2 is open, or the RTC ignores it")); return; }
  float ppm=((float)dt/4000000.0 - 1.0)*1e6;
  label(F("4096 edges took")); Serial.print(dt); PL(" us (4000000 expected)");
  label(F("MCU clock")); Serial.print((float)F_CPU*(1.0+ppm/1e6), 0); P(" Hz  ("); Serial.print(ppm, 0); PL(" ppm)");
  if(fabs(ppm)<500)        result(R_PASS, F("crystal on frequency (resolution ~2 ppm + crystal tolerance)"));
  else if(fabs(ppm)<20000) result(R_WARN, F("clock off by 0.05-2 %: crystal or loading capacitors?"));
  else                     result(R_FAIL, F("clock off by more than 2 %: built for the wrong clock, or wrong crystal"));
  result(R_PASS, F("INT/SQW line reaches D2"));
}

// The whole wake-up path: alarm 1 set 3 s ahead must pull INT/SQW low. This is
// what wakes the logger; if it does not happen here, the logger sleeps forever.
void testRtcAlarm(){
  section(F("RTC ALARM -> WAKE LINE"));
  if(!i2cPresent(CLOCK_ADDRESS)){ result(R_FAIL, F("no RTC")); return; }
  byte saved[4], ctrl=rtcReadReg(0x0E);
  rtcReadRegs(0x07, saved, 4);
  byte s=bcd(rtcReadReg(0x00)&0x7F);
  byte a[4]={toBcd((s+3)%60), 0x80, 0x80, 0x80};  // match seconds only
  rtcWriteRegs(0x07, a, 4);
  rtcWriteReg(0x0F, rtcReadReg(0x0F) & ~0x03);
  rtcWriteReg(0x0E, (ctrl | 0x05) & ~0x40);
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  bool previo=digitalRead(WAKEUP_PIN);
  unsigned long t0=millis();
  bool disparo=false;
  while(millis()-t0 < 5000){
    if(!digitalRead(WAKEUP_PIN)){ disparo=true; break; }
  }
  unsigned long dt=millis()-t0;
  rtcWriteReg(0x0F, rtcReadReg(0x0F) & ~0x03);
  delay(2);
  bool suelta=digitalRead(WAKEUP_PIN);
  rtcWriteRegs(0x07, saved, 4);
  rtcWriteReg(0x0E, ctrl);
  if(!previo) result(R_WARN, F("INT/SQW was already low before arming"));
  if(!disparo){ result(R_FAIL, F("alarm did not pull INT/SQW low in 5 s: the logger would never wake")); return; }
  label(F("Alarm fired after")); Serial.print(dt); PL(" ms (2000-3000 expected)");
  result(R_PASS, F("alarm pulls the wake line low"));
  result(suelta ? R_PASS : R_FAIL, F("clearing A1F releases the line"));
  PL("  Alarm registers and control restored to what they were.");
}
#endif // WITH_BOARD
