//####################################################################
//############### DS18B20 1-WIRE TEMPERATURE (D3) ####################
//####################################################################
// One or more DS18B20 sensors on the 1-Wire header. LOG_DS18B20 in the main
// file is the NUMBER of sensors expected, and the long note beside it explains
// the wiring, the resolution trade-off and how slots are bound to sensors.
// Everything here compiles to nothing when that count is 0.
//
// WIRING. The bus needs an external 4.7k pull-up to 3.3 V. The AVR's internal
// pull-up is 20-50k, far too weak to pull a 1-Wire line up inside a bit slot,
// so without the resistor every read returns 0xFF and the sensors look absent.
// Parasite power is deliberately NOT supported: it needs a strong bus pull-up
// held high for the whole conversion, which conflicts with everything else this
// pin does, and a normally powered part on the permanent 3.3 V rail costs about
// 1 uA idle.

#if LOG_DS18B20

#include <OneWire.h>

OneWire oneWire(ONE_WIRE_PIN);

#define DS18B20_CONVERT_T     0x44
#define DS18B20_WRITE_SCRATCH 0x4E
#define DS18B20_READ_SCRATCH  0xBE
#define DS18B20_SKIP_ROM      0xCC
#define DS18B20_MATCH_ROM     0x55
#define DS18B20_FAMILY        0x28   // DS18B20. DS18S20 is 0x10 and is NOT compatible:
                                     // it reports in 1/2 degC steps, so it is rejected.

// Configuration register value and worst-case conversion time for each setting.
// The low bits of the raw reading are undefined below 12 bits and are masked off.
#if DS18B20_RESOLUTION == 9
  #define DS18B20_CONFIG   0x1F
  #define DS18B20_CONV_MS  110
  #define DS18B20_RAW_MASK 0xFFF8
#elif DS18B20_RESOLUTION == 10
  #define DS18B20_CONFIG   0x3F
  #define DS18B20_CONV_MS  210
  #define DS18B20_RAW_MASK 0xFFFC
#elif DS18B20_RESOLUTION == 11
  #define DS18B20_CONFIG   0x5F
  #define DS18B20_CONV_MS  400
  #define DS18B20_RAW_MASK 0xFFFE
#elif DS18B20_RESOLUTION == 12
  #define DS18B20_CONFIG   0x7F
  #define DS18B20_CONV_MS  800
  #define DS18B20_RAW_MASK 0xFFFF
#else
  #error "DS18B20_RESOLUTION must be 9, 10, 11 or 12"
#endif

// Orders two ROM codes by serial number, most significant byte first. Returns
// true if a sorts before b.
//
// The ROM layout is [0] family code, [1..6] serial with the LEAST significant
// byte first, [7] CRC. Comparing the raw bytes in order would therefore sort by
// the least significant digits, which is a valid ordering but a confusing one
// to read next to the DS-xxxx column headings. Walking [6] down to [1] sorts by
// the serial number as it is normally written and printed.
bool dsRomLess(const byte *a, const byte *b){
  for(int8_t i=6;i>=1;i--){
    if(a[i]!=b[i]){
      return a[i]<b[i];
    }
  }
  return false;
}

// Enumerates the bus and binds each slot to one sensor.
//
// Sorting is what makes the slot assignment reproducible. The 1-Wire search is
// deterministic for a given set of devices, but it is NOT stable when the set
// changes, so relying on discovery order would silently renumber every column
// the first time a probe was added or died. A sort by serial number depends
// only on the parts themselves.
void ds18b20Discover(){
  byte rom[8];
  ds18b20Found=0;
  ds18b20Extra=false;
  oneWire.reset_search();
  while(oneWire.search(rom)){
    if(ds18b20Found>=LOG_DS18B20){
      // More sensors on the bus than this build has slots for. Which ones got
      // logged would then depend on the order the search happened to return
      // them in, so say so rather than quietly dropping the surplus.
      if(OneWire::crc8(rom,7)==rom[7] && rom[0]==DS18B20_FAMILY){
        ds18b20Extra=true;
      }
      continue;
    }
    // A bad CRC means the search hit a glitch rather than a device; skipping it
    // is better than binding a slot to a ROM code that does not exist.
    if(OneWire::crc8(rom,7)!=rom[7]){
      continue;
    }
    if(rom[0]!=DS18B20_FAMILY){
      continue;
    }
    memcpy(dsRom[ds18b20Found],rom,8);
    ds18b20Found++;
  }
  // Insertion sort. At most MAX_DS18B20 entries, so nothing cleverer is wanted.
  for(byte i=1;i<ds18b20Found;i++){
    byte key[8];
    memcpy(key,dsRom[i],8);
    int8_t j=i-1;
    while(j>=0 && dsRomLess(key,dsRom[j])){
      memcpy(dsRom[j+1],dsRom[j],8);
      j--;
    }
    memcpy(dsRom[j+1],key,8);
  }
}

// Writes the resolution into every sensor's scratchpad at once.
//
// The write is volatile -- it is not copied to the sensor's own EEPROM -- so it
// has to be redone after every power cycle, which is what calling this from
// setup() achieves. Broadcasting with SKIP_ROM sets all of them in one
// transaction; a scratchpad write is output only, so there is no bus contention.
//
// The command takes exactly three bytes (TH, TL, config) and the part ignores
// anything shorter, so the two alarm bytes must be sent even though nothing
// here uses them.
void ds18b20SetResolution(){
  if(!oneWire.reset()){
    return;
  }
  oneWire.write(DS18B20_SKIP_ROM);
  oneWire.write(DS18B20_WRITE_SCRATCH);
  oneWire.write(0x00);            // TH alarm, unused
  oneWire.write(0x00);            // TL alarm, unused
  oneWire.write(DS18B20_CONFIG);  // resolution
}

void ds18b20Setup(){
  for(byte i=0;i<LOG_DS18B20;i++){
    currentTempDS[i]=INVALID_TEMP;
    memset(dsRom[i],0,8);
  }
  ds18b20Discover();
  if(ds18b20Found>0){
    ds18b20SetResolution();
  }
}

// Column heading, and the name used in the I report: "DS-" followed by the
// bottom four hex digits of the serial number. Four digits is not unique in
// principle, but a collision needs two parts whose serials match in the low 16
// bits, and the full codes are listed at start-up if it ever matters.
// dst must have room for 8 characters.
void ds18b20Label(byte slot, char *dst){
  dst[0]='D'; dst[1]='S'; dst[2]='-';
  if(slot>=ds18b20Found){
    dst[3]='n'; dst[4]='o'; dst[5]='n'; dst[6]='e'; dst[7]='\0';
    return;
  }
  dst[3]=hexDigit(dsRom[slot][2]>>4);
  dst[4]=hexDigit(dsRom[slot][2]);
  dst[5]=hexDigit(dsRom[slot][1]>>4);
  dst[6]=hexDigit(dsRom[slot][1]);
  dst[7]='\0';
}

// Prints the slot-to-sensor binding, so the mapping behind the DS-xxxx column
// headings can be recorded when the loggers are commissioned.
void ds18b20ListSensors(){
  if(ds18b20Extra){
    out << F("WARNING: more DS18B20 on the bus than LOG_DS18B20 slots; surplus ignored\n");
  }
  for(byte i=0;i<ds18b20Found;i++){
    char label[8];
    ds18b20Label(i,label);
    out << NOSPACER << ' ' << label << " ROM";
    for(int8_t b=7;b>=0;b--){
      out << ' ' << hexDigit(dsRom[i][b]>>4) << hexDigit(dsRom[i][b]);
    }
    out << NORMALTEXT << NL;
  }
}

// Reads one sensor's scratchpad by ROM code and returns its temperature in
// hundredths of a degree C, or INVALID_TEMP.
//
// MATCH_ROM rather than SKIP_ROM: with more than one device on the bus a
// broadcast read makes every sensor drive the line at once and returns the
// bitwise AND of all of them, which looks like a plausible temperature and is
// not one.
int ds18b20ReadOne(byte slot){
  if(!oneWire.reset()){
    return INVALID_TEMP;
  }
  oneWire.write(DS18B20_MATCH_ROM);
  for(byte i=0;i<8;i++){
    oneWire.write(dsRom[slot][i]);
  }
  oneWire.write(DS18B20_READ_SCRATCH);
  byte scratch[9];
  for(byte i=0;i<9;i++){
    scratch[i]=oneWire.read();
  }
  // The CRC covers the first eight bytes and is the only defence against a
  // marginal bus returning plausible rubbish. All-0xFF, which is what an absent
  // or unpowered sensor gives, fails it, so it doubles as a presence check.
  if(OneWire::crc8(scratch,8)!=scratch[8]){
    return INVALID_TEMP;
  }
  int raw=(int)(((uint16_t)scratch[1]<<8) | scratch[0]);
  raw &= (int)DS18B20_RAW_MASK;
  // One LSB is 1/16 degC, so centi°C = raw*100/16 = raw*25/4 exactly.
  return (int)(((long)raw*25)/4);
}

// Converts every sensor at once, then reads them one at a time into
// currentTempDS[].
//
// The wait polls the bus instead of delaying blindly. A normally powered
// DS18B20 holds the line low while it is converting and releases it when the
// result is ready; with several on the bus the line stays low until the LAST
// one has finished, which is exactly the condition to wait for. At 9 bits this
// returns in about 94 ms rather than sitting out the 110 ms worst case, and
// because the conversion is broadcast it costs the same for eight sensors as
// for one. That time is spent awake on battery, which is the whole reason the
// resolution is a compile-time choice.
void getDS18B20Temp(){
  for(byte i=0;i<LOG_DS18B20;i++){
    currentTempDS[i]=INVALID_TEMP;
  }
  if(ds18b20Found==0){
    return;
  }
  if(!oneWire.reset()){
    return;
  }
  oneWire.write(DS18B20_SKIP_ROM);
  oneWire.write(DS18B20_CONVERT_T);

  unsigned long start=millis();
  bool ready=false;
  while(millis()-start < DS18B20_CONV_MS){
    if(oneWire.read_bit()){
      ready=true;
      break;
    }
  }
  if(!ready){
    return;      // nobody released the bus
  }

  for(byte i=0;i<ds18b20Found;i++){
    currentTempDS[i]=ds18b20ReadOne(i);
  }
}

#endif // LOG_DS18B20
