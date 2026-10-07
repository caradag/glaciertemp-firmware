//====================== MCU, SUPPLY, EEPROM, PINS ==========================

void testMcu(){
  section(F("MCU"));
  byte s0=boot_signature_byte_get(0), s1=boot_signature_byte_get(2), s2=boot_signature_byte_get(4);
  label(F("Signature")); hex2(s0); P(" "); hex2(s1); P(" "); hex2(s2); Serial.println();
  if(s0==0x1E && s1==0x95 && s2==0x0F)      result(R_PASS, F("ATmega328P, as fitted"));
  else if(s0==0x1E && s1==0x95 && s2==0x14) result(R_WARN, F("ATmega328 (non-P): runs, but sleep current is higher"));
  else                                      result(R_FAIL, F("not an ATmega328P: wrong chip or wrong board setting"));

  byte lf=boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
  byte hf=boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
  byte ef=boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
  label(F("Fuses low/high/ext")); hex2(lf); P(" "); hex2(hf); P(" "); hex2(ef); Serial.println();
  // BODLEVEL is ext[2:0]: 101 = 2.7 V, 110 = 1.8 V, 111 = disabled
  label(F("Brown-out level"));
  switch(ef & 0x07){
    case 0x05: PL("2.7 V"); result(R_PASS, F("BOD 2.7V as the board settings ask")); break;
    case 0x06: PL("1.8 V"); result(R_WARN, F("BOD 1.8V: the flash may be written below its minimum voltage")); break;
    case 0x07: PL("disabled"); result(R_WARN, F("BOD off: a sagging cell can corrupt the flash or EEPROM")); break;
    default:   PL("4.3 V"); result(R_FAIL, F("BOD 4.3V on a 3.3V board: the chip is held in reset")); break;
  }

  label(F("F_CPU (as built)")); Serial.print(F_CPU); PL(" Hz");
  if(F_CPU==7372800UL) result(R_PASS, F("built for the 7.3728 MHz crystal"));
  else                 result(R_FAIL, F("NOT built for 7.3728 MHz: baud rates and timing are off"));

  label(F("Reset cause (MCUSR)")); hex2(mcusrAtBoot);
  if(mcusrAtBoot & 0x08) P(" watchdog");
  if(mcusrAtBoot & 0x04) P(" brown-out");
  if(mcusrAtBoot & 0x02) P(" external");
  if(mcusrAtBoot & 0x01) P(" power-on");
  if(!mcusrAtBoot)       P(" (cleared by the bootloader)");
  Serial.println();
  if(mcusrAtBoot & 0x04) result(R_WARN, F("last reset was a brown-out: check the cell and the boost converter"));

  extern int __heap_start, *__brkval;
  int v;
  int libre=(int)&v - (__brkval==0 ? (int)&__heap_start : (int)__brkval);
  label(F("Free RAM now")); Serial.print(libre); PL(" bytes");
  PL("  (MCU crystal against the RTC: see RTCSQW)");
}

// The 3.3 V rail, measured as the 1.1 V bandgap against AVCC. The bandgap is
// only specified to +-0.1 V, so this is +-10 %: enough to see a dead boost
// converter or a rail far off, not to calibrate anything.
long readVccMv(){
  ADMUX=_BV(REFS0) | 0x0E;   // AVCC reference, bandgap input
  adcRef=255;                // the next analogRead() rewrites ADMUX
  delay(5);
  long s=0;
  for(byte i=0;i<17;i++){
    ADCSRA|=_BV(ADSC);
    while(ADCSRA & _BV(ADSC));
    if(i) s+=ADC;            // first one discarded
  }
  long raw=s/16;
  return raw ? 1100L*1023L/raw : 0;
}

void testPower(){
  section(F("POWER"));
  long vcc=readVccMv();
  label(F("3.3V rail (bandgap)")); Serial.print(vcc); PL(" mV  (+-10 %)");
  if(vcc>2900 && vcc<3700) result(R_PASS, F("rail present and near 3.3V"));
  else                     result(R_FAIL, F("rail far from 3.3V: check the boost converter"));

  useRef(INTERNAL);
  float raw=adcMean(BATT_VOLTAGE_PIN, BATT_SAMPLES);
  label(F("Battery, raw count")); Serial.println(raw, 1);
  int v1=eeInt(REFERENCE_VOLTAGE_1), c1=eeInt(REFERENCE_VOLTAGE_COUNT_1);
  int v2=eeInt(REFERENCE_VOLTAGE_2), c2=eeInt(REFERENCE_VOLTAGE_COUNT_2);
  label(F("Calibration V1/V2")); Serial.print(v1); P("mV@"); Serial.print(c1);
  P("  "); Serial.print(v2); P("mV@"); Serial.println(c2);
  long mv=twoPointMv(v1,c1,v2,c2,raw);
  // Nominal divider 10M/2M against 1.1 V: one count is 6.45 mV of cell.
  long nominal=(long)(raw*1100.0*6.0/1023.0);
  label(F("Battery (nominal div.)")); Serial.print(nominal); PL(" mV");
  if(mv<0){
    result(R_WARN, F("battery calibration not set (V1/V2): the logger logs raw counts"));
  }else{
    label(F("Battery (calibrated)")); Serial.print(mv); PL(" mV");
    if(labs(mv-nominal) > nominal/5)
      result(R_WARN, F("calibration and nominal divider disagree by >20 %: check V1/V2"));
    else
      result(R_PASS, F("battery calibration consistent with the divider"));
    if(mv<900)       result(R_FAIL, F("cell below 0.9 V: replace it"));
    else if(mv<1150) result(R_WARN, F("cell low (<1.15 V)"));
    else             result(R_PASS, F("cell voltage fine"));
  }
}

#if WITH_BOARD
//-------------------------------- EEPROM ------------------------------------
void testEeprom(){
  section(F("EEPROM (logger configuration, read only)"));
  unsigned long interval=eeULong(INTERVAL_EE_ADDR);
  label(F("Measurement interval")); Serial.print(interval); PL(" s");
  if(interval==0 || interval>86400UL) result(R_WARN, F("interval out of range: the logger falls back to 600 s"));
  int tz=eeInt(TIMEZONE_EE_ADDR);
  label(F("Time zone")); Serial.println(tz);
  if(tz<-12 || tz>14) result(R_WARN, F("time zone out of range: the logger falls back to UTC"));

  unsigned long full=0;
  bool blanco=false;
  for(byte i=0;i<COUNTERS_SLOTS;i++){
    unsigned long c=eeULong(COUNT_ADDR+4*i);
    if(c==0xFFFFFFFFUL) blanco=true;
    else full+=c;
  }
  unsigned long reset=eeULong(COUNT_RESET_ADDR);
  label(F("Records since reset")); Serial.println(full-reset);
  if(blanco) result(R_WARN, F("counter slots never initialised: run the initializer sketch"));

  for(byte pin=0;pin<4;pin++){
    int a=ANALOG_CAL_ADDR+pin*8;
    int v1=eeInt(a), c1=eeInt(a+2), v2=eeInt(a+4), c2=eeInt(a+6);
    P("  A"); Serial.print(pin); P(" calibration ........... ");
    if(c1==c2 || c1==-1) PL("not set");
    else { Serial.print(v1); P("mV@"); Serial.print(c1); P("  "); Serial.print(v2); P("mV@"); Serial.println(c2); }
  }
  uint16_t sig=(uint16_t)eeInt(LOG_SIGNATURE_ADDR);
  label(F("Log signature")); P("0x"); hex4(sig);
  if(sig==0xFFFF){ PL("  (no log started)"); }
  else{ P("  "); printSignature(sig); Serial.println(); }
}

// The channels a log signature declares, as the logger defines its bits.
void printSignature(uint16_t sig){
  P("v"); Serial.print(sig>>12); P(":");
  if(sig & 0x0001) P(" Volt");
  if(sig & 0x0002) P(" HDCtemp");
  if(sig & 0x0004) P(" RH");
  if(sig & 0x0008) P(" TMP119");
  if(sig & 0x0010){ P(" DS18B20x"); Serial.print(((sig>>9)&7)+1); }
  for(byte i=0;i<4;i++) if(sig & (0x0020<<i)){ P(" A"); Serial.print(i); }
}

// Bytes per record a signature implies: 4 for the time plus 2 per channel, and 2
// more for the milliseconds of a CONT log (format version 2).
byte recordBytes(uint16_t sig){
  byte n=0;
  if(sig & 0x0001) n++;
  if(sig & 0x0002) n++;
  if(sig & 0x0004) n++;
  if(sig & 0x0008) n++;
  if(sig & 0x0010) n+=((sig>>9)&7)+1;
  for(byte i=0;i<4;i++) if(sig & (0x0020<<i)) n++;
  return ((sig>>12)==2 ? 6 : 4)+2*n;
}


//--------------------------------- PINS -------------------------------------
// What every pin is doing right now. In the logger this is what decides the
// sleep current: an output high into an unpowered part, a pull-up fighting a
// pull-down, or a floating input each cost tens to hundreds of microamps.
void testPins(){
  section(F("PINS"));
  P("  DDRB "); hex2(DDRB); P(" PORTB "); hex2(PORTB); P(" PINB "); hex2(PINB);
  P("  DDRC "); hex2(DDRC); P(" PORTC "); hex2(PORTC); P(" PINC "); hex2(PINC);
  P("  DDRD "); hex2(DDRD); P(" PORTD "); hex2(PORTD); P(" PIND "); hex2(PIND);
  P("  DIDR0 "); hex2(DIDR0); Serial.println();
  for(byte p=0;p<20;p++){
    uint8_t bit=digitalPinToBitMask(p);
    uint8_t port=digitalPinToPort(p);
    bool out=*portModeRegister(port) & bit;
    bool hi=*portOutputRegister(port) & bit;
    P("  D"); Serial.print(p); if(p<10) P(" ");
    if(out) P("  output "); else if(hi) P("  in+pull"); else P("  input  ");
    P("  reads "); Serial.print(digitalRead(p));
    if(p==WAKEUP_PIN)          P("   RTC INT/SQW (low = alarm pending)");
    if(p==ONE_WIRE_PIN)        P("   1-Wire");
    if(p==MEM_POWER)           P("   flash supply");
    if(p==GREEN_LED)           P("   green LED");
    if(p==FLASH_MEMORY_CS)     P("   flash CS");
    if(p==BLUETOOTH_SATUS_PIN) P("   BT STATE");
    if(p==LED_PIN)             P("   red LED");
    if(p>=14 && p<=17){ P("   A"); Serial.print(p-14); P(" header"); }
    if(p==18 || p==19)         P("   I2C");
    Serial.println();
  }
  if(!digitalRead(WAKEUP_PIN))
    result(R_WARN, F("INT/SQW is LOW: an RTC alarm flag is set; asleep this sinks ~100 uA"));
  if(!digitalRead(18) || !digitalRead(19))
    result(R_FAIL, F("SDA or SCL held low: the I2C bus is stuck"));
}

void testLeds(){
  section(F("LEDS"));
  PL("  Green blinks 3 times, then red 3 times. Watch the board.");
  for(byte led=0; led<2; led++){
    byte p=led ? LED_PIN : GREEN_LED;
    for(byte i=0;i<3;i++){ digitalWrite(p, HIGH); delay(250); digitalWrite(p, LOW); delay(250); }
  }
  result(R_INFO, F("no automatic check: the LEDs are judged by eye"));
}

void testBluetooth(){
  section(F("BLUETOOTH"));
  byte altos=0;
  for(byte i=0;i<20;i++){ altos+=digitalRead(BLUETOOTH_SATUS_PIN); delay(5); }
  label(F("STATE pin high")); Serial.print(altos); PL(" of 20 reads");
  if(altos==20)     result(R_INFO, F("module reports a connection"));
  else if(altos==0) result(R_INFO, F("no connection (or no module fitted)"));
  else              result(R_INFO, F("STATE blinking: module advertising, not connected"));
}

#endif // WITH_BOARD

#if WITH_SENSORS
//-------------------------------- TIMING ------------------------------------
// How long each part of a measurement keeps the logger awake. This is also
// the time a switched sensor gets to settle for free: the logger powers it at
// the start of the cycle, and these readings happen before the analog pins.
void testTiming(){
  section(F("TIMING (per logger measurement)"));
  unsigned long t=millis();
  bool okH=hdcRead(NULL, NULL);
  unsigned long tH=millis()-t;
  t=millis();
  bool okT=tmpOneShot(0x0C20, NULL, NULL);
  unsigned long tT=millis()-t;
  tmpWrite(TMP119_CONFIG_REG, TMP119_SHUTDOWN);
  adcRef=255;
  t=millis();
  useRef(INTERNAL);
  adcMean(BATT_VOLTAGE_PIN, BATT_SAMPLES);
  unsigned long tB=millis()-t;
  t=millis();
  useRef(DEFAULT);
  adcMean(A0, BATT_SAMPLES);
  unsigned long tA=millis()-t;
  label(F("HDC1080 read")); Serial.print(tH); P(" ms"); if(!okH) P("  (no answer)"); Serial.println();
  label(F("TMP119 read, 8 avg")); Serial.print(tT); P(" ms"); if(!okT) P("  (no answer)"); Serial.println();
  label(F("Battery (ref switch)")); Serial.print(tB); PL(" ms");
  label(F("Analog pin (ref switch)")); Serial.print(tA); PL(" ms");
  label(F("Before the analog pins")); Serial.print(tH+tT+tB); PL(" ms");
  PL("  A switched sensor needing less than the last figure costs no extra");
  PL("  awake time: the logger powers it on before these readings.");
}

#endif // WITH_SENSORS

#if WITH_BOARD
//--------------------------------- SLEEP ------------------------------------
// Puts the board in the state the logger sleeps in and powers down until an
// RTC alarm, so the sleep current can be read on a meter. The RTC wakes it,
// as in the logger: a watchdog wake-up would add ~4 uA of its own and spoil
// exactly the figure being measured.
void testSleep(long secs){
  section(F("SLEEP"));
  if(secs<5) secs=5;
  if(secs>3600) secs=3600;
  if(!i2cPresent(CLOCK_ADDRESS)){ result(R_FAIL, F("no RTC: nothing would wake the board")); return; }

  // Logger sleep state: sensors parked, flash in deep power-down with its
  // supply on and CS high, header and 1-Wire pins as grounded inputs, LEDs off.
  tmpWrite(TMP119_CONFIG_REG, TMP119_SHUTDOWN);
  flashSend(0xB9);
  for(byte i=0;i<4;i++){ pinMode(A0+i, INPUT); digitalWrite(A0+i, LOW); }
  pinMode(ONE_WIRE_PIN, INPUT); digitalWrite(ONE_WIRE_PIN, LOW);
  digitalWrite(LED_PIN, LOW); digitalWrite(GREEN_LED, LOW);

  byte saved[4], ctrl;
  rtcReadRegs(0x07, saved, 4);
  ctrl=rtcReadReg(0x0E);
  byte t[3];
  rtcReadRegs(0x00, t, 3);
  long s=bcd(t[0]&0x7F) + 60L*bcd(t[1]&0x7F) + 3600L*bcd(t[2]&0x3F) + secs;
  s%=86400L;
  // Alarm 1 on seconds, minutes and hours (A1M4 set: any day).
  byte a[4]={toBcd(s%60), toBcd((s/60)%60), toBcd(s/3600), 0x80};
  rtcWriteRegs(0x07, a, 4);
  rtcWriteReg(0x0F, rtcReadReg(0x0F) & ~0x03);
  rtcWriteReg(0x0E, (ctrl | 0x05) & ~0x40);   // INTCN, A1IE, no BBSQW

  P("  Sleeping "); Serial.print(secs); PL(" s. Read the current now.");
  P("  (In series with the AAA cell, the reading is ~2.2x the 3.3V rail current;");
  PL(" a healthy rev02 reads ~6 uA there.)");
  Serial.flush();
  attachInterrupt(digitalPinToInterrupt(WAKEUP_PIN), wakeIsr, LOW);
  unsigned long t0=millis();
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
  detachInterrupt(digitalPinToInterrupt(WAKEUP_PIN));

  rtcWriteReg(0x0F, rtcReadReg(0x0F) & ~0x03);
  rtcWriteRegs(0x07, saved, 4);
  rtcWriteReg(0x0E, ctrl);
  flashWake();
  adcRef=255;
  PL("  Awake again (woken by the RTC alarm).");
  P("  millis() advanced "); Serial.print(millis()-t0); PL(" ms (Timer0 stops in power-down).");
  result(R_PASS, F("power-down and RTC wake-up work"));
}

void wakeIsr(){}
#endif // WITH_BOARD
