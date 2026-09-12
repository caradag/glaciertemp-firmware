
byte memReadStatus2(){
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(READ_STATUS_2);
  byte status = SPI.transfer(0x00);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  return status;
}

// Reports the flash status registers, and warns if anything is protecting them.
//
// THIS DELIBERATELY DOES NOT TRY TO CLEAR THE QUAD ENABLE BIT. It used to, and
// it could not succeed: on this part QE is fixed at 1 by the factory and is not
// a writable bit. Verified on the bench 2026-08-22 with JEDEC EF 40 17,
// SR1=0x00 SR2=0x02 SR3=0x60, i.e. no protection of any kind set:
//
//   31h (write SR2, one byte)        -> QE unchanged
//   01h (write SR1+SR2, two bytes)   -> QE unchanged
//   50h + 31h (volatile)             -> QE unchanged
//   50h + 01h (volatile)             -> QE unchanged
//
// In every case WEL was 1 before the write and 0 afterwards, so the chip
// accepted and executed the instruction; the bit simply does not change. That
// is the documented behaviour of the "Quad Enabled" ordering option, for which
// QE=1 is the factory state.
//
// WHAT THAT MEANS FOR THIS BOARD. Pin 3 is permanently IO2 and pin 7 is
// permanently IO3, and both are tied to a rail: pin 3 to GND (fitted to stop it
// floating, worth 84uA of sleep current) and pin 7 hard-shorted to VCC. Those
// pins are inputs at all times EXCEPT during a quad instruction, when the chip
// drives them -- into a short, in both cases.
//
//   *** NEVER ISSUE A QUAD INSTRUCTION TO THIS FLASH. ***
//
// Everything here is single-I/O by construction: 03h read, 02h page program,
// 20h sector erase, 06h write enable, 05h/35h read status, B9h/ABh power. Adding
// a Fast Read Quad (6Bh/EBh), a Quad Page Program (32h), or enabling QPI (38h)
// would short an output driver to a rail. This is not a style preference; it is
// the reason the board survives having those pins tied.
void flashReportStatus(){
  byte sr1 = memReadStatus();
  byte sr2 = memReadStatus2();

  // SRP1 is S8, i.e. SR2 bit 0 -- NOT in SR1 alongside SRP0. Together they say
  // whether the status register can be written at all:
  //   (SRP1,SRP0) = (0,0) writable [factory default]
  //                 (0,1) locked while /WP is low
  //                 (1,0) power-supply lock-down, until the next power cycle
  //                 (1,1) one time program, permanent
  // Neither is expected to be set here; both are reported because a surprise in
  // either would explain a flash that silently refuses writes.
  if(sr1 & STATUS1_SRP0){
    out << F("WARNING: flash SRP0 set\n");
  }
  if(sr2 & STATUS2_SRP1){
    out << F("WARNING: flash SRP1 set, status register locked\n");
  }
  if(!(sr2 & STATUS2_QE)){
    // Would be a genuine surprise: a part whose QE can be, or has been, cleared
    out << F("Note: flash QE is clear, WP#/HOLD# are live\n");
  }

  // Los bits BP/TB protegen el ARRAY, que es lo unico que puede impedir registrar; los SRP
  // solo protegen el registro de estado. Se avisa aparte para no confundir una cosa con la
  // otra.
  if(sr1 & STATUS1_BP_MASK){
    out << F("WARNING: flash block protection active, writes may fail\n");
  }

  // Los bytes crudos, siempre: un 0xFF 0xFF significa que el chip no contesta --tipicamente
  // porque quedo apagado-- y no que este bloqueado. Sin verlos, ambos casos dan el mismo
  // aviso y llevan a diagnosticos opuestos.
  out << F("Flash status SR1/SR2:");
  printHex8(sr1);
  out << NOSPACER << '/';
  printHex8(sr2);
  out << NORMALTEXT;
  ln();
}

void printHex8(byte v){
  out << NOSPACER << "0x" << hexDigit(v>>4) << hexDigit(v) << NORMALTEXT;
}

// Put the flash into its lowest state and leave its pins in the matching
// safe position. Two strategies, chosen by SLEEP_FLASH_POWERED.
//
// Must be called while SPI is still enabled: it sends 0xB9, and SPI.transfer()
// with SPE cleared waits forever for a flag that never arrives.
void flashPowerDown(){
  memSendControlByte(POWER_DOWN);          // 0xB9, deep power-down, ~1uA typ
#if SLEEP_FLASH_POWERED
  // Supply stays on, so nothing can back-feed the chip and CS is free to rest
  // high -- both ends of R26 at 3.3V, no current through it.
  digitalWrite(FLASH_MEMORY_CS, HIGH);
#else
  // Supply removed, so every line into the chip must be held low or it is
  // powered through its protection diodes instead.
  digitalWrite(MEM_POWER, LOW);
  digitalWrite(FLASH_MEMORY_CS, LOW);
#endif
}

bool memSendControlByte(byte controlByte){
    if(controlByte==POWER_UP){
      digitalWrite(MEM_POWER, HIGH);
      digitalWrite(FLASH_MEMORY_CS, HIGH);
      delay(1);
    }

    digitalWrite(FLASH_MEMORY_CS, LOW);
    SPI.transfer(controlByte); // Write Enable
    digitalWrite(FLASH_MEMORY_CS, HIGH);

    if(controlByte==WRITE_ENABLE){
      if (!(memReadStatus() & 0x02)) { // WEL must be set once WREN has been sent
        out << F("Flash write-enable failed\n");
        return false;
      }
    }    
    return true;
}

// Returns true only if every byte was erased, programmed and confirmed.
bool writeBytesToFlash(uint32_t address, const byte* data, uint32_t length) {
  if(checkMemoryOverflow(address+length)){
    return false;
  }

  uint16_t startPosInSector=address % SECTOR_SIZE;
  // Is we are going to write in a new sector, ne erase it first
  if(startPosInSector == 0){
    if(!memEraseSector(address/SECTOR_SIZE)) return false;
  }else if(SECTOR_SIZE<startPosInSector+length){
    if(!memEraseSector((address/SECTOR_SIZE)+1)) return false;
  }
  const uint16_t PAGE_SIZE = 256;
  while (length > 0) {
    uint16_t pageOffset = address % PAGE_SIZE;
    uint16_t spaceInPage = PAGE_SIZE - pageOffset;
    uint16_t chunkSize = length;
    if(spaceInPage<chunkSize){
      chunkSize = spaceInPage;
    }

    // Write Enable
    if (!memSendControlByte(WRITE_ENABLE)){
      return false;
    }

    memSelectPageAddress(address, WRITE);

    for (uint16_t i = 0; i < chunkSize; i++) {
      SPI.transfer(data[i]);
    }
    digitalWrite(FLASH_MEMORY_CS, HIGH);
    // Wait for write to complete
    if (!memWaitUntilReady()) return false;

    // Update pointers
    address += chunkSize;
    data += chunkSize;
    length -= chunkSize;
  }
  return true;
}

void readBytesFromFlash(uint32_t address, byte* buffer, uint32_t length) {
  memSelectPageAddress(address, READ);

  for (uint32_t i = 0; i < length; i++) {
    buffer[i] = SPI.transfer(0x00);
  }

  digitalWrite(FLASH_MEMORY_CS, HIGH);
}

bool checkMemoryOverflow(uint32_t address){
  if(address>=(SECTOR_SIZE*(MAX_SECTORS+1))){
    out << F("Memory full\n");
    return true;
  }
  return false;
}
void memSelectPageAddress(unsigned long address, byte mode){
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(mode); // Page Program

  byte addr[3] = {
    (byte)(address >> 16),
    (byte)(address >> 8),
    (byte)(address)
  };
  SPI.transfer(addr[0]);
  SPI.transfer(addr[1]);
  SPI.transfer(addr[2]);

  // This is equivalent to the above, it is shorter but uses the same memory
  // SPI.transfer((address >> 16) & 0xFF);
  // SPI.transfer((address >> 8) & 0xFF);
  // SPI.transfer(address & 0xFF);  
}

// Old version of the function without time out
// void memWaitUntilReady() {
//   while (memReadStatus() & 0x01) {
//     delay(1); // Wait for WIP bit to clear
//   }
// }

bool memWaitUntilReady() {
  unsigned long start = millis();
  byte deadBusReads = 0;

  while (millis() - start < FLASH_READY_TIMEOUT) {
    byte status = memReadStatus();

    if (status == 0xFF) {
      // MISO stuck high: chip unpowered, deselected or absent.
      // Require several consecutive reads so a legitimate 0xFF can't trip it.
      if (++deadBusReads > 5) {
        out << F("Flash not responding\n");
        return false;
      }
    } else {
      deadBusReads = 0;
      if (!(status & 0x01)) return true;   // WIP clear -> done
    }
    delay(1);
  }
  out << F("Flash timeout\n");
  return false;
}


byte memReadStatus() {
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x05);
  byte status = SPI.transfer(0x00);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  return status;
}

byte detectSPImemory() {
  // We read the JEDEC ID (Joint Electron Device Engineering Council)
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x9F); // JEDEC ID
  // byte mfg = SPI.transfer(0x00);
  // byte memType = SPI.transfer(0x00);
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  byte capacity = intPow(SPI.transfer(0x00),2)/intPow(20,2);
  digitalWrite(FLASH_MEMORY_CS, HIGH);
  return capacity;
}

// Numero de serie unico de 64 bits del chip de flash (opcode 0x4B, "Read Unique
// ID"). Winbond lo graba en fabrica y es unico por chip, asi que sirve como
// identidad estable de la placa: el W25Q64 va soldado y no se sustituye.
//
// El ATmega328P no ofrece nada equivalente. El 328PB si lleva un numero de serie
// documentado, pero el 328P-MU que monta esta placa no: los bytes de su signature
// row son datos de calibracion y de posicion en la oblea, y Microchip no los
// garantiza como unicos, de modo que dos chips del mismo lote pueden coincidir.
//
// El opcode va seguido de cuatro bytes dummy antes de los ocho de datos.
// El QUE LLAMA debe tener la flash encendida, y decide si apagarla despues.
//
// Antes esta funcion encendia y apagaba por su cuenta, y eso rompio el arranque: al
// imprimir el identificador justo antes de flashReportStatus(), lo dejaba apagado y las
// lecturas de los registros de estado devolvian 0xFF. Un 0xFF tiene puestos SRP0 y SRP1,
// asi que la placa avisaba de un bloqueo que no existia. Manejar la alimentacion dentro de
// una funcion de lectura es lo que hace posible ese tipo de sorpresa a distancia.
void readFlashUniqueID(byte* id8){
  digitalWrite(FLASH_MEMORY_CS, LOW);
  SPI.transfer(0x4B);
  for(byte i=0;i<4;i++){
    SPI.transfer(0x00);          // dummy
  }
  for(byte i=0;i<8;i++){
    id8[i]=SPI.transfer(0x00);
  }
  digitalWrite(FLASH_MEMORY_CS, HIGH);
}

// Imprime el identificador como 16 digitos hexadecimales, sin separadores, para
// que la app pueda tomarlo tal cual.
void printBoardId(){
  byte id[8];
  readFlashUniqueID(id);
  out << NOSPACER;
  for(byte i=0;i<8;i++){
    out << hexDigit(id[i]>>4) << hexDigit(id[i]);
  }
  out << NORMALTEXT;
}

// Identificador corto de la placa, con la forma "GT001-XXXXXX".
//
//   GT      tipo de hardware, de BOARD_TYPE
//   001     REVISION del hardware, de BOARD_HW_VERSION -- no la version de firmware
//   XXXXXX  los seis ultimos digitos hexadecimales del numero de serie de fabrica
//
// Los seis digitos son los bytes 5, 6 y 7 completos, es decir los 24 bits bajos de los 64
// --dos digitos por byte. Antes se derivaban 32 bits por CRC precisamente para NO recortar:
// Winbond no documenta como esta compuesto el numero de serie, y en los identificadores de
// silicio es habitual que los bytes altos codifiquen lote y oblea. Medido sobre las placas
// reales resulto lo contrario --solo varian los digitos bajos--, asi que recortar identifica
// igual y produce un codigo que se puede leer en voz alta y escribir en una libreta.
//
// El precio son 24 bits en vez de 32: entre 100 placas la probabilidad de choque sube de
// 1/870.000 a 1/3.400. Con una flota de decenas, la unicidad se comprueba una vez leyendo
// todos los identificadores en vez de confiarla al calculo.
//
// El identificador completo de 64 bits sigue estando en el comando ID, en la cabecera INFO
// y en la linea de arranque, y es el que desempata si alguna vez hiciera falta.
void printShortBoardId(){
  byte id[8];
  readFlashUniqueID(id);
  // Un solo literal y no tres elementos: el preprocesador los concatena en tiempo de
  // compilacion, asi que cuesta una cadena en flash en vez de tres llamadas al stream.
  out << NOSPACER << F(BOARD_TYPE BOARD_HW_VERSION "-");
  for(byte i=5;i<8;i++){
    out << hexDigit(id[i]>>4) << hexDigit(id[i]);
  }
  out << NORMALTEXT;
}

// El comando ID: la MISMA linea que sale en el arranque, con el identificador corto y el
// completo entre parentesis.
//
// Antes imprimia solo los 16 digitos del completo, con el argumento de que es lo que una app
// quiere leer. Pero ninguna app lee de aqui: el contrato de maquina es la cabecera INFO, que
// trae los dos en campos con nombre. Este comando lo escribe una persona en el terminal, y
// lo que necesita es el corto --el que nombra la placa-- sin perder el completo.
//
// Es una operacion aislada, asi que enciende la flash y vuelve a apagarla.
void printBoardIdStandalone(){
  memSendControlByte(POWER_UP);
  printBoardIdLine();
  flashPowerDown();
}

void printBoardIdLine(){
  // Espaciado explicito: el separador automatico del stream no pone nada antes de un "(",
  // y sin NOSPACER metia uno dentro del parentesis.
  out << F("Board ID:");
  printShortBoardId();
  out << NOSPACER << F("  (full ");
  printBoardId();
  out << NOSPACER << F(")\n") << NORMALTEXT;
}

bool flashWriteFloat(uint32_t address, float value) {
    // Create a pointer to a uint8_t, and cast the address of the float to it.
    // This allows us to treat the float's memory as an array of bytes.
    uint8_t* bytePtr = (uint8_t*)&value;

    // Write the bytes to flash memory
    return writeBytesToFlash(address, bytePtr, sizeof(float));
}



bool flashWriteInt(uint32_t address, int value) {
    // Create a pointer to a uint8_t, and cast the address of the float to it.
    // This allows us to treat the float's memory as an array of bytes.
    uint8_t* bytePtr = (uint8_t*)&value;

    // Write the bytes to flash memory
    return writeBytesToFlash(address, bytePtr, sizeof(int));
}

bool flashWriteUnsignedLong(uint32_t address, unsigned long value) {
    // Create a pointer to a uint8_t, and cast the address of the float to it.
    // This allows us to treat the float's memory as an array of bytes.
    uint8_t* bytePtr = (uint8_t*)&value;

    // Write the bytes to flash memory
    return writeBytesToFlash(address, bytePtr, sizeof(unsigned long));
}

float flashReadFloat(uint32_t address) {
    float result;
    // Create a pointer to a uint8_t, and cast the address of the float result to it.
    uint8_t* bytePtr = (uint8_t*)&result;

    // Read 4 bytes from flash memory into the memory location of 'result'
    readBytesFromFlash(address, bytePtr, sizeof(float));

    return result;
}

int flashReadInt(uint32_t address) {
    int result;
    // Create a pointer to a uint8_t, and cast the address of the float result to it.
    uint8_t* bytePtr = (uint8_t*)&result;

    // Read 4 bytes from flash memory into the memory location of 'result'
    readBytesFromFlash(address, bytePtr, sizeof(int));

    return result;
}

byte flashReadByte(uint32_t address) {
    byte result;
    // Read 4 bytes from flash memory into the memory location of 'result'
    readBytesFromFlash(address, &result, sizeof(byte));

    return result;
}

unsigned long flashReadUnsignedLong(uint32_t address) {
    unsigned long result;
    // Create a pointer to a uint8_t, and cast the address of the float result to it.
    uint8_t* bytePtr = (uint8_t*)&result;

    // Read 4 bytes from flash memory into the memory location of 'result'
    readBytesFromFlash(address, bytePtr, sizeof(unsigned long));

    return result;
}

bool memEraseSector(uint16_t sector) {
  if (sector > MAX_SECTORS) return false; // prevent overflow
  uint32_t address = sector * SECTOR_SIZE;

  // Write Enable
  if (!memSendControlByte(WRITE_ENABLE)){
    return false;
  }

  // Sector Erase (0x20)
  memSelectPageAddress(address,SECTOR_ERASE);
  digitalWrite(FLASH_MEMORY_CS, HIGH);

  out << sector << "erased" << NL;

  return memWaitUntilReady();
}
