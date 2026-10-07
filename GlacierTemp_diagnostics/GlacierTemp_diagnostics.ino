// ===========================================================================
// GlacierTemp 1-cell rev02 -- DIAGNOSTICS FIRMWARE
// ===========================================================================
// REQUIRED BOARD SETTINGS (Tools menu / FQBN) -- the same as the logger:
//   Board .......... MiniCore -> ATmega328
//   Variant ........ 328P / 328PA
//   Clock .......... External 7.3728 MHz     <-- NOT 8 MHz, and NOT 16 MHz
//   BOD ............ 2.7V
//   Bootloader ..... Yes (UART0)
// Console: 115200 baud, any line ending. Arduino IDE 2.x keeps the board
// selection PER SKETCH: setting it on the logger does not set it here.
//
// WHAT THIS IS. A separate sketch that does nothing but test the board and the
// sensors on it, verbosely: every test the logger runs at start-up and more,
// each one saying what it measured, what it expected and why it matters. It is
// flashed for a bench check and replaced by the logger afterwards.
//
// WHAT IT LEAVES BEHIND. Nothing the logger depends on:
//   - the internal EEPROM is only READ (configuration, calibration, counters);
//   - the flash log is only READ. The one write test (FLASHRW) uses the LAST
//     sector and runs only if that sector is already erased, so it leaves it
//     exactly as it found it;
//   - the RTC time is never set. The square-wave and alarm tests change the
//     control and alarm registers and put back what they found;
//   - the TMP119 is parked in shutdown and the flash in deep power-down, which
//     is how the logger leaves them between measurements.
//
// Type HELP for the list of commands. ALL runs every non-destructive test.
// ===========================================================================

#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <avr/boot.h>
#include "LowPower.h"

#define DIAG_VERSION "1.1"
#define BAUDRATE 115200

//------------------------------ WHICH TESTS ---------------------------------
// Everything does not fit: the full set, verbose as it is, is about 48 kB and
// the ATmega328P holds 32 kB. So the tests come in two groups, chosen here,
// and the board is flashed once with each when both are wanted. MCU, POWER
// and I2C are in both.
//   DIAG_BOARD    EEPROM, RTC (time, square wave, alarm), FLASH, FLASHRW,
//                 DS18B20, SLEEP, PINS, LEDS, BT
//   DIAG_SENSORS  HDC1080, TMP119 (and noise vs averaging), A0..A3, SETTLE,
//                 STEP, PWR, TIMING
#define DIAG_BOARD   1
#define DIAG_SENSORS 2
#define DIAG_MODE DIAG_SENSORS
#define WITH_BOARD   (DIAG_MODE==DIAG_BOARD)
#define WITH_SENSORS (DIAG_MODE==DIAG_SENSORS)
#if !WITH_BOARD && !WITH_SENSORS
  #error "DIAG_MODE must be DIAG_BOARD or DIAG_SENSORS"
#endif

//------------------------------- BOARD PINOUT -------------------------------
// These MUST match the logger (GlacierTemp_1_cell_v02_claude.ino). They are
// copied rather than shared because a sketch cannot include another sketch's
// tabs; test/check_diag_consts.py compares the two and fails on any mismatch.
#define WAKEUP_PIN 2           // DS3231 INT/SQW, open drain, needs the pull-up
#define ONE_WIRE_PIN 3         // 1-Wire header, DS18B20 (external 4.7k pull-up)
#define MEM_POWER 4            // PD4, the W25Q64's ONLY supply
#define GREEN_LED 5
#define FLASH_MEMORY_CS 6      // 47k pull-up to 3.3V on the board (R26)
#define BLUETOOTH_SATUS_PIN 7  // HM-10 STATE
#define LED_PIN 10             // red
#define BATT_VOLTAGE_PIN A6    // cell through the 10M/2M divider

#define HDC1080_ADDR 0x40
#define TMP119_ADDR 0x48
#define CLOCK_ADDRESS 0x68
#define TMP119_TEMP_REG   0x00
#define TMP119_CONFIG_REG 0x01
#define TMP119_SHUTDOWN   0x0420

#define BATT_SAMPLES 21        // the logger averages this many conversions
#define ADC_REF_SETTLE_MS 5

#define SECTOR_SIZE 4096UL
#define MAX_SECTORS 2047UL

//------------------------------ EEPROM MAP ----------------------------------
// From the logger's initializer. Read only.
#define COUNT_ADDR 0
#define COUNTERS_SLOTS 30
#define COUNT_RESET_ADDR 120
#define RESET_TIME_ADDR 124
#define REFERENCE_VOLTAGE_1 139
#define REFERENCE_VOLTAGE_COUNT_1 141
#define REFERENCE_VOLTAGE_2 143
#define REFERENCE_VOLTAGE_COUNT_2 145
#define ANALOG_CAL_ADDR 147
#define LOG_SIGNATURE_ADDR 179
#define INTERVAL_EE_ADDR 181   // INT, unsigned long, seconds
#define TIMEZONE_EE_ADDR 254   // TZN, int, hours

//----------------------- SWITCHED SENSOR POWER (SETTLE) ---------------------
// The same masks as the logger's PIN_POWER: bit n is An, which is PCn.
#define PIN_POWER_A0 0x01
#define PIN_POWER_A1 0x02
#define PIN_POWER_A2 0x04
#define PIN_POWER_A3 0x08
// Defaults for SETTLE and STEP when they are typed with no arguments: the
// sensor read on A0, powered from A1, A2 and A3 in parallel.
#define DIAG_SENSOR_PIN 0
#define DIAG_POWER_MASK (PIN_POWER_A1|PIN_POWER_A2|PIN_POWER_A3)

#define P(x)  Serial.print(F(x))
#define PL(x) Serial.println(F(x))

//------------------------------- RESULTS ------------------------------------
// Every check ends in one of these, and ALL counts them for the summary.
#define R_PASS 0
#define R_WARN 1
#define R_FAIL 2
#define R_INFO 3
unsigned int nPass=0, nWarn=0, nFail=0;

// Running mean, standard deviation, minimum and maximum without keeping the
// samples (Welford): there are 2 kB of RAM.
struct Stat {
  unsigned int n;
  float mean, m2, mn, mx;
  void clear(){ n=0; mean=0; m2=0; mn=1e30; mx=-1e30; }
  void add(float x){
    n++;
    float d=x-mean;
    mean+=d/n;
    m2+=d*(x-mean);
    if(x<mn) mn=x;
    if(x>mx) mx=x;
  }
  float sd() const { return n>1 ? sqrt(m2/(n-1)) : 0; }
};

byte mcusrAtBoot;
byte adcRef=255;            // reference currently in force, 255 = unknown
char line[48];
byte lineLen=0;

//==================================================================
void setup(){
  mcusrAtBoot=MCUSR;
  MCUSR=0;
  Serial.begin(BAUDRATE);
  Wire.begin();
#if defined(WIRE_TIMEOUT)
  Wire.setWireTimeout(25000, true);
#endif
  pinMode(LED_PIN, OUTPUT);   digitalWrite(LED_PIN, LOW);
  pinMode(GREEN_LED, OUTPUT); digitalWrite(GREEN_LED, LOW);
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  pinMode(BLUETOOTH_SATUS_PIN, INPUT);
  pinMode(BATT_VOLTAGE_PIN, INPUT);
  // Flash powered and deselected, as the logger keeps it.
  pinMode(MEM_POWER, OUTPUT);       digitalWrite(MEM_POWER, HIGH);
  pinMode(FLASH_MEMORY_CS, OUTPUT); digitalWrite(FLASH_MEMORY_CS, HIGH);
  SPI.begin();
  // Header pins: plain inputs, nothing driven, until a test says otherwise.
  for(byte i=0;i<4;i++){ pinMode(A0+i, INPUT); digitalWrite(A0+i, LOW); }
  // The TMP119 boots free-running at ~16 uA: park it like the logger does.
  tmpWrite(TMP119_CONFIG_REG, TMP119_SHUTDOWN);

  Serial.println();
  PL("==================================================");
  P("GlacierTemp DIAGNOSTICS "); Serial.print(F(DIAG_VERSION));
  if(WITH_BOARD) PL("  -- BOARD tests"); else PL("  -- SENSOR tests");
  P("Built "); Serial.print(F(__DATE__)); P(" "); Serial.println(F(__TIME__));
  PL("Read-only: EEPROM, flash log and RTC time are not changed.");
  PL("==================================================");
  printHelp();
  P("\n> ");
}

void loop(){
  while(Serial.available()){
    char c=Serial.read();
    if(c=='\r' || c=='\n'){
      if(lineLen){
        line[lineLen]=0;
        Serial.println(line);
        runCommand(line);
        lineLen=0;
        P("\n> ");
      }
    }else if(lineLen<sizeof(line)-1){
      line[lineLen++]=(c>='a' && c<='z') ? c-32 : c;
    }
  }
}

//------------------------------ CONSOLE -------------------------------------
void printHelp(){
  PL("\nCommands (any case; [x] optional):");
  PL(" ALL              every non-destructive test in this build, then a summary");
  PL(" MCU              chip signature, fuses, reset cause");
  PL(" POWER            3.3V rail (bandgap) and battery reading");
  PL(" I2C              bus scan: who answers, who is missing");
#if WITH_BOARD
  PL(" PINS             every pin: direction, pull-up, level");
  PL(" LEDS             blinks green, then red");
  PL(" BT               bluetooth module STATE pin");
  PL(" EEPROM           logger configuration, calibrations, counters");
  PL(" RTC              DS3231 time, flags, temperature, aging");
  PL(" RTCSQW           MCU crystal vs RTC (1.024 kHz on INT/SQW)");
  PL(" RTCALARM         alarm -> INT pin -> wake line, end to end");
  PL(" FLASH            W25Q64 ID, status, power-down, log consistency");
  PL(" FLASHRW          write/erase test on the LAST sector, only if blank");
  PL(" DS               1-Wire bus: pull-up, sensors, CRC, temperatures");
  PL(" SLEEP [s]        sleep like the logger for s seconds (measure current)");
  PL(" Sensor tests (HDC, TMP, TMPAVG, ANALOG, SETTLE, STEP, TIMING): build");
  PL(" with DIAG_MODE DIAG_SENSORS.");
#else
  PL(" HDC [n]          HDC1080 IDs, config and n readings with noise");
  PL(" HDCHEAT          HDC1080 heater: does the sensor respond?");
  PL(" TMP [n]          TMP119 IDs, config, offset and n readings");
  PL(" TMPAVG [n]       TMP119 noise vs averaging 0/8/32/64, n rounds (30)");
  PL(" ANALOG           A0..A3: counts, mV, noise, open or driven");
  PL(" SETTLE [s] [pp]  settle-time sweep: sensor on As, powered from pins");
  PL("                  pp (digits, e.g. 123 = A1+A2+A3). Default 0 123");
  PL(" STEP [s] [pp]    sensor output vs time after power-on");
  PL(" PWR [s] [pp] [t] power on, read As every second for t s (60)");
  PL(" TIMING           how long each reading takes the logger");
  PL(" Board tests (EEPROM, RTC, FLASH, DS, SLEEP, PINS, LEDS, BT): build with");
  PL(" DIAG_MODE DIAG_BOARD.");
#endif
  PL(" HELP             this list");
}

// Second word of the command as a number, or def if there is none.
long argNum(const char* cmd, byte which, long def){
  const char* p=cmd;
  for(byte w=0; w<which; w++){
    while(*p && *p!=' ') p++;
    while(*p==' ') p++;
  }
  // A hand-rolled parse instead of strtol(): strtol costs 558 bytes, and this
  // sketch is within a few hundred of the chip's limit.
  bool neg=(*p=='-');
  if(neg) p++;
  if(*p<'0' || *p>'9') return def;
  long v=0;
  while(*p>='0' && *p<='9') v=v*10+(*p++-'0');
  return neg ? -v : v;
}

// Header mask from a string of digits ("123" -> A1|A2|A3), or def.
byte argMask(const char* cmd, byte which, byte def){
  const char* p=cmd;
  for(byte w=0; w<which; w++){
    while(*p && *p!=' ') p++;
    while(*p==' ') p++;
  }
  if(!*p) return def;
  byte m=0;
  while(*p>='0' && *p<='3'){ m|=1<<(*p-'0'); p++; }
  return m ? m : def;
}

bool is(const char* cmd, const char* name){
  byte n=strlen(name);
  return !strncmp(cmd, name, n) && (cmd[n]==0 || cmd[n]==' ');
}

void runCommand(const char* c){
  if(is(c,"HELP"))          printHelp();
  else if(is(c,"ALL"))      runAll();
  else if(is(c,"MCU"))      testMcu();
  else if(is(c,"POWER"))    testPower();
  else if(is(c,"I2C"))      testI2c();
#if WITH_BOARD
  else if(is(c,"PINS"))     testPins();
  else if(is(c,"LEDS"))     testLeds();
  else if(is(c,"BT"))       testBluetooth();
  else if(is(c,"EEPROM"))   testEeprom();
  else if(is(c,"RTCSQW"))   testRtcSqw();
  else if(is(c,"RTCALARM")) testRtcAlarm();
  else if(is(c,"RTC"))      testRtc();
  else if(is(c,"FLASHRW"))  testFlashWrite();
  else if(is(c,"FLASH"))    testFlash();
  else if(is(c,"DS"))       testDs18b20();
  else if(is(c,"SLEEP"))    testSleep(argNum(c,1,30));
#else
  else if(is(c,"HDCHEAT"))  testHdcHeater();
  else if(is(c,"HDC"))      testHdc(argNum(c,1,20));
  else if(is(c,"TMPAVG"))   testTmpAveraging(argNum(c,1,30));
  else if(is(c,"TMP"))      testTmp(argNum(c,1,20));
  else if(is(c,"ANALOG"))   testAnalog();
  else if(is(c,"SETTLE"))   testSettle(argNum(c,1,DIAG_SENSOR_PIN), argMask(c,2,DIAG_POWER_MASK));
  else if(is(c,"STEP"))     testStep(argNum(c,1,DIAG_SENSOR_PIN), argMask(c,2,DIAG_POWER_MASK));
  else if(is(c,"PWR"))      testSensorPower(argNum(c,1,DIAG_SENSOR_PIN), argMask(c,2,DIAG_POWER_MASK), argNum(c,3,60));
  else if(is(c,"TIMING"))   testTiming();
#endif
  else { P("Unknown command (or not in this build). "); PL("Type HELP."); }
}

void runAll(){
  nPass=nWarn=nFail=0;
  unsigned long t0=millis();
  testMcu();
  testPower();
  testI2c();
#if WITH_BOARD
  testEeprom();
  testRtc();
  testRtcSqw();
  testRtcAlarm();
  testFlash();
  testDs18b20();
#else
  testHdc(20);
  testTmp(20);
  testAnalog();
  testTiming();
#endif
#if WITH_BOARD
  testBluetooth();
  testPins();
#endif
  section(F("SUMMARY"));
  P("  PASS "); Serial.print(nPass);
  P("   WARN "); Serial.print(nWarn);
  P("   FAIL "); Serial.println(nFail);
  P("  Took "); Serial.print((millis()-t0)/1000); PL(" s.");
  if(nFail){
    PL("  Look for [FAIL] above: each one says what was expected.");
  }
#if WITH_BOARD
  PL("  Not run by ALL: FLASHRW (writes), SLEEP, LEDS. Sensor tests: DIAG_SENSORS.");
#else
  PL("  Not run by ALL: TMPAVG (about a minute), SETTLE/STEP (need to know which");
  PL("  pins carry the sensor), HDCHEAT, PWR. Board tests: DIAG_BOARD.");
#endif
  digitalWrite(nFail ? LED_PIN : GREEN_LED, HIGH);
  delay(1500);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(GREEN_LED, LOW);
}

//------------------------------ REPORTING -----------------------------------
void section(const __FlashStringHelper* name){
  P("\n---------- "); Serial.print(name); PL(" ----------");
}

// "  [PASS] text" -- and the counters for the summary.
void result(byte r, const __FlashStringHelper* text){
  switch(r){
    case R_PASS: P("  [PASS] "); nPass++; break;
    case R_WARN: P("  [WARN] "); nWarn++; break;
    case R_FAIL: P("  [FAIL] "); nFail++; break;
    default:     P("  [info] "); break;
  }
  Serial.println(text);
}

void hex2(byte v){
  if(v<16) Serial.print('0');
  Serial.print(v, HEX);
}

void hex4(uint16_t v){
  hex2(v>>8); hex2(v&0xFF);
}

// "  label ...... " aligned so the values line up in a column.
void label(const __FlashStringHelper* t){
  P("  ");
  Serial.print(t);
  int n=strlen_P((const char*)t);
  Serial.print(' ');
  for(int i=n+1; i<26; i++) Serial.print('.');
  Serial.print(' ');
}

//--------------------------------- ADC --------------------------------------
// The reference is switched only when it changes, and then given time to
// settle plus a discarded conversion, as in the logger.
void useRef(byte ref){
  if(adcRef!=ref){
    analogReference(ref);
    analogRead(BATT_VOLTAGE_PIN);
    delay(ADC_REF_SETTLE_MS);
    adcRef=ref;
  }
}

// What the logger does: one discarded conversion, then the mean of n.
float adcMean(byte pin, byte n){
  analogRead(pin);
  long s=0;
  for(byte i=0;i<n;i++) s+=analogRead(pin);
  return (float)s/n;
}

// Two-point calibration as in the logger: mV from a count through the line
// defined by two stored (mV, count) points. -1 if the points are unset.
long twoPointMv(int v1, int c1, int v2, int c2, float count){
  if(c1==c2 || c1==-1 || c2==-1) return -1;
  return (long)(v1 + (float)(v2-v1)*(count-c1)/(c2-c1));
}

int eeInt(int addr){ int v; EEPROM.get(addr, v); return v; }
unsigned long eeULong(int addr){ unsigned long v; EEPROM.get(addr, v); return v; }
