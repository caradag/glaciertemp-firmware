
void getRTCtemp(){
  currentTemp=getRTCTemperature()*100;
  currentRH=-1;
}

// Hasta SENSOR_TRIES intentos de una lectura. [attempt] devuelve 0 si leyo o el
// codigo del fallo, que se anota aunque el intento siguiente funcione: un fallo que un
// reintento arregla no deja hueco en el log, y sin anotarlo no quedaria rastro de el.
// Una funcion para los dos sensores I2C en vez de un bucle en cada uno.
bool readWithRetries(byte (*attempt)()){
  for(byte i=0;i<SENSOR_TRIES;i++){
    byte err=attempt();
    if(!err){
      return true;
    }
    logSensorError(err);
  }
  return false;
}

// Un intento de leer el HDC1080. Deja temperatura y humedad en currentTemp/currentRH.
byte hdc1080Attempt(){
  Wire.beginTransmission(HDC1080_ADDR);
  Wire.write(0x00); // trigger temperature and humidity measurement
  if(Wire.endTransmission()!=0){
    return ERR_HDC_TRIGGER;
  }

  delay(15); // wait for conversion (15ms typical)

  if(Wire.requestFrom(HDC1080_ADDR, 4)!=4){
    return ERR_HDC_READ;
  }
  uint16_t rawTemp = (Wire.read() << 8) | Wire.read();
  uint16_t rawHum  = (Wire.read() << 8) | Wire.read();

  // En enteros, sin coma flotante: T = raw/65536*165 - 40 C y RH = raw/65536*100 %,
  // en centesimas de grado y decimas de %. La division de C trunca hacia cero, igual
  // que el (int) sobre el float que habia antes, asi que bajo cero da lo mismo; y es
  // exacta: el float fallaba por una centesima en 22 de los 65.536 valores posibles.
  currentTemp = ((long)rawTemp*16500L - 4000L*65536L) / 65536L;
  currentRH   = ((long)rawHum*1000L) >> 16;
  return 0;
}

void getTempAndRH() {
  if(!readWithRetries(hdc1080Attempt)){
    currentTemp = INVALID_TEMP;
    currentRH = INVALID_RH;
  }
}


//####################################################################
//########## TMP119 ULTRA-HIGH ACCURACY TEMPERATURE (U24) ############
//####################################################################
// The TMP119 shares the I2C bus with the HDC1080 and the RTC. It is powered from
// the permanent 3.3V rail, NOT from MEM_POWER (that pin feeds only the flash),
// so it is always alive and there is nothing to power up before reading it.
//
// Out of reset the part free-runs in continuous conversion mode drawing ~16uA,
// which on its own is several times the whole logger's sleep budget. It is
// therefore parked in shutdown (0.15uA typ) and woken one conversion at a time.
// A one-shot conversion returns the device to shutdown automatically when it ends.

// Devuelve true si el sensor confirmo la escritura.
bool tmp119WriteReg(byte reg, uint16_t value){
  Wire.beginTransmission(TMP119_ADDR);
  Wire.write(reg);
  Wire.write(value >> 8);
  Wire.write(value & 0xFF);
  return Wire.endTransmission()==0;
}

// Returns false if the sensor does not acknowledge or returns short data
bool tmp119ReadReg(byte reg, uint16_t &value){
  Wire.beginTransmission(TMP119_ADDR);
  Wire.write(reg);
  if(Wire.endTransmission()!=0){
    return false;
  }
  if(Wire.requestFrom(TMP119_ADDR, 2)!=2){
    return false;
  }
  value  = ((uint16_t)Wire.read()) << 8;
  value |= Wire.read();
  return true;
}

// Park the sensor in shutdown. Called once at start-up, because the part boots
// into continuous conversion and would otherwise drain the cell between samples.
void tmp119Sleep(){
  tmp119WriteReg(TMP119_CONFIG_REG, TMP119_SHUTDOWN);
}

// One attempt: triggers one conversion, waits for it, and leaves the result in
// currentTempHA in hundredths of a degree C. Returns 0, or the code of the step that
// failed. The sensor returns to shutdown on its own after a one-shot.
byte tmp119Attempt(){
  uint16_t config;

  // Sin confirmacion no hay conversion, y esperarla solo acabaria en ERR_TMP_TIMEOUT
  // escondiendo que el sensor no recibio la orden.
  if(!tmp119WriteReg(TMP119_CONFIG_REG, TMP119_AVERAGING)){
    return ERR_TMP_TRIGGER;
  }

  // Poll the Data_Ready flag (bit 13) rather than trusting a fixed delay.
  // Reading the configuration register clears the flag, so the temperature
  // register must be read right after.
  unsigned long start=millis();
  bool ready=false;
  while(millis()-start < TMP119_CONV_TIMEOUT){
    delay(5);
    if(!tmp119ReadReg(TMP119_CONFIG_REG, config)){
      return ERR_TMP_POLL;
    }
    if(config & 0x2000){
      ready=true;
      break;
    }
  }
  if(!ready){
    return ERR_TMP_TIMEOUT;
  }

  uint16_t raw;
  if(!tmp119ReadReg(TMP119_TEMP_REG, raw)){
    return ERR_TMP_READ;
  }
  // One LSB is 7.8125 m°C, so centi°C = raw*0.78125 = raw*25/32 exactly.
  // The cast to int reinterprets the two's complement value before widening.
  currentTempHA = ((long)(int)raw * 25) / 32;
  return 0;
}

// Hasta SENSOR_TRIES intentos; INVALID_TEMP_HA si ninguno funciona.
void getHighAccuracyTemp(){
  if(!readWithRetries(tmp119Attempt)){
    currentTempHA=INVALID_TEMP_HA;
  }

  // Assert shutdown rather than trusting the one-shot to return there on its
  // own. The part is documented to drop back to shutdown when a one-shot
  // finishes, but the configuration register still reads MOD=11 afterwards, so
  // the state cannot be confirmed from outside -- and the difference between
  // being right and being wrong is ~16uA sitting under every sleep. One I2C
  // write per measurement removes the doubt, and makes the POWER_DEBUG dump
  // report MOD=01 instead of an ambiguous MOD=11. After a failed attempt it
  // matters more: the part may be left mid-conversion.
  tmp119Sleep();
}
