// ===========================================================================
// REQUIRED BOARD SETTINGS (Tools menu / FQBN)
//   Board .......... MiniCore -> ATmega328
//   Variant ........ 328P / 328PA
//   Clock .......... External 7.3728 MHz     <-- NOT 8 MHz, and NOT 16 MHz
//   BOD ............ 2.7V
//   Bootloader ..... Yes (UART0)
//
// The 7.3728 MHz crystal is not an arbitrary choice: it divides exactly to
// 230400 baud. With F_CPU 7372800 the core computes UBRR=3 in double-speed
// mode, giving 7372800/(8*4) = 230400 with ZERO error. That is the whole
// reason this part is fitted instead of a round 8 MHz one -- at 8 MHz the
// closest the hardware can get to 230400 is 250000, an 8.5% error, which no
// receiver will decode.
//
// Getting the clock wrong does not fail to build and does not fail to upload;
// it silently changes the baud rate on the wire, and the console fills with
// rubbish at what looks like the right setting. Building this sketch for
// 16 MHz puts 102400 baud on the wire; for 8 MHz the UART survives by
// coincidence (same UBRR) but every delay() and millis() runs 8.5% fast.
// Arduino IDE 2.x remembers the board selection PER SKETCH, so setting it on
// the main firmware does not set it here, or the other way round.
// ===========================================================================

#include <EEPROM.h> // For writing and reading to EEPROM

#define maxWrites 100000 // Maximum numbers of write cycles for a memory byte, this limits the number acounter can reach
#define maxCount 3000000 // The maximum number of pictures the controller will be able to count to. 500,000 is equivalent to 57 years with pictures every hour

#define DEFAULT_LOW_VOLTAGE_INTERVAL_MULTIPLIER 4
#define DEFAULT_ADJUST_RTC_INTERVAL 10
#define DEFAULT_MSG_FREQUENCY_DAYS 7
#define DEFAULT_MEASURE_INTERVAL 600


void setup() {
  Serial.begin(230400);

// Calculating timestamp of compilation time
const char date[] = __DATE__;
const char time[] = __TIME__;
int HH=10*(time[0]-'0')+(time[1]-'0');
int MM=10*(time[3]-'0')+(time[4]-'0');
int SS=10*(time[6]-'0')+(time[7]-'0');
int dd=10*(date[4]-'0')+(date[5]-'0');
int yyyy=1000*(date[7]-'0')+100*(date[8]-'0')+10*(date[9]-'0')+(date[10]-'0');
int mm;
if (date[0]=='J' & date[1]=='a'){
  mm=1;
}else if (date[0]=='F'){
  mm=2;
}else if(date[0]=='M' & date[2]=='r'){
  mm=3;
}else if(date[0]=='A' & date[1]=='p'){
  mm=4;
}else if(date[0]=='M' & date[1]=='y'){
  mm=5;
}else if(date[0]=='J' & date[2]=='n'){
  mm=6;
}else if(date[0]=='J' & date[2]=='l'){
  mm=7;
}else if(date[0]=='A' & date[1]=='u'){
  mm=8;
}else if(date[0]=='S'){
  mm=9;
}else if(date[0]=='O'){
  mm=10;
}else if(date[0]=='N'){
  mm=11;
}else if(date[0]=='D'){
  mm=12;
}

Serial.println(F("// Copy this definitions to the main code "));
Serial.print(F("// Initialization date: "));Serial.print(yyyy);Serial.print('-');Serial.print(mm);Serial.print('-');Serial.print(dd);Serial.print(' ');Serial.print(HH);Serial.print(':');Serial.print(MM);Serial.print(':');Serial.println(SS);
unsigned long compilationTimestamp= mktime2(yyyy,mm,dd,HH,MM,SS);

Serial.print(F("#define COMPILATION_TIME ")); Serial.print(compilationTimestamp); Serial.println(F(" // time of compilation"));

// Seting up the pictur count counters
int countSlots=maxCount/maxWrites;
Serial.print(F("#define COUNT_ADDR 0 // Unsigned int values that stores total number of measurements. "));
Serial.print(countSlots); Serial.print(F(" values are used and each written up to "));
Serial.print(maxWrites); Serial.println(F(" times only, reached that number the counts continue in the next slot to avoid EPROM errors after 100,000+ writing cycles"));
Serial.print(F("#define COUNTERS_SLOTS ")); Serial.print(countSlots); Serial.println(F(" // Number of slots used to store the measurement count"));
// Initializing all count slots to zero
for(int i=0;i<countSlots;i++){
  EEPROM.put(i*4,(unsigned long) 0);
}
int memPointer=countSlots*4;

// COUNT_RESET_ADDR
EEPROM.put(memPointer,(unsigned long) 0);
Serial.print(F("#define COUNT_RESET_ADDR ")); Serial.print(memPointer); Serial.println(F(" // Unsigned long storing the count of the last count reset (note that reseting all counters would loose track of how many times they have been written)"));
memPointer+=4;

// RESET_TIME_ADDR
EEPROM.put(memPointer,(unsigned long) compilationTimestamp);
Serial.print(F("#define RESET_TIME_ADDR ")); Serial.print(memPointer); Serial.println(F(" // Unsigned long storing timestamp of last battery change"));
memPointer+=4;

// VOLTAGE_RESET_ADDR
EEPROM.put(memPointer,(unsigned int) 0);
Serial.print(F("#define VOLTAGE_RESET_ADDR ")); Serial.print(memPointer); Serial.println(F(" // Unsigned int storing batery voltage in millivolts at the time of last picture count reset"));
memPointer+=2;

// lastRTCTimeCheck
EEPROM.put(memPointer,(unsigned long) 0);
Serial.print(F("#define LAST_RTC_TIME_CHECK ")); Serial.print(memPointer); Serial.println(F(" // Unsigned long storing timestamp of last RTC check"));
memPointer+=4;

// lastTimeAdjustment
EEPROM.put(memPointer,(int) 0);
Serial.print(F("#define LAST_TIME_ADJUSTMENT_SECS ")); Serial.print(memPointer); Serial.println(F(" // int storing last RTC adjustment in seconds"));
memPointer+=2;

// cumulativeTimeAdjustment
EEPROM.put(memPointer,(int) 0);
Serial.print(F("#define CUMULATIVE_TIME_ADJUSTMENT_SECS ")); Serial.print(memPointer); Serial.println(F(" // int storing cumulative RTC adjustment in seconds"));
memPointer+=2;

// defaultOSCCAL
EEPROM.put(memPointer,(byte) OSCCAL);
Serial.print(F("#define DEFAULT_OSCCAL ")); Serial.print(memPointer); Serial.println(F(" // byte storing the default value of the oscillator calibration OSCCAL"));
memPointer+=1;

// ReferenceVoltage1
EEPROM.put(memPointer,(int) 0);
Serial.print(F("#define REFERENCE_VOLTAGE_1 ")); Serial.print(memPointer); Serial.println(F(" // int storing the first reference voltage in milivolts"));
memPointer+=2;

// ReferenceVoltageCount1
EEPROM.put(memPointer,(int) 0);
Serial.print(F("#define REFERENCE_VOLTAGE_COUNT_1 ")); Serial.print(memPointer); Serial.println(F(" // int storing the digital count associated with reference voltage 1"));
memPointer+=2;

// ReferenceVoltage2
EEPROM.put(memPointer,(int) 4335); // Value asociated with the maximum count of 1023 with a multiplier of 0.0042378
Serial.print(F("#define REFERENCE_VOLTAGE_2 ")); Serial.print(memPointer); Serial.println(F(" // int storing the second reference voltage in milivolts"));
memPointer+=2;

// ReferenceVoltageCount1
EEPROM.put(memPointer,(int) 1023);
Serial.print(F("#define REFERENCE_VOLTAGE_COUNT_2 ")); Serial.print(memPointer); Serial.println(F(" // int storing the digital count associated with reference voltage 1"));
memPointer+=2;

// ANALOG_CAL_ADDR -- two-point calibration for the four H1 header pins.
// Layout per pin, 8 bytes: point1 mV (int), point1 count (int),
//                          point2 mV (int), point2 count (int)
// Pins follow one another, A0 first, so the address of one slot is
//   ANALOG_CAL_ADDR + pin*8 + (point-1)*4 + (0 for mV, 2 for count)
// Both points are zeroed. Equal counts mean "never calibrated", which the
// firmware reports by passing the raw ADC count straight through instead of
// dividing by zero.
for(int i=0;i<16;i++){
  EEPROM.put(memPointer+i*2,(int) 0);
}
Serial.print(F("#define ANALOG_CAL_ADDR ")); Serial.print(memPointer); Serial.println(F(" // Base of the H1 analog calibration table: 4 pins x (int mV, int count) x 2 points = 32 bytes"));
memPointer+=32;

// LOG_SIGNATURE_ADDR -- which channels wrote the log currently in flash.
// Initialised to 0xFFFF, the same value a blank EEPROM reads, which the
// firmware treats as "no log has been stamped yet" and adopts on first use.
EEPROM.put(memPointer,(unsigned int) 0xFFFF);
Serial.print(F("#define LOG_SIGNATURE_ADDR ")); Serial.print(memPointer); Serial.println(F(" // unsigned int storing the channel signature of the log currently in flash"));
memPointer+=2;

//*****************************************************
//*************** User defined variables **************
//*****************************************************
byte nVars=5;
int addresses[nVars];
byte varLengths[nVars];
char varTypes[nVars+1];
varTypes[nVars]='\0';
char comands[nVars*3+1];
for (int i=0; i<nVars*3+1;i++){
  comands[i]='\0';
}
byte counter=0;
char text[80];

Serial.println(F("// Variables that can be changed by the user"));
//MEASURE_INTERVAL uint
addresses[counter]=memPointer;
char varType='U';
varLengths[counter]=getVarLength(varType);
varTypes[counter]=varType;
memPointer+=initializeMem(memPointer,varType,DEFAULT_MEASURE_INTERVAL);
appendNotTerminatedStr(comands,counter*3, "INT");
appendStr(text,"Interval between measurements (sec)");
Serial.print(F("#define MEASURE_INTERVAL ")); Serial.print(counter); Serial.print(" // "); printVarType(varType); Serial.println(text);
memPointer+=writeStr(memPointer,text);
counter++;

//LOW_VOLTAGE_INTERVAL_MULTIPLIER_ADD byte
addresses[counter]=memPointer;
varType='b';
varLengths[counter]=getVarLength(varType);
varTypes[counter]=varType;
memPointer+=initializeMem(memPointer,varType,DEFAULT_LOW_VOLTAGE_INTERVAL_MULTIPLIER);
appendNotTerminatedStr(comands,counter*3, "LVM");
appendStr(text,"Low voltage interval multiplier");
Serial.print(F("#define LOW_VOLTAGE_INTERVAL_MULTIPLIER ")); Serial.print(counter); Serial.print(" // "); printVarType(varType); Serial.println(text);
memPointer+=writeStr(memPointer,text);
counter++;

//TIMEZONE_ADDR int
addresses[counter]=memPointer;
varType='i';
varLengths[counter]=getVarLength(varType);
varTypes[counter]=varType;
memPointer+=initializeMem(memPointer,varType,0);
appendNotTerminatedStr(comands,counter*3, "TZN");
appendStr(text,"Time Zone (hours)");
Serial.print(F("#define TIMEZONE ")); Serial.print(counter); Serial.print(" // "); printVarType(varType); Serial.println(text);
memPointer+=writeStr(memPointer,text);
counter++;

//ADJUST_RTC_INTERVAL_ADDR uint
addresses[counter]=memPointer;
varType='u';
varLengths[counter]=getVarLength(varType);
varTypes[counter]=varType;
memPointer+=initializeMem(memPointer,varType,DEFAULT_ADJUST_RTC_INTERVAL);
appendNotTerminatedStr(comands,counter*3, "ADJ");
appendStr(text,"GPS clock adjustments frequency (days)");
Serial.print(F("#define ADJUST_RTC_INTERVAL ")); Serial.print(counter); Serial.print(" // "); printVarType(varType); Serial.println(text);
memPointer+=writeStr(memPointer,text);
counter++;

//MESSAGE_FREQUENCY_WEEKS byte
addresses[counter]=memPointer;
varType='b';
varLengths[counter]=getVarLength(varType);
varTypes[counter]=varType;
memPointer+=initializeMem(memPointer,varType,DEFAULT_MSG_FREQUENCY_DAYS);
appendNotTerminatedStr(comands,counter*3, "MSW");
appendStr(text,"Satellite messages frequency (days)");
Serial.print(F("#define MESSAGE_FREQUENCY_DAYS ")); Serial.print(counter); Serial.print(" // "); printVarType(varType); Serial.println(text);
memPointer+=writeStr(memPointer,text);
counter++;


  Serial.println();
  Serial.println(F("// Help text"));
  Serial.print(F("#define HELP_TEXT ")); Serial.print(memPointer); Serial.println(F(" // Memory address of HELP text"));Serial.println();

  
  Serial.print(F("char varComm[]="));Serial.print('"');Serial.print(comands);Serial.print('"');Serial.println(';');
  Serial.print(F("char varTypes[]="));Serial.print('"');Serial.print(varTypes);Serial.print('"');Serial.println(';');

  Serial.print(F("int varAddr[]={"));
  for(int v=0; v<nVars; v++){
    Serial.print(addresses[v]);
    if (v<nVars-1){
      Serial.print(',');
    }
  } 
  Serial.println("};");

  Serial.print(F("byte varLengths[]={"));
  for(int v=0; v<nVars; v++){
    Serial.print(varLengths[v]);
    if (v<nVars-1){
      Serial.print(',');
    }
  } 
  Serial.println("};");
  Serial.println();


//   // Serial.println("Reseting voltage history memory");
//   // for(int a=VOLTAGE_HISTORY; a<(VOLTAGE_HISTORY+256); a++){
//   //   EEPROM.write(a,(byte) 0);
//   // }
//   // Serial.println("Reseting picture count history memory");
//   // for(int a=PIC_COUNT_HISTORY; a<(VOLTAGE_HISTORY+256); a++){
//   //   EEPROM.write(a,(byte) 0);
//   // }

  // help text
  char helpStr[]= "Available commands:\n"
              "I$Info\n"
              "M$Take measurement\n"
              "GPS$Get GPS time\n"
              "MSG$Send satellite message\n"
              "S[x]$Stay in command mode for x min\n"
              "LOG$Show data log\n"
              "LOGC$Compact data log\n"
              "LOGH$Raw log as Intel HEX\n"
              "V1=/V2=$Battery calibration points\n"
              "A01=/A02=$A0 calibration (A11= for A1..)\n"
              "RC$Reset counter and memory\n"
              "BX$Bluetooth OFF and quit\n"
              "Q$Quit command mode\n\n"
              "The folowing parameters can be queryed\n"
              "or adjusted (use CMD=XX to change)\n\n"
              "TIME\tTime in format yyyy-mm-dd HH:MM";            
  
  int helpSize=sizeof(helpStr);

  Serial.print(F("// "));Serial.print(1024-memPointer-helpSize); Serial.println(F(" bytes left in EEPROM"));
  Serial.println();
  
  Serial.print(F("Help string size: "));Serial.println(helpSize);
  Serial.print(F("Availble: ")); Serial.print(1024-memPointer);Serial.println(F(" characters"));
  
  if (1024-memPointer<helpSize){
    Serial.println(F("ERROR NOT ENOUGH MEMORY"));Serial.println();
    return;
  }
  Serial.println();

  writeStr(memPointer,helpStr);
  char c;
  int helpEnd;
  for(int a=memPointer; a<1024; a++){
    c=char(EEPROM.read(a));
    if (c=='\0'){
      helpEnd=a;
      break;
    }
    Serial.print(c);
  }
  for(int v=0; v<nVars; v++){
    Serial.print(comands[v*3]);Serial.print(comands[v*3+1]);Serial.print(comands[v*3+2]);
    Serial.print("   ");printMsg(addresses[v]+varLengths[v]);Serial.println();
  } 
  
  Serial.println();
  Serial.println();
  Serial.print(F("Help text extends to position ")); Serial.print(helpEnd);Serial.print(F(". There are ")); Serial.print(1023-helpEnd); Serial.println(F(" characters left."));

  Serial.println();
  Serial.println("DONE");

  for(int a=0; a<1024; a++){
    c=char(EEPROM.read(a));
    Serial.print(a);Serial.print(':');
    if (c=='\0'){
      Serial.println("Null");
    }else if (c=='\n'){
      Serial.println("Line break");
    }else if (c>=32 && c<=127){
      Serial.println(c);
    }else{
      Serial.println("Non printable");
    }
    
  }  
}



void loop() {
  // put your main code here, to run repeatedly:

}

int writeStr(int add, char *str){
  int i = 0;
  while(str[i]!='\0'){
    EEPROM.write(add+i,byte(str[i]));
     i++;
  }
  EEPROM.write(add+i,'\0');
  return i+1;
}
int appendStr(char *destination, char *str){
  int i=-1;
  do{
    i++;
    destination[i]=str[i];
  }while(str[i]!='\0');
  return i;
}

int appendNotTerminatedStr(char *destination, int pos, char *str){
  int i=0;
  while(str[i]!='\0'){
    destination[pos+i]=str[i];
    i++;
  }
  return --i;
}
void printVarType(char type){
  if (type=='i') {
    Serial.print(F("(signed int) "));
  }else if (type=='u') {
    Serial.print(F("(unsigned int) "));
  } else if (type=='l') {
    Serial.print(F("(signed long) "));
  } else if (type=='U') {
    Serial.print(F("(unsigned long) "));
  } else if (type=='f'){
    Serial.print(F("(float) "));
  } else if (type=='b') {
    Serial.print(F("(byte) "));
  }
}
int getVarLength(char type){
  if (type=='i' | type=='u') {
    return 2;
  }
  if (type=='l' | type=='U' | type=='f') {
    return 4;
  }
  if (type=='b') {
    return 1;
  }
}
int initializeMem(int memPointer,char type,long value){
  if (type=='i') {
    EEPROM.put(memPointer,(int) value);
    return 2;
  }else if (type=='u') {
    EEPROM.put(memPointer,(unsigned int) value);
    return 2;
  }
  if (type=='l') {
    EEPROM.put(memPointer,(long) value);
    return 4;
  }
  if (type=='U') {
    EEPROM.put(memPointer,(unsigned long) value);
    return 4;
  }
  if (type=='f') {
    EEPROM.put(memPointer,(float) value);
    return 4;
  }  
  if (type=='b') {
    EEPROM.put(memPointer,(byte) value);
    return 1;
  }
}

void printMsg(int messageAddress){
  char msg[80];
  appendEepromStr(msg,messageAddress,0);
  Serial.print(msg);
}

int appendEepromStr(char *destination, int address, int pos){
  // Read a string from EEPROM and writes it to a char array
  int i=0;
  do{
    destination[pos++]=char(EEPROM.read(address+i));
    i++;
  }while(destination[pos-1]!='\0');
  return pos-1;
}

uint32_t mktime2(int YYYY,int MM,int DD,int hh,int mm,int ss){
    // Function to turn date elements into a a variation of UNIX timestamp that starts from year 2000. 
    // VALID ONLY UNTIL YEAR 2100
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

int month2DOY(int yyyy, int mm){
  // Returns the number of days elapsed before the start of a given month
  // VALID ONLY UNTIL YEAR 2100
  //int DOY[12]={0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};//These are the days elapsed before the start of each month
  byte DOYdeviation[12]={1,2,0,1,1,2,2,3,4,4,5,5};// This are the additional days to add if all months were 30 days long (plus one to avoid negatives), this approach saves 20 bytes of memory
  mm--;// Month in range 0 to 11 (february is 1)
  int DOY=mm*30+DOYdeviation[mm]-1; // DOY for non-leap years
  if ((yyyy % 4)==0 & mm>1){
    DOY++;// Adding one day if leap year and month is after February
  }
  return DOY;
}