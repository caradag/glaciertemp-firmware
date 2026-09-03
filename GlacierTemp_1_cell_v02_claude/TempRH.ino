
void getRTCtemp(){
  currentTemp=getRTCTemperature()*100;
  currentRH=-1;
}

void getTempAndRH() {
  Wire.beginTransmission(HDC1080_ADDR);
  Wire.write(0x00); // trigger temperature and humidity measurement
  Wire.endTransmission();
  
  delay(15); // wait for conversion (15ms typical)

  Wire.requestFrom(HDC1080_ADDR, 4);
  if (Wire.available() == 4) {
    uint16_t rawTemp = (Wire.read() << 8) | Wire.read();
    uint16_t rawHum  = (Wire.read() << 8) | Wire.read();

    currentTemp = (rawTemp / 65536.0) * 165.0 *100.0 - 4000.0;
    currentRH    = (rawHum / 65536.0) * 100.0 * 10.0;
    //currentTemp = (rawTemp * 0.25177) - 4000.0;
    //currentRH    = rawHum * 0.001526;
    //currentTemp = rawTemp/4 + rawTemp/565 - 4000;// This formula saves 20 bytes and can lead to errors up to 0.02 ºC
    //currentRH    = rawHum / 655;// This formula saves 20 bytes and can lead to errors up to 1%
  } else {
    // Error fallback
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

void tmp119WriteReg(byte reg, uint16_t value){
  Wire.beginTransmission(TMP119_ADDR);
  Wire.write(reg);
  Wire.write(value >> 8);
  Wire.write(value & 0xFF);
  Wire.endTransmission();
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

// Triggers one conversion, waits for it, and leaves the result in currentTempHA
// in hundredths of a degree C. The sensor returns to shutdown on its own.
void getHighAccuracyTemp(){
  uint16_t config;

  tmp119WriteReg(TMP119_CONFIG_REG, TMP119_AVERAGING);

  // Poll the Data_Ready flag (bit 13) rather than trusting a fixed delay.
  // Reading the configuration register clears the flag, so the temperature
  // register must be read right after.
  unsigned long start=millis();
  bool ready=false;
  while(millis()-start < TMP119_CONV_TIMEOUT){
    delay(5);
    if(!tmp119ReadReg(TMP119_CONFIG_REG, config)){
      currentTempHA=INVALID_TEMP_HA;
      return;
    }
    if(config & 0x2000){
      ready=true;
      break;
    }
  }
  if(!ready){
    currentTempHA=INVALID_TEMP_HA;
    return;
  }

  uint16_t raw;
  if(!tmp119ReadReg(TMP119_TEMP_REG, raw)){
    currentTempHA=INVALID_TEMP_HA;
    return;
  }
  // One LSB is 7.8125 m°C, so centi°C = raw*0.78125 = raw*25/32 exactly.
  // The cast to int reinterprets the two's complement value before widening.
  currentTempHA = ((long)(int)raw * 25) / 32;

  // Assert shutdown rather than trusting the one-shot to return there on its
  // own. The part is documented to drop back to shutdown when a one-shot
  // finishes, but the configuration register still reads MOD=11 afterwards, so
  // the state cannot be confirmed from outside -- and the difference between
  // being right and being wrong is ~16uA sitting under every sleep. One I2C
  // write per measurement removes the doubt, and makes the POWER_DEBUG dump
  // report MOD=01 instead of an ambiguous MOD=11.
  tmp119Sleep();
}
