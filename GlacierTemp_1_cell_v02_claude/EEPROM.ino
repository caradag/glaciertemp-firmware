
void readConfiguration(){
  // Read all variables stored in EEPROM, range-checking each one as it is loaded.
  // EEPROM reads back 0xFF on a chip that has never been initialised, cells wear
  // out after ~100,000 writes, and a brownout during a write corrupts a byte. A
  // marginal cell can also read correctly on a retry, so each value is re-read up
  // to three times before falling back to its default.

  // Measurement interval (seconds). setWakeUp() divides by it, so 0 is fatal.
  for(byte attempt=0; attempt<3; attempt++){
    measureInterval=getULong(varAddr[MEASURE_INTERVAL]);
    if(measureInterval>0 && measureInterval<=MAX_MEASURE_INTERVAL){
      break;
    }
  }
  if(measureInterval==0 || measureInterval>MAX_MEASURE_INTERVAL){
    out << F("Bad interval in EEPROM, using default\n");
    measureInterval=DEFAULT_MEASURE_INTERVAL;
  }

  // Time zone (whole hours from UTC). A wild value shifts every timestamp.
  for(byte attempt=0; attempt<3; attempt++){
    timeZone=getInt(varAddr[TIMEZONE]);
    if(timeZone>=MIN_TIMEZONE && timeZone<=MAX_TIMEZONE){
      break;
    }
  }
  if(timeZone<MIN_TIMEZONE || timeZone>MAX_TIMEZONE){
    out << F("Bad timezone in EEPROM, using UTC\n");
    timeZone=DEFAULT_TIMEZONE;
  }

  // RTC adjustment interval (days). Used as DOY % adjustRTCinterval, so 0 is fatal.
  for(byte attempt=0; attempt<3; attempt++){
    adjustRTCinterval=getUInt(varAddr[ADJUST_RTC_INTERVAL]);
    if(adjustRTCinterval>0 && adjustRTCinterval<=MAX_ADJUST_RTC_INTERVAL){
      break;
    }
  }
  if(adjustRTCinterval==0 || adjustRTCinterval>MAX_ADJUST_RTC_INTERVAL){
    out << F("Bad RTC adjust interval in EEPROM, using default\n");
    adjustRTCinterval=DEFAULT_ADJUST_RTC_INTERVAL;
  }

  // Low voltage interval multiplier. 0 and 1 both mean "no slow-down", but a
  // blank EEPROM would otherwise read 255 and stretch the interval to a full day.
  for(byte attempt=0; attempt<3; attempt++){
    lowVoltageMultiplier=EEPROM.read(varAddr[LOW_VOLTAGE_INTERVAL_MULTIPLIER]);
    if(lowVoltageMultiplier>=1 && lowVoltageMultiplier<=MAX_LOW_VOLTAGE_MULTIPLIER){
      break;
    }
  }
  if(lowVoltageMultiplier<1 || lowVoltageMultiplier>MAX_LOW_VOLTAGE_MULTIPLIER){
    out << F("Bad low voltage multiplier in EEPROM, using default\n");
    lowVoltageMultiplier=DEFAULT_LOW_VOLTAGE_MULTIPLIER;
  }
}

void printMsg(int messageAddress){
  // Read a string from EEPROM and prints it
  int i=0;
  while (char(EEPROM.read(messageAddress+i))!='\0'){
    out << NOSPACER << char(EEPROM.read(messageAddress+i));
    i++;
  }
}

void displayCommands(){
  for(byte i=0; i<sizeof(varLengths); i++){
    out << NOSPACER << varComm[i*3] << varComm[i*3+1] << varComm[i*3+2] << '\t'; printMsg(varAddr[i]+varLengths[i]); ln();
  }
}

void displayVars(int first, int last){
  for(int i=first; i<=last; i++){
    printMsg(varAddr[i]+varLengths[i]); out << ": ";
    if (varTypes[i]=='i') {
      out << getInt(varAddr[i]) << NL;
    }else if (varTypes[i]=='u') {
      out << getUInt(varAddr[i]) << NL;
    }else if (varTypes[i]=='U') {
      out << getULong(varAddr[i]) << NL;      
    }else if (varTypes[i]=='f') {
      out << '\xB6' << getFloat(varAddr[i]) << NL;
    }else if (varTypes[i]=='b') {
      out << EEPROM.read(varAddr[i]) << NL;
    }
  }   
}

byte isVarCommand(char *inputStr){
  int nVars=sizeof(varLengths);
  for(int i=0;i<nVars;i++){
    if(!strncasecmp(inputStr, varComm+i*3,3)){
      return i;
    }
  }
  return 255;
}

// Accepted range for each user-settable variable. Returns false for variables
// that have no range to check. These are the SAME limits readConfiguration()
// applies when loading, so a value refused here could never have survived a
// reload anyway -- checking on the way in just means the rejection is visible
// immediately instead of silently reverting on the next boot.
bool varLimits(byte varID, long &lo, long &hi){
  switch(varID){
    case MEASURE_INTERVAL:
      lo=1;             hi=MAX_MEASURE_INTERVAL;        return true;
    case LOW_VOLTAGE_INTERVAL_MULTIPLIER:
      lo=1;             hi=MAX_LOW_VOLTAGE_MULTIPLIER;  return true;
    case TIMEZONE:
      lo=MIN_TIMEZONE;  hi=MAX_TIMEZONE;                return true;
    case ADJUST_RTC_INTERVAL:
      lo=1;             hi=MAX_ADJUST_RTC_INTERVAL;     return true;
    case MESSAGE_FREQUENCY_DAYS:
      lo=0;             hi=MAX_MESSAGE_FREQUENCY_DAYS;  return true;
  }
  return false;
}

void updateVar(byte varID,float value){
  // Refuse out-of-range values instead of storing them. The comparison is done
  // in float so an absurd input cannot overflow on the way to the integer cast,
  // which is also how a value like MSW=1000 used to become 232 without warning.
  long lo,hi;
  if(varLimits(varID,lo,hi) && (value<lo || value>hi)){
    // NOSPACER goes before hi so the stream does not append its usual separator
    // after the number, which would print "86400 )" instead of "86400)".
    out << F("Out of range (") << lo << F("to") << NOSPACER << hi << F("), not stored\n");
    return;
  }
  if (varTypes[varID]=='i') {
    EEPROM.put(varAddr[varID],(int) value);
  }else if (varTypes[varID]=='u') {
    EEPROM.put(varAddr[varID],(unsigned int) value);  
  }else if (varTypes[varID]=='U') {
    EEPROM.put(varAddr[varID],(unsigned long) value);
  }else if (varTypes[varID]=='f') {
    EEPROM.put(varAddr[varID],(float) value);
  }else if (varTypes[varID]=='b') {
    EEPROM.put(varAddr[varID],(byte) value);
  }  
}

//##################### READ - WRITE PARAMETERS ###########################
unsigned long getCount(){
  return getFullCount()-getULong(COUNT_RESET_ADDR);
}

unsigned long getFullCount(){
  unsigned long count=0;
  for(int i=0;i<COUNTERS_SLOTS;i++){
    count+=getULong(i*4+COUNT_ADDR);
  }
  return count;
}

void addCount(){
  unsigned long counter;
  bool done=false;
  // COUNTERS_SLOTS, not a hard-coded 10: only a third of the slots were used
  for(int i=0;i<COUNTERS_SLOTS;i++){
    counter=getULong(i*4+COUNT_ADDR);
    if(counter<50000){
      EEPROM.put(i*4+COUNT_ADDR,counter+1);
      done=true;
      break;
    }
  }
  if(!done){
    out << F("Count maxout\n");
  }
}

void resetCount(){
  //Reseting counters  
  EEPROM.put(COUNT_RESET_ADDR,getFullCount());
  // The log restarts here, so it is now this build's log: adopt the channel set
  // and lift any format-mismatch suspension.
  EEPROM.put(LOG_SIGNATURE_ADDR,(unsigned int)LOG_SIGNATURE);
  logFormatMismatch=false;
  getCurrentTime();
  EEPROM.put(VOLTAGE_RESET_ADDR,(unsigned int) getBatteryVoltage());
  EEPROM.put(LAST_TIME_ADJUSTMENT_SECS,(int)0);
  EEPROM.put(CUMULATIVE_TIME_ADJUSTMENT_SECS,(int)0);
  // The awake-time maxima describe the run that just ended, so they are cleared
  // with the counter. The "last cycle" values are left alone; they still
  // describe the most recent measurement.
  maxPreMs=0;
  maxMeasMs=0;
  maxPostMs=0;
  out << F("Memory reset\n");
}

//################## HISTORY MANAGEMENT FUNCTIONS ##########################


#if LOG_DS18B20
// Writes the whole DS18B20 block. A helper rather than another line in the &&
// chain because the number of sensors is a compile-time count, not a flag, and
// the chain cannot loop. Returns false on the first field that fails, which
// keeps the same all-or-nothing behaviour as the rest of the record.
bool storeDS18B20(uint32_t address){
  for(byte i=0;i<LOG_DS18B20;i++){
    if(!flashWriteInt(address+OFF_DS18B20+2*i, currentTempDS[i])){
      return false;
    }
  }
  return true;
}
#endif

// Reads the sensors and appends one record to the flash log.
// Returns false if any part of the record failed to reach the flash, in which
// case the sample counter is NOT advanced, so the next measurement reuses the
// same slot instead of leaving a corrupt record behind.
bool takeMeasurement(){
  // A log written by a build with a different channel set cannot be extended by
  // this one without producing a file no single reader can parse. Refuse, and
  // keep saying why: the red flash from the caller plus this line are the only
  // signals the user gets if they reflashed and forgot to clear the log.
  if(logFormatMismatch){
#if SUSPEND_ON_FORMAT_MISMATCH
    out << F("Log format changed: clear with RC. NOT logging.\n");
    return false;
#else
    out << F("WARNING: log format changed, records are mixed\n");
#endif
  }
  memSendControlByte(POWER_UP);
  unsigned long measurementCount=getCount();
  uint32_t address;
  // Both I2C sensors run from the permanent 3.3V rail, so there is nothing to
  // settle before reading them.
#if LOG_HDC_TEMP || LOG_HDC_RH
  getTempAndRH();
#endif
#if LOG_TMP119
  getHighAccuracyTemp();
#endif
#if LOG_DS18B20
  getDS18B20Temp();
#endif
  // Read while the INTERNAL reference is still in force, before the header
  // channels switch it to VCC and back.
  int battMv=getBatteryVoltage();
#if ANALOG_CHANNELS
  readAnalogChannels();
#endif
  if(measurementCount>0){
    address=measurementCount*BYTES_PER_SAMPLE;
  }else{
    address=0;
    EEPROM.put(RESET_TIME_ADDR,currentTime);
    // First record of a log: stamp it with the channel set that produced it.
    EEPROM.put(LOG_SIGNATURE_ADDR,(unsigned int)LOG_SIGNATURE);
  }

#if IRIDIUM_INSTALLED == 1
  // Min/max are only ever consumed by the Iridium message builder
  int trackTemp = (currentTempHA!=INVALID_TEMP_HA) ? currentTempHA : currentTemp;
  if(trackTemp!=INVALID_TEMP){
    if((trackTemp<minTemp) || (minTemp==INVALID_TEMP)){
      minTemp=trackTemp;
    }
    if((trackTemp>maxTemp) || (maxTemp==INVALID_TEMP)){
      maxTemp=trackTemp;
    }
  }
#endif

  // Temperatures are stored in 0.01°C increments so they fit in two bytes.
  // && short-circuits, so the first failure abandons the rest of the record.
  // Each field is written at its OFF_* offset, so a channel switched off simply
  // is not there and everything after it moves up.
  bool stored = flashWriteUnsignedLong(address+OFF_TIME, currentTime)
#if LOG_VOLTAGE
             && flashWriteInt(address+OFF_VOLTAGE, battMv)
#endif
#if LOG_HDC_TEMP
             && flashWriteInt(address+OFF_HDC_TEMP, currentTemp)
#endif
#if LOG_HDC_RH
             && flashWriteInt(address+OFF_HDC_RH, currentRH)
#endif
#if LOG_TMP119
             && flashWriteInt(address+OFF_TMP119, currentTempHA)
#endif
#if LOG_DS18B20
             && storeDS18B20(address)
#endif
#if LOG_A0
             && flashWriteInt(address+OFF_A0, currentAnalog[0])
#endif
#if LOG_A1
             && flashWriteInt(address+OFF_A1, currentAnalog[1])
#endif
#if LOG_A2
             && flashWriteInt(address+OFF_A2, currentAnalog[2])
#endif
#if LOG_A3
             && flashWriteInt(address+OFF_A3, currentAnalog[3])
#endif
             ;
#if !LOG_VOLTAGE
  (void)battMv;   // still read above, for the low-battery logic and the I report
#endif

  memSendControlByte(POWER_DOWN);

  if(stored){
    addCount();
    if(out.getVerbose()){
      displayHistory(SHOW_LAST);
    }
  }else{
    out << F("Error: Measurement NOT stored\n");
  }

  flashPowerDown();
  return stored;
 }


// Column widths for the aligned LOG layout. Compact ignores them entirely.
#define W_VOLT   5
#define W_TEMP   7
#define W_RH     5
#define W_TEMPHA 8
#define W_TEMPDS 8
#define W_ANALOG 8

// Prints one column heading, padded to width in the aligned layout.
//
// The length is counted in GLYPHS, not bytes: the headings contain a degree
// sign, which is two bytes of UTF-8, and padding on the byte count would make
// those two columns come out a character narrow.
void logHeader(const __FlashStringHelper* name, byte width, bool compact){
  out << NOSPACER << ',';
  if(!compact){
    out << ' ';
    byte len=0;
    PGM_P q=reinterpret_cast<PGM_P>(name);
    char c;
    while((c=pgm_read_byte(q++))){
      if(((byte)c & 0xC0) != 0x80){
        len++;
      }
    }
    for(byte i=len;i<width;i++){
      out << ' ';
    }
  }
  out << name;
}

// Same as logHeader() but for a heading held in RAM rather than flash. The
// DS18B20 columns are named after each sensor's serial number, which is only
// known once the bus has been enumerated, so those names cannot live in flash.
void logHeaderRam(const char* name, byte width, bool compact){
  out << NOSPACER << ',';
  if(!compact){
    out << ' ';
    byte len=0;
    for(const char*q=name; *q; q++){
      if(((byte)*q & 0xC0) != 0x80){
        len++;
      }
    }
    for(byte i=len;i<width;i++){
      out << ' ';
    }
  }
  out << name;
}

// Prints one data field, or NaN if the sensor did not return a reading.
// decimals is where the implied decimal point goes: the values are stored as
// integers scaled by a power of ten (centi°C, deci %, mV) and the stream places
// the point on the way out.
void logField(long value, bool invalid, byte decimals, byte width, bool forceSign, bool compact){
  out << NOSPACER << ',';
  if(!compact){
    out << ' ';
  }
  if(invalid){
    if(!compact){
      for(byte i=3;i<width;i++){
        out << ' ';
      }
    }
    out << "NaN";
    return;
  }
  if(!compact){
    out << (char)(PAD+width);
    if(forceSign){
      out << '+';
    }
  }
  out << (char)(DECIMALS+decimals) << value;
}

// mode selects one of three presentations of the same records:
//   SHOW_ALL          padded, column-aligned, one header, easy to read on screen
//   SHOW_LAST         the same layout, but only the record just written
//   SHOW_ALL_COMPACT  the LOGC dump: same fields and same units, but no sample
//                     number, no padding and no space after the commas
//
// WHY COMPACT EXISTS. Dumping the log is limited by the serial line, not by the
// flash: a padded row is about 62 characters, which at 230400 baud is 2.7 ms,
// while reading the bytes behind it over SPI takes about 60 us. Dropping the
// sample number and the padding takes the row to about 41 characters, so a full
// log comes out in roughly two thirds of the time. The sample number is no loss
// -- it is just the line number, which any reader can recreate.
//
// The one space kept in a compact row is the one inside the timestamp, between
// the date and the time, so the dump still reads as dates rather than digits.
// Every other field is separated by a bare comma.
//
// Which columns appear is decided at compile time by LOG_VOLTAGE and friends;
// see the channel block in the main file.
 void displayHistory(byte mode){
  memSendControlByte(POWER_UP);
  unsigned long nSamples=getCount();
  bool compact = (mode==SHOW_ALL_COMPACT);
  // Records written under a different channel set cannot be read here. The row
  // stride is BYTES_PER_SAMPLE and the fields are read at this build's offsets,
  // so a changed record size misaligns everything after the first record and a
  // changed field order misreads the fields within it. The output would still
  // look like a plausible table, which is precisely why it needs saying.
  if(logFormatMismatch){
    out << F("WARNING: these records were written with a different channel set.\n");
    out << F("The values below are misparsed. Re-flash the firmware that wrote them to read them.\n");
  }
  if (nSamples>0){
    // We print the column headers
    if(compact){
      out << NOSPACER << F("Time");
    }else{
      out << NOSPACER << (char)(PAD+8) << F("Sample #") << ',' << F("                Time");
    }
#if LOG_VOLTAGE
    logHeader(F("Volt"),    W_VOLT,   compact);
#endif
#if LOG_HDC_TEMP
    logHeader(F("Temp°C"),  W_TEMP,   compact);
#endif
#if LOG_HDC_RH
    logHeader(F("RH"),      W_RH,     compact);
#endif
#if LOG_TMP119
    logHeader(F("HAtemp°C"),W_TEMPHA, compact);
#endif
#if LOG_DS18B20
    for(byte ds=0; ds<LOG_DS18B20; ds++){
      char label[8];
      ds18b20Label(ds,label);
      logHeaderRam(label, W_TEMPDS, compact);
    }
#endif
#if LOG_A0
    logHeader(F(A0_NAME),   W_ANALOG, compact);
#endif
#if LOG_A1
    logHeader(F(A1_NAME),   W_ANALOG, compact);
#endif
#if LOG_A2
    logHeader(F(A2_NAME),   W_ANALOG, compact);
#endif
#if LOG_A3
    logHeader(F(A3_NAME),   W_ANALOG, compact);
#endif
    out << NL;

    unsigned long start=1;
    if(mode==SHOW_LAST){
      start=nSamples;
    }
    unsigned long logDumpStart=millis();
    for(unsigned long i=start;i<=nSamples;i++){
      unsigned long address=(i-1)*BYTES_PER_SAMPLE;
      if(checkMemoryOverflow(address+BYTES_PER_SAMPLE)){
        break;
      }
      unsigned long sampleTime=flashReadUnsignedLong(address+OFF_TIME);
      // The sample number, then the timestamp. NOSPACER has to be re-issued
      // after displayUnixTime(), which ends with NORMALTEXT and so restores the
      // stream's default space separator.
      if(!compact){
        out << NOSPACER << (char)(PAD+8) << i << ", ";
      }
      displayUnixTime(sampleTime, false);
      out << NOSPACER;
#if LOG_VOLTAGE
      {
        int v=flashReadInt(address+OFF_VOLTAGE);
        logField(v/10, false, 2, W_VOLT, false, compact);
      }
#endif
#if LOG_HDC_TEMP
      {
        int t=flashReadInt(address+OFF_HDC_TEMP);
        logField(t, t==INVALID_TEMP, 2, W_TEMP, true, compact);
      }
#endif
#if LOG_HDC_RH
      {
        // "< 0" rather than "== INVALID_RH": humidity is never negative, and
        // this also catches the -1 sentinel older firmware wrote.
        int rh=flashReadInt(address+OFF_HDC_RH);
        logField(rh, rh<0, 1, W_RH, false, compact);
      }
#endif
#if LOG_TMP119
      {
        int t=flashReadInt(address+OFF_TMP119);
        logField(t, t==INVALID_TEMP_HA, 2, W_TEMPHA, true, compact);
      }
#endif
#if LOG_DS18B20
      for(byte ds=0; ds<LOG_DS18B20; ds++){
        int t=flashReadInt(address+OFF_DS18B20+2*ds);
        logField(t, t==INVALID_TEMP, 2, W_TEMPDS, true, compact);
      }
#endif
#if LOG_A0
      {
        int a=flashReadInt(address+OFF_A0);
        logField(a, a==INVALID_ANALOG, 3, W_ANALOG, false, compact);
      }
#endif
#if LOG_A1
      {
        int a=flashReadInt(address+OFF_A1);
        logField(a, a==INVALID_ANALOG, 3, W_ANALOG, false, compact);
      }
#endif
#if LOG_A2
      {
        int a=flashReadInt(address+OFF_A2);
        logField(a, a==INVALID_ANALOG, 3, W_ANALOG, false, compact);
      }
#endif
#if LOG_A3
      {
        int a=flashReadInt(address+OFF_A3);
        logField(a, a==INVALID_ANALOG, 3, W_ANALOG, false, compact);
      }
#endif
      out << PAD << NL;
    }
    // Work in milliseconds. Dividing by a whole-second count gave 4294967295
    // ("samples per second") for any dump that finished in under a second.
    unsigned long elapsedMs=millis()-logDumpStart;
    out << nSamples << F("transmitted in") << '\xB2' << elapsedMs/10 << F("seconds");
    if(elapsedMs>0){
      // nSamples is at most 699050, so nSamples*1000 cannot overflow
      out << "(" << (nSamples*1000UL)/elapsedMs << F("samples per second)");
    }
    ln();
  }else{
    out << F("Log empty\n");
  }
  flashPowerDown();
}

// Emits one Intel HEX record: ':' count addr type data checksum, all in ASCII
// hex. The checksum is the two's complement of the sum of every byte in the
// record, which is what lets a receiver notice a character lost on the wire.
void ihexRecord(byte type, unsigned int addr, const byte* data, byte len){
  byte sum = len + (byte)(addr>>8) + (byte)addr + type;
  out << NOSPACER << ':'
      << hexDigit(len>>4)   << hexDigit(len)
      << hexDigit(addr>>12) << hexDigit(addr>>8)
      << hexDigit(addr>>4)  << hexDigit(addr)
      << hexDigit(type>>4)  << hexDigit(type);
  for(byte i=0;i<len;i++){
    sum += data[i];
    out << hexDigit(data[i]>>4) << hexDigit(data[i]);
  }
  byte cc = (byte)(0x100 - sum);
  out << hexDigit(cc>>4) << hexDigit(cc) << NL;
}

// LOGH: dumps the raw contents of the log as Intel HEX, decoding nothing.
//
// WHY THIS EXISTS. Every other way of getting data out of this logger goes
// through the record layout, so all of them fail in the one situation where it
// matters most: a log written by a different build, in the field, with no
// toolchain to re-flash the firmware that could read it. This command sidesteps
// the problem by not interpreting the bytes at all. Capture the output, take it
// home, and reconstruct the records offline once the original channel set is
// known.
//
// FORMAT. Everything from the first ':' to the closing :00000001FF is a valid
// Intel HEX file. Save that span on its own and any standard tool will turn it
// back into a binary image of the log, for example
//     avr-objcopy -I ihex -O binary dump.hex log.bin
// The lines before it are metadata for a human and MUST be removed first --
// strict Intel HEX parsers reject anything that is not a record. Addresses are
// byte offsets into the log, so offset 0 is the first byte of record 0, and a
// type 04 record appears whenever the dump crosses a 64 kB boundary because a
// plain Intel HEX address is only 16 bits wide.
//
// HOW MUCH IS DUMPED. With no argument the span is the whole log as this build
// measures it, count times BYTES_PER_SAMPLE. When the signature says the log
// was written by a different build that stride is not trustworthy -- the old
// records may well be longer -- so the fallback is count times MAX_RECORD_BYTES,
// the largest record any build of this firmware can emit. That over-reads
// rather than truncating, which is the right way round for a rescue dump: the
// surplus is erased flash and is obvious as such. LOGH=n overrides the span
// with an explicit byte count when even that is not enough.
// ---------------------------------------------------------------------------
// LOGB: volcado BINARIO del log.
//
// Frente a LOGC ahorra un factor 3,4: un registro de 12 bytes ocupa 12 bytes en
// vez de los ~41 caracteres de la fila CSV. Sobre un enlace BLE lento eso es la
// diferencia entre una descarga util y una inviable.
//
//   texto    LOGB begin sig=0x100F rec=12 from=0 to=99 blocks=5 blocksize=256
//   bloques  AA 55 <u16 idx LE> <u16 len LE> <len bytes> <u16 crc LE>
//   texto    LOGB end
//
// El CRC va POR BLOQUE, no al final: asi un bloque danado se reintenta pidiendo
// solo su rango, en vez de repetir la descarga entera. Es tambien lo que detecta
// a un modulo BLE que anuncia un MTU que luego no sostiene -- el sintoma seria,
// si no, un CSV con datos corruptos y ningun aviso.
//
// El cuerpo se escribe con Serial.write() y NUNCA con el stream out: out trata
// 0xAD, 0xAF, 0xB2 y 0xB3 como codigos de formato, y los convertiria en espacios
// o los tragaria en mitad de los datos.
#define LOGB_BLOCK 256
#define LOGB_CHUNK 32     // el buffer vive en RAM; el ATmega328P solo tiene 2 kB

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Barato en un AVR y verificable
// contra el vector estandar: "123456789" da 0x29B1.
unsigned int crc16Ccitt(const byte* data, byte len, unsigned int crc){
  for(byte i=0;i<len;i++){
    crc ^= ((unsigned int)data[i]) << 8;
    for(byte b=0;b<8;b++){
      crc = (crc & 0x8000) ? (unsigned int)((crc<<1) ^ 0x1021) : (unsigned int)(crc<<1);
    }
  }
  return crc;
}

// Control de flujo por software durante el volcado. Un puente BLE transparente
// entrega mucho mas despacio de lo que la UART emite a 230400 y se desborda en
// silencio; con esto el receptor puede frenar la fuente.
//
// No confundir con los comandos XON y XOFF del dispatcher, que conmutan un pin de
// salida: aqui se trata de los bytes de control 0x11 y 0x13 en la linea serie.
#define FLOW_XOFF 0x13
#define FLOW_XON  0x11
void flowControlCheck(){
  if(!Serial.available()){
    return;
  }
  if(Serial.peek()!=FLOW_XOFF){
    return;
  }
  Serial.read();
  unsigned long t0=millis();
  // Con un tope: si el receptor desaparece tras pedir la pausa, el logger no
  // puede quedarse esperando para siempre y perder la ventana de medicion.
  while(millis()-t0 < 30000UL){
    if(Serial.available() && Serial.read()==FLOW_XON){
      return;
    }
  }
}

void writeU16LE(unsigned int v){
  Serial.write((byte)(v & 0xFF));
  Serial.write((byte)(v >> 8));
}

// Vuelca los registros [fromRec, toRec] inclusive. Un rango vacio se rechaza.
void dumpLogBinary(unsigned long fromRec, unsigned long toRec){
  unsigned long nSamples=getCount();
  if(nSamples==0){
    out << F("LOGB empty\n");
    return;
  }
  if(toRec>=nSamples){
    toRec=nSamples-1;
  }
  if(fromRec>toRec){
    out << F("LOGB empty\n");
    return;
  }

  memSendControlByte(POWER_UP);
  byte stride=BYTES_PER_SAMPLE;
  unsigned long startAddr=fromRec*(unsigned long)stride;
  unsigned long nBytes=(toRec-fromRec+1)*(unsigned long)stride;
  unsigned int nBlocks=(unsigned int)((nBytes + LOGB_BLOCK - 1)/LOGB_BLOCK);

  // Los espacios van escritos a mano dentro de los literales. El stream inserta
  // separadores por su cuenta entre elementos, y con eso la linea sale como
  // "sig= 0x100Frec= 12": un espacio donde no toca y ninguno donde hace falta.
  // NOSPACER apaga ese automatismo; printHex16 lo vuelve a encender al terminar,
  // asi que hay que reponerlo despues de cada llamada.
  out << NOSPACER << F("LOGB begin sig=");
  printHex16(getUInt(LOG_SIGNATURE_ADDR));
  out << NOSPACER << F(" rec=") << (unsigned long)stride;
  out << F(" from=") << fromRec;
  out << F(" to=") << toRec;
  out << F(" blocks=") << (unsigned long)nBlocks;
  out << F(" blocksize=") << (unsigned long)LOGB_BLOCK;
  out << NORMALTEXT;
  ln();
  Serial.flush();

  byte buf[LOGB_CHUNK];
  for(unsigned int blk=0; blk<nBlocks; blk++){
    unsigned long blockStart=startAddr + (unsigned long)blk*LOGB_BLOCK;
    unsigned long left=nBytes - (unsigned long)blk*LOGB_BLOCK;
    unsigned int blockLen=(left>=LOGB_BLOCK) ? LOGB_BLOCK : (unsigned int)left;

    flowControlCheck();
    Serial.write(0xAA);
    Serial.write(0x55);
    writeU16LE(blk);
    writeU16LE(blockLen);

    unsigned int crc=0xFFFF;
    unsigned int done=0;
    while(done<blockLen){
      // Atendido en cada trozo y no solo entre bloques: quien pide la pausa lo
      // hace porque su buffer se esta llenando, y 256 bytes mas de sobrepaso son
      // justo lo que trata de evitar. Comprobarlo cada 32 cuesta una lectura de
      // registro.
      flowControlCheck();
      byte n=(blockLen-done >= LOGB_CHUNK) ? LOGB_CHUNK : (byte)(blockLen-done);
      readBytesFromFlash(blockStart+done, buf, n);
      Serial.write(buf, n);
      crc=crc16Ccitt(buf, n, crc);
      done+=n;
    }
    writeU16LE(crc);
    Serial.flush();
  }
  out << F("LOGB end\n");
  flashPowerDown();
}

// Cabecera de metadatos en UNA linea de campos "clave=valor", para que un cliente
// automatico configure su decodificador sin adivinar nada.
//
// Version de firmware y de protocolo, sin tocar la memoria flash: es la consulta
// mas barata que puede hacer una app para decidir si entiende a esta placa.
void printVersion(){
  out << NOSPACER << F("fw=") << F(FIRMWARE_VERSION);
  out << F(" proto=") << PROTOCOL_VERSION;
  out << NORMALTEXT;
  ln();
}

// El comando I imprime un bloque pensado para leerlo con los ojos, y una app que
// tuviera que sacar de ahi el tamano de registro dependeria de como esta redactado.
// Esta linea es el contrato de maquina y por eso la cubre PROTOCOL_VERSION.
void printMetadata(){
  memSendControlByte(POWER_UP);
  byte id[8];
  readFlashUniqueID(id);
  // Espaciado explicito, por lo mismo que en dumpLogBinary: el separador
  // automatico del stream no coincide con lo que un parser espera leer.
  out << NOSPACER << F("INFO fw=") << F(FIRMWARE_VERSION);
  out << F(" proto=") << PROTOCOL_VERSION;
  out << F(" id=");
  for(byte i=0;i<8;i++){
    out << hexDigit(id[i]>>4) << hexDigit(id[i]);
  }
  out << F(" sig=");
  printHex16(getUInt(LOG_SIGNATURE_ADDR));
  out << NOSPACER << F(" rec=") << (unsigned long)BYTES_PER_SAMPLE;
  out << F(" count=") << getCount();
  out << F(" flash=") << (unsigned long)(SECTOR_SIZE*(MAX_SECTORS+1));
  out << NORMALTEXT;
  ln();
  flashPowerDown();
}

void displayHistoryHex(unsigned long nBytes){
  memSendControlByte(POWER_UP);
  unsigned long nSamples=getCount();
  unsigned long flashBytes=SECTOR_SIZE*(MAX_SECTORS+1);
  unsigned int stored=getUInt(LOG_SIGNATURE_ADDR);
  byte stride = logFormatMismatch ? MAX_RECORD_BYTES : BYTES_PER_SAMPLE;

  if(nBytes==0){
    nBytes = nSamples * (unsigned long)stride;
  }
  if(nBytes>flashBytes){
    nBytes=flashBytes;
  }

  // Metadata first, so the capture carries everything needed to decode it.
  out << F("LOGH raw log dump\n");
  out << F("samples:") << nSamples << NL;
  out << F("bytes:") << nBytes << F("of") << flashBytes << NL;
  out << F("this build record size:") << (unsigned long)BYTES_PER_SAMPLE << NL;
  out << F("log signature:"); printHex16(stored); ln();
  out << F("this build signature:"); printHex16((unsigned int)LOG_SIGNATURE); ln();
  if(logFormatMismatch){
    out << F("MISMATCH: record size below is a guess, dumping at") << (unsigned long)stride << NL;
  }
  out << F("Intel HEX follows. Keep from the first ':' to :00000001FF\n");

  if(nBytes==0){
    out << F("Log empty\n");
  }else{
    byte b[16];
    // 0xFFFF cannot be a real upper address here, so the first data record is
    // always preceded by its type 04 record and no parser has to assume a base.
    unsigned int upper=0xFFFF;
    unsigned long logDumpStart=millis();
    for(unsigned long a=0; a<nBytes; a+=16){
      unsigned long left=nBytes-a;
      byte n = (left>=16) ? 16 : (byte)left;
      unsigned int hi=(unsigned int)(a>>16);
      if(hi!=upper){
        byte ext[2]={ (byte)(hi>>8), (byte)hi };
        ihexRecord(0x04, 0x0000, ext, 2);
        upper=hi;
      }
      readBytesFromFlash(a,b,n);
      ihexRecord(0x00, (unsigned int)(a & 0xFFFFUL), b, n);
    }
    ihexRecord(0x01, 0x0000, NULL, 0);   // end of file
    unsigned long elapsedMs=millis()-logDumpStart;
    out << nBytes << F("bytes in") << '\xB2' << elapsedMs/10 << F("seconds\n");
  }
  flashPowerDown();
}

// Compares the channel set that wrote the log in flash against the one this
// firmware was built with. See the signature notes in the main file.
//
// Only a NON-EMPTY log can be wrong: with no samples there is nothing to be
// incompatible with, so the signature is simply refreshed and the new build
// adopts the log. A never-initialised EEPROM reads 0xFFFF, which is treated the
// same way rather than as a mismatch.
// Prints a 16-bit value as 0xXXXX. The stream has no hex mode, and the channel
// signatures are written in hex everywhere in the source, so a decimal 4127
// here would have to be converted by hand before it could be matched against
// the 0x101F in the channel block.
void printHex16(unsigned int v){
  out << NOSPACER << "0x";
  for(int8_t i=12;i>=0;i-=4){
    out << hexDigit(v>>i);
  }
  out << NORMALTEXT;
}

void checkLogFormat(){
  unsigned int stored=getUInt(LOG_SIGNATURE_ADDR);
  if(getCount()==0 || stored==LOG_SIGNATURE_NONE){
    EEPROM.put(LOG_SIGNATURE_ADDR,(unsigned int)LOG_SIGNATURE);
    logFormatMismatch=false;
    return;
  }
  logFormatMismatch = (stored != (unsigned int)LOG_SIGNATURE);
  if(logFormatMismatch){
    out << (char)(ASTERISK_BAR+3) << NL;
    out << F("LOG FORMAT CHANGED\n");
    out << F("in flash:") << getCount() << F("samples, signature");
    printHex16(stored); ln();
    out << F("this build's signature:");
    printHex16((unsigned int)LOG_SIGNATURE); ln();
    out << F("This build CANNOT read those records.\n");
    out << F("To recover them, re-flash the firmware that wrote them and dump BEFORE clearing.\n");
    out << F("Otherwise clear the log with RC.\n");
#if SUSPEND_ON_FORMAT_MISMATCH
    out << F("Logging is SUSPENDED until then.\n");
#endif
    out << (char)(ASTERISK_BAR+3) << NL;
    setupFailed=true;   // lights the red LED
  }
}

// ######### HELP ################
void printHelp(){
  out << (char)(ASTERISK_BAR+1) << F("HELP") << (char)(ASTERISK_BAR+1) << NL;
  char c;
  unsigned int charsSinceCommandStart=0;
  for(int a=HELP_TEXT; a<1024; a++){
    c=char(EEPROM.read(a));
    charsSinceCommandStart++;
    if (c=='$'){
      // Al menos un espacio: un comando de nueve caracteres o mas dejaba el
      // relleno en cero y su descripcion salia pegada, como ocurria con
      // "A01=/A02=A0 calibration".
      int pad = 10-(int)charsSinceCommandStart;
      if(pad<1){
        pad=1;
      }
      for(int i=0; i<pad; i++){
        out << ' ';
      }
    }else if (c=='\0'){
      ln();
      break;
    }else if (c=='\n'){
      ln();
      charsSinceCommandStart=0;
    }else{
      out << NOSPACER << c;
    }
  }
  displayCommands();
}

// ######### LOW LEVEL INTERNAL EEPROM READ - WRITE ###############
//This approach using EEPROM.get uses 60 bytes more than the one in GlaciarLapse
unsigned int getUInt(int address){
  unsigned int val;
  EEPROM.get(address,val);
  return val;
}

int getInt(int address){
  int val;
  EEPROM.get(address,val);
  return val;
}

unsigned long getULong(int address){
  unsigned long val;
  EEPROM.get(address,val);
  return val;
}

float getFloat(int address){
  float val;
  EEPROM.get(address,val);
  return val;
}
