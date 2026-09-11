# GlacierTemp -- firmware

Firmware del registrador de temperatura **GlacierTemp 1-cell rev02**, un logger de bajo
consumo para despliegues largos en glaciares.

## Sketches

| Sketch | Que hace |
|---|---|
| `GlacierTemp_1_cell_v02_claude` | Firmware principal: medicion, registro en flash, consola serie, GPS e Iridium |
| `GlacierTemp_1_cell_initializer` | Utilidad de puesta en marcha: escribe la configuracion inicial en la EEPROM |

## Hardware

- **MCU**: ATmega328P-MU (VQFN-32) con MiniCore
- **Cristal**: 7,3728 MHz -- divide exacto a 230400 baudios (`UBRR=3` en doble velocidad,
  error cero). Un 8 MHz daria 250000, un 8,5 % de error que ningun receptor decodifica.
- **Flash de datos**: Winbond W25Q64, 8 MiB por SPI, alimentada por un pin propio
- **Alimentacion**: una celda AAA con convertidor boost TPS610994
- **Radio**: modulo BLE (HM-10 o equivalente) como accesorio, en la misma UART que el USB
- **Opcionales**: GPS y modem Iridium RockBLOCK por puerto serie por software

## Compilar desde la linea de ordenes

Las librerias viven en `~/Documents/sketchbook/libraries`, que NO es el sketchbook por
defecto de `arduino-cli`; sin decirselo, la compilacion falla por `LowPower.h`:

```bash
arduino-cli compile --fqbn MiniCore:avr:328 \
  --libraries ~/Documents/sketchbook/libraries \
  GlacierTemp_1_cell_v02_claude
```

### Ocupacion de flash de programa

El ATmega328P con el bootloader de MiniCore deja **32.384 bytes**. Conviene anotar la
ocupacion en cada hito, porque el margen se agota antes de lo que parece y un sketch que no
cabe se descubre al final de una tanda de cambios y no al principio.

| Hito | Programa | % | RAM global |
|---|---|---|---|
| `v1.0.0` | 24.148 | 74 % | -- |
| `v1.3.0` (firmware 2.3) | 26.838 | 82 % | -- |
| firmware 2.7, antes de la tanda G1--G11 | 27.714 | 85 % | 869 B (42 %) |

## AJUSTES DE PLACA OBLIGATORIOS

El Arduino IDE 2.x guarda la seleccion de placa **por sketch**, no por proyecto. Compilar
con el reloj equivocado no da error: el UART queda a 102400 baudios reales mientras
`Serial.begin(230400)` afirma otra cosa, y la salida sale ilegible. Cada sketch lleva en su
cabecera el bloque `REQUIRED BOARD SETTINGS`; respetarlo.

## Registro de datos

El formato del registro se elige en tiempo de compilacion con las macros `LOG_*`. Los
desplazamientos se derivan en cadena, de modo que el escritor, el lector y el tamano del
registro no pueden discrepar. Una firma de 16 bits en EEPROM (`LOG_SIGNATURE`) codifica que
canales escribieron el log y cuantas sondas DS18B20 habia, y se compara en cada arranque:
si no coincide, el firmware se niega a interpretar el log en vez de imprimir una tabla de
sinsentidos.

## Volcado del log

| Comando | Formato |
|---|---|
| `LOG` | columnas alineadas, para leer con los ojos |
| `LOGC` | CSV, ~34 % menos de tiempo de linea serie; el que conviene para descargar |
| `LOGH` | Intel HEX crudo, sin interpretar |

`LOGH` es la salida de emergencia: vuelca los bytes tal cual para el caso en que el propio
firmware no pueda leer su log -- una compilacion con otro conjunto de canales, en terreno y
sin forma de reprogramar. `decode_logh.py`, que viaja junto al sketch, recupera el formato
desde la propia captura y emite el mismo CSV que `LOGC`.

## Licencia

Sin licencia declarada todavia.
