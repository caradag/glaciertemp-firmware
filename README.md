# GlacierTemp -- firmware

Firmware del registrador de temperatura **GlacierTemp 1-cell rev02**, un logger de bajo
consumo para despliegues largos en glaciares.

## Sketches

| Sketch | Que hace |
|---|---|
| `GlacierTemp_1_cell_v02_claude` | Firmware principal: medicion, registro en flash, consola serie, GPS e Iridium |
| `GlacierTemp_1_cell_initializer` | Utilidad de puesta en marcha: escribe la configuracion inicial en la EEPROM |
| `GlacierTemp_diagnostics` | Firmware de banco: prueba la placa y los sensores de forma detallada y no toca el log |

## Firmware de diagnostico

`GlacierTemp_diagnostics` se flashea para revisar una placa y se reemplaza despues por el
logger. Consola a 115200; `HELP` lista los comandos y `ALL` corre todas las pruebas no
destructivas con un resumen PASS/WARN/FAIL. No cambia la EEPROM, ni el log, ni la hora del
RTC; lo que toca para probar (registros de alarma y de onda cuadrada del RTC) lo restaura,
y deja el TMP119 y la flash dormidos como el logger.

Todo junto ocupa ~48 kB y el ATmega328P tiene 32, asi que las pruebas van en dos grupos,
elegidos con `DIAG_MODE` al principio del sketch (las dos variantes al 95 %):

| `DIAG_MODE` | Pruebas |
|---|---|
| `DIAG_SENSORS` (por defecto) | HDC1080 (IDs, configuracion, ruido, calefactor); TMP119 (ID, offset, lecturas) y **ruido frente a `TMP119_AVERAGING`** (0/8/32/64, intercaladas para que la deriva afecte a todas por igual); A0..A3 (cuentas, mV, ruido, abierto o conectado); **barrido del tiempo de estabilizacion** de un sensor alimentado desde otros pines (`SETTLE 0 123`: cada lectura comparada con la del mismo encendido a los 3 s, para que la deriva lenta no cuente; caida de tension en reposo; aviso si la salida esta en un extremo, donde estabilizado y muerto se ven igual) y su respuesta al escalon (`STEP`); `PWR`, alimentacion encendida con lectura cada segundo (para el multimetro o el balde); tiempos de cada lectura |
| `DIAG_BOARD` | EEPROM del logger (configuracion, calibraciones, contadores, firma del log); RTC (hora, OSF, flags, envejecimiento, temperatura), cristal del MCU contra el RTC, alarma -> linea de despertar; flash (ID, estado, power-down, coherencia log/contador) y prueba de escritura en el ultimo sector solo si esta vacio; bus 1-Wire; consumo dormido (`SLEEP 30`, despertado por el RTC); estado de todos los pines, LEDs, modulo Bluetooth |
| ambos | firma y fusibles del MCU, causa del reinicio, riel de 3,3 V y bateria, escaneo I2C |

`test/check_diag.py` compara las constantes que el diagnostico copia del logger (pines,
direcciones, mapa de EEPROM) y prueba en el PC sus funciones puras extraidas del fuente.

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
| firmware 2.8, con G1 + G6 + G4 (firmware) | 28.220 | 87 % | 906 B (44 %) |
| firmware 2.9, LOGH vuelca la memoria entera | 28.152 | 86 % | 906 B (44 %) |
| firmware 3.0, LOGH con volcado rapido | 28.346 | 87 % | 906 B (44 %) |
| firmware 3.1, cancelacion de volcado | 28.484 | 87 % | 907 B (44 %) |
| firmware 3.2, ID con corto y completo | 28.464 | 87 % | 907 B (44 %) |
| firmware 3.3, sin prompt tras LOGB/LOGH | 28.476 | 87 % | 907 B (44 %) |
| firmware 3.4, comando LIVE | 29.044 | 89 % | 913 B (44 %) |
| firmware 3.5, huso sin atrasar el reloj; TMP119 y A0 en el log | 30.628 | 94 % | 934 B (45 %) |
| firmware 3.6, registro de fallos de sensores y reintentos; sin coma flotante | 28.146 | 86 % | 924 B (45 %) |
| firmware 3.7, fallos de sensores tambien en I; compilado SIN A0 | 27.208 | 84 % | 902 B (44 %) |
| firmware 3.8 por defecto (sin canales analogicos) -- medido con `-DWIRE_TIMEOUT`, igual que el 3.7 en las mismas condiciones | 30.030 | 92 % | 907 B (44 %) |
| firmware 3.8, A0 registrado, alimentacion fija | 31.074 | 95 % | 927 B (45 %) |
| firmware 3.8, A0 alimentado desde A1 (o A1+A2+A3) | 31.524 / 31.546 | 97 % | 935 B (45 %) |
| firmware 3.10 por defecto, con CONT (`CONT_CAPTURE` activo) | 32.072 | 99 % | 935 B (45 %) |
| firmware 3.10 por defecto, `CONT_CAPTURE 0` | 30.212 | 93 % | 907 B (44 %) |
| firmware 3.10, A0 alimentado desde A1+A2+A3, sin TMP119 (CONT se apaga solo) | 31.196 | 96 % | 953 B (46 %) |
| firmware 3.11 por defecto, con CONT: sin NAME ni XON/XOFF; GPS, MSG y TUNNEL solo si hay GPS o Iridium | 31.454 | 97 % | -- |
| firmware 3.11, A0 alimentado desde A1+A2+A3, sin TMP119 (sin CONT) | 30.578 | 94 % | -- |
| firmware 3.11 + calibraciones unificadas (`calMv`), por defecto con CONT | 31.458 | 97 % | -- |
| idem, A0 alimentado desde pines, sin TMP119 (sin CONT) | 30.386 | 93 % | -- |
| idem, A0 alimentado desde pines, sin TMP119, con `CONT_CAPTURE 1` forzado | 32.256 | 99 % | -- |

Las filas del 3.8 se midieron con `arduino-cli` y `-DWIRE_TIMEOUT` (el codigo de timeout del
I2C), que es como compila la placa en terreno; el 3.7 da exactamente los mismos 30.030 bytes
en esas condiciones, de modo que la configuracion por defecto del 3.8 no cambia el binario.

## AJUSTES DE PLACA OBLIGATORIOS

El Arduino IDE 2.x guarda la seleccion de placa **por sketch**, no por proyecto. Compilar
con el reloj equivocado no da error: el UART queda a 102400 baudios reales mientras
`Serial.begin(230400)` afirma otra cosa, y la salida sale ilegible. Cada sketch lleva en su
cabecera el bloque `REQUIRED BOARD SETTINGS`; respetarlo.

## Sensores alimentados desde un pin (PIN_POWER)

Un sensor leido en un pin del header (A0..A3) se puede alimentar desde otro pin del header,
que el firmware enciende solo mientras lo lee. Un sensor de presion conectado a 3,3 V fijos
subia el consumo en reposo a ~6 mA medidos en la pila; alimentado asi, gasta solo durante la
lectura. Se configura en el bloque `SWITCHED POWER FOR THE SENSORS ON A0..A3` del sketch:

```c
#define LOG_A0        1
#define A0_NAME       "Depth"
#define A0_POWER      (PIN_POWER_A1|PIN_POWER_A2|PIN_POWER_A3)  // tres pines en paralelo
#define A0_SETTLE_MS  100   // ms desde el encendido hasta la lectura
```

- Varios pines en paralelo reparten la corriente y reducen la caida de tension que ve el
  sensor; se encienden con una sola escritura de `PORTC`, en el mismo ciclo de reloj.
- La espera se cuenta **desde el encendido**: el sensor se enciende al empezar el ciclo de
  medicion y antes de leerlo solo se espera lo que falte, asi que el tiempo que ya tardaron
  los sensores I2C y la bateria no se paga dos veces.
- `LIVE` mantiene el sensor encendido toda la sesion; `I`, `A0` y la calibracion `A01`/`A02`
  lo encienden, esperan y apagan. Antes de dormir se apaga siempre.
- Configuraciones imposibles (un canal que se alimenta a si mismo, un pin que alimenta y a la
  vez se registra, una alimentacion para un canal apagado, esperas de mas de 5 s) no compilan.
- `test/check_power.py` comprueba sobre el fuente que todos los caminos que leen el header
  encienden y apagan; `--self-test` rompe el fuente a proposito y exige que lo detecte.
- Sensor ratiometrico: la caida de tension en los pines aparece como error de ganancia
  (caida/3300 mV). Calibrar con `A01`/`A02` con el sensor alimentado asi la absorbe a la
  temperatura de la calibracion.
- `Ax_SETTLE_MS` se mide con el firmware de diagnostico (`SETTLE`, `STEP`, `PWR`). Con el
  sensor de presion del 2026-10-06: salida FALSA y alta (~1,6 V) durante los primeros ~100 ms
  tras encender, valor real desde 120 ms; se eligio 250 ms. Falta repetirlo en agua con
  hielo: en frio el arranque suele alargarse.

## Captura continua (CONT), para perfiles con dron

| Comando | Que hace |
|---|---|
| `CONT ON` | mide y graba sin pausa entre registros (~140 ms con la TMP119 a 8 promedios) |
| `CONT ON+H` | lo mismo con el bit HEAT del HDC1080, que calienta solo mientras convierte |
| `CONT?` | estado (`CONT n=... t=...s`); sin captura en marcha responde `CONT idle` |
| `CONT OFF` | termina e informa `CONT end reason=... n=... dur=...ms mean=...ms max=...ms drift=...ms heater=...` |

- Solo arranca con el log vacio: descargar y `RC` antes. Un log de CONT lleva firma de
  version 2 (`0x2...`): los mismos canales, con un `uint16` de milisegundos tras los
  segundos. Despues, el registro normal queda suspendido hasta otro `RC`.
- La hora: espera el cambio de segundo del RTC y cuenta con `millis()`; `drift` es la
  diferencia frente al RTC al terminar. Para alinear con el log del dron, la app pone la
  hora justo en el cambio de segundo.
- No duerme ni usa la alarma: es un bucle dentro de la sesion de comandos, como `LIVE`.
  Termina con `CONT OFF`, a la hora, con la memoria llena o con bateria critica. Ignora
  cualquier otra linea (el Bluetooth se cae en vuelo y eso no la detiene) y escribe una
  linea de estado cada 10 s.
- El contador no gasta las ranuras de la EEPROM: CONT mueve la referencia del ultimo
  reset (`COUNT_RESET = total - n`) cada minuto y al terminar.
- `LOG`/`LOGC` no leen un log de CONT (no hay sitio en el programa para la columna de
  ms): se descarga con la app (`LOGB`) o con `LOGH` + `decode_logh.py`, que conocen la
  version 2.
- **Espacio**: CONT ocupa ~2 kB. `CONT_CAPTURE` vale `(ANALOG_CHANNELS==0)`: sin canales
  analogicos cabe (97 %). Con A0 alimentado desde pines, desde que se unificaron las calibraciones
  cabe con `CONT_CAPTURE 1` forzado (99 %, 128 B libres); el valor automatico lo sigue apagando
  con canales analogicos para conservar margen.

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
