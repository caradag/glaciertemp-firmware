#if WITH_SENSORS
//===================== HEADER A0..A3, SETTLE TIME, STEP ====================

// mV on header pin p from a count against VCC: through the logger's stored
// calibration if there is one, otherwise nominally (3300 mV full scale).
long headerMv(byte p, float count, bool* calibrated){
  int a=ANALOG_CAL_ADDR+p*8;
  long mv=twoPointMv(eeInt(a), eeInt(a+2), eeInt(a+4), eeInt(a+6), count);
  if(calibrated) *calibrated=(mv>=0);
  return mv>=0 ? mv : (long)(count*3300.0/1023.0);
}

void testAnalog(){
  section(F("HEADER A0..A3"));
  PL("  Each pin as a plain input, then with the internal pull-up for a moment:");
  PL("  a pin nothing drives jumps to the rail with the pull-up; a pin a sensor");
  PL("  drives barely moves.");
  PL("  pin  count(VCC) sd     mV          count(1.1V)  with pull-up  verdict");
  for(byte p=0;p<4;p++){
    byte pin=A0+p;
    pinMode(pin, INPUT); digitalWrite(pin, LOW);
    delay(5);
    useRef(DEFAULT);
    Stat s; s.clear();
    analogRead(pin);
    for(byte i=0;i<32;i++) s.add(analogRead(pin));
    useRef(INTERNAL);
    float ci=adcMean(pin, BATT_SAMPLES);
    useRef(DEFAULT);
    pinMode(pin, INPUT_PULLUP);
    delay(3);
    float cp=adcMean(pin, 8);
    pinMode(pin, INPUT); digitalWrite(pin, LOW);
    bool cal;
    long mv=headerMv(p, s.mean, &cal);
    P("  A"); Serial.print(p);
    P("   "); pad(s.mean, 7, 1);
    P("  "); pad(s.sd(), 5, 2);
    P("  "); pad(mv, 5, 0); if(cal) P(" mV cal"); else P(" mV nom");
    P("  "); pad(ci, 8, 1);
    P("      "); pad(cp, 7, 1);
    float salto=cp-s.mean;
    if(salto > 300)      P("     open / not driven");
    else if(salto < 20)  P("     driven (sensor?)");
    else                 P("     high impedance");
    Serial.println();
  }
  PL("  'cal' uses the logger's A01/A02 points; 'nom' assumes a 3.3 V full scale.");
  PL("  A 1.1 V count of 1023 just means the pin is above 1.1 V.");
}

//------------------------------- SETTLE -------------------------------------
// How long a sensor powered from header pins needs before its output can be
// trusted. Procedure, for each candidate time t:
//   power off, wait for the sensor to discharge, power on, wait t, read the pin
//   exactly as the logger does (one discarded conversion + mean of 21)
// repeated several times, against a reference taken after 3 s of power. The
// shortest t from which every longer t also lands within tolerance of the
// reference is the value for Ax_SETTLE_MS.
#define SETTLE_REPS 5
#define SETTLE_OFF_MS 1000

void settleOff(byte mask){ PORTC &= ~mask; }
void settleOn(byte mask){ PORTC |= mask; }

bool settleArgsOk(byte s, byte mask){
  if(s>3){ result(R_FAIL, F("sensor pin must be 0..3")); return false; }
  if(mask & (1<<s)){ result(R_FAIL, F("the sensor pin cannot also be a power pin")); return false; }
  if(!mask){ result(R_FAIL, F("no power pins given")); return false; }
  return true;
}

void printMask(byte mask){
  bool primero=true;
  for(byte i=0;i<4;i++) if(mask & (1<<i)){ if(!primero) P("+"); P("A"); Serial.print(i); primero=false; }
}

void testSettle(long s, byte mask){
  section(F("SETTLE TIME SWEEP"));
  if(!settleArgsOk(s, mask)) return;
  byte pin=A0+s;
  P("  Sensor read on A"); Serial.print(s); P(", powered from "); printMask(mask);
  P(". "); Serial.print(SETTLE_REPS); P(" trials per time, "); Serial.print(SETTLE_OFF_MS); PL(" ms off between trials.");
  PL("  Takes about 100 s. Do not touch the sensor or its wiring meanwhile.");
  pinMode(pin, INPUT); digitalWrite(pin, LOW);
  settleOff(mask);
  DDRC |= mask;
  useRef(DEFAULT);

  // Supply actually reaching the sensor: the power pin itself, read by the ADC
  // while it drives. Against VCC, so the ratio is exact whatever VCC is. Read
  // twice: at 50 ms, during the sensor's start-up, and after 3 s, when it draws
  // its steady current -- the second is the one that sets the gain error.
  settleOn(mask);
  delay(50);
  byte p0=0; while(!(mask & (1<<p0))) p0++;
  float cpow50=adcMean(A0+p0, BATT_SAMPLES);
  delay(2950);
  Stat ref; ref.clear();
  for(byte i=0;i<16;i++) ref.add(adcMean(pin, BATT_SAMPLES));
  float cpow=adcMean(A0+p0, BATT_SAMPLES);
  settleOff(mask);
  long vcc=readVccMv();
  useRef(DEFAULT);
  float caida=(1.0-cpow/1023.0)*100.0;
  label(F("Supply, at 50 ms")); Serial.print(cpow50, 1); P(" counts = "); Serial.print(cpow50*100.0/1023.0, 2); PL(" % of VCC");
  label(F("Supply, steady (3 s)")); Serial.print(cpow, 1); P(" counts = "); Serial.print(100.0-caida, 2);
  P(" % of VCC (drop ~"); Serial.print(caida*vcc/100.0, 0); PL(" mV)");
  P("  Ratiometric sensor: the steady drop is a gain error of "); Serial.print(caida, 2); PL(" %.");
  bool cal;
  label(F("Reference after 3 s")); Serial.print(ref.mean, 2); P(" counts, sd "); Serial.print(ref.sd(), 2);
  P("  = "); Serial.print(headerMv(s, ref.mean, &cal)); if(cal) PL(" mV (cal)"); else PL(" mV (nominal)");
  float tol=ref.sd()*3.0; if(tol<1.0) tol=1.0;
  label(F("Tolerance")); Serial.print(tol, 2); PL(" counts (3 sd of the reference, at least 1)");
  // A reference at a rail cannot tell "settled" from "dead": 0 V is also what a
  // sensor with no output, a broken wire or an output clamped by a fault reads.
  // The sweep still runs -- the start-up transient is worth seeing -- but its
  // verdict is not trusted.
  bool enRiel=(ref.mean<=3.0 || ref.mean>=1020.0);
  if(enRiel){
    result(R_WARN, F("reference at a rail (0 V or VCC): settled and dead look the same"));
    PL("  Put the sensor where its output is mid-range (e.g. under water) and repeat.");
  }

  // 120 and 150 bracket what the logger already spends on the I2C sensors
  // before the analog pins (~150 ms, see TIMING): settling inside that is free.
  const unsigned int tiempos[]={0, 1, 2, 5, 10, 20, 50, 100, 120, 150, 200, 300, 500, 1000, 2000};
  const byte nt=sizeof(tiempos)/sizeof(tiempos[0]);
  float medias[nt], peores[nt];
  PL("\n    t ms   mean count   dev count   worst dev   within");
  for(byte k=0;k<nt;k++){
    Stat d; d.clear();
    float peor=0;
    for(byte r=0;r<SETTLE_REPS;r++){
      settleOff(mask);
      delay(SETTLE_OFF_MS);
      settleOn(mask);
      delay(tiempos[k]);
      float c=adcMean(pin, BATT_SAMPLES);
      settleOff(mask);
      d.add(c);
      if(fabs(c-ref.mean)>fabs(peor)) peor=c-ref.mean;
    }
    medias[k]=d.mean; peores[k]=peor;
    P("  "); pad(tiempos[k], 6, 0);
    P("   "); pad(d.mean, 10, 2);
    P("  "); pad(d.mean-ref.mean, 10, 2);
    P("  "); pad(peor, 10, 2);
    if(fabs(peor)<=tol) P("     yes"); else P("     no");
    Serial.println();
  }
  // The shortest time from which every longer one is also within tolerance.
  int elegido=-1;
  for(int k=nt-1;k>=0;k--){
    if(fabs(peores[k])<=tol) elegido=k; else break;
  }
  if(elegido<0){
    result(R_FAIL, F("never within tolerance, even at 2 s: the sensor drifts, or the power pins cannot carry it"));
  }else{
    P("  Shortest settle within tolerance: "); Serial.print(tiempos[elegido]); PL(" ms.");
    P("  Suggested Ax_SETTLE_MS: "); Serial.print(tiempos[elegido]*2 < 10 ? 10 : tiempos[elegido]*2);
    PL(" (twice that, for margin and cold).");
    if(enRiel) result(R_WARN, F("settle time NOT confirmed: the reference sits at a rail"));
    else       result(R_PASS, F("settle time found"));
  }
  PL("  The logger powers the sensor before reading the I2C sensors (see TIMING),");
  PL("  so only the part of the settle time beyond those readings costs awake time.");
  DDRC &= ~mask;
  (void)medias;
}

// The sensor's output against time after power-on: single conversions at
// fixed instants, stored and printed afterwards (printing would set the pace).
void testStep(long s, byte mask){
  section(F("STEP RESPONSE"));
  if(!settleArgsOk(s, mask)) return;
  byte pin=A0+s;
  const unsigned int ms[]={0,1,2,3,5,7,10,15,20,30,50,70,100,150,200,300,500,700,1000,1500,2000,3000};
  const byte n=sizeof(ms)/sizeof(ms[0]);
  int cuentas[n];
  unsigned long reales[n];
  pinMode(pin, INPUT); digitalWrite(pin, LOW);
  settleOff(mask);
  DDRC |= mask;
  useRef(DEFAULT);
  delay(SETTLE_OFF_MS);
  analogRead(pin);
  unsigned long t0=micros();
  settleOn(mask);
  for(byte k=0;k<n;k++){
    while(micros()-t0 < (unsigned long)ms[k]*1000UL);
    reales[k]=micros()-t0;
    cuentas[k]=analogRead(pin);
  }
  settleOff(mask);
  DDRC &= ~mask;
  P("  A"); Serial.print(s); P(" after powering "); printMask(mask); PL(" (single conversions):");
  PL("     t ms    count     mV");
  for(byte k=0;k<n;k++){
    P("  "); pad(reales[k]/1000.0, 7, 2);
    P("  "); pad(cuentas[k], 6, 0);
    P("  "); pad(headerMv(s, cuentas[k], NULL), 6, 0);
    Serial.println();
  }
  result(R_INFO, F("where the curve flattens is the settle time; SETTLE measures it with averaging"));
}
//--------------------------------- PWR --------------------------------------
// Holds the power pins on and reports the sensor once a second, until a key is
// typed or the time runs out: for checking the sensor with a multimeter, or
// watching it while it goes into the bucket.
void testSensorPower(long s, byte mask, long secs){
  section(F("SENSOR POWER ON"));
  if(!settleArgsOk(s, mask)) return;
  if(secs<1) secs=1;
  if(secs>600) secs=600;
  byte pin=A0+s;
  byte p0=0; while(!(mask & (1<<p0))) p0++;
  pinMode(pin, INPUT); digitalWrite(pin, LOW);
  DDRC |= mask;
  settleOn(mask);
  useRef(DEFAULT);
  P("  "); printMask(mask); P(" on, reading A"); Serial.print(s); P(" every second for up to ");
  Serial.print(secs); PL(" s. Any key stops.");
  while(Serial.available()) Serial.read();
  PL("     s   sensor count     mV   supply % VCC");
  for(long t=0; t<secs; t++){
    unsigned long t0=millis();
    float c=adcMean(pin, BATT_SAMPLES);
    float v=adcMean(A0+p0, BATT_SAMPLES);
    P("  "); pad(t, 4, 0);
    P("   "); pad(c, 10, 1);
    P("   "); pad(headerMv(s, c, NULL), 5, 0);
    P("   "); pad(v*100.0/1023.0, 8, 2);
    Serial.println();
    bool parar=false;
    while(millis()-t0 < 1000){ if(Serial.available()){ parar=true; break; } }
    if(parar) break;
  }
  while(Serial.available()) Serial.read();
  settleOff(mask);
  DDRC &= ~mask;
  PL("  Power off.");
}
#endif // WITH_SENSORS
