// One hexadecimal digit for the low nibble of n.
//
// Deliberately NOT a lookup table: `static const char hex[]="0123456789ABCDEF"`
// has no PROGMEM qualifier, so avr-gcc copies it into .data at start-up and it
// costs 17 bytes of RAM per copy. This costs none and is no slower.
char hexDigit(byte n){
  n &= 0x0F;
  return (n<10) ? ('0'+n) : ('A'+n-10);
}

long readInt(char *str){
  return readLong(str,0,strlen(str)-1);
}

float readFloat(char *str){
  return readFloat(str,0,strlen(str)-1);
}

long readLong(char *str,int fstart,int fend){
  bool isNegative;
  unsigned long tmpValue = readULong(str,fstart,fend, &isNegative, 10);
  if(isNegative){
    return ((long)tmpValue)*-1;
  }
  return (long)tmpValue;
}

unsigned long readULong(char *str, int base){
  // This overload has no use for the sign, but the worker below writes through
  // the pointer unconditionally, so it must never be handed a null. Passing the
  // address of a throwaway local costs one byte of stack and removes the
  // null-pointer write that a nullptr here used to cause.
  bool discardSign;
  return readULong(str,0,strlen(str)-1, &discardSign, base);
}

unsigned long readULong(char *str,int fstart,int fend, bool* isNegative, int base){
  // Reads unsigned longs from char array
  // It returns cero if no number have been found
  // Accepts base 10 (decimal) and 16 (hexadecimal)
  // * In decimal format ignore decimal points and returns the value found before any invalid character is found
  // Invalid characters is anything that is not: ".","-",\r,\n, " "
  // Note that those characters are ignored even if in between numbres
  // * In hexadecimal format an invalid character is anything that is not: ".","-",\r,\n, " "
  // Note that those characters are ignored even if in between numbres
  // Returns zero if a invalid character is found

  unsigned long result=0;
  unsigned long magnitude=1;
  *isNegative=false;
  for (int i=fend; i>=fstart; i--){
    uint8_t offset;
    if(str[i]=='-'){
      *isNegative=true;
      continue;    
    }else if(str[i]>='0' && str[i]<='9'){
      offset= '0';      
    }else if(str[i] >= 'a' && str[i] <= 'f' && base==16){
      offset= 'a' - 10;
    }else if(str[i] >= 'A' && str[i] <= 'F' && base==16){
      offset= 'A' - 10;     
    }else if(str[i] == ' ' || str[i] == '\r' || str[i] == '\n' || str[i]=='.'){
	  //If no digit has been read ignores white spaces until something is readable
      continue; 
    }else{
      if(base==16){
        return 0;
      }
      break;
    }
    result+=(str[i]-offset)*magnitude;
    magnitude*=base;
  }
  return result;
}

// El numero que sigue al '=' de un comando, multiplicado por 10^decimals y en
// enteros: readScaled("V1=1.234",3) da 1234 y readScaled("INT=600",0) da 600. Los
// decimales que sobran se truncan hacia cero, como hacia el (int) sobre el float de
// antes, y los espacios se ignoran. Un numero de mas de nueve cifras se SATURA en vez
// de dar la vuelta, para que la comprobacion de rango lo rechace: con float,
// "INT=99999999999" era 1e11 y se rechazaba; con un long que diera la vuelta seria
// un intervalo cualquiera aceptado en silencio.
long readScaled(const char *str, byte decimals){
  const char *p=strchr(str,'=');
  p = p ? p+1 : str;
  bool negative=false, saturated=false;
  long value=0;
  int8_t frac=-1;                 // decimales leidos; -1 mientras no haya punto
  for(;;p++){
    char c=*p;
    if(c=='-'){
      negative=true;
    }else if(c=='+'){
      // "TZN=+2": el signo explicito se acepta, como hacia el lector anterior
    }else if(c=='.'){
      frac=0;
    }else if(c>='0' && c<='9'){
      if(frac>=(int8_t)decimals){
        continue;                 // decimal de mas: se trunca
      }
      if(value>=100000000L){
        saturated=true;
      }else{
        value=value*10+(c-'0');
      }
      if(frac>=0){
        frac++;
      }
    }else if(c!=' '){
      break;
    }
  }
  for(int8_t i=(frac<0 ? 0 : frac); i<(int8_t)decimals; i++){
    if(value>=100000000L){
      saturated=true;             // la escala tampoco puede dar la vuelta
      break;
    }
    value*=10;
  }
  if(saturated){
    return negative ? -2000000000L : 2000000000L;
  }
  return negative ? -value : value;
}

float readFloat(char *str,int fstart,int fend){
  int dotPos=findChar(str,fstart,fend,'.');
  return (float)readLong(str,fstart,fend)/intPow(fend-dotPos,10);
}

// Entero decimal sin signo a partir de p, saltando los espacios de delante; para en el
// primer caracter que no sea cifra. Sustituye a strtoul(), que arrastraba 470 bytes de
// biblioteca (bases, signos, desbordamiento, errno) solo para leer "LOGB=0,99".
unsigned long readUL(const char *p){
  unsigned long v=0;
  while(*p==' '){
    p++;
  }
  while(*p>='0' && *p<='9'){
    v=v*10+(*p++ - '0');
  }
  return v;
}

long intPow(int power,int base){
  long outVal=1;
  for (int i=0; i<power;i++){
    outVal*=base;
  }
  return outVal;
}

long int10Pow(byte power){
  return intPow(power,10);
}


int mean(int *x, int n) {
  // Rounded average. This replaces an equivalent version that scaled the sum by
  // 10 to recover the first fractional digit and then rounded on it; adding n/2
  // before the division does the same rounding directly. Verified identical for
  // every reachable input (non-negative ADC counts). The two differ only for
  // negative sums, because C truncates division toward zero.
  long sumX = 0;
  for (int i = 0; i < n; i++) {
    sumX += x[i];
  }
  return (sumX + n/2)/n;
}

byte findChar(const char *a,byte posIni,byte posEnd, const char charToFind){
  // && (not &) so the index is bounds-checked before a[posIni] is read again
  while(posIni<posEnd && a[posIni]!=charToFind){
     posIni++;
  }
  return posIni;
}


#if GPS_INSTALLED == 1 || IRIDIUM_INSTALLED == 1
void serialTunnel(){
  out << F("Tunnel (q to quit)\n");
  char inputStr[34];
  while(true){
    if(Serial.available()){
      int inputLength=Serial.readBytesUntil('\n',inputStr, sizeof(inputStr));
      if (inputStr[0]=='q'){
        return;
      }else{
        inputStr[inputLength]='\0';
        out << ">:" << inputStr << NL;
        SoftSerial.print(inputStr);
      }
    }

    if(SoftSerial.available()){
      int inputLength=SoftSerial.readBytesUntil('\n',inputStr, sizeof(inputStr));
      inputStr[inputLength]='\0';
      out << "<:" << inputStr << NL;
    }      
  }
}
#endif