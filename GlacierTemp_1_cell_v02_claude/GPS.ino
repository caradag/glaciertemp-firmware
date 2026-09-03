void gpsNotInstalled(){
  out << F("No GPS installed\n");
}


#if GPS_INSTALLED == 1

void gpsUpdate() {
  out << F("Updating GPS data (Q to quit)...\n");
  digitalWrite(ACCESORY_POWER,ACCESORY_ON);
  SoftSerial.begin(9600);

  //digitalWrite(BLUETOOTH_POWER_PIN, HIGH);
  displayNMEA=false;
  
  unsigned long millisecondCount=millis();
  char inputStr[81];
  bool gpsExist=false; //Set to true when we have a proof that the GPS is connected and alive
  int satCount=0;
  while ((millis()-millisecondCount)<GPS_TIMEOUT) {
    if (SoftSerial.available()) {
      gpsExist=true;
      byte nmeaReadLength=SoftSerial.readBytesUntil('\n',inputStr, sizeof(inputStr));
      if (displayNMEA){
        inputStr[nmeaReadLength]='\0';
        out << inputStr << NL;
      }
      if (!strncasecmp("$GPRMC", inputStr, 6) || !strncasecmp("$GNRMC", inputStr, 6)){ // In old GPS modules this NMEA sentence and others started with "GP" (for GPS) but in new modules they start with "GN" (for GNSS)   
        // Documentation of NMEA 0183 sentences was extracted from https://w3.cs.jmu.edu/bernstdh/web/common/help/nmea-sentences.php
        // NMEA Checsum function was adapted from https://forum.arduino.cc/t/nmea-checksums-explained/1046083
        if (nmeaChecksumCompare(inputStr)) {
          //*********************************************
          // First field is time in format HHMMSS.SSS    
          byte fstart = findNmeaComma(inputStr,0)+1;
          byte fend = findNmeaComma(inputStr,fstart)-1;
          int HH= readLong(inputStr,fstart,fstart+1);
          int MM= readLong(inputStr,fstart+2,fstart+3);
          int SS= readFloat(inputStr,fstart+4,fend);
          //*********************************************
          // Second field is position status (A for valid, V for invalid)
          fstart = fend+2;
          if(inputStr[fstart]=='A'){
            getCurrentTime();
            //*********************************************
            // Third field is latitude in format DDMM.MMM
            fstart += 2;
            fend = findNmeaComma(inputStr,fstart)-1;
            latitude= readFloat(inputStr,fstart,fstart+1)+readFloat(inputStr,fstart+2,fend)/60.0;
            //*********************************************
            // Fouth field is latitude sign N/S
            fstart = fend+2;
            if(inputStr[fstart]=='S'){
              latitude*=-1;
            }else if (inputStr[fstart]!='N'){
              //Parse error
            }
            //*********************************************
            // Fifth field is longitude in format DDDMM.MMM
            fstart += 2;
            fend = findNmeaComma(inputStr,fstart)-1;
            longitude= readFloat(inputStr,fstart,fstart+2)+readFloat(inputStr,fstart+3,fend)/60.0;
            //*********************************************
            // Sixth field is longitude sign W/E
            fstart = fend+2;
            if(inputStr[fstart]=='W'){
              longitude*=-1;
            }else if (inputStr[fstart]!='E'){
              //Parse error
            }
            //*********************************************
            // Seventh field is Speed (in knots per hour)
            // we skip it
            fstart += 2;
            fend = findNmeaComma(inputStr,fstart)-1;        
            //*********************************************
            // Eight field is Heading
            // we skip it
            fstart = fend+2;
            fend = findNmeaComma(inputStr,fstart)-1;  
             //*********************************************
            // Nineth field is date in format ddmmyy
            fstart = fend+2;
            fend = findNmeaComma(inputStr,fstart)-1;
            int dd= readLong(inputStr,fstart,fstart+1);
            int mm= readLong(inputStr,fstart+2,fstart+3);
            int yy= readLong(inputStr,fstart+4,fend);
            
            autoClockAdjust(mktime2(yy,mm,dd,HH,MM,SS)+timeZone*3600);
            
            // We store new latitude and longitude
            updateVar(LATITUDE,latitude);
            updateVar(LONGITUDE,longitude);
            updateMinMaxTime();
            setWakeUp();
            displayVars(LATITUDE,LONGITUDE);
            displaySolarMaskStatus();
            // Storing the time of the last time check with GPS
            EEPROM.put(LAST_RTC_TIME_CHECK,currentTime);
            break;
                    
          }else if (inputStr[fstart]=='V'){
            out << F("Waiting GPS,") << satCount << F(" sats fix (") << (GPS_TIMEOUT-(millis()-millisecondCount))/1000 << F("), N: NMEA, Q: quit\n");
            satCount=0;
            if (serialQuitInput(&millisecondCount)){
              break;
            }
          }else{
            //Parse error
          }
        }else{
          //Ckecksum fail
        } 
      }else if (!strncasecmp("$GPGSV", inputStr, 6) || !strncasecmp("$GNGSV", inputStr, 6)){ // Satelites in view, we wil count number of satelites fix
        if (nmeaChecksumCompare(inputStr)) {
          byte commaCount=0;
          for(int i=1;i<sizeof(inputStr);i++){
            if(inputStr[i]==','){
              commaCount++;
              if(commaCount>=7 && (commaCount-7)%4 == 0 && (inputStr[i+1]==',' || inputStr[i+1]=='*')){
                satCount--;
              }
            }
            if(inputStr[i]=='\0'){
              break;
            }
          }
          satCount+=(commaCount-3)/4;
        }
      }
  
    }else{
      if (serialQuitInput(&millisecondCount)){
        break;
      }      
      if ((millis()-millisecondCount)>GPS_NORESPONSE_TIMEOUT & !gpsExist){
        out << "GPS unavailable\n";
        break;
      }
    }
  }
  digitalWrite(ACCESORY_POWER,ACCESORY_OFF);
}

bool serialQuitInput(unsigned long *millisecondCount){
  char serialInputStr[2];
  if(Serial.available()){
    int inputLength=Serial.readBytesUntil('\n',serialInputStr, sizeof(serialInputStr));
    if (!strncasecmp("Q", serialInputStr, 1)){
        return true;
    }else if (!strncasecmp("N", serialInputStr, 1)){
        displayNMEA=!displayNMEA;
    }else if (!strncasecmp("W", serialInputStr, 1)){
        *millisecondCount=millis();
    }
  }
  return false;
}


bool nmeaChecksumCompare(char packet[]){
  uint8_t calcChecksum = 0;

  uint8_t packetEndIndex = 0;
  while(packet[packetEndIndex]!='*'){
     packetEndIndex++;
  }
        
  for(uint8_t i=1; i<packetEndIndex; ++i) // packetEndIndex is the "size" of the packet minus 1. Loop from 1 to packetEndIndex-4 because the checksum is calculated between $ and *
  {
    calcChecksum = calcChecksum^packet[i];
  }

  uint8_t nibble1 = (calcChecksum&0xF0) >> 4; //"Extracts" the first four bits and shifts them 4 bits to the right. Bitwise AND followed by a bitshift
  uint8_t nibble2 = calcChecksum&0x0F; 

  uint8_t translatedByte1 = (nibble1<=0x9) ? (nibble1+'0') : (nibble1-10+'A'); //Converting the number "nibble1" into the ASCII representation of that number
  uint8_t translatedByte2 = (nibble2<=0x9) ? (nibble2+'0') : (nibble2-10+'A'); //Converting the number "nibble2" into the ASCII representation of that number

  if(translatedByte1==packet[packetEndIndex+1] && translatedByte2==packet[packetEndIndex+2]) //Check if the checksum calculated from the packet payload matches the checksum in the packet
  { 
    return true; 
  }else{
    return false; 
  }
}

byte findNmeaComma(const char *a,byte pos){
  return findChar(a, pos,80, ',');
}
#endif
