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

// Reads the distance into currentDist (cm), or INVALID_DIST. Must run after the
// temperature sensors: it uses the temperature they just read.
void getDistance(){
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
    if(us>0){
      // insertion in order: the median is then the middle element
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
  // distance = time * speed / 2; us * (0.1 m/s) / 2 -> cm: divide by 200000, rounded
  currentDist=(int)((t[n/2]*c10+100000UL)/200000UL);
}

#endif // LOG_HCSR04
