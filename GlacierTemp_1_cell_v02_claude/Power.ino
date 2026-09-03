
//====================== SLEEP-CURRENT DIAGNOSTIC =====================
// Set POWER_DEBUG to 1 to print the exact hardware state at the instant before
// power-down. This is the tool that found the 190uA -> 6uA path on rev02: an
// output stuck high into an unpowered device, a pull-up fighting something
// that holds a pin low, or a sensor that never actually entered shutdown are
// each worth tens to hundreds of microamps and none of them is visible by
// reading the source, because the port registers are the accumulated result of
// every library call that has touched a pin.
//
// It costs nothing while it is 0 -- the compiler drops all of it.
// The switch itself lives in the main .ino, because the IDE appends this tab
// after that file and a #define made here would be invisible to it.
#if POWER_DEBUG
bool isOutputHigh(byte p){
  uint8_t bit=digitalPinToBitMask(p); uint8_t port=digitalPinToPort(p);
  if(port==NOT_A_PIN) return false;
  return (*portModeRegister(port) & bit) && (*portOutputRegister(port) & bit);
}
bool isPulledUpInput(byte p){
  uint8_t bit=digitalPinToBitMask(p); uint8_t port=digitalPinToPort(p);
  if(port==NOT_A_PIN) return false;
  return !(*portModeRegister(port) & bit) && (*portOutputRegister(port) & bit);
}
bool isFloatingInput(byte p){
  uint8_t bit=digitalPinToBitMask(p); uint8_t port=digitalPinToPort(p);
  if(port==NOT_A_PIN) return false;
  return !(*portModeRegister(port) & bit) && !(*portOutputRegister(port) & bit);
}

void dumpPinState(){
  // Serial.print directly, not out: verbose output is switched off before the
  // first sleep and never switched back on.
  Serial.println(F("--- before sleep ---"));
  Serial.print(F("DDRB "));   Serial.print(DDRB,HEX);
  Serial.print(F(" PORTB ")); Serial.print(PORTB,HEX);
  Serial.print(F(" DDRC "));  Serial.print(DDRC,HEX);
  Serial.print(F(" PORTC ")); Serial.print(PORTC,HEX);
  Serial.print(F(" DDRD "));  Serial.print(DDRD,HEX);
  Serial.print(F(" PORTD ")); Serial.println(PORTD,HEX);

  Serial.print(F("out HIGH:"));
  for(byte p=0;p<20;p++){ if(isOutputHigh(p)){ Serial.print(' '); Serial.print(p); } }
  Serial.print(F(" | pullup:"));
  for(byte p=0;p<20;p++){ if(isPulledUpInput(p)){ Serial.print(' '); Serial.print(p); } }
  Serial.print(F(" | floating:"));
  for(byte p=0;p<20;p++){ if(isFloatingInput(p)){ Serial.print(' '); Serial.print(p); } }
  Serial.println();

  // WAKEUP_PIN carries the RTC open-drain INT/SQW against an internal pull-up
  // of only 20-50k. A 0 here means the board is about to sleep while sinking
  // 70-165uA through it, and the status register says why.
  Serial.print(F("INT/SQW="));       Serial.print(digitalRead(WAKEUP_PIN));
  Serial.print(F("  DS3231 ctrl ")); Serial.print(readControlByte(0),HEX);
  Serial.print(F(" status "));       Serial.print(readControlByte(1),HEX);
  Serial.print(F("  ACSR "));        Serial.println(ACSR,HEX);

  // The TMP119 state cannot be inferred from any pin.
  //   MOD 01 shutdown (0.15uA)  00 continuous (~16uA)  11 one-shot
  uint16_t cfg;
  Serial.print(F("TMP119 cfg "));
  if(tmp119ReadReg(TMP119_CONFIG_REG,cfg)){
    Serial.print(cfg,HEX);
    switch((cfg>>10)&0x03){
      case 1:  Serial.println(F(" MOD=01 shutdown, ok"));            break;
      case 3:  Serial.println(F(" MOD=11 one-shot *** not parked")); break;
      default: Serial.println(F(" MOD=00 CONTINUOUS *** ~16uA"));    break;
    }
  }else{
    Serial.println(F("unreadable"));
  }
  Serial.flush();
}
#endif

void powerManagementSetup() {
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);      // red
  digitalWrite(LED_PIN, LOW);
  pinMode(GREEN_LED, OUTPUT);    // green
  digitalWrite(GREEN_LED, LOW);

  // Analog pin measuring battery voltage
  pinMode(BATT_VOLTAGE_PIN, INPUT);
  analogReference(INTERNAL);

  //Pin to read bluetooth status
  pinMode (BLUETOOTH_SATUS_PIN,INPUT);

  //SPI flash memory
  pinMode(FLASH_MEMORY_CS, OUTPUT);
  digitalWrite(FLASH_MEMORY_CS, LOW);  

  // Flash memory power. This pin feeds ONLY the W25Q64 (net MEM_POWER goes to
  // U20 VCC and HOLD# and nowhere else). Both the HDC1080 and the TMP119 are on
  // the permanent 3.3V rail and are NOT affected by this pin.
  pinMode(MEM_POWER, OUTPUT);
  digitalWrite(MEM_POWER, LOW);

  // Unused pins are left as inputs driven low, so nothing floats and nothing
  // leaks. A pin that carries a sensor must NOT be treated this way: the 1-Wire
  // line needs to be released for the DS18B20 and its external pull-up to drive,
  // and an analog input must be a high impedance input with its pull-up off.
  #if !LOG_DS18B20
    pinMode(ONE_WIRE_PIN, INPUT);
    digitalWrite(ONE_WIRE_PIN, LOW);
  #endif
  #if !LOG_A0
    pinMode(A0, INPUT);
    digitalWrite(A0, LOW);
  #endif
  #if !LOG_A1
    pinMode(A1, INPUT);
    digitalWrite(A1, LOW);
  #endif
  #if !LOG_A2
    pinMode(A2, INPUT);
    digitalWrite(A2, LOW);
  #endif
  #if !LOG_A3
    pinMode(A3, INPUT);
    digitalWrite(A3, LOW);
  #endif
  #if ANALOG_CHANNELS
    // Enabled header pins: plain high impedance inputs, pull-up off.
    #if LOG_A0
      pinMode(A0, INPUT);
    #endif
    #if LOG_A1
      pinMode(A1, INPUT);
    #endif
    #if LOG_A2
      pinMode(A2, INPUT);
    #endif
    #if LOG_A3
      pinMode(A3, INPUT);
    #endif
  #endif
}


void goToSleep(){
  // Allow wake up pin to trigger interrupt on low.
  attachInterrupt(digitalPinToInterrupt(WAKEUP_PIN), arduinoWakeUpCallback, LOW);
  delay(SLEEP_DELAY);//Without this delay it misses the first sleep and creates noise in the serial output
  // Enter power down state with ADC and BOD module disabled.
  // Wake up when wake up pin is low.
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
}

void arduinoWakeUpCallback() {
  // Just a handler for the pin interrupt.
  //Serial print  use interrupts, so don't use it here!

}

// Averaged raw ADC count on any pin. Does NOT touch the reference: the caller
// is responsible for having the right one in force and settled, because
// switching it per reading would cost a settling delay on every channel.
int getRawAnalog(byte pin) {
  int stackV[BATT_SAMPLES];
  // The first conversion after the multiplexer or the reference is changed is
  // taken while the input is still settling, so it is read and discarded.
  analogRead(pin);
  for (int i = 0; i < BATT_SAMPLES; i++) {
    stackV[i] = analogRead(pin);
  }
  return mean(stackV,BATT_SAMPLES);
}

int getRawBatteryVoltage() {
  return getRawAnalog(BATT_VOLTAGE_PIN);
}

// Millivolts from a raw count, through the line defined by two calibration
// points. Shared by the battery and by every analog header channel, which use
// the same two-point scheme with their own pair of EEPROM slots.
//
// A pair that has never been set reads back equal counts, which would divide by
// zero, so that case returns the raw count unchanged: visibly wrong, and far
// easier to recognise in a log than a NaN or a random number.
int twoPointMv(int refV1,int refC1,int refV2,int refC2,int count){
  if(refC1==refC2){
    return count;
  }
  float RV1=refV1, RV2=refV2, RC1=refC1, RC2=refC2;
  float slope=(RV2-RV1)/(RC2-RC1);
  float k;
  if(RV1<RV2){
    k=RV1-slope*RC1;
  }else{
    k=RV2-slope*RC2;
  }
  return (slope*count+k);
}

int getBatteryVoltage(){
  // Voltage is calculated using the ecuation of the line throught two points
  // This points are difined by the two reference voltages and corresponding counts values
  // This voltages can be set with the command V1 and V2 and are stored in EEPROM
  return twoPointMv(getInt(REFERENCE_VOLTAGE_1), getInt(REFERENCE_VOLTAGE_COUNT_1),
                    getInt(REFERENCE_VOLTAGE_2), getInt(REFERENCE_VOLTAGE_COUNT_2),
                    getRawBatteryVoltage());
}

// eqPos is where the '=' sits in the command: 2 for "V1=", 3 for "A01=".
void setRefVoltage(char *inputStr, int VOLT_MEM, int VAL_MEM, byte eqPos, int rawCount){
  if(inputStr[eqPos]=='=' && strlen(inputStr)>(unsigned)(eqPos+1)){
    EEPROM.put(VOLT_MEM,(int)(readFloat(inputStr)*1000));
    EEPROM.put(VAL_MEM,rawCount);
  }
  out << getInt(VOLT_MEM) << "mV->" << getInt(VAL_MEM) << NL;
}

#if ANALOG_CHANNELS
//================= EXPANSION HEADER H1 ANALOG CHANNELS ====================
// The header pins are read against the DEFAULT reference (VCC, 3.3 V) while the
// battery divider needs INTERNAL (1.1 V), so the reference is switched around
// the group of header readings rather than around each one. See the long note
// beside LOG_A0..LOG_A3 in the main file for why this costs what it costs.

// EEPROM address of one calibration slot. pin is 0..3, point is 1 or 2. Each
// pin owns four ints: point 1 mV, point 1 count, point 2 mV, point 2 count.
int analogCalAddr(byte pin, byte point, bool wantCount){
  return ANALOG_CAL_ADDR + pin*8 + (point-1)*4 + (wantCount?2:0);
}

void analogPinsBegin(){
  analogReference(DEFAULT);
  delay(ADC_REF_SETTLE_MS);
}

void analogPinsEnd(){
  analogReference(INTERNAL);
  delay(ADC_REF_SETTLE_MS);
}

// Raw count on one header pin, switching the reference around the reading.
// Used by the calibration commands, which read a single pin at a time.
int getRawAnalogPin(byte pin){
  analogPinsBegin();
  int count=getRawAnalog(analogPinNumber(pin));
  analogPinsEnd();
  return count;
}

// Is this header pin switched on in this build?
bool analogPinEnabled(byte pin){
  switch(pin){
    case 0:  return LOG_A0;
    case 1:  return LOG_A1;
    case 2:  return LOG_A2;
    default: return LOG_A3;
  }
}

// The compile-time name given to what is measured on this pin
const __FlashStringHelper* analogPinName(byte pin){
  switch(pin){
    case 0:  return F(A0_NAME);
    case 1:  return F(A1_NAME);
    case 2:  return F(A2_NAME);
    default: return F(A3_NAME);
  }
}

// Arduino pin number for header index 0..3
byte analogPinNumber(byte pin){
  switch(pin){
    case 0:  return A0;
    case 1:  return A1;
    case 2:  return A2;
    default: return A3;
  }
}

// Calibrated millivolts on one header pin. The reference must already be
// switched: this is called from inside the grouped read in takeMeasurement().
int getAnalogMv(byte pin){
  return twoPointMv(getInt(analogCalAddr(pin,1,false)), getInt(analogCalAddr(pin,1,true)),
                    getInt(analogCalAddr(pin,2,false)), getInt(analogCalAddr(pin,2,true)),
                    getRawAnalog(analogPinNumber(pin)));
}

// Handles A01=..., A02=..., A11=... and so on: stores the stated voltage
// against the count the pin reads right now, exactly as V1/V2 do for the cell.
void setAnalogRefVoltage(char *inputStr, byte pin, byte point){
  setRefVoltage(inputStr, analogCalAddr(pin,point,false), analogCalAddr(pin,point,true),
                3, getRawAnalogPin(pin));
}

// Reads every enabled header pin in one reference switch
void readAnalogChannels(){
  analogPinsBegin();
  #if LOG_A0
    currentAnalog[0]=getAnalogMv(0);
  #endif
  #if LOG_A1
    currentAnalog[1]=getAnalogMv(1);
  #endif
  #if LOG_A2
    currentAnalog[2]=getAnalogMv(2);
  #endif
  #if LOG_A3
    currentAnalog[3]=getAnalogMv(3);
  #endif
  analogPinsEnd();
}
#endif // ANALOG_CHANNELS

int getBatteryCapacity(){
  return voltageToCapacity(getBatteryVoltage());
}

// Measurement interval actually used by setWakeUp(). Once the cell drops below
// LOW_VOLTAGE the interval is stretched by LOW_VOLTAGE_INTERVAL_MULTIPLIER, so
// the logger samples less often and the remaining charge lasts longer.
// Dormant while LOW_VOLTAGE is 0 (the default), since nothing can be below it.
unsigned long effectiveMeasureInterval(){
  if(LOW_VOLTAGE>0 && lowVoltageMultiplier>1 && getBatteryVoltage()<=LOW_VOLTAGE){
    unsigned long stretched=measureInterval*(unsigned long)lowVoltageMultiplier;
    if(stretched>86400UL){
      stretched=86400UL;  // never less than one measurement per day
    }
    return stretched;
  }
  return measureInterval;
}

// Remaining capacity of a single AAA alkaline cell (Duracell/Energizer class),
// as a percentage, from its measured terminal voltage in millivolts.
//
// The previous implementation was a 5th order polynomial fitted to a 2-cell
// 8.4V Li-ion pack. A polynomial is a poor fit here: outside the fitted range it
// diverges instead of saturating, and evaluating pow() five times costs both
// flash and time. This is a monotonic breakpoint table with linear interpolation
// between points, which cannot diverge, needs no floating point, and can be
// re-tuned from field data by editing two rows.
//
// Curve basis: alkaline AAA (IEC LR03 / ANSI 24A) at very low drain and ~21degC.
// This logger draws microamps on average with brief milliamp bursts, i.e. far
// below the 25mA lightest published discharge test, so the cell stays close to
// its open-circuit curve: a fast drop from ~1.60V, a long gentle plateau through
// the middle of the discharge, and a knee below ~1.15V.
//
// 0% is pegged at 900mV rather than the 800mV the mAh ratings are quoted to.
// Below ~0.9V the TPS610994 boost converter can no longer reliably hold 3.3V at
// this load, and at low drain an alkaline cell has only a few percent of its
// charge left under 0.9V anyway, so 900mV is the practical end of life.
//
// CAVEAT: this is a nominal room-temperature curve. Alkaline cells lose both
// capacity and terminal voltage badly in the cold, so on a glacier this will
// read pessimistically low while cold, and partly recover when the cell warms.
// Treat it as a coarse fuel gauge, not a measurement.

static const int alkalineMv[]  PROGMEM =
  { 900,1000,1050,1100,1150,1200,1250,1300,1350,1400,1450,1500,1550,1600};
static const int alkalinePct[] PROGMEM =
  {   0,   5,   9,  14,  20,  27,  35,  44,  54,  65,  77,  88,  95, 100};
#define ALKALINE_POINTS (sizeof(alkalineMv)/sizeof(alkalineMv[0]))

int voltageToCapacity(int mV){
  if(mV <= (int)pgm_read_word(&alkalineMv[0])){
    return 0;
  }
  if(mV >= (int)pgm_read_word(&alkalineMv[ALKALINE_POINTS-1])){
    return 100;
  }
  for(byte i=1;i<ALKALINE_POINTS;i++){
    int v1=(int)pgm_read_word(&alkalineMv[i]);
    if(mV <= v1){
      int v0=(int)pgm_read_word(&alkalineMv[i-1]);
      int p0=(int)pgm_read_word(&alkalinePct[i-1]);
      int p1=(int)pgm_read_word(&alkalinePct[i]);
      return p0 + (int)(((long)(mV-v0)*(p1-p0))/(v1-v0));
    }
  }
  return 100;
}

int battDrop(){
  return voltageToCapacity(getUInt(VOLTAGE_RESET_ADDR))-getBatteryCapacity();
}

int battDaysLeft(){
  int drop=battDrop();
  if(drop<=0){
    // No measurable drop yet, so there is no basis for an estimate.
    // Returning here also avoids a division by zero.
    return -1;
  }
  return (getBatteryCapacity()*runningDays())/drop;
}

// void turnOffBTifDisconnected(){
//   if (!bluetoothConnected() && doTurnOffBluetooth){
//     // Bluetooth is disconnected or needs to be turned off
//     out << F("BT OFF\n");
//     Serial.flush();
//     digitalWrite(BLUETOOTH_POWER_PIN, BLUETOOTH_OFF);
//   }
// }

bool bluetoothConnected(){
  return digitalRead(BLUETOOTH_SATUS_PIN)==HIGH;
}
