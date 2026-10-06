//============================ W25Q64 SPI FLASH ==============================
// Single-I/O instructions only. On this board the flash's QE bit is fixed at 1
// and pins 3 and 7 are tied to rails, so a quad instruction would drive an
// output into a short (see the logger's FlashMem.ino). Nothing here uses one.

#if WITH_BOARD
void flashSend(byte cmd){
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(cmd);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
}

byte flashStatus(byte cmd){
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(cmd);
  byte s=SPI.transfer(0);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  return s;
}

// Release from deep power-down; returns the device ID it reports (0x16).
byte flashWake(){
  digitalWrite(MEM_POWER, HIGH);
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0xAB);
  SPI.transfer(0); SPI.transfer(0); SPI.transfer(0);
  byte id=SPI.transfer(0);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  delayMicroseconds(50);
  return id;
}

void flashRead(uint32_t addr, byte* buf, uint16_t n){
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x03);
  SPI.transfer(addr>>16); SPI.transfer(addr>>8); SPI.transfer(addr);
  for(uint16_t i=0;i<n;i++) buf[i]=SPI.transfer(0);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
}

bool flashWaitReady(unsigned long ms){
  unsigned long t0=millis();
  while(millis()-t0 < ms){
    if(!(flashStatus(0x05) & 0x01)) return true;
  }
  return false;
}

void testFlash(){
  section(F("SPI FLASH W25Q64"));
  digitalWrite(MEM_POWER, HIGH);
  delay(2);
  byte devId=flashWake();
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x9F);
  byte mf=SPI.transfer(0), ty=SPI.transfer(0), cap=SPI.transfer(0);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  label(F("JEDEC ID")); hex2(mf); P(" "); hex2(ty); P(" "); hex2(cap);
  if(cap>=0x10 && cap<=0x20){ P("  ("); Serial.print(1UL<<(cap-20)); P(" MB)"); }
  Serial.println();
  if(mf==0xFF || mf==0x00){
    result(R_FAIL, F("no answer: flash unpowered (D4), CS or SPI wiring, or chip absent"));
    return;
  }
  result((mf==0xEF && ty==0x40 && cap==0x17) ? R_PASS : R_WARN, F("Winbond W25Q64 (EF 40 17)"));
  label(F("Device ID (ABh)")); P("0x"); hex2(devId); Serial.println();

  byte id[8];
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x4B);
  for(byte i=0;i<4;i++) SPI.transfer(0);
  for(byte i=0;i<8;i++) id[i]=SPI.transfer(0);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  label(F("Unique ID (board ID)")); for(byte i=0;i<8;i++) hex2(id[i]);
  P("  short GT001-"); for(byte i=5;i<8;i++) hex2(id[i]); Serial.println();

  byte s1=flashStatus(0x05), s2=flashStatus(0x35), s3=flashStatus(0x15);
  label(F("Status SR1/SR2/SR3")); hex2(s1); P(" "); hex2(s2); P(" "); hex2(s3); Serial.println();
  if(s1 & 0x01) result(R_WARN, F("BUSY set with nothing running"));
  if(s1 & 0x1C) result(R_FAIL, F("block protection (BP) set: writes to part of the array fail"));
  if(s1 & 0x80) result(R_WARN, F("SRP0 set"));
  if(s2 & 0x01) result(R_WARN, F("SRP1 set: status register locked"));
  if(s2 & 0x02) result(R_INFO, F("QE=1: factory-fixed on this part; NEVER issue quad instructions"));
  else          result(R_INFO, F("QE=0: WP#/HOLD# are live pins on this chip"));
  if(!(s1 & 0x9C) && !(s2 & 0x01)) result(R_PASS, F("no protection bits set"));

  // Read throughput, as LOGB/LOGH see it from the chip side.
  byte buf[64];
  unsigned long t0=micros();
  for(uint16_t i=0;i<512;i++) flashRead((uint32_t)i*64, buf, 64);
  unsigned long dt=micros()-t0;
  label(F("Read 32 kB")); Serial.print(dt/1000); P(" ms  ("); Serial.print(32768000UL/dt); PL(" kB/s)");

  // Deep power-down and back: the logger relies on both.
  flashSend(0xB9);
  delayMicroseconds(10);
  byte s=flashStatus(0x05);
  byte w=flashWake();
  label(F("In power-down SR1 reads")); P("0x"); hex2(s); PL(" (ignored while asleep)");
  result(w==devId ? R_PASS : R_FAIL, F("wakes from deep power-down (B9h -> ABh)"));

  testLogConsistency();
  flashSend(0xB9);   // left as the logger leaves it between measurements
  PL("  Flash left in deep power-down.");
}

// Does the log in flash agree with the counters in EEPROM? The last record the
// counter claims must hold data, and the slot after it must still be erased.
void testLogConsistency(){
  uint16_t sig=(uint16_t)eeInt(LOG_SIGNATURE_ADDR);
  P("  Log: ");
  if(sig==0xFFFF){ PL("no signature in EEPROM (no log started)."); return; }
  printSignature(sig); Serial.println();
  byte rb=recordBytes(sig);
  unsigned long full=0;
  for(byte i=0;i<COUNTERS_SLOTS;i++){
    unsigned long c=eeULong(COUNT_ADDR+4*i);
    if(c!=0xFFFFFFFFUL) full+=c;
  }
  unsigned long count=full-eeULong(COUNT_RESET_ADDR);
  label(F("Records / bytes each")); Serial.print(count); P(" / "); Serial.println(rb);
  uint32_t usado=count*rb;
  label(F("Flash used")); Serial.print(usado); P(" bytes ("); Serial.print(usado*100.0/(SECTOR_SIZE*(MAX_SECTORS+1)), 2); PL(" %)");
  if(usado > SECTOR_SIZE*(MAX_SECTORS+1)){ result(R_FAIL, F("counter claims more than the flash holds")); return; }
  byte r[4];
  if(count){
    flashRead((count-1)*rb, r, 4);
    unsigned long t=*(unsigned long*)r;
    label(F("Last record time")); Serial.print(t); P(" s since 2000");
    if(t==0xFFFFFFFFUL){ Serial.println(); result(R_FAIL, F("last counted record is blank: counter ahead of the data")); }
    else { P(" ("); Serial.print(t/86400L); PL(" days)"); result(R_PASS, F("last counted record holds data")); }
  }
  flashRead(count*rb, r, 4);
  if(*(unsigned long*)r==0xFFFFFFFFUL) result(R_PASS, F("next slot is erased: counter and data agree"));
  else result(R_WARN, F("data beyond the counter: records written after a counter reset, or a lost increment"));
}

// Write/erase on the LAST sector, and only if it is already erased: then the
// test leaves it as it found it, and no logged data can be touched.
void testFlashWrite(){
  section(F("FLASH WRITE/ERASE (last sector)"));
  digitalWrite(MEM_POWER, HIGH);
  delay(2);
  flashWake();
  uint32_t base=MAX_SECTORS*SECTOR_SIZE;
  byte buf[64];
  for(uint32_t a=0; a<SECTOR_SIZE; a+=64){
    flashRead(base+a, buf, 64);
    for(byte i=0;i<64;i++){
      if(buf[i]!=0xFF){
        result(R_FAIL, F("last sector is NOT blank: refusing to write (it may hold log data)"));
        flashSend(0xB9);
        return;
      }
    }
  }
  result(R_PASS, F("last sector blank, safe to test"));

  // Page program: 256 bytes of a pattern that exercises every bit both ways.
  flashSend(0x06);
  if(!(flashStatus(0x05) & 0x02)){ result(R_FAIL, F("write enable did not set WEL")); flashSend(0xB9); return; }
  unsigned long t0=micros();
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x02);
  SPI.transfer(base>>16); SPI.transfer(base>>8); SPI.transfer(base);
  for(uint16_t i=0;i<256;i++) SPI.transfer((byte)(i ^ 0xA5));
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  bool ok=flashWaitReady(20);
  unsigned long tp=micros()-t0;
  uint16_t malos=0;
  for(uint16_t a=0;a<256;a+=64){
    flashRead(base+a, buf, 64);
    for(byte i=0;i<64;i++) if(buf[i]!=(byte)((a+i)^0xA5)) malos++;
  }
  label(F("Page program")); Serial.print(tp); P(" us, "); Serial.print(malos); PL(" bad bytes");
  result((ok && !malos) ? R_PASS : R_FAIL, F("page written and read back"));

  flashSend(0x06);
  t0=millis();
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x20);
  SPI.transfer(base>>16); SPI.transfer(base>>8); SPI.transfer(base);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  ok=flashWaitReady(1000);
  unsigned long te=millis()-t0;
  malos=0;
  for(uint32_t a=0; a<SECTOR_SIZE; a+=64){
    flashRead(base+a, buf, 64);
    for(byte i=0;i<64;i++) if(buf[i]!=0xFF) malos++;
  }
  label(F("Sector erase")); Serial.print(te); P(" ms (45 typ, 400 max), "); Serial.print(malos); PL(" bytes not erased");
  result((ok && !malos) ? R_PASS : R_FAIL, F("sector erased: left blank, as found"));
  flashSend(0xB9);
}
#endif // WITH_BOARD
