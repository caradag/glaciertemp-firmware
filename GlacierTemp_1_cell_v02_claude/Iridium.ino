
void iridiumNotInstalled(){
  out << F("This system have no Iridium modem installed\n");
}
void noIridiumOrGpsInstalled(){
  out << F("This system have no GPS or Iridium modem installed\n");
}
#if IRIDIUM_INSTALLED == 1

bool iridiumStart(){
  digitalWrite(ACCESORY_POWER,ACCESORY_ON);
  out << "Iridium" << PRINT;
  SoftSerial.begin(19200); // Start the serial port connected to the satellite modem
  // Begin satellite modem operation
  char statusResponse[5];
  delay(1000);
  for(int tries=0;tries<4;tries++){
    out << '.' << PRINT;
    if(getATresponse("", statusResponse,sizeof(statusResponse),5000)==0){
      //out << F("OK\n");
      msgOK();
      return true;
    }
  }
  out << F("fail\n");
  return false;  
}

void iridiumSendMessage(){
  unsigned long iniTime=millis();
  if(!iridiumStart()){
    iridiumSleep();
    return;
  }
  int signalQuality=getIridiumSignal();
  out << F("Signal:") << signalQuality << NL;  

  #if LONG_MASSEGES == 1    
    // Longest 100 byte message:
    //99999Pics(99999 in session,99999Not confirmed),8.40V,+20.1C[-12.5,+25.2],51%,Batt. until yyyy-mm-dd
    //12345678910121416182022242628303234363840424446485052545658606264666870727476788082848688909294969810

    //In terms of cost 1 or 50 bytes both cost one credit
    // Creating message

    // Total picture count
    out << NOSPACER << "+SBDWT=" << getCount() << "Pics(" << sessionPictureCount << "inSession," << sessionPictureCount-sessionHotshoeConfirmationCount << "notConfirmed),";
    // Battery voltage   
    out << '\xB2' << getBatteryVoltage()/10 << "V,";
    // Current temperature and [minimum, maximum] since last message
    getTempAndRH();    
    out << '\xB1' << currentTemp/10 << "C[" << '+' << '\xB1' << minTemp/10 << ',' << '+' << '\xB1' << maxTemp/10 << "],RH" << currentRH << "%,Batt.until ";
    unsigned long battUntil=currentTime+(battDaysLeft()*86400);
    byte battDateVec[6];
    unix2date(battUntil,battDateVec);
    displayDate(battDateVec[0]+2000,battDateVec[1],battDateVec[2]);
    out << PRINT;
  #else
    // Longest 50 byte message:
    //99999P,8.40V,+20.1C[-12.5,+25.2],51%,99999,99999NC
    //123456789101214161820222426283032343638404244464850

    // Creating message

    // Total picture count
    out << NOSPACER << "+SBDWT=" << getCount() << 'P' << '\xB2' << getBatteryVoltage()/10 << "V,";
    // Current temperature and [minimum, maximum] since last message
    getTempAndRH();    
    out << '\xB1' << '+' << currentTemp/10 << "C[" << '\xB1' << '+' << minTemp/10 << ',' << '\xB1' << '+' << maxTemp/10 << "]," << currentRH << "%," << sessionPictureCount << ',' << sessionPictureCount-sessionHotshoeConfirmationCount << "NC" << PRINT;
  #endif

  if(signalQuality<2){
    // Not enough
  }else{
    out.direct("\nSending...");
    int status = sendIridiumTextMessage(buf);
    if (status == 0){
      messageSent=true;
      // We reset minimum and maximum temperatures
      minTemp=INVALID_TEMP;
      maxTemp=INVALID_TEMP;      
      msgOK();
      getIridiumTime();
      getIridiumPos(false);
      soundMsg(500,3);   
    }else{
      msgFail();
    }
  }
  out.clear();
  out << NL << (millis()-iniTime)/1000 << F(" sec elapsed\n");
  //out << getIridiumPowerUsage() << "uAh\n";
  
  // Turning modem off
  iridiumSleep();
}

void iridiumSleep(){
  digitalWrite(ACCESORY_POWER,ACCESORY_OFF);
}

// Iridium module documentation at
// https://docs.rockblock.rock7.com/docs/transmit-ascii-data
// AT commands documentation at
// https://www.groundcontrol.com/us/wp-content/uploads/sites/4/2022/02/IRDM_ISU_ATCommandReferenceMAN0009_Rev2.0_ATCOMM_Oct2012.pdf

void getIridiumPos(bool doUpdate){
  // Function to retreive coordinates from Iridium modem
  // The AT command -MSGEO return geocentric coordinates (1 km resolution) and the time at which that coordinate was updated
  // this function do not read that time, so it is recomended to use it after a successful time reteival from the network or a sent message.
  // There is an AT command -MSGEOS that provides coordinates in spherical lat lon format, but it is not implemented in Iridium 9603 and 9602 modems
  // so the only option is to perform the conversion from geocentric to spherical coordinates, which is implemented in this function.
  char geoResponse[30];
  if(getATresponse("-MSGEO", geoResponse,sizeof(geoResponse),10000)==0){
    byte comma1= findChar(geoResponse,0,sizeof(geoResponse), ',');
    long x= readLong(geoResponse,0,comma1-1);
    byte comma2= findChar(geoResponse,comma1+1,sizeof(geoResponse), ',');
    long y= readLong(geoResponse,comma1+1,comma2-1);
    byte comma3= findChar(geoResponse,comma2+1,sizeof(geoResponse), ',');
    long z1= readLong(geoResponse,comma2+1,comma3-1);
    // This prints the positions of the commas
    //out << comma1 << ',' << comma2 << ',' << comma3 << NL;
    // This prints the parsed geocentric cordinates X, Y, Y
    out << geoResponse << NL;
    out << x << ',' << y << ',' << z1 << NL;

    // Trasformig geocentric coordinates to lat lon
    const float a=6356.7523141; 
    const float b=6378.1370;

    int signos=1;
    if(z1<0){
      signos=-1;
    }
    z1=-abs(z1);

    float iridiumLon=atan2(y,x)*180/pi; //Longitude
    float d1=sqrt(x*x+y*y);
    float z2=-sqrt(1/((1/(a*a))+(1/(b*b*(z1/d1)*(z1/d1)))));
    float d2=z2/(z1/d1);
    float p2=-a*d2/(b*b*sqrt(1-(d2*d2/(b*b)))); //slope of the tangent to the ellipse in the point
    float dp,zp;
    for(int i=1;i<=5;i++){
        //points in the palne
        dp=(z1-z2-(d1/p2)+(p2*d2))/(p2-(1/p2));
        zp=(p2*dp)+z2-(p2*d2);
        //points in the ellipse
        z2=-sqrt(1/((1/(a*a))+(1/(b*b*(zp/dp)*(zp/dp)))));
        d2=z2/(zp/dp);
        p2=-a*d2/(b*b*sqrt(1-(d2*d2/(b*b))));
    }
    float iridiumLat=-90.0-(atan(p2)*180/pi);   //geographic latitude
    iridiumLat=abs(iridiumLat)*signos;
    if(abs(z1)<0.000001){
      iridiumLat=0;
    }

    out << "Lat:" << '\xB2' << iridiumLat << "Lon:" << '\xB2' << iridiumLon << NL;
    if(doUpdate){
      latitude=iridiumLat;
      longitude=iridiumLon;
      updateVar(LATITUDE,iridiumLat);
      updateVar(LONGITUDE,iridiumLon);
    }
    updateMinMaxTime();
    setWakeUp();
    displayVars(LATITUDE,LONGITUDE);
    displaySolarMaskStatus();
  }else{
    out << "No pos\n";
  }
}

int getIridiumSignal(){
  // Get iridium signal in scale 0-5
  char signalResponse[5];
  if(getATresponse("+CSQ", signalResponse,sizeof(signalResponse),10000)==0){
    return readInt(signalResponse);
  }else{
    return -1;
  }
}

long getIridiumPowerUsage(){
  // Get iridium power usage
  char powerResponse[12];
  if(getATresponse("+GEMON", powerResponse,sizeof(powerResponse),10000)==0){
    //out << powerResponse << NL;
    return readInt(powerResponse);
  }else{
    return -1;
  }
}

int sendIridiumTextMessage(char *msg){
  // Function to send an iridium text message
  // The message MUST start with "+SBDWT=" followed by the text to send, this can easily modified but in this particular implementation it was the 
  // most efficient way in terms of code size
  char statusResponse[24];
  if(getATresponse("&K0", statusResponse,sizeof(statusResponse),10000)==0){
    //Flow control has been disabled
  }else{
    return -1;
  }
  if(getATresponse(msg, statusResponse,sizeof(statusResponse),15000)==0){
    //Message has been inserted into MO buffer
  }else{
    return -1;
  }
  unsigned long iridiumWait = millis();
  int status=-1;
  while (millis()-iridiumWait < 60000){
    if(getATresponse("+SBDIX", statusResponse,sizeof(statusResponse),30000)==0){
      //out.direct(statusResponse);ln();
      byte comma1= findChar(statusResponse,0,sizeof(statusResponse), ',');
      status= readLong(statusResponse,0,comma1-1);
      //out.direct(status);ln();

      // The following are the possible status values
      // 0 MO message, if any, transferred successfully.
      // 1 MO message, if any, transferred successfully, but the MT message in the queue was too big to be transferred.
      // 2 MO message, if any, transferred successfully, but the requested Location Update was not accepted.
      // 3..4 Reserved, but indicate MO session success if used.
      // 5..8 Reserved, but indicate MO session failure if used.
      // 10 GSS reported that the call did not complete in the allowed time.
      // 11 MO message queue at the GSS is full.
      // 12 MO message has too many segments.
      // 13 GSS reported that the session did not complete.
      // 14 Invalid segment size.
      // 15 Access is denied.
      // ISU-reported values:
      // 16 ISU has been locked and may not make SBD calls (see +CULK command).
      // 17 Gateway not responding (local session timeout).
      // 18 Connection lost (RF drop).
      // 19 Link failure (A protocol error caused termination of the call).
      // 20..31 Reserved, but indicate failure if used.
      // 32 No network service, unable to initiate call.
      // 33 Antenna fault, unable to initiate call.
      // 34 Radio is disabled, unable to initiate call (see *Rn command).
      // 35 ISU is busy, unable to initiate call.
      // 36 Try later, must wait 3 minutes since last registration.
      // 37 SBD service is temporarily disabled.
      // 38 Try later, traffic management period (see +SBDLOE command)
      // 39..63 Reserved, but indicate failure if used.
      // 64 Band violation (attempt to transmit outside permitted frequency band).
      // 65 PLL lock failure; hardware error during attempted transmit. 

      if(status<5){
        return 0;
      }
    }else{
      return -1;
    }
  }
  return status;
}

bool getIridiumTime(){
  char timeResponse[24];
  if(getATresponse("-MSSTM", timeResponse,sizeof(timeResponse),10000)==0){
    unsigned long iridiumTime_90ms_units = readULong(timeResponse,16);
    // char *endptr;
    // unsigned long iridiumTime_90ms_units_old = strtoul(timeResponse, &endptr, 16);
    // out << "Raw:" << timeResponse << NL;
    // out << "strtoul:" << iridiumTime_90ms_units_old << NL;
    // out << "readULong:" << iridiumTime_90ms_units << NL;

    // Iridium time is reported as a 32bit integer on which each unit is represent a 90ms tick 
    // Example: "e98542ef" which corresponds to 3917824751 (the message comes with a leading space and finish by \r\n so there is is read with a trailing \r)
    // To transform this time to seconds, one must be carefull, as transforming to float to multily for 0.09
    // can lead to loss of precision. If done in integer math, dividing first by 100 would also loss precision and
    // multiplying first by 9 can lead to overflow. The math below divides by 100 first and then saves in an intermediate 
    // variable the loss ticks. Then multiplies by 9. The lost tick are multiplied by 9 first and then divided by 100. Because 
    // being a small number it will never overflow the 32bit
    unsigned long iridiumTime_9s_units=iridiumTime_90ms_units/100;
    unsigned long iridiumTime=iridiumTime_9s_units*9+(((iridiumTime_90ms_units-iridiumTime_9s_units*100)*9)/100);
    //out << "Epoch:" << iridiumTime << NL;

    if (iridiumTime>0){
      getCurrentTime();
      // To transform Iridium EPOCH to Unix add                            1399818235
      // To transform Unix to modified Unix (internal timestamp) substract  946684800
      // Therefore, to transform Iridium Epoch to modified UNIX add         453133435
      autoClockAdjust(iridiumTime+453133435+timeZone*3600);
      setWakeUp();
      updateMinMaxTime();
      return true;
    }else{
      //out << "Raw:" << timeResponse << NL;
    }
  }else{
    //out.direct("No response");ln();
  }
  return false;
}

byte getATresponse(char *command, char *response, byte responseLength,unsigned long waitTimeMilliSecs){
  // This function sends an AT command, retrieve the response (string following the : character) and waits
  // for an OK to be received. Once the OK is received, it returns zero (or 1 otherwise). The response
  // is stored in the response char array received as input
  SoftSerial.print("AT");SoftSerial.print(command);SoftSerial.print('\r');
  unsigned long iridiumWait = millis();
  char atInputStr[34];
  byte OK=1;
  while (millis()-iridiumWait < waitTimeMilliSecs){
    if(SoftSerial.available()){
      int inputLength=SoftSerial.readBytesUntil('\n',atInputStr, sizeof(atInputStr));
      //atInputStr[inputLength]='\0';
      //out << ">>" << atInputStr << NL;
      
      byte pos=findChar(atInputStr,0,inputLength,':')+1;
      if(pos<inputLength){
        for(int i=0;i<responseLength;i++){
          if(pos<inputLength){
            response[i]=atInputStr[pos++];
          }else{
            response[i]='\0';
          }
        }
      }
      if(!strncasecmp("OK", atInputStr, 2)){
        OK=0;
        break;
      }
    }
  }
  return OK;
}

void iridiumPosUpdate() {
  // Function that attempts to receive a position updates waiting 3 minutes. This wait time can be longer or shorter on user request
  if(iridiumStart()){
    unsigned long millisecondCount=millis();
    char serialInputStr[2];
    serialFlush();// When this routine was automatically started after reset/inicialization, the buffer contained a fair amount of junk that took a while
                  // to read at 2 bytes steps (size if input string), making the routine irresponsive to the "q" command for a while. That's why this small 
                  // function to empty the input buffer was implemented. Alternatively one can make the input buffer as long as the serial buffer (64) so 
                  // it will be emptied in the fisrt read. But would be the only use of Serial.readBytes(), so the memory used is more than with this approach
    while ((millis()-millisecondCount)<GPS_TIMEOUT) {
      if(getIridiumTime()){
        getIridiumPos(true);
        break;
      }else{
        out << (GPS_TIMEOUT-(millis()-millisecondCount))/1000 << F(") Waiting pos... (q to quit)\n");
        delay(1000);
      }
      if(Serial.available()){
        Serial.readBytesUntil('\n',serialInputStr, sizeof(serialInputStr));
        if (serialInputStr[0]=='q'){
          break;
        }
      }
    }
  }
  iridiumSleep();
}

void serialFlush() {
  byte count=0;
  while (Serial.available()){
    Serial.read();
    delay(10);
    count++;
    if(count>=64){
      return;
    }
  }
}
#endif
