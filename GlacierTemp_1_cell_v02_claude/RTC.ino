
//                   Year  Month Day   Hour  Minute Second
byte dateAddress[]={0x06, 0x05, 0x04, 0x02, 0x01,  0x00};

// Those are the ALARM Bits that can be used. They need to be combined into a
// single value (see armAlarm1). Found here:
// https://github.com/mlepard/ArduinoChicken/blob/master/roboCoop/alarmControl.ino
#define ALRM1_MATCH_EVERY_SEC  0b1111  // once a second
#define ALRM1_MATCH_SEC        0b1110  // when seconds match, i.e. once a minute
#define ALRM1_MATCH_MIN_SEC    0b1100  // when minutes and seconds match, once an hour
#define ALRM1_MATCH_HR_MIN_SEC 0b1000  // when hours, minutes and seconds match, once a day

#define ALRM2_ONCE_PER_MIN     0b111   // once per minute (00 seconds of every minute)
#define ALRM2_MATCH_MIN        0b110   // when minutes match
#define ALRM2_MATCH_HR_MIN     0b100   // when hours and minutes match

// Upper bound on how many interval slots the arm-and-verify loop may skip in
// one call. Only reached if a single awake window lasts longer than eight
// intervals, in which case skipping is the correct answer anyway.
#define MAX_WAKEUP_SKIPS 8

// Coarsest Alarm-1 match mask that still identifies the wake-up instant
// unambiguously for a given interval.
//
// WHY THIS MATTERS. Alarm 1 with ALRM1_MATCH_HR_MIN_SEC matches hour, minute
// AND second (A1M4 is set, so the date is ignored), which means it fires
// exactly ONCE PER DAY. If the target second slips past while the alarm is
// still being armed, the flag is cleared by the arming sequence itself and the
// logger sleeps for a full 24 hours. Choosing a coarser mask does not change
// which instant the alarm fires at -- the registers still hold the exact target
// and the alarm is re-armed on every wake-up -- it only changes how long the
// hole is if one is ever missed: one second, one minute or one hour instead of
// one day.
//
// The masks are safe because the target is never further away than the period
// of the mask, so the next register match IS the target:
//   interval == 1 s     every second   the alarm cannot be missed at all
//   interval <= 60 s    seconds match  next occurrence of that second
//   interval <= 3600 s  min+sec match  next occurrence of that minute:second
//   longer              hr+min+sec     unavoidable, but such an interval loses
//                                      at most one sample to a missed alarm
byte alarm1MaskFor(unsigned long interval){
  if(interval<=1){
    return ALRM1_MATCH_EVERY_SEC;
  }
  if(interval<=60UL){
    return ALRM1_MATCH_SEC;
  }
  if(interval<=3600UL){
    return ALRM1_MATCH_MIN_SEC;
  }
  return ALRM1_MATCH_HR_MIN_SEC;
}

// Writes the Alarm-1 registers, enables the alarm and clears its flag.
// Split out of setWakeUp() so the arm-and-verify loop can call it repeatedly.
void armAlarm1(byte *dateVec, byte alarm1Set){
  // combine the AlarmBits. Alarm 2 is never enabled (A2IE stays clear below),
  // so only the low four bits, which belong to alarm 1, have any effect.
  int AlarmBits = alarm1Set;

  //Extracted from DS3231 library function Clock.turnOffAlarms();
  // turns off BOTH alarms. Leaves interrupt pin alone. Modify control byte
  writeControlByte(readControlByte(0) & 0b11111100, 0);

  // Extracted from DS3231 library function Clock.setA1Time (and the version I
  // had already modified setA1TimeLite). As the day is always masked off it was
  // simplified.
  Wire.beginTransmission(CLOCK_ADDRESS);
  Wire.write(0x07); // A1 starts at 07h
  // Send A1 second and A1M1
  Wire.write(decToBcd(dateVec[5]) | ((AlarmBits & 0b00000001) << 7));
  // Send A1 Minute and A1M2
  Wire.write(decToBcd(dateVec[4]) | ((AlarmBits & 0b00000010) << 6));
  // Sending A1 hour and A1M3
  Wire.write(decToBcd(dateVec[3]) | ((AlarmBits & 0b00000100) << 5));
  // A1 day/date and A1M4. A1M4 is always set: the date never takes part in the
  // match, which is what lets the same registers mean "every second", "every
  // minute" or "every hour" depending on the mask above.
  Wire.write(0x80);
  Wire.endTransmission();
  // Now we turn alarm 1 on: A1IE (bit 0) and INTCN (bit 2). A2IE (bit 1) is
  // deliberately left clear -- alarm 2 is unused.
  writeControlByte(readControlByte(0) | 0b00000101, 0);
  // Check Alarm and clear flag. Bit 3 (EN32kHz) is cleared in the same write:
  // the DS3231 powers up with its 32kHz output running and nothing on this
  // board uses it. Measured below the noise floor, but it is free.
  writeControlByte(readControlByte(1) & 0b11110110, 1);
}

// Computes the next measurement instant and arms the RTC for it.
//
// SCHEDULING. The wake-up instant is the next point on the fixed grid of
// interval-second slots measured from midnight, so the timestamps stay aligned
// to round numbers and do not drift with the length of the awake window.
//
// WHAT THIS REPLACED, AND WHY. The previous version pushed the wake-up out by a
// whole extra interval whenever the next slot was less than three seconds away:
//
//     if((nextSOD-SOD)<3){ nextSOD += interval; }
//
// The distance to the next slot is interval-(SOD%interval), which is between 1
// and interval, so for any short interval that test fires as a matter of
// arithmetic rather than as a matter of timing. A 1 s interval logged at 2 s, a
// 2 s interval logged at 4 s, and a 3 s interval logged at 3 s only while the
// awake window stayed inside a single second and at 6 s whenever it did not.
// Four seconds and above were unaffected, which is exactly where the symptom
// was first noticed.
//
// The guard was there to stop the logger from arming an alarm for an instant
// that had already passed by the time the registers were written, which with a
// once-per-day match mask costs 24 hours of data. Guessing a safe margin in
// advance is the wrong way to do that, because the guess has to be conservative
// enough for the worst case and is therefore wrong in the common case. Instead
// the alarm is armed and then CHECKED against a freshly read clock: if the
// target is no longer in the future it is moved on by one interval and re-armed.
//
// No minimum distance is imposed on the first alarm after a reset either. It
// would only be needed if a missed alarm were expensive, and with the mask
// selection above it is not: at one second the alarm repeats every tick and
// cannot be missed at all, and at anything up to a minute the worst case is one
// extra minute. Paying for that insurance with a deliberately late first sample
// is not worth it.
//
// That check is airtight in one direction, which is the one that matters. If
// the clock read after arming is still behind the target, then the target had
// not yet occurred when armAlarm1() cleared the alarm flag, so any flag that
// sets from now on is genuine. And if the target arrives in the window between
// the flag being cleared and the MCU actually powering down, the INT/SQW line
// latches low and stays low, so the LOW-LEVEL interrupt attached in goToSleep()
// fires immediately and the sleep simply returns. Nothing is lost either way.
void setWakeUp() {
  getCurrentTime();
  unsigned long dayStart=mktime2(currentDateVec[0],currentDateVec[1],currentDateVec[2]);// Unix time of the start of current day
  unsigned long SOD=(unsigned long)secOfDay(currentDateVec);

  // effectiveMeasureInterval() stretches the interval once the cell is low
  unsigned long interval = effectiveMeasureInterval();
  unsigned long nextSOD = SOD - (SOD % interval) + interval;

  byte alarm1Set = alarm1MaskFor(interval);

  unsigned long unixTime=dayStart+nextSOD;
  unix2date(unixTime,wakeupDateVec);// Add the new wake up time to the global variable wakeupDateVec
  armAlarm1(wakeupDateVec, alarm1Set);

  // Arm-and-verify. Skipped for a one-second interval: that mask fires on every
  // tick regardless of what the registers hold, so there is no instant to miss
  // and re-reading the clock could only push the wake-up needlessly further out.
  if(alarm1Set!=ALRM1_MATCH_EVERY_SEC){
    for(byte skips=0; skips<MAX_WAKEUP_SKIPS; skips++){
      getCurrentTime();
      if(currentTime<unixTime){
        break;   // target still ahead of the clock: armed correctly
      }
      // The target second has been reached or passed while we were arming.
      // Move to the following slot and arm again.
      nextSOD += interval;
      unixTime = dayStart+nextSOD;
      unix2date(unixTime,wakeupDateVec);
      armAlarm1(wakeupDateVec, alarm1Set);
    }
  }

  // Displaying info
  out << F("Next Wakeup:");
  displayDateVec(wakeupDateVec); ln();
}

int minOfDay(byte *dateVec){
  return dateVec[3]*60+dateVec[4];
}

long secOfDay(byte *dateVec){
  return ((long)dateVec[3])*3600L+((long)dateVec[4])*60L+((long)dateVec[5]);
}

void getCurrentTime(){
  // Look at https://docs.macetech.com/doku.php/chronodot_v2.0 the first four lines in the loop can be taken out and request the six values at onece and assig them starting from seconds
  // the problem is to skip the value in between the hours and the day (0x03), no much to gain but it would ran faster
  //           Year       Month      Day        Hour       Minute     Second
  byte mask[]={0b11111111,0b01111111,0b11111111,0b00111111,0b11111111,0b11111111};
  for(int i=0; i<6; i++){
    Wire.beginTransmission(CLOCK_ADDRESS);
    Wire.write(dateAddress[i]);
    Wire.endTransmission();
    Wire.requestFrom(CLOCK_ADDRESS, 1);
    currentDateVec[i]=  (bcdToDec(Wire.read() & mask[i])) ;    
  }  
  currentTime=mktime2(2000+currentDateVec[0],currentDateVec[1],currentDateVec[2],currentDateVec[3],currentDateVec[4],currentDateVec[5]);
}

void setDateVector(byte *dateVec) {
  //           Year       Month      Day        Hour       Minute     Second
  byte mask[]={0b11111111,0b11111111,0b11111111,0b10111111,0b11111111,0b11111111};
  // The bite mask for the Hour is needed and ir guarantee that the hour is set in 24h mode

  for(int i=0; i<6; i++){
    Wire.beginTransmission(CLOCK_ADDRESS);
    Wire.write(dateAddress[i]);
    Wire.write(decToBcd(dateVec[i]) & mask[i]); 
    Wire.endTransmission(); 
  }
  // This function also resets the Oscillator Stop Flag (OSF), which is set
  // whenever power is interrupted.
  writeControlByte((readControlByte(1) & 0b01111111), 1);
  getCurrentTime();
}

// TIME=yyyy-mm-dd HH:MM  o  TIME=yyyy-mm-dd HH:MM:SS
//
// Los segundos son opcionales para no romper lo que ya se escribe a mano, pero hacen falta
// para sincronizar contra el reloj de un telefono: sin ellos el ajuste arrastra hasta 59 s
// de error, que es mucho mas que la deriva que se pretende corregir.
//
//         1111111111222
// 1234567890123456789012
// TIME=2023-10-23 12:11:30
bool manualClockAdjust(char *inputStr){
  byte newDateVec[6]={0};
  for(int i=2;i<7;i++){
    newDateVec[i-2]=readLong(inputStr,i*3+1,i*3+2);
  }
  // Se mira la longitud antes del caracter: sin esto, una cadena corta hace leer memoria
  // sin inicializar del buffer, que un dia de cada 256 contiene ':'.
  if(strlen(inputStr)>=24 && inputStr[21]==':'){
    newDateVec[5]=readLong(inputStr,22,23);
  }
  if(inputStr[4]=='=' && inputStr[9]=='-' && inputStr[12]=='-' && inputStr[15]==' ' && inputStr[18]==':' && newDateVec[0]>24 && newDateVec[1]>0 && newDateVec[1]<13 && newDateVec[2]>0 && newDateVec[2]<32 && newDateVec[3]<25 && newDateVec[4]<60 && newDateVec[5]<60){
    setDateVector(newDateVec);
    return true;
  }else{
    out << F("Wrong format:") << inputStr << NL;
  }
  return false;
}

void autoClockAdjust(unsigned long newTime){
  byte dateVector[6];
  unix2date(newTime,dateVector);
  long deltaSeconds=newTime-currentTime;
  
  if (abs(deltaSeconds)>0){
    // Updating RTC time
    setDateVector(dateVector);                      
    out << F("Clock adjusted") << deltaSeconds << " s\n";
                
    // Storing the last applied time adjustment
    EEPROM.put(LAST_TIME_ADJUSTMENT_SECS,(int)deltaSeconds);

    if (deltaSeconds<31536000){// We add the ajustment to the cumulative adjustment only if is less than a year, to avoid loosing time-drift data due to the first adjustment
      int cumulativeTimeAdjustment=deltaSeconds+getInt(CUMULATIVE_TIME_ADJUSTMENT_SECS);
      EEPROM.put(CUMULATIVE_TIME_ADJUSTMENT_SECS,cumulativeTimeAdjustment);
    } 
  }
  out << F("Ref. time:");
  displayDateVec(dateVector); ln();
  printRTCTime();
}
void displayDateVec(byte *dateVec, bool showTimezone){
    out << DATETIME << dateVec[0]+2000 << '-' << dateVec[1] << '-' << dateVec[2] << ' ' << dateVec[3] << ':' << dateVec[4] << ':' << dateVec[5] << NORMALTEXT;
    if(showTimezone){
      out << NOSPACER << " (UTC" << '+' << timeZone << ')';
    }
}

void updateTimeZone(int oldTimeZone){
  byte newDateVec[6];
  unix2date(currentTime+(timeZone-oldTimeZone)*3600,newDateVec); 
  setDateVector(newDateVec);
}
void displayDate(unsigned int yyyy,unsigned int mm,unsigned int dd){
    out << DATETIME << yyyy << '-' << mm << '-' << dd << NORMALTEXT;
}

void displayTime(unsigned int HH,unsigned int MM,unsigned int SS){
    out << DATETIME << HH << ':' << MM;
    if(SS<60){
      out << ':' << SS;
    }
    out << NORMALTEXT;
}

void displayUnixTime(unsigned long uTime, bool showTimezone){
  byte dateVec[6];
  unix2date(uTime,dateVec);
  displayDateVec(dateVec, showTimezone);
}

void displayMinOfDay(int MOD){
  int minutes=MOD % 60;
  int hours=(MOD-minutes)/60;
  displayTime(hours,minutes);
  out << 'h' << NL;
}

int wrapMinOfDay(int MOD){
  if (MOD>=1440){
    MOD-=1440;
  }
  if (MOD<0){
    MOD+=1440;
  }
  return MOD;
}

uint32_t mktime2(int YYYY,int MM,int DD,int hh,int mm,int ss){
    // Function to turn date elements into a a variation of UNIX timestamp that starts from year 2000. 
    // To gransform in standard UNIX timestamps add 946710360
    // VALID ONLY UNTIL YEAR 2100
    if(YYYY<255){
      YYYY+=2000;
    }
    int leaps=((YYYY-2000)/4)+1;
    if ((YYYY % 4)==0){
      leaps--;
    }
    return (uint32_t)(YYYY-2000)*365*86400 + (uint32_t)(leaps + dayOfYear(YYYY, MM, DD) - 1)*86400 + (uint32_t)hh*3600 + mm*60 + ss ;
}

int dayOfYear(int yyyy, int mm, int dd){
    // VALID ONLY UNTIL YEAR 2100
    return month2DOY(yyyy, mm)+ dd;
}

int month2DOY(int yyyy, byte mm){
  // VALID ONLY UNTIL YEAR 2100  
  // Returns the number of days elapsed before the start of a given month, which are:
  //int DOY[12]={0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  // Whith a correction for leap years
  // To save memory, the avove values were approximated as mm*30 and only the difference to that approximation need to be stored, which is
  // deviations = 0     1    -1     0     0     1     1     2     3     3     4     4
  // To avoid negative numbers and save memory we can add one to get
  // 1     2     0     1     1     2     2     3     4     4     5     5
  // Which can be stored as a byte.
  // To optimize further, the above sequence con be approximated by the function mm/2, with mm from 0 to 11, which in integer arithmetic results in the sequence
  // 0 0 1 1 2 2 3 3 4 4 5 5
  // The deviation from this sequence would be
  // 1     2    -1     0    -1     0    -1     0     0     0     0     0
  // adding one results in
  // 2     3     0     1     0     1     0     1     1     1     1     1
  // which with the exception of january and february can be stored asa boolean
  static const bool DOYdeviation[12]={1,1,0,1,0,1,0,1,1,1,1,1};
  // we take mm from the ragne 1-12 to 0-11
  mm--;
  // Then we calculate as: 30*mm
  // plus the first correction: +mm/2
  // plus the second correction: +DOYdeviation[mm]
  // minus the offset applied to make all corrections positive: -2
  // plus the extra day for January a February: +(mm<2)
  // plus an additional extra day for february: +(mm==1)
  // plus an extra day for February in leap years: +((yyyy % 4)==0 & mm>1)

  // Parenthesised and using && : '&' bound tighter than the comparison, so
  // this read as ((yyyy%4)==0) & (mm>1) only by luck of operator precedence.
  return 30*mm+mm/2+DOYdeviation[mm]-2+(mm<2)+(mm==1)+(((yyyy % 4)==0) && (mm>1));
}

void unix2date(uint32_t utime,byte *dateVec){
    // Function to turn a UNIX timestamp INTO date elements
    // This use a modified Unix timestap starting from year 2000
    // dateVec must a 6-element int array
    // VALID ONLY UNTIL YEAR 2100
    //                  secs in 4 years  secs in a day  secs in an hour  secs in a min
    uint32_t blocks[4]={126230400,       86400,         3600,            60};

    // We calculate how many of these bolcks fit in the utime and store in the same array
    for(int i=0;i<4;i++){
      uint32_t tmp= utime % blocks[i];
      blocks[i]=(utime-tmp)/blocks[i];
      utime=tmp;
    }
    // At this point blocks contain the number of 4-year periods, the number of remaining days, remaining hours and minutes
    // Now we calculate how many single years have elapsed since the last 4-year full block
    int nOneYear=0;
    //  days       days in a leap year
    if (blocks[1]>=366){// The first year after the block is always a leap year, if there are more than 366 days we calculate how many years are there
        blocks[1]--;
        nOneYear=blocks[1]/365;
        blocks[1]=blocks[1] % 365;
    } 
    int yy = blocks[0]*4+nOneYear;
    // Now blocks[1] contains the Day Of Year, we have to find out to which month and day of month does this corresponds
    int mm=12;
    int dd=0;
    while (dd<=0){
        dd=blocks[1]-month2DOY(yy,mm)+1;
        if (dd>0){
            break;
        }
        mm--;
    }
    *(dateVec)=yy; 
    *(dateVec+1)=mm;
    *(dateVec+2)=dd;
    *(dateVec+3)=blocks[2];
    *(dateVec+4)=blocks[3];    
    *(dateVec+5)=utime;    
}


float runningDays(){
  return ((float)(currentTime-sessionStartTime))/86400;
}

void printRTCTime(){
  out << F("Time:");  displayDateVec(currentDateVec); ln();
}

float getRTCTemperature() {
  // Checks the internal thermometer on the DS3231 and returns the 
  // temperature as a floating-point value.

  // Updated / modified a tiny bit from "Coding Badly" and "Tri-Again"
  // http://forum.arduino.cc/index.php/topic,22301.0.html
  
  byte tMSB, tLSB;
  float temp3231;
  
  // temp registers (11h-12h) get updated automatically every 64s
  Wire.beginTransmission(CLOCK_ADDRESS);
  Wire.write(0x11);
  Wire.endTransmission();
  Wire.requestFrom(CLOCK_ADDRESS, 2);

  // Should I do more "if available" checks here?
  if(Wire.available()) {
    tMSB = Wire.read(); //2's complement int portion
    tLSB = Wire.read(); //fraction portion

    temp3231 = ((((short)tMSB << 8) | (short)tLSB) >> 6) / 4.0;
  }
  else {
    temp3231 = NAN; // Some obvious error value
  }
   
  return temp3231;
}

int frequency() {
  // Calculate the real frequency at which the ATMEGA328 processor is running by comparing the timing of the processor
  // With the DS3231 RTC. For this it sets the SQW pin of the RTC as signal generator at 1024 kHz 
    Wire.beginTransmission(CLOCK_ADDRESS);
    Wire.write(0x0E);
    Wire.write(0b01001000); // SQW 1024Hz is set
    Wire.endTransmission();

    while (digitalRead(WAKEUP_PIN)==LOW) ;
    while (digitalRead(WAKEUP_PIN)==HIGH) ;
    uint32_t t = micros();
    for (int i=0;i<256;i++) {
        while (digitalRead(WAKEUP_PIN)==LOW) ;
        while (digitalRead(WAKEUP_PIN)==HIGH) ;
    }
    return (int32_t)((micros()-t)*4)/125;//625
}       

byte decToBcd(byte val) {
// Convert normal decimal numbers to binary coded decimal
  return ( (val/10*16) + (val%10) );
}

byte bcdToDec(byte val) {
// Convert binary coded decimal to normal decimal numbers
  return ( (val/16*10) + (val%16) );
}

byte readControlByte(bool which) {
  // Read selected control byte
  // first byte (0) is 0x0e, second (1) is 0x0f
  selectControlByte(which);
  Wire.endTransmission();
  Wire.requestFrom(CLOCK_ADDRESS, 1);
  return Wire.read(); 
}

void writeControlByte(byte control, bool which) {
  // Write the selected control byte.
  // first byte (0) is 0x0e, second (1) is 0x0f  
  selectControlByte(which);
  Wire.write(control);
  Wire.endTransmission();
}

void selectControlByte(bool which) {
  // Write the selected control byte.
  // which=false -> 0x0e, true->0x0f.
  Wire.beginTransmission(CLOCK_ADDRESS);
  if (which) {
    // second control byte
    Wire.write(0x0f);
  } else {
    // first control byte
    Wire.write(0x0e);
  }
}

bool i2c_DeviceConnected(byte ADDRESS) {
  Wire.beginTransmission(ADDRESS);
  byte error = Wire.endTransmission();
  return (error == 0); // 0 = success
}