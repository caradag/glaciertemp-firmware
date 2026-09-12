#!/bin/bash
# Reconstruye el banco a partir del firmware actual. Ejecutar tras cualquier
# cambio en el volcado LOGB o en la cabecera INFO.
#
# Se extrae en DOS ficheros porque las constantes hacen falta antes que las funciones:
# el buffer de salida se dimensiona con OUT_BUFFER_SIZE al principio del banco, mientras
# que los cuerpos de las funciones necesitan tener ya declarado el entorno falso.
set -e
cd "$(dirname "$0")"
SRC=../GlacierTemp_1_cell_v02_claude

python3 extract.py "$SRC" \
  'GlacierTemp_1_cell_v02_claude.ino:#FIRMWARE_VERSION,#PROTOCOL_VERSION,#BOARD_TYPE,#BOARD_HW_VERSION,#SECTOR_SIZE,#MAX_SECTORS,#FAST_BAUDRATE,#OUT_BUFFER_SIZE' \
  'EEPROM.ino:#LOGB_BLOCK,#LOGB_CHUNK,#FLOW_XOFF,#FLOW_XON,#FLOW_CANCEL,#SWITCH_SETTLE_MS' \
  > extracted_defs.h

python3 extract.py "$SRC" \
  'Functions.ino:hexDigit' \
  'Display.ino:reportClockNotSet' \
  'FlashMem.ino:readFlashUniqueID,printBoardId,printShortBoardId,printBoardIdLine,printHex8,flashReportStatus' \
  'EEPROM.ino:switchBaud,printHex16,crc16Ccitt,writeU16LE,flowControlCheck,dumpLogBinary,printMetadata,printVersion,printMemoryLifetime,ihexRecord,displayHistoryHex' \
  > extracted.h

g++ -std=c++14 -Wall -I. -I"$SRC" -o logb_host logb_host.cpp
