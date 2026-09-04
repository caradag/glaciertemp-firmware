#!/bin/bash
# Reconstruye el banco a partir del firmware actual. Ejecutar tras cualquier
# cambio en el volcado LOGB o en la cabecera INFO.
set -e
cd "$(dirname "$0")"
SRC=../GlacierTemp_1_cell_v02_claude
python3 extract.py "$SRC" \
  'GlacierTemp_1_cell_v02_claude.ino:#FIRMWARE_VERSION,#PROTOCOL_VERSION,#SECTOR_SIZE,#MAX_SECTORS,#FAST_BAUDRATE' \
  'Functions.ino:hexDigit' \
  'EEPROM.ino:#LOGB_BLOCK,#LOGB_CHUNK,#FLOW_XOFF,#FLOW_XON,switchBaud,printHex16,crc16Ccitt,writeU16LE,flowControlCheck,dumpLogBinary,printMetadata,printVersion' \
  > extracted.h
g++ -std=c++14 -Wall -I. -I"$SRC" -o logb_host logb_host.cpp
