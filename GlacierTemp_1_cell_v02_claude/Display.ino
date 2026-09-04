void printWaitingCommands(unsigned long timeLeft){
  out << (char)(ASTERISK_BAR+1) << F(" Waiting commands (for") << timeLeft/1000 << F("s)") << (char)(ASTERISK_BAR+1) << NL;
}

void displayInfo(){
  printRTCTime();

  out << F("Measurement interval:") << measureInterval << "seconds\n";
  out << F("Next measurement:"); displayDateVec(wakeupDateVec); ln();
  unsigned long maxMeasurements= (SECTOR_SIZE*(MAX_SECTORS+1))/BYTES_PER_SAMPLE;
  unsigned long measurementsCount=getCount();
  out << F("Measurement count:") << measurementsCount << '(' << (100*measurementsCount)/maxMeasurements << "% of a maximum of" <<  maxMeasurements << ")\n";

#if LOG_HDC_TEMP || LOG_HDC_RH
  getTempAndRH();
#endif
#if LOG_TMP119
  getHighAccuracyTemp();
#endif
#if LOG_DS18B20
  getDS18B20Temp();
#endif
#if LOG_HDC_TEMP
  out << F("Temperature (HDC1080):");
  if(currentTemp==INVALID_TEMP){
    out << F("NaN\n");
  }else{
    out << '\xB2' << currentTemp << F("°C\n");
  }
#endif
#if LOG_TMP119
  out << F("Temperature (TMP119):");
  if(currentTempHA==INVALID_TEMP_HA){
    out << F("NaN\n");
  }else{
    out << '\xB2' << currentTempHA << F("°C\n");
  }
#endif
#if LOG_DS18B20
  for(byte ds=0; ds<LOG_DS18B20; ds++){
    char label[8];
    ds18b20Label(ds,label);
    out << F("Temperature (") << NOSPACER << label << "):" << NORMALTEXT;
    if(currentTempDS[ds]==INVALID_TEMP){
      out << F("NaN\n");
    }else{
      out << '\xB2' << currentTempDS[ds] << F("°C\n");
    }
  }
#endif
#if LOG_HDC_RH
  out << F("Humidity:");
  // Humidity is never negative, so any negative value is a failed read --
  // including the -1 that older firmware used as its sentinel.
  if(currentRH<0){
    out << F("NaN\n");
  }else{
    out << '\xB1' << currentRH << "%\n";
  }
#endif
#if ANALOG_CHANNELS
  // Header H1. Reading these switches the ADC reference to VCC and back, so it
  // is done once for the whole group rather than per line.
  readAnalogChannels();
  for(byte pin=0; pin<4; pin++){
    if(!analogPinEnabled(pin)){
      continue;
    }
    out << analogPinName(pin) << ':';
    if(currentAnalog[pin]==INVALID_ANALOG){
      out << F("NaN\n");
    }else{
      out << '\xB3' << currentAnalog[pin] << "V\n";
    }
  }
#endif

  int battVoltage=getBatteryVoltage();
  out << F("Batt.:") << '\xB2' << battVoltage/10 << "V (" << voltageToCapacity(battVoltage) << "%)\n";
}

// Aviso destacado de reloj sin ajustar.
//
// Que el RTC marque una hora ANTERIOR a la compilacion del firmware solo puede significar
// que perdio la hora --pila agotada, primer arranque de la placa-- porque el firmware no
// puede haberse ejecutado antes de existir. Con esa condicion el logger sigue midiendo, pero
// cada marca de tiempo del log queda mal, que es un dato inservible: es un error, no un
// detalle.
//
// El LED rojo ya lo senalaba, pero un LED no dice CUAL de los fallos posibles ocurrio, y al
// reiniciar la placa no quedaba constancia de nada.
void reportClockNotSet(){
  // Tres lineas y no siete: en un ATmega328P cada literal ocupa flash, y las dos horas
  // juntas ya dicen por que la del RTC es imposible.
  out << (char)(ASTERISK_BAR+3) << NL;
  out << F("ERROR: RTC clock not set\n");
  out << F("  RTC reads:"); displayUnixTime(currentTime); ln();
  out << F("  Firmware built:"); displayUnixTime(COMPILATION_TIME); ln();
  out << (char)(ASTERISK_BAR+3) << NL;
}

void displayExtendedInfo(){  
  displayInfo();

  // Va aqui y no en displayInfo() para no repetirlo: displayInfo() ya se imprime en el
  // arranque, donde el identificador sale junto a la deteccion de la memoria.
  // Aqui la flash esta apagada, asi que hay que encenderla para leer el identificador.
  memSendControlByte(POWER_UP);
  printBoardIdLine();
  flashPowerDown();

  out << F("\tSesion start:"); displayUnixTime(sessionStartTime); ln();
  out << '\t' << '\xB1' << runningDays() << F("days running\n");
  out << F("\tExpected battery life:") << battDaysLeft() << F("days (drop") << battDrop() << F("% over") << '\xB1' << runningDays() << F("days)\n");
  
  out << F("\tLast time check:");
  // getULong, not getUInt: the stored value is a 32-bit timestamp, so a 16-bit
  // read could never exceed COMPILATION_TIME and this always printed "Never".
  if (getULong(LAST_RTC_TIME_CHECK)>COMPILATION_TIME){
     displayUnixTime(getULong(LAST_RTC_TIME_CHECK));
  }else{
    out << F("Never");
  }
  ln();
  out << F("\tRTC adjusted by:") << getInt(LAST_TIME_ADJUSTMENT_SECS) << "s (" << getInt(CUMULATIVE_TIME_ADJUSTMENT_SECS) << F("s accumulated)\n");

  // Awake time per measurement cycle, split into its three phases. NOSPACER
  // keeps the slashes tight against the numbers; NORMALTEXT restores the normal
  // word spacing for the rest of the line. Maxima are since the last RC.
  out << F("\tAwake ms pre/meas/post, last:") << NOSPACER
      << lastPreMs << "/" << lastMeasMs << "/" << lastPostMs
      << NORMALTEXT << F(" total")   // leading space: NOSPACER suppressed the one after the last number
      << (unsigned long)lastPreMs+lastMeasMs+lastPostMs << F("ms\n");
  out << F("\tAwake ms pre/meas/post, max: ") << NOSPACER
      << maxPreMs << "/" << maxMeasMs << "/" << maxPostMs
      << NORMALTEXT << NL;

  out << F("CONFIGURATION\n");
  displayVars(0,sizeof(varLengths)-1);
}
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


void ln() {
  out << NL;
}
void msgOK(){
  out.direct("OK\n");
}
void msgFail(){
  setupFailed=true;   // lights the red LED at the end of setup()
  out.direct("FAIL\n");
}