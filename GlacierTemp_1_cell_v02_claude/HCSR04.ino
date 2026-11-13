//##############################################################
//############# HC-SR04P ULTRASONIC DISTANCE (LOG_HCSR04) #####################
//##############################################################
// The sensor takes the four pins of H1: A0 echo, A1 trigger, A2+A3 its supply,
// switched with the PIN_POWER machinery. See the note beside LOG_HCSR04 in the
// main file for why these pins and not D3.

#if LOG_HCSR04

// Port bits of the echo (A0 = PC0) and the trigger (A1 = PC1). Direct port
// access instead of pinMode()/digitalWrite()/pulseInLong(): the same thing in
// some 200 bytes less, on a part that has little more than a kilobyte left.
#define HC_ECHO _BV(0)
#define HC_TRIG _BV(1)

// Pins while nothing is being measured: both outputs held LOW. With the sensor
// unpowered, a pin left high (or floating) would feed it through its own input
// protection; LOW leaves it with nothing to draw.
void hcsr04PinsIdle(){
  PORTC &= ~(HC_TRIG|HC_ECHO);
  DDRC  |=  (HC_TRIG|HC_ECHO);
}

// One ping: a 10 us trigger pulse, then the width of the echo pulse in us, or 0
// if it did not come back within HCSR04_TIMEOUT_US.
//
// Timed with micros(), which MiniCore computes correctly at 7.3728 MHz. Not with
// pulseIn(): that one turns clock cycles into us with F_CPU/1000000 in integers,
// 7 instead of 7.37 at this clock, and every distance would come out 5 % long.
unsigned long hcsr04Ping(){
  PORTC |= HC_TRIG;
  delayMicroseconds(10);
  PORTC &= ~HC_TRIG;
  unsigned long t0=micros();
  while(!(PINC & HC_ECHO)){
    if(micros()-t0 > HCSR04_TIMEOUT_US) return 0;
  }
  t0=micros();
  while(PINC & HC_ECHO){
    if(micros()-t0 > HCSR04_TIMEOUT_US) return 0;
  }
  return micros()-t0;
}

// Reads the distance into currentDist (mm), or INVALID_DIST. Must run after the
// temperature sensors: it uses the temperature they just read.
//
// progress: for the I command and the startup banner. The sensor needs 800 ms
// from power-on and the pings take some 400 ms more; in silence, the app takes
// that pause for the end of the reply (it waits 400-800 ms of quiet) and reports
// that the board did not answer. With progress, a dot every 200 ms keeps the
// line alive.
void getDistance(bool progress){
  while(progress && millis()-analogPowerOnAt+200 < (unsigned long)PIN_POWER_SETTLE_MS){
    delay(200);
    out << '.';
  }
  analogPowerSettle();                 // on since the start of the cycle; waits what is missing
  DDRC  &= ~HC_ECHO;                   // echo: input, pull-up off
  PORTC &= ~HC_ECHO;
  unsigned long t[HCSR04_PINGS];
  byte n=0;
  for(byte i=0; i<HCSR04_PINGS; i++){
    if(i>0){
      delay(HCSR04_PING_GAP_MS);
    }
    unsigned long us=hcsr04Ping();
    if(progress){
      out << '.';
    }
    if(us>0){
      // insertion in order: the extremes are then the first and the last
      byte j=n++;
      while(j>0 && t[j-1]>us){
        t[j]=t[j-1];
        j--;
      }
      t[j]=us;
    }
  }
  analogPowerOff();                    // unless a LIVE session holds it on
  if(!analogPowerIsOn){
    hcsr04PinsIdle();
  }
  if(n<HCSR04_MIN_VALID){
    currentDist=INVALID_DIST;
    logSensorError(ERR_HCSR04);
    return;
  }
  // Speed of sound at the air temperature, in 0.1 m/s: 331.3 + 0.606*T. With
  // both temperatures failed, 0 C (a glacier is closer to it than to 20 C).
  long tc=0;
#if LOG_TMP119
  if(currentTempHA!=INVALID_TEMP_HA){
    tc=currentTempHA;
  }
#if LOG_HDC_TEMP
  else if(currentTemp!=INVALID_TEMP){
    tc=currentTemp;
  }
#endif
#else
  if(currentTemp!=INVALID_TEMP){
    tc=currentTemp;
  }
#endif
  unsigned long c10=3313+(606L*tc)/10000;
  // Trimmed mean (since 3.19): the shortest and the longest echo are dropped
  // and the rest averaged. Like the median it ignores an outlier, and the
  // average also smooths the ~1.5 mm step of micros() (8.7 us at 7.3728 MHz)
  // and the sensor's own jitter, so the millimetre is worth storing. With 3
  // valid echoes it is the median.
  unsigned long sum=0;
  byte k=0;
  for(byte i=1; i+1<n; i++){
    sum+=t[i];
    k++;
  }
  // distance = time * speed / 2; us * (0.1 m/s) / 2 -> mm: divide by 20000,
  // rounded. sum*c10 <= 3*30000*3600 < 2^32.
  currentDist=(int)((sum*c10+10000UL*k)/(20000UL*k));
}

#endif // LOG_HCSR04
