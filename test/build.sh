#!/bin/bash
# Reconstruye el banco a partir del firmware actual. Ejecutar tras cualquier
# cambio en el volcado LOGB o en la cabecera INFO.
set -e
cd "$(dirname "$0")"
SRC=../GlacierTemp_1_cell_v02_claude
python3 extract.py "$SRC" \
  'Functions.ino:hexDigit' \
  'EEPROM.ino:#LOGB_BLOCK,#LOGB_CHUNK,#FLOW_XOFF,#FLOW_XON,printHex16,crc16Ccitt,writeU16LE,flowControlCheck,dumpLogBinary,printMetadata,printVersion,printNewCommands' \
  > extracted.h
g++ -std=c++14 -Wall -I. -I"$SRC" -o logb_host logb_host.cpp
