//####################################################################
//################ CONT: CAPTURA CONTINUA (perfiles con dron) ########
//####################################################################
// "CONT ON" mide y graba sin pausa entre registros, hasta "CONT OFF", una hora o
// la memoria llena. El intervalo es lo que tardan las lecturas y la escritura:
// ~140 ms con la TMP119 a 8 promedios, que es la que manda.
//
// NO toca el ciclo normal. Es un bucle dentro de la sesion de comandos, igual que
// LIVE: no pasa por goToSleep() ni por la alarma del RTC. Al volver, la sesion
// sigue como despues de cualquier otro comando.
//
// EL LOG. Solo arranca con el log vacio (descargar y RC antes), y lo marca con la
// firma de version 2 (LOG_SIGNATURE_CONT): los mismos canales de esta compilacion
// con un uint16 de milisegundos tras los segundos. Despues, el registro normal
// queda suspendido por la comprobacion de formato hasta otro RC, asi que un log
// nunca mezcla las dos clases de registro.
//
// LA HORA. El RTC solo da segundos. Al empezar se espera su cambio de segundo y
// desde ahi cuenta millis(); cada registro lleva t0 + transcurrido. Al terminar se
// vuelve a esperar un cambio de segundo y se informa la deriva de millis() frente
// al RTC: ~7 ms en 20 min con el cristal a -6 ppm medido en la GT001-1B4237.
// La hora del registro es la del COMIENZO de sus lecturas.
//
// EL CONTADOR va en RAM. Cada minuto y al terminar se graba, pero NO en las ranuras
// del contador: se mueve la referencia del ultimo reset, COUNT_RESET = total - n,
// y getCount() = total - COUNT_RESET da n (en aritmetica sin signo, aunque n supere
// al total). Asi las ranuras siguen contando solo escrituras reales --cada una
// aguanta 50000-- y CONT no gasta ninguna: un registro cada ~140 ms las agotaria en
// unos 175 vuelos. Un corte a mitad de vuelo pierde a lo sumo el ultimo minuto del
// contador; los registros siguen en la flash, a la vista de LOGH.
//
// EL CALENTADOR (CONT ON+H) es el bit HEAT del HDC1080. Por diseno del sensor
// (hoja de datos 8.3.3) solo calienta mientras convierte, asi que en CONT esta
// encendido durante cada medida del HDC, y nada entre medidas.

#if CONT_CAPTURE
#define CONT_MAX_MS     3600000UL   // una hora: el tope si nadie manda CONT OFF
#define CONT_STATUS_MS    10000UL   // una linea de estado cada 10 s, para probar en tierra
#define CONT_COMMIT_MS    60000UL   // el contador a la EEPROM cada minuto

#endif

// El bit HEAT en el registro de configuracion (02h). El resto del registro queda en
// el valor de fabrica que el logger supone: T y HR en secuencia, 14 bits cada una.
void hdcHeater(bool on){
  Wire.beginTransmission(HDC1080_ADDR);
  Wire.write(0x02);
  Wire.write(on ? 0x30 : 0x10);
  Wire.write(0x00);
  Wire.endTransmission();
}

#if CONT_CAPTURE
// Espera el cambio de segundo del RTC y devuelve millis() en ese instante, con
// currentTime ya en el segundo nuevo. Cada lectura del RTC tarda ~1 ms, que es la
// resolucion del ajuste.
unsigned long waitSecondEdge(){
  getCurrentTime();
  unsigned long antes=currentTime;
  unsigned long inicio=millis();
  while(currentTime==antes && millis()-inicio<1500){
    getCurrentTime();
  }
  return millis();
}

void contCapture(bool heater){
  if(getCount()>0){
    out << F("CONT needs an empty log: download it, then RC\n");
    return;
  }
  memSendControlByte(POWER_UP);
  EEPROM.put(LOG_SIGNATURE_ADDR,(unsigned int)LOG_SIGNATURE_CONT);
#if PIN_POWER_MASK
  analogPowerHoldOn();   // los sensores conmutados, encendidos toda la captura
#endif
  if(heater){
    hdcHeater(true);
  }
  unsigned long m0=waitSecondEdge();
  unsigned long t0=currentTime;
  EEPROM.put(RESET_TIME_ADDR,t0);
  out << NOSPACER << F("CONT begin heater=") << (heater ? F("on") : F("off"));
  out << F(" rec=") << (unsigned long)BYTES_PER_SAMPLE_CONT << NL;

  byte buf[BYTES_PER_SAMPLE_CONT];
  unsigned long total=getFullCount();
  unsigned long n=0;
  // anterior: comienzo del ciclo previo; ultimo: comienzo del ultimo registro
  // GRABADO, que es de donde sale la media (un ciclo que termina sin grabar, por
  // tiempo o memoria, no cuenta). maxDt en 16 bits: ningun intervalo se acerca a
  // 65 s, y en 32 la aritmetica no cabe en el programa.
  unsigned long anterior=m0, ultimo=m0;
  unsigned int maxDt=0;
  unsigned long ultimoEstado=m0, ultimoCommit=m0;
  // stop, time, full, battery
  char motivo='s';
  while(true){
    unsigned long ahora=millis();
    unsigned long e=ahora-m0;
    if(n>0){
      unsigned int dt=(unsigned int)(ahora-anterior);
      if(dt>maxDt){
        maxDt=dt;
      }
    }
    anterior=ahora;
    if(e>=CONT_MAX_MS){
      motivo='t';
      break;
    }
    currentTime=t0+e/1000;
    unsigned int ms=e%1000;

    int battMv=readSensors();   // los mismos sensores que takeMeasurement()

    // Los campos van CONT_MS_BYTES mas alla que en un registro normal: se arma el
    // registro normal desde buf+2 y luego se escriben delante los segundos (que
    // pisan la copia de packRecord) y los milisegundos.
    packRecord(buf+CONT_MS_BYTES, currentTime, battMv);
    memcpy(buf, &currentTime, 4);
    memcpy(buf+4, &ms, 2);
    uint32_t addr=n*(uint32_t)BYTES_PER_SAMPLE_CONT;
    if(checkMemoryOverflow(addr+BYTES_PER_SAMPLE_CONT)){
      motivo='f';
      break;
    }
    if(writeBytesToFlash(addr, buf, BYTES_PER_SAMPLE_CONT)){
      n++;
      ultimo=ahora;
    }else{
      logSensorError(ERR_FLASH_WRITE);   // el siguiente intento reusa el hueco
    }

    if(millis()-ultimoCommit>=CONT_COMMIT_MS){
      EEPROM.put(COUNT_RESET_ADDR, total-n);
      ultimoCommit=millis();
    }
    // El estado: cada CONT_STATUS_MS, o enseguida si llego un CONT? (que adelanta
    // ultimoEstado). Un solo sitio que lo imprime.
    if(millis()-ultimoEstado>=CONT_STATUS_MS){
      out << NOSPACER << F("CONT n=") << n << F(" t=") << e/1000 << F("s\n");
      printLiveSample(battMv);
      ultimoEstado=millis();
    }
#if CRITICAL_VOLTAGE
    if(battMv<=CRITICAL_VOLTAGE){
      motivo='b';
      break;
    }
#endif
    // Solo dos ordenes. Lo demas --tambien lo que el modulo Bluetooth escribe al
    // conectar y desconectar-- se ignora y no detiene nada: en vuelo la conexion se
    // cae, y la captura no puede depender de ella.
    if(Serial.available()){
      char cmd[16];
      byte len=Serial.readBytesUntil('\n', cmd, sizeof(cmd)-1);
      if(len>0 && cmd[len-1]=='\r'){
        len--;
      }
      cmd[len]='\0';
      if(!strcasecmp("CONT OFF", cmd)){
        break;
      }
      if(!strcasecmp("CONT?", cmd)){
        ultimoEstado=millis()-CONT_STATUS_MS;
      }
    }
  }
  unsigned long dur=ultimo-m0;   // del primer registro (en m0) al ultimo

  // Deriva: millis() transcurrido menos lo que dice el RTC, en el siguiente cambio
  // de segundo. Positiva si millis() se adelanta. Su resolucion es la de
  // waitSecondEdge(), ~1-2 ms.
  unsigned long m1=waitSecondEdge();
  long deriva=(long)(m1-m0)-(long)(currentTime-t0)*1000L;

  EEPROM.put(COUNT_RESET_ADDR, total-n);
  hdcHeater(false);
#if PIN_POWER_MASK
  analogPowerRelease();
#endif
  flashPowerDown();
  if(n==0){
    // Nada grabado: el log sigue vacio y vuelve a ser de esta compilacion.
    EEPROM.put(LOG_SIGNATURE_ADDR,(unsigned int)LOG_SIGNATURE);
  }
  logFormatMismatch=(n>0);

  out << NOSPACER << F("CONT end reason=");
  if(motivo=='s'){
    out << F("stop");
  }else if(motivo=='t'){
    out << F("time");
  }else if(motivo=='f'){
    out << F("full");
  }else{
    out << F("battery");
  }
  out << F(" n=") << n << F(" dur=") << dur;
  out << F("ms mean=") << (n>1 ? dur/(n-1) : 0UL);
  out << F("ms max=") << maxDt;
  out << F("ms drift=") << deriva << F("ms heater=") << (heater ? F("on") : F("off")) << NL;
}
#endif // CONT_CAPTURE
