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

// Alejar la traza del borde en la parte inferior
// En el chip de memoria poner pins 7 y 3 con un pull-up the 10 kOhm a VCC (pin 3 flotante consumia 84 uA), y alimentar el chip directo de VCC (no hace falta alimentarlo con un pin IO)
// Add decoupling capacitor to the flash. From MEM_POWER Add a 100 nF (plus ~1–4.7 µF) next to the chip
// Order the non-quad variant of the memory chip (ordering option IG/IF), where QE defaults to 0 and /WP and /HOLD are real pins. For dos variants it is OK to tie pins 3 and 7 dirctly to VCC but a resistor is still recommended.

/*
  GlacierTemp 1-cell rev02 -- board pinout (U19 = ATMEGA328P-MU, VQFN-32).
  Package pin numbers run counter-clockwise from the pin-1 dot at the top-left.
  D##/A## are the Arduino / MiniCore names; PB6 and PB7 carry the crystal and
  are NOT usable as I/O with an external oscillator selected.

                          WAKEUP    TX      RX    RESET    SCL     SDA     H1-4    H1-3
                            D2      D1      D0     RST      A5      A4      A3      A2
                           PD2     PD1     PD0     PC6     PC5     PC4     PC3     PC2
                            32      31      30      29      28      27      26      25
                        +----------------------------------------------------------------+
    1-Wire hdr  PD3  D3 | 1                                                            24| PC1  A1  H1-2
    FLASH VCC   PD4  D4 | 2                                                            23| PC0  A0  H1-1
                GND     | 3                        ATMEGA328P-MU                       22| ADC7 A7  n/c
    3.3V        VCC     | 4                             VQFN-32                        21| GND
                GND     | 5                                U19                         20| AREF     n/c
    3.3V        VCC     | 6                                                            19| ADC6 A6  Vbatt/6
    XTAL1       PB6  D20| 7                                                            18| AVCC     3.3V
    XTAL2       PB7  D21| 8                                                            17| PB5  D13 SCK
                        +----------------------------------------------------------------+
                            9       10      11      12      13      14      15      16
                           PD5     PD6     PD7     PB0     PB1     PB2     PB3     PB4
                            D5      D6      D7      D8      D9     D10     D11     D12
                         GRN_LED FLSH_CS BT_STAT  SS_RX   SS_TX  RED_LED   MOSI    MISO

  Legend for the abbreviated signal names:
    WAKEUP   RTC DS3231 INT/SQW alarm output, wakes the MCU from power-down
    TX / RX  console UART, shared by the FTDI header and the HM-10 bluetooth
    SCL/SDA  I2C bus: DS3231 RTC 0x68, HDC1080 0x40, TMP119 0x48
    H1-1..4  4-pin expansion header H1 (A0..A3)
    GRN_LED  green LED, lit on a successful measurement and after a reset
    RED_LED  red LED, lit if a start-up test fails, flashed if a write fails
    FLSH_CS  W25Q64 flash chip select (has a 47k pull-up to 3.3V, R26)
    BT_STAT  HM-10 bluetooth connection status input
    SS_RX/TX AltSoftSerial pins, only used when a GPS or Iridium modem is fitted
    MOSI/MISO/SCK  SPI to the W25Q64 flash and to the ICSP header
    FLASH VCC      MEM_POWER: this pin is the flash chip's ONLY supply. Neither
                   temperature sensor is powered from it; both sit on 3.3V.
    Vbatt/6        raw cell voltage through the R13/R12 = 10M/2M divider

  CODE SIZE NOTE: roughly 1.5 kB of flash is spent on floating point. The soft
  float helpers alone are ~1.1 kB (__addsf3x, __mulsf3x, __divsf3x and friends),
  pulled in by getBatteryVoltage(), getTempAndRH(), readFloat()/updateVar() and
  lightOStream::operator<<(float). All four could be done in integer arithmetic.
  A further ~2 kB is available simply by turning on Tools > Compiler LTO.
*/

#include "./lightOStream.h"
char buf[127];
lightOStream out(buf, sizeof(buf));

void displayDateVec(byte *dateVec, bool showTimezone=true);
void displayUnixTime(unsigned long uTime, bool showTimezone=true);
unsigned long readULong(char *str, int base=10);

#define HDC1080_ADDR 0x40

// TMP119 ultra-high accuracy temperature sensor (U24). ADD0 is tied to GND on
// this board, which selects address 0x48.
#define TMP119_ADDR 0x48
#define TMP119_TEMP_REG   0x00
#define TMP119_CONFIG_REG 0x01
// Config register: MOD[11:10]=01 shutdown, AVG[6:5]=01 -> 8 averaged conversions.
// The sensor is parked here between readings: 0.15uA typ, versus ~16uA if it is
// left free-running in the continuous conversion mode it boots into.
//
// The field layout is easy to miss a bit in, so for reference:
//     15..12  alert flags and EEPROM_Busy
//     11..10  MOD    00 continuous, 01 shutdown, 11 one-shot
//      9..7   CONV   conversion cycle time, continuous mode only
//      6..5   AVG    00 none, 01 x8, 10 x32, 11 x64
//
// This was 0x0220 until 2026-08-22, which sets bit 9 and bit 5 -- CONV=100 and
// AVG=01, with MOD left at 00. It did not shut the sensor down at all: it put
// it into CONTINUOUS conversion on a one-second cycle. MOD=01 is bit 10, so the
// correct word is 0x0420.
//
// The error stayed hidden because a one-shot conversion returns the part to
// shutdown by itself, so the only exposure here was between setup() calling
// tmp119Sleep() and the first measurement -- up to one measurement interval of
// ~16uA. It was found on the bench with the power-test firmware, where the same
// call was made after every reading and cost 16uA permanently.
#define TMP119_SHUTDOWN   0x0420

//-------------------- Conversion mode and averaging ---------------------------
// The value written to TMP119_CONFIG_REG sets two fields:
//   MOD[11:10]  01 = shutdown
//               11 = one-shot: run ONE conversion, then drop back to shutdown
//   AVG[6:5]    00 = no averaging, 01 = 8, 10 = 32, 11 = 64 averaged conversions
//
// Note there is no 4-average option: the hardware offers only 0, 8, 32 and 64.
//
// Averaging reduces NOISE only. It does not improve the +/-0.1 degC absolute
// accuracy, which is set by factory trim. Since the MCU stays awake for the
// whole conversion, the choice is a straight trade of noise against awake time,
// and therefore against battery life.
//
//   AVG   config    conversion    noise      choose when
//   ---   ------    ----------    -------    --------------------------------
//    0    0x0C00      15.5 ms     highest    battery matters more than noise
//    8    0x0C20       125 ms     good       default, best general compromise
//   32    0x0C40       500 ms     better     noisy site and power to spare
//   64    0x0C60         1 s      lowest     bench calibration only
//
// At the 600 s measurement interval, 8 averages costs about 0.02% duty cycle;
// 64 averages costs about 0.17%, which is comparable to the whole sleep budget.
// Uncomment ONE of the following, and set TMP119_CONV_TIMEOUT to match. Each
// value carries MOD=11 (one-shot) in the top bits plus the AVG selection.
#define TMP119_AVERAGING   0x0C20   // one-shot, 8 averages,  ~125 ms  (default)
//#define TMP119_AVERAGING 0x0C00   // one-shot, no averaging, ~15.5 ms
//#define TMP119_AVERAGING 0x0C40   // one-shot, 32 averages, ~500 ms
//#define TMP119_AVERAGING 0x0C60   // one-shot, 64 averages, ~1 s

// Upper bound on how long getHighAccuracyTemp() waits for the Data_Ready flag.
// It must stay comfortably above the conversion time selected above, otherwise
// every reading times out and returns INVALID_TEMP_HA.
// Suggested values: 100 for no averaging, 250 for 8, 800 for 32, 1500 for 64.
#define TMP119_CONV_TIMEOUT 250 // ms, matches the 8-average setting above

// GlacierLapse v04
// LEFT SIDE
// RESET (pin 22)
// SERIAL RX (pin 0)
// SERIAL TX (pin 1)
#define WAKEUP_PIN 2 //Pin used to trigger the interrupt to wake up controlled by the RTC
#define ONE_WIRE_PIN 3 // 1-Wire header, DS18B20. Needs an external 4.7k pull-up to 3.3V
// #define UNUSED 4
// #define UNUSED PIN_PB6 //PIN_PB6 equivalent to 20
// #define UNUSED PIN_PB7 //PIN_PB7 equivalent to 21
#define GREEN_LED 5
#define FLASH_MEMORY_CS 6 
#define BLUETOOTH_SATUS_PIN 7 
#define SOFTWARE_SERIAL_RX 8 

//RIGHT SIDE
// A5 SCL
// A4 SDA
#define BATT_VOLTAGE_PIN A6
// A0..A3 are the four pins of expansion header H1 (H1-1..H1-4). They are held
// as grounded inputs unless LOG_A0..LOG_A3 enable them; see powerManagementSetup().
#define MEM_POWER 4 // PD4. Powers ONLY the W25Q64 flash, not the sensors
// SPI interfae
//#define SCK 13
//#define MISO 12
//#define MOSI 11
#define LED_PIN 10 // RED led (the green one is GREEN_LED, pin 5)
#define SOFTWARE_SERIAL_TX 9 // Data from GPS to the microcontroller, corresponding to TX pin on the GPS board

// #define UNUSED A7



//********************************************************************
//***************** MAIN CONFIGURATION PARAMETERS ********************
//********************************************************************


#define GPS_INSTALLED 0
#define IRIDIUM_INSTALLED 0
#define LONG_MASSEGES 1

// Now we define functions calls depending on whether the different hardware components are installed or not
// functions that are left undefined will be simply removed by the precompiler
#if GPS_INSTALLED == 1
  #define timeUpdateCall() gpsUpdate()
#else
  #if IRIDIUM_INSTALLED == 1
    #define timeUpdateCall() iridiumPosUpdate()
  #else
    #define timeUpdateCall() noIridiumOrGpsInstalled()
  #endif
  
#endif

#if IRIDIUM_INSTALLED == 1
  #define iridiumSendMessageCall() iridiumSendMessage()
#else
  #define iridiumSendMessageCall() iridiumNotInstalled()
#endif


#if GPS_INSTALLED == 1 || IRIDIUM_INSTALLED == 1
  #include <AltSoftSerial.h>
  AltSoftSerial SoftSerial; // Creating softwate serial port to communicate with GPS or Iridium RockBLOCK
  #define serialTunnelCall() serialTunnel()
#else
  #define serialTunnelCall() noIridiumOrGpsInstalled()
#endif


//********************************************************************
//****************** LOGGED CHANNEL SELECTION ************************
//********************************************************************
// Each channel below is included in the record, in the console output and in
// the LOG/LOGC dumps only when its switch is 1. A channel switched off costs
// nothing at all: the compiler removes its measurement code, its two bytes in
// the record and its column in the dump.
//
// ------------------------- READ THIS FIRST --------------------------------
// CHANGING ANY OF THESE SWITCHES CHANGES THE RECORD LAYOUT, and records written
// before the change CANNOT BE READ by a firmware built after it. LOG and LOGC
// step through the log at this build's BYTES_PER_SAMPLE and read each field at
// this build's offsets, so a changed record size misaligns every record after
// the first, and a changed field order misreads the fields inside it. The dump
// still looks like a neat table, which is exactly what makes it dangerous.
//
// The build signature below detects the change: on the first start-up after it
// the logger prints a warning, lights the red LED and REFUSES TO LOG.
//
// TO RECOVER THE OLD DATA, re-flash the firmware that wrote it -- the same
// channel switches it was built with -- and dump the log with that. Only then
// clear the log with RC and go back to the new build.
//
// If the old data is not wanted, RC on its own is enough. Note that RC does not
// erase the flash, it only resets the sample counter, so the old records
// physically survive until new ones overwrite them. That is a thin second
// chance, not a plan: the first sector erase of the new run destroys the start
// of the old log.
// --------------------------------------------------------------------------

#define LOG_VOLTAGE   1   // battery cell voltage, mV, via the R13/R12 divider on A6
#define LOG_HDC_TEMP  1   // HDC1080 temperature, centi°C
#define LOG_HDC_RH    1   // HDC1080 relative humidity, deci %
#define LOG_TMP119    0   // TMP119 high accuracy temperature, centi°C
#define LOG_DS18B20   0   // NUMBER of DS18B20 sensors on the D3 1-Wire bus. See below.

//---------------------- EXPANSION HEADER H1, A0..A3 -------------------------
// Four general purpose analog inputs on header H1 (H1-1=A0 .. H1-4=A3). Each
// enabled pin is sampled, converted to millivolts through its own two-point
// calibration (commands A01/A02 for A0, A11/A12 for A1, and so on, stored in
// EEPROM exactly as V1/V2 are for the battery) and logged as an int in mV.
//
// *** ADC REFERENCE AND SETTLING TIME -- THE COST OF ENABLING THESE ***
// The battery divider needs the INTERNAL 1.1 V reference: the cell is nominally
// 1.5 V behind a 10M/2M divider, so full scale is 6.6 V and one count is 6.4 mV.
// Sensors on H1 almost always swing over the full 0-3.3 V rail instead, which
// the 1.1 V reference cannot see, so the analog pins are read against the
// DEFAULT reference (VCC, 3.3 V, one count = 3.2 mV).
//
// Both cannot be in force at once, so enabling ANY analog pin makes every
// measurement cycle switch the reference to VCC, read the pins, and switch it
// back. Each switch needs ADC_REF_SETTLE_MS plus a discarded conversion before
// the readings are trustworthy, and that time is spent awake, on battery. The
// present cost is about 2*ADC_REF_SETTLE_MS per cycle, which is negligible at a
// 600 s interval and starts to matter at 1-2 s.
//
// IF YOU DO NOT NEED THE BATTERY VOLTAGE you can avoid the switching entirely:
// set the reference to DEFAULT once in powerManagementSetup() and delete the
// switching in analogPinsBegin()/analogPinsEnd(). Note that LOG_VOLTAGE 0 alone
// is NOT enough to get that -- it only drops the voltage from the record. The
// reading itself is still taken for the low-battery warnings, the LOW_VOLTAGE
// interval stretch, the battery gauge in I, and the V command, all of which
// keep needing the 1.1 V reference. Removing those uses is what frees the
// reference, and then the battery reading against VCC is both coarse and
// referenced to a rail that itself sags as the cell empties.
//
// AREF (package pin 20) is not connected on rev02, so there is no reference
// capacitor to charge and the settle is short. A board that fits one needs a
// considerably longer ADC_REF_SETTLE_MS -- 10 ms or more for a 100 nF cap.
#define ADC_REF_SETTLE_MS 5

#define LOG_A0  0
#define LOG_A1  0
#define LOG_A2  0
#define LOG_A3  0

// Column heading for each analog pin: what is actually being measured there.
// These are printed as the column title in LOG and LOGC, and by the I report and
// the bare A0..A3 commands. Keep them to 8 characters or fewer: that is the
// width of an analog column in the aligned LOG layout, and a longer name is not
// truncated, it just pushes its own heading out of line with the data beneath
// it. LOGC is unaffected, having no columns to align.
#define A0_NAME "A0"
#define A1_NAME "A1"
#define A2_NAME "A2"
#define A3_NAME "A3"

//----------------------------- DS18B20 on D3 --------------------------------
// LOG_DS18B20 is a COUNT, not a flag: 0 for none, 1 for a single sensor, N for
// N sensors sharing the D3 1-Wire bus. Each one adds its own column to the log
// and its own two bytes to the record. The bus needs an external 4.7k pull-up
// to 3.3 V; there is no internal pull-up strong enough.
//
// HOW MULTIPLE SENSORS ARE KEPT APART. Every DS18B20 carries a unique 64-bit
// ROM code burned in at the factory. At start-up the bus is enumerated, the
// codes are sorted by serial number, and slot 0 is bound to the lowest, slot 1
// to the next, and so on. Readings are then addressed to a specific ROM code
// rather than broadcast, which buys two things:
//   - the column order is fixed by the hardware and is identical on every
//     power-up, whatever order the sensors happen to answer the search in;
//   - a sensor that stops responding leaves NaN in ITS OWN column and does not
//     shift everybody else's readings one column to the left.
// Each column is headed DS-xxxx, xxxx being the bottom four hex digits of that
// sensor's serial number, so a column can always be traced to the physical
// part. The full list is printed at start-up and by the I command.
//
// If fewer sensors answer than LOG_DS18B20 expects, the logger says so and
// lights the red LED but carries on, logging NaN in the unbound slots. That is
// deliberate: in the field a broken probe should cost one column, not the whole
// record.
//
// All sensors are converted SIMULTANEOUSLY by one broadcast command, so the
// conversion wait below is paid once however many are fitted. Only the
// scratchpad reads are per sensor, and those cost roughly 5 ms each.
//
// The conversion time is charged to the awake window, i.e. straight to the
// battery, and at the top setting it is longer than a whole short measurement
// interval. The sensor is polled rather than blindly delayed, so a faster
// setting really does return sooner.
//
//   DS18B20_RESOLUTION   step        conversion   comment
//         9            0.5    °C       93.75 ms   fine as a second probe
//        10            0.25   °C      187.5  ms
//        11            0.125  °C      375    ms
//        12            0.0625 °C      750    ms   longer than a 1 s interval
//
// The TMP119 already resolves 0.0078 °C, so this part is normally a remote or
// second-point probe where the lower settings are perfectly adequate.
//
// Idle current is about 1 uA per sensor on the permanent 3.3 V rail. Against a
// 6 uA sleep budget that is not nothing, and it multiplies by the number fitted;
// a board that cares should feed the bus from a GPIO.
#define DS18B20_RESOLUTION 12

// Upper limit on LOG_DS18B20. Three bits are reserved for the count in the log
// signature, which is what lets a change from three sensors to five register as
// a layout change instead of silently corrupting the log. Eight is also about
// as many as a 4.7k pull-up will drive reliably down a field cable, and costs
// 16 bytes of record and 80 bytes of RAM.
#define MAX_DS18B20 8

//--------------------- DERIVED: record layout and signature -----------------
// Nothing below here is meant to be edited.
//
// Record layout. The timestamp is always present; every enabled channel adds
// one int, in the order listed. Writing the offsets as a chain rather than as
// literal numbers means the record, the reader and BYTES_PER_SAMPLE can never
// disagree about where a field lives.
//
//   +0  unsigned long  timestamp
//   then, each only if enabled, in this order:
//       int  battery voltage (mV)
//       int  HDC1080 temperature (centi°C)
//       int  HDC1080 relative humidity (deci %)
//       int  TMP119 temperature (centi°C)
//       int  DS18B20 temperature (centi°C)
//       int  A0, A1, A2, A3 (mV)
//
// With the four original channels on and the rest off this reproduces the
// historic 12 byte record exactly, so an existing log stays readable.
#define OFF_TIME      0
#define OFF_VOLTAGE   4
#define OFF_HDC_TEMP  (OFF_VOLTAGE  + 2*LOG_VOLTAGE)
#define OFF_HDC_RH    (OFF_HDC_TEMP + 2*LOG_HDC_TEMP)
#define OFF_TMP119    (OFF_HDC_RH   + 2*LOG_HDC_RH)
#define OFF_DS18B20   (OFF_TMP119   + 2*LOG_TMP119)
#define OFF_A0        (OFF_DS18B20  + 2*LOG_DS18B20)
#define OFF_A1        (OFF_A0       + 2*LOG_A0)
#define OFF_A2        (OFF_A1       + 2*LOG_A1)
#define OFF_A3        (OFF_A2       + 2*LOG_A2)
#define BYTES_PER_SAMPLE (OFF_A3    + 2*LOG_A3)

#define ANALOG_CHANNELS (LOG_A0+LOG_A1+LOG_A2+LOG_A3)

// Largest record ANY build of this firmware can produce: the timestamp plus one
// int for every channel that exists, with the DS18B20 bus full. LOGH uses it as
// the fallback stride when the log in flash was written by a build whose record
// size is unknown, so that a raw rescue dump cannot come up short.
#define MAX_RECORD_BYTES (4 + 2*(4 + MAX_DS18B20 + 4))

// Build signature: which channels this firmware writes, plus the version of the
// encoding itself. Stored in EEPROM when the log starts and compared at every
// start-up; see checkLogFormat() in EEPROM.ino.
//
// The version occupies the top nibble so that a never-initialised EEPROM, which
// reads 0xFFFF, decodes as version 15 and is recognised as "no signature" rather
// than as some other channel set. Bump LOG_FORMAT_VERSION if the MEANING of a
// field changes without its bit changing -- new units, a different sentinel --
// because the channel mask alone cannot see that.
#define CH_BIT_VOLTAGE  0x0001
#define CH_BIT_HDC_TEMP 0x0002
#define CH_BIT_HDC_RH   0x0004
#define CH_BIT_TMP119   0x0008
#define CH_BIT_DS18B20  0x0010
#define CH_BIT_A0       0x0020
#define CH_BIT_A1       0x0040
#define CH_BIT_A2       0x0080
#define CH_BIT_A3       0x0100
// Bits 9..11 hold the DS18B20 count minus one, so three sensors and five
// produce different signatures even though both set CH_BIT_DS18B20. Without it
// the record size would change while the signature did not, which is precisely
// the silent corruption the signature exists to catch. A single sensor encodes
// as zero, so signatures written before the count existed still match and no
// spurious mismatch is reported.
#define CH_DS_COUNT_SHIFT 9
#define LOG_FORMAT_VERSION 1
#define LOG_SIGNATURE_NONE 0xFFFF

#define LOG_SIGNATURE ((uint16_t)( ((uint16_t)LOG_FORMAT_VERSION<<12) \
  | (LOG_VOLTAGE  ? CH_BIT_VOLTAGE  : 0) \
  | (LOG_HDC_TEMP ? CH_BIT_HDC_TEMP : 0) \
  | (LOG_HDC_RH   ? CH_BIT_HDC_RH   : 0) \
  | (LOG_TMP119   ? CH_BIT_TMP119   : 0) \
  | (LOG_DS18B20  ? CH_BIT_DS18B20  : 0) \
  | (LOG_DS18B20  ? ((uint16_t)((LOG_DS18B20)-1)<<CH_DS_COUNT_SHIFT) : 0) \
  | (LOG_A0       ? CH_BIT_A0       : 0) \
  | (LOG_A1       ? CH_BIT_A1       : 0) \
  | (LOG_A2       ? CH_BIT_A2       : 0) \
  | (LOG_A3       ? CH_BIT_A3       : 0) ))

// Set to 0 to make a layout mismatch a warning only, and keep appending records
// to a log that a single reader can no longer parse. Suspending is the default
// because the mismatch can only appear on the first run after a reflash, which
// is a bench operation, whereas a silently mixed log is discovered months later.
#define SUSPEND_ON_FORMAT_MISMATCH 1

#if BYTES_PER_SAMPLE < 6
  #error "No channel enabled: the record would hold nothing but a timestamp."
#endif
#if LOG_DS18B20 > MAX_DS18B20
  #error "LOG_DS18B20 exceeds MAX_DS18B20: only three bits are reserved for the count in the log signature."
#endif

//********************************************************************
//******************** INTERNAL EEPROM MANAGEMENT ********************
//********************************************************************
#include <EEPROM.h>
// See EEPROM tab for data stored on EEPROM

// EEPROM USAGE, the following of this section is generated automatically by the initialization script

// Copy this definitions to the main code 
// Initialization date: 2026-8-22 21:19:19
#define COMPILATION_TIME 840748759 // time of compilation
#define COUNT_ADDR 0 // Unsigned int values that stores total number of measurements. 30 values are used and each written up to 100000 times only, reached that number the counts continue in the next slot to avoid EPROM errors after 100,000+ writing cycles
#define COUNTERS_SLOTS 30 // Number of slots used to store the measurement count
#define COUNT_RESET_ADDR 120 // Unsigned long storing the count of the last count reset (note that reseting all counters would loose track of how many times they have been written)
#define RESET_TIME_ADDR 124 // Unsigned long storing timestamp of last battery change
#define VOLTAGE_RESET_ADDR 128 // Unsigned int storing batery voltage in millivolts at the time of last picture count reset
#define LAST_RTC_TIME_CHECK 130 // Unsigned long storing timestamp of last RTC check
#define LAST_TIME_ADJUSTMENT_SECS 134 // int storing last RTC adjustment in seconds
#define CUMULATIVE_TIME_ADJUSTMENT_SECS 136 // int storing cumulative RTC adjustment in seconds
#define DEFAULT_OSCCAL 138 // byte storing the default value of the oscillator calibration OSCCAL
#define REFERENCE_VOLTAGE_1 139 // int storing the first reference voltage in milivolts
#define REFERENCE_VOLTAGE_COUNT_1 141 // int storing the digital count associated with reference voltage 1
#define REFERENCE_VOLTAGE_2 143 // int storing the second reference voltage in milivolts
#define REFERENCE_VOLTAGE_COUNT_2 145 // int storing the digital count associated with reference voltage 1
#define ANALOG_CAL_ADDR 147 // Base of the H1 analog calibration table: 4 pins x (int mV, int count) x 2 points = 32 bytes
#define LOG_SIGNATURE_ADDR 179 // unsigned int storing the channel signature of the log currently in flash
// Variables that can be changed by the user
#define MEASURE_INTERVAL 0 // (unsigned long) Interval between measurements (sec)
#define LOW_VOLTAGE_INTERVAL_MULTIPLIER 1 // (byte) Low voltage interval multiplier
#define TIMEZONE 2 // (signed int) Time Zone (hours)
#define ADJUST_RTC_INTERVAL 3 // (unsigned int) GPS clock adjustments frequency (days)
#define MESSAGE_FREQUENCY_DAYS 4 // (byte) Satellite messages frequency (days)

// Help text
#define HELP_TEXT 352 // Memory address of HELP text

char varComm[]="INTLVMTZNADJMSW";
char varTypes[]="Ubiub";
int varAddr[]={181,221,254,274,315};
byte varLengths[]={4,1,2,2,1};

// 243 bytes left in EEPROM


//********************************************************************
//*************** POWER AND SLEEP MANAGEMENT *************************
//********************************************************************
#include "LowPower.h" // Default arduino low-power library

void(* resetFunc) (void) = 0; //declare reset function @ address 0


//********** VOLTAGE DIVIDER - Battery voltage measurement **********
//                        GND
//                         |
//                       0.1 uF
//                         |
//        |-----[ Rd ]-----|------[  Ru  ]-----|
//        |                |                   |
//        |                |                   |
//       GND       BATT_VOLTAGE_PIN           VCC

// Measurements are done in a voltage divider with a pull-down (Rd) and pull-up (Ru) resistors
// Low value resistors produce a high stand-by leakage current. However, using high value resustors can produce wrong measurements
// because the resistance of the measuring input pin start to affect the voltage devider. To avoid that effect a capacitor was included, to keep 
// the voltage stable during measurement.
// Multiplier = (Rd+Ru)/Rd
// With  Rd=680k & Ru=2M  Multiplier=(1+2)/1 = 3.94
// Using the internal voltge reference of 1.1 volts
// This produce saturation at 4.35V
// And a Leakage current at 3.7 V of V/(Ru+Rd)= 3.7/2680k = 1.38 uA which is a significant fraction of the total standby current of 2.9 uA with this configuration

// With  Rd=2M & Ru=10M  Multiplier=(2+10)/2 = 6
// Using the internal voltge reference of 1.1 volts
// This produce saturation at 6.60V and a resolution of 6.4 mV
// And a Leakage current at 3.7 V of V/(Ru+Rd)= 3.7/12M = 0.31 uA which is a razonable fraction of the total standby current of 1.6 uA with this configuration

// That gives a measurement range between 0 and 9.9 V (maximum expected battery voltage is 8.4 V)
// The 10bit ADC of Arduino measure voltage from 0 to 1023 counts. Therefore, the count multiplier K would be
// K = RefVoltage * Multiplier /1023 = 0.00894 Volts


#define BATT_SAMPLES 21 // number of samples for battery voltage and solar current measurements

// Battery thresholds in MILLIVOLTS for the single AAA alkaline cell.
// Typical terminal voltage at low drain and ~21 degC (see voltageToCapacity()):
//     25% remaining ....... ~1190 mV
//     10% remaining ....... ~1060 mV
//      5% remaining ....... ~1000 mV
//      0% (boost converter can no longer hold 3.3V) ... ~900 mV
// Both are currently 0, which disables the warnings and also disables the
// LOW_VOLTAGE_INTERVAL_MULTIPLIER slow-down below. Set LOW_VOLTAGE to about
// 1190 and CRITICAL_VOLTAGE to about 1000 to turn them on.
#define LOW_VOLTAGE 0      // e.g. 1190 mV (~25% left) -> start stretching the interval
#define CRITICAL_VOLTAGE 0 // e.g. 1000 mV (~5% left)


//********************************************************************
//******************* GPS (Adafruit Ultimate GPS) ********************
//********************************************************************


#define GPS_TIMEOUT 900000 //Maximum time to wait for a GPS fix, 900000 = 15 minutes
#define GPS_NORESPONSE_TIMEOUT 30000 //Maximum time to wait for a GPS to output any kind of data (this is implemented to avoid waiting GPS_TIMEOUT if there is no GPS or the GPS died), 30000 = 30 seconds

// Configuration variables. Stored in EEPROM and editable by the user
float latitude;
float longitude;
int timeZone;
unsigned int adjustRTCinterval;


bool displayNMEA=false;


//********************************************************************
//********************* RTC DS3231 MODULE ****************************
//********************************************************************
// We use the common DS3231 module that comes with an EEPROM chip (24C32N) and a LED. It also comes with a charging function of the battery even if it is not a rechargable battery.
// The power consumption of the module is approximately 2.0mA, by removing the LED (and optionally the resistor besides it) consumption drops to 0.5mA which is still a lot (the microprocesor
// in sleep mode uses 0.05mA). Further reductions can be achived by removing the block of 4 resistors beside the capacitors that are pullups on the I2C lines and the alarm lines, all these
// lines already have pull-ups on the Arduino. The diode and resistor of the charging circuit can be removed as well with no loss at all. And also the EEPROM chip and its block of resistors can be removed.
// Sumarizing, everything can be removed beside the two capacitors. With all this modification the RTC consumption dropped to 0.09mA. Which added to the Arduino standby and GPS standby totals 0.16mA
// More details on these modifications are in https://thecavepearlproject.org/2014/05/21/using-a-cheap-3-ds3231-rtc-at24c32-eeprom-from-ebay/
// A further power reduction was achieved powering the RTC through the battery lines. Whichs drops power consumption to a few uA, however, this can be incompatible with using a non-rechargable 
// battery, so a rechargable battery was used instead, witha diod and resistor to limit chargig current
#include <Wire.h>
#define CLOCK_ADDRESS 0x68
// NOTE: When the clock is not present, the displayed time is Time: 2165-85-165 45:165:165

unsigned long sessionStartTime=0;// Time equivalent to year 2000
int nextMinuteOfDay=0;
unsigned int prevDay=0;
unsigned long measureInterval;
byte lowVoltageMultiplier=1;// LOW_VOLTAGE_INTERVAL_MULTIPLIER, applied by effectiveMeasureInterval()

byte currentDateVec[6];
unsigned long currentTime;
byte wakeupDateVec[6];

uint32_t mktime2(int YYYY,int MM=1,int DD=1,int hh=0,int mm=0,int ss=0);
void displayTime(unsigned int HH=24,unsigned int MM=60,unsigned int SS=60);

//********************************************************************
//************************* IRIDIUM MODEM ****************************
//********************************************************************
// I've used two tipes of RockBlock Iridium modems
// The RockBlock Mk2 is vased on a RockBLOCK 9602, the the other is the RockBOK 9603
// The 9602 have a minimum input voltage of 4.5v while the 9603 have a minimum of 3.0v therefore it can work stright from a Li-Ion battery
// When using the 9603 with Li-ion you MUST CONNECT THE BATTERY TO THE 5V PIN, because the Li-ion pin has a diode preventing current going into the
// battery if  5V power s connected in addition to the battery, that results in a voltage drop. IF ONLY battery power is used, it is better to skip the diode 
// by connecting direct to the 5v pin. See https://docs.groundcontrol.com/iot/rockblock/electrical/power-supply
// The 9603 uses 100 uA on sleep (enagle pin low), and the 9602 uses 200 uA, that's why it is better to fully cut power. As 100 uA is too much in the standby status (Glacier lapse uses less than 2 uA)
// See https://docs.groundcontrol.com/Power/Compare

/* Iridium modem RockBlock Mk2 (9602)
  Pinout:
  1 RTS Iridium 9602 RTS -> Not connected
  2 RXD Iridium 9602 RX (input to RockBLOCK) -> To Arduino pin ARDUINO_IRIDIUM_TX (7)
  3 TXD Iridium 9602 TX (output from RockBLOCK) -> To Arduino pin ARDUINO_IRIDIUM_RX (8)
  4 Vcc 5V Power supply (450mA limit) -> Unused
  5 CTS Iridium 9602 CTS -> Not connected
  6 GND Ground
  7 GND Ground
  8 5v In 5V Power supply (450mA limit) -> Unused
  9 5v Out 5V regulated output, for powering external Arduino host -> Unused
  10 RI Ring Indicator -> Unused
  11 NetAv Network available signal -> Unused
  12 OnOff Sleep control -> To Arduino pin ARDUINO_IRIDIUM_SLEEP (9)
  13 LiIon 3.7V Li-Ion power supply -> To Li-Ion battery pack
  14 GND Ground
*/

/* Iridium modem RockBlock 9603
  Pinout:
  1  RXD Iridium 9602 RX (input to RockBLOCK) -> To Arduino pin ARDUINO_IRIDIUM_TX (7)
  2  CTS Iridium 9602 CTS -> Not connected
  3  RTS Iridium 9602 RTS -> Not connected
  4  NetAv Network available signal -> Unused
  5  RI Ring Indicator -> Unused
  6  TXD Iridium 9602 TX (output from RockBLOCK) -> To Arduino pin ARDUINO_IRIDIUM_RX (8)
  7  OnOff Sleep control -> To Arduino pin ARDUINO_IRIDIUM_SLEEP (9)
  8  5v In 5V Power supply (450mA limit) -> To Li-Ion battery pack (IMPORTANT: BATTERY HERE NOT TI LI-ION PIN, SEE NOTES)
  9  LiIon 3.7V Li-Ion power supply -> Unused
  10 GND Ground
*/
bool messageSent=true;// Flag to know if the last message to be sent was actually sent (to retry if not)

// Sentinel for "no valid reading". It has to sit outside the range of every
// sensor on the board (-55..+150 C). The old value of -999 could not: in centi°C
// that is -9.99 C, a perfectly ordinary temperature on a glacier, so a failed
// read was indistinguishable from real data once it was in the log.
#define INVALID_TEMP -32768
#define INVALID_TEMP_HA INVALID_TEMP
// Was -1 until 2026-08-22, which printed as "-0.1" in the log instead of being
// recognised as a failed read. It now matches INVALID_TEMP, and because
// humidity can never legitimately be negative, every display test is written as
// "< 0" so that records written by the older firmware are still recognised.
#define INVALID_RH -32768
// Analog header channels are stored in mV, and a calibration with a negative
// offset can legitimately produce a small negative reading, so the sentinel has
// to sit outside anything the 10-bit ADC could ever produce.
#define INVALID_ANALOG -32768
#if IRIDIUM_INSTALLED == 1
  // Only the Iridium message builder reads or resets these
  int minTemp=INVALID_TEMP;
  int maxTemp=INVALID_TEMP;
#endif
int currentTemp=0;//temperature in centiºC
int currentRH=0;// Relative humidity in deci %
int currentTempHA=INVALID_TEMP_HA;// TMP119 temperature in centiºC
#if LOG_DS18B20
int currentTempDS[LOG_DS18B20];  // one temperature in centiºC per sensor slot
byte dsRom[LOG_DS18B20][8];      // ROM code bound to each slot, sorted by serial
byte ds18b20Found=0;             // how many actually answered the bus search
bool ds18b20Extra=false;         // set if the bus holds more sensors than there are slots
#endif
#if ANALOG_CHANNELS
// Calibrated H1 readings in mV, indexed by pin number 0..3. Entries for pins
// that are switched off are never written or read.
int currentAnalog[4]={INVALID_ANALOG,INVALID_ANALOG,INVALID_ANALOG,INVALID_ANALOG};
#endif
// Set by checkLogFormat() when the log in flash was written by a build with a
// different set of channels. Blocks further logging while SUSPEND_ON_FORMAT_MISMATCH.
bool logFormatMismatch=false;


// SPI Memory
#include <SPI.h>
#define READ 0x03
#define WRITE 0x02
#define SECTOR_ERASE 0x20
#define POWER_DOWN 0xB9
#define POWER_UP 0xAB
#define WRITE_ENABLE 0x06
#define READ_STATUS_1 0x05
#define READ_STATUS_2 0x35
#define STATUS2_QE   0x02    // S9, Quad Enable
#define STATUS1_SRP0 0x80    // S7,  Status Register Protect 0 (in SR1)
#define STATUS2_SRP1 0x01    // S8,  Status Register Protect 1 (in SR2)

// Set to 1 to print the exact hardware state immediately before power-down.
// The implementation is in Power.ino; this switch has to be here because the
// IDE appends the other tabs after this file. Costs nothing while it is 0.
#define POWER_DEBUG 0
// FLASH POWER STRATEGY DURING SLEEP
// 0 = cut MEM_POWER (what this firmware did until 2026-08-22)
// 1 = leave the supply on and rely on the 0xB9 deep power-down command
//
// MEASURED on rev02, in series with the AAA cell: 0 gives 190uA of sleep
// current, 1 gives 110uA. Cutting the supply is the expensive option here,
// because a chip with no VCC has to be defended against its own I/O pins --
// every line into it must be held low or current flows in through the
// protection diodes and parasitically powers it. CS in particular then fights
// R26, a 47k pull-up to the permanent 3.3V rail, for 3.3V/47k = 70uA of pure
// loss. Left powered, the chip sits in deep power-down at ~1uA, CS rests high
// with both ends of R26 at the same potential, and nothing needs defending.
// It also stops power-cycling a flash that has no decoupling capacitor.
//
// The board-level fix is the one noted at the top of this file: tie the flash
// VCC straight to 3.3V and drop the GPIO switching entirely.
#define SLEEP_FLASH_POWERED 1

#define SECTOR_SIZE 4096UL
#define MAX_SECTORS 2047UL
#define FLASH_READY_TIMEOUT 1000UL  // ms; worst case is a 400 ms sector erase

// Fallbacks and accepted ranges used by readConfiguration(). Every configuration
// value is range-checked as it is loaded, so a single corrupt or never-initialised
// EEPROM byte cannot reach setWakeUp() as a division by zero or silently shift
// every timestamp. A blank chip reads 0xFF everywhere, which is out of range for
// all four of these.
#define DEFAULT_MEASURE_INTERVAL 600  // seconds, 10 minutes
#define MAX_MEASURE_INTERVAL 86400UL  // one day; 0 would divide by zero

#define DEFAULT_TIMEZONE 0            // UTC: unambiguous, and never wrong by a whole day
#define MIN_TIMEZONE -12              // real UTC offsets run -12..+14 whole hours
#define MAX_TIMEZONE 14

#define DEFAULT_ADJUST_RTC_INTERVAL 30 // days between GPS clock corrections
#define MAX_ADJUST_RTC_INTERVAL 365    // beyond 366 the DOY modulo can never match

#define DEFAULT_LOW_VOLTAGE_MULTIPLIER 1 // 1 = no slow-down
#define MAX_LOW_VOLTAGE_MULTIPLIER 24

#define MAX_MESSAGE_FREQUENCY_DAYS 255 // stored in a byte; 0 disables messages

// displayHistory() modes
#define SHOW_ALL 0         // LOG   every record, padded columns, human reading
#define SHOW_LAST 1        // the record just written, same padded layout
#define SHOW_ALL_COMPACT 2 // LOGC  every record, no sample number, no padding
// LOGH is not a displayHistory() mode: it does not decode records at all, so it
// has its own function. See displayHistoryHex() in EEPROM.ino.


#define SLEEP_DELAY 0
#define WAKEUP_DELAY 0

// Version del firmware y del PROTOCOLO de la consola serie. Son cosas distintas:
// el firmware cambia con cualquier arreglo, mientras que la version de protocolo
// solo sube cuando cambia lo que un cliente automatico ve -- los comandos, sus
// respuestas o el formato de LOGB. La app comprueba la segunda y se niega a hablar
// con un protocolo que no entiende, en vez de malinterpretar la respuesta.
#define FIRMWARE_VERSION "2.1"
#define PROTOCOL_VERSION 1

#define BAUDRATE 230400
// Baudrate error calculator. NOTE the clock argument: this board runs at
// 7.3728 MHz, not the 8 MHz the link used to say. At 8 MHz the table shows
// 230400 with an 8.5% error, i.e. unusable; at 7.3728 MHz the error is zero.
// https://wormfood.net/avrbaudcalc.php?bitrate=38.4k%2C57.6k%2C74.88%2C76.8k%2C115.2k%2C230.4k%2C250k&clock=7.3728&databits=8

//

//--------------------------------------------------------------------
//------------------------- SETUP ------------------------------------
//--------------------------------------------------------------------

bool doAdjustTimeOnStartup=false;
bool startUp = true;
// ---- Awake time accounting -------------------------------------------------
// The awake part of a measurement cycle is split into three phases so it is
// visible where the time (and therefore the battery) is going:
//   pre     wake-up to the start of takeMeasurement()
//   measure inside takeMeasurement() itself
//   post    takeMeasurement() returning to the moment before goToSleep()
// Only cycles that actually take a measurement are timed. The start-up cycle
// runs displayInfo() instead, and any cycle that opens the serial command
// window can stay awake for minutes, so neither is representative.
// Durations are milliseconds. The maxima are cleared by the RC command.
// Per-cycle working state. These deliberately stay in ordinary RAM so that the
// C start-up code zeroes them on every reset. cycleTimed in particular MUST come
// up false: if it were left set, the pre-sleep code would compute a post time
// from a stale measureEndMs belonging to the previous run.
unsigned long cycleStartMs=0;  // millis() at wake-up
unsigned long measureEndMs=0;  // millis() when takeMeasurement() returned
bool cycleTimed=false;         // a measurement was taken this cycle

// The statistics themselves live in the .noinit section.
//
// WHY: the serial command window that displayExtendedInfo() is reached from only
// opens in the startUp branch of loop(), i.e. once per reset. So the only way to
// read these numbers is to reset the logger -- and a reset is exactly what wipes
// ordinary RAM, which would make them read 0/0/0 every single time. Variables
// placed in .noinit are skipped by the start-up code that clears .bss, so they
// survive the reset (including the DTR pulse a serial adapter sends when the
// port is opened) and are still there when the I command runs.
//
// WHAT SURVIVES: any reset that does not interrupt the supply -- the RESET pin,
// the DTR pulse, a watchdog reset, and a brown-out shallow enough to leave the
// RAM cells holding their charge.
// WHAT DOES NOT: removing the cell, or any outage long enough to drain the rail.
// After those the section holds whatever the RAM powered up as, which is why the
// magic word below is needed: there is no other way to tell initialised data
// from power-up garbage. A 1-in-4-billion chance of garbage matching the magic
// is accepted, which is the usual bargain for this technique.
//
// NOTE: a .noinit variable cannot have an initialiser. Giving one a "= 0" is a
// compile error, because there is no start-up code left to perform the
// assignment. They are cleared explicitly in setup() instead.
#define TIMING_MAGIC 0xC0FFEE01UL
unsigned long timingMagic __attribute__((section(".noinit")));
unsigned int lastPreMs    __attribute__((section(".noinit")));
unsigned int maxPreMs     __attribute__((section(".noinit")));
unsigned int lastMeasMs   __attribute__((section(".noinit")));
unsigned int maxMeasMs    __attribute__((section(".noinit")));
unsigned int lastPostMs   __attribute__((section(".noinit")));
unsigned int maxPostMs    __attribute__((section(".noinit")));

// Phase durations are structurally bounded well under a minute, but clamping
// keeps a wildly long cycle from silently wrapping the 16-bit counters.
unsigned int clampMs(unsigned long ms){
  return (ms>65535UL) ? 65535U : (unsigned int)ms;
}
bool setupFailed=false;   // set by msgFail(); lights the red LED at the end of setup()

#define LED_BLINK_MS 50

// Short red blinks, used when something went wrong but the logger carries on
void flashRedLED(byte times){
  for(byte i=0;i<times;i++){
    digitalWrite(LED_PIN, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_PIN, LOW);
    delay(LED_BLINK_MS);
  }
}

void setup() {  

  // Validate the .noinit statistics before anything can read them. On a cold
  // start the magic word is garbage and everything is zeroed; on a reset it
  // matches and the accumulated figures carry through untouched.
  if(timingMagic != TIMING_MAGIC){
    timingMagic = TIMING_MAGIC;
    lastPreMs=0;  maxPreMs=0;
    lastMeasMs=0; maxMeasMs=0;
    lastPostMs=0; maxPostMs=0;
  }

  powerManagementSetup();
  // Green stays on through start-up. Red is added at the end only if a test failed.
  digitalWrite(GREEN_LED, HIGH);

  // Start the console serial port
  Wire.begin();//Starting I2C interface (RTC)
  // Without a timeout, a device holding SDA low blocks endTransmission() forever
  // and the logger hangs in the field with nothing to recover it.
  //
  // The timeout code is already present in the Wire library (utility/twi.c) but
  // MiniCore compiles it out unless WIRE_TIMEOUT is defined for EVERY translation
  // unit, libraries included. build_opt.h is the usual way to do that, but the
  // arduino-cli bundled with IDE 2.3.0 does not implement build_opt.h, so the
  // define has to come from platform.local.txt -- see README_build_opt.txt.
  // The call is guarded so the sketch builds either way; if the guard is false
  // there is NO I2C timeout and the watchdog is the only protection.
  #if defined(WIRE_TIMEOUT)
    Wire.setWireTimeout(25000, true);
  #else
    #warning "WIRE_TIMEOUT not defined: I2C calls can block forever. See README_build_opt.txt"
  #endif
  SPI.begin();//Starting SPI interface for memory

  readConfiguration();
  getCurrentTime();

  setWakeUp();

  Serial.begin(BAUDRATE);
  
  Serial.write("Starting\n");
  printRTCTime();

  memSendControlByte(POWER_UP);
  byte capacity=detectSPImemory();
  if (capacity>0){
    out << capacity << F("MB");
  }else{
    out << F("No");
    setupFailed=true;
  }
  out << F("memory detected\n");
  if(capacity>0){
    flashReportStatus();
  }
  flashPowerDown();

  out << F("RTC") << PRINT;
  if(i2c_DeviceConnected(CLOCK_ADDRESS)){
    msgOK();
  }else{
    msgFail();
  }

  out << F("TEMP+RH") << PRINT;
  if(i2c_DeviceConnected(HDC1080_ADDR)){
    msgOK();
  }else{
    msgFail();
  }  

  out << F("HA TEMP") << PRINT;
  if(i2c_DeviceConnected(TMP119_ADDR)){
    msgOK();
    // The TMP119 boots free-running at ~16uA. Park it in shutdown; every reading
    // is taken as a one-shot which returns it to shutdown by itself.
    tmp119Sleep();
  }else{
    msgFail();
  }

#if LOG_DS18B20
  out << F("DS18B20") << PRINT;
  // Enumerates the bus, binds each slot to a ROM code, and writes the
  // resolution, which lives in a volatile scratchpad and so has to be set again
  // after every power cycle.
  ds18b20Setup();
  if(ds18b20Found==LOG_DS18B20){
    msgOK();
  }else{
    msgFail();   // lights the red LED; the found ones still log normally
  }
  out << ds18b20Found << '/' << LOG_DS18B20 << F("sensors\n");
  ds18b20ListSensors();
#endif

  // Does the log already in flash come from a build with these same channels?
  checkLogFormat();

  // Updating GPS data and adjusting RTC clock
  if (COMPILATION_TIME>currentTime){
    doAdjustTimeOnStartup=true;
  }
  prevDay=dayOfYear(currentDateVec[0],currentDateVec[1],currentDateVec[2]);
  sessionStartTime=currentTime;

  // Any failed start-up test lights the red LED alongside the green one
  if(setupFailed){
    digitalWrite(LED_PIN, HIGH);
  }
}
//##############################################################
//######################## LOOP ################################
//##############################################################
void loop() {

  int battVoltage=getBatteryVoltage();
  if (battVoltage<=CRITICAL_VOLTAGE){ //Default 6.3 V
    out << F("CRITICALLY");
  }
  if (battVoltage<=LOW_VOLTAGE){ //Default 6.76 V
    out << F("LOW VOLTAGE\n");
  }

  char inputStr[64];
  unsigned long serialMonitorTimeout = 0;
      
  if (startUp) {
    digitalWrite(GREEN_LED, HIGH);
    displayInfo();
    //Magnet switch activated
    // Turn on Bluethooth
    serialMonitorTimeout=30000;//Leave controller in serial mode for 30 seconds
    if(doAdjustTimeOnStartup){
      timeUpdateCall();
    }    
    printWaitingCommands(serialMonitorTimeout);
  }else{
    // Phase 1 closes here, phase 2 is takeMeasurement() itself
    lastPreMs=clampMs(millis()-cycleStartMs);
    if(lastPreMs>maxPreMs){
      maxPreMs=lastPreMs;
    }

    unsigned long measureBeginMs=millis();
    bool stored=takeMeasurement();
    lastMeasMs=clampMs(millis()-measureBeginMs);
    if(lastMeasMs>maxMeasMs){
      maxMeasMs=lastMeasMs;
    }
    // Phase 3 starts here, so the LED blink below is counted as post time
    measureEndMs=millis();
    cycleTimed=true;

    // Green on a stored measurement, red flashes if the record did not reach the flash
    if(stored){
      digitalWrite(GREEN_LED, HIGH);
      delay(LED_BLINK_MS);
      digitalWrite(GREEN_LED, LOW);
    }else{
      flashRedLED(3);
    }
  }
  unsigned long serialMonitorStart = millis();
  while (millis()-serialMonitorStart < serialMonitorTimeout){
    if(Serial.available()){   
      // -1 leaves room for the terminator: a full buffer would otherwise make
      // inputStr[inputLength] write one byte past the end of the array.
      int inputLength=Serial.readBytesUntil('\n',inputStr, sizeof(inputStr)-1);
      inputStr[inputLength]='\0';
      // BX       Turn Off bluetooth and quit serial monitor mode
      // CALC     Calculates when available memory would run out
      // LOG      Dump the whole data log, column aligned
      // LOGC     Dump the whole data log, compact (no sample number, no spaces)
      // LOGH     Dump the raw log bytes as Intel HEX, decoding nothing (LOGH=n for n bytes)
      // GPS      Aquire GPS position and time
      // H        Help
      // I        Info
      // OFF      Camera Off
      // ON       Camera On
      // P        Take picture
      // RC       Reset counter
      // S[x]     Keep serial monitor mode for x minutes
      // Q        Quit serial monitor mode
      //***** CAMERA CONTROL *****
      byte varID=isVarCommand(inputStr);
      bool validCommand=true;
      bool hiddenCommand=false;
      if(varID<255){// isVarCommand returns the variable ID and 255 if no variable is recognized
        int oldTimeZone=timeZone;// In case the time zone is changed we remember the old one
        bool doUpdate=inputStr[3]=='=';
        if(doUpdate){ // If there is an equal sign after the command, we update the variable with the new value
          updateVar(varID,readFloat(inputStr));// We write the new value to EEPROM
          readConfiguration();// We update workspace variables fron the values stored in the EEPROM
        }
        displayVars(varID, varID); // We display the current value of the variable
        if (doUpdate && varID==TIMEZONE){
          updateTimeZone(oldTimeZone);
          printRTCTime();
          // true: still inside the serial command window, so the alarm shown
          // here gets the same start-up slack the real arming will get below.
          setWakeUp();
        }            
        if (varID==MEASURE_INTERVAL){
          // Re-show the next wake-up with the interval the user just set
          setWakeUp();
        } 
      }else if(!strcasecmp("M", inputStr)){
        getCurrentTime();
        takeMeasurement();   
      }else if(!strncasecmp("TIME", inputStr, 4)){
        if(inputLength>4){
          if(manualClockAdjust(inputStr)){
            setWakeUp();
          }
        }
        getCurrentTime();
        printRTCTime();
      //***** INFORMATION *****       
      }else if(!strcasecmp("I", inputStr)){
        displayExtendedInfo();
      //***** GPS AND SOLAR ELEVATION MASK*****
      }else if(!strcasecmp("GPS", inputStr)){
        timeUpdateCall();
      //***** IRIDIUM *****
      }else if(!strncasecmp("MSG", inputStr, 3)){
        byte tries=1;
        if(inputLength>3){
          tries=readInt(inputStr);
          messageSent=false;
        }
        for(int i=0;i<tries;i++){
          out << i+1 << NL;
          iridiumSendMessageCall();
          if(messageSent){
            break;
          }
          delay(1000);
        }
        
      //***** BLUETOOTH *****
      //Connecting message 1: +CONNECTING<<BC:7A:BF:0E:7E:67
      //Connecting message 2: CONNECTED
      //Disconnecting message: +DISC:SUCCESS
      }else if(!strncasecmp("NAME", inputStr, 4)){
        // This sets the name of the Bluetooth module
        if(inputStr[4]=='=' && inputLength>5){
          for(int i=0;i<40;i++){
            if(digitalRead(BLUETOOTH_SATUS_PIN)){
              out << F("Please disconnect\n");
              delay(500);   
            }else{
              delay(1000);
              out << F("AT+NAME");
              int pos=5;
              while(pos<(int)sizeof(inputStr) && inputStr[pos]!='\0'){
                out << inputStr[pos];
                pos++;
              }
              ln();
              break;
            }
          }
        }else{
          out << F("Sintax: NAME=...\n");
        }
        
      // }else if(!strncasecmp("STAT", inputStr)){
      //   soundMsg(500,1);
      //           delay(100); 
      //           soundMsg(500,1);
      //   SerialPrint(digitalRead(BLUETOOTH_SATUS_PIN));ln();
      //   for(int i=0;i<20;i++){
      //     if(digitalRead(BLUETOOTH_SATUS_PIN)){
      //           soundMsg(500,1);
      //           delay(100); 
      //           soundMsg(500,1);
      //     }else{
      //           soundMsg(2000,1);
      //           delay(100); 
      //           soundMsg(2000,1);
      //     }
      //     delay(800);
      //   }
        

      //***** SERIAL PORT MONITORING *****
      // }else if(!strncasecmp("S", inputStr)){// S[x] Stay in command mode for x min
      //   byte nMinutes;
      //   if(inputLength==1){
      //     nMinutes=1;
      //   }else{
      //     nMinutes=readInt(inputStr);
      //   }
      //   serialMonitorTimeout=(millis()-serialMonitorStart)+(nMinutes*60000);
      //   SerialPrint(F("Serial mode for ")); SerialPrint(nMinutes); SerialPrint(F(" min"));ln();
      }else if(!strcasecmp("Q", inputStr)){
        serialMonitorTimeout=0;
        out << "Bye\n";
        break;
      //***** MEMORY MANAGEMENT *****   
      }else if(!strcasecmp("RC", inputStr)){
        resetCount();        
      }else if(!strcasecmp("log", inputStr)){// Display battery history data
        displayHistory(SHOW_ALL);
      }else if(!strcasecmp("logc", inputStr)){// Same log, compact: no sample number, no spaces
        displayHistory(SHOW_ALL_COMPACT);
      }else if(!strncasecmp("logb", inputStr, 4)){// Volcado binario; LOGB=a,b para un rango
        // Sin rango se vuelca el log entero. Con "LOGB=a,b" solo esos registros,
        // que es el caso habitual: bajar lo nuevo desde la ultima visita.
        unsigned long a=0, b=0xFFFFFFFFUL;
        char* eq=strchr(inputStr,'=');
        if(eq!=NULL){
          a=strtoul(eq+1,NULL,10);
          char* comma=strchr(eq,',');
          b = (comma!=NULL) ? strtoul(comma+1,NULL,10) : a;
        }
        dumpLogBinary(a,b);
      }else if(!strncasecmp("logh", inputStr, 4)){// Raw log as Intel HEX; LOGH=n for an explicit byte count
        // readULong returns 0 for a bare "logh", which selects the default span
        displayHistoryHex(readULong(inputStr));
      }else if(!strcasecmp("VER", inputStr)){// Version de firmware y de protocolo
        out << F("fw=") << NOSPACER << F(FIRMWARE_VERSION) << NORMALTEXT
            << F("proto=") << NOSPACER << PROTOCOL_VERSION << NORMALTEXT;
        ln();
      }else if(!strcasecmp("INFO", inputStr)){// Cabecera de metadatos legible por maquina
        printMetadata();
      }else if(!strcasecmp("ID", inputStr)){// Identificador unico de la placa
        printBoardId();
        ln();
      }else if(!strcasecmp("H", inputStr)){// Prints help
        printHelp();
      }else if(!strncasecmp("XON", inputStr, 3)){// Prints help  
        if(inputLength!=3){
          byte pin=readInt(inputStr);
          digitalWrite(pin,HIGH);
          out << "Pin" << pin << "HIGH\n";
        }           
      }else if(!strncasecmp("XOFF", inputStr, 4)){// Prints help  
        if(inputLength!=4){
          byte pin=readInt(inputStr);
          digitalWrite(pin,LOW);
          out << "Pin" << pin << "LOW\n";
        }
      }else if(!strcasecmp("V", inputStr)){//
        out << getBatteryVoltage() << "mV (" << getRawBatteryVoltage() << ")\n"; 
      }else if(!strncasecmp("V1", inputStr, 2)){// V1=1.234 stores 1.234 V against the count read now
        setRefVoltage(inputStr, REFERENCE_VOLTAGE_1, REFERENCE_VOLTAGE_COUNT_1, 2, getRawBatteryVoltage());
      }else if(!strncasecmp("V2", inputStr, 2)){// V2=1.234, the second calibration point
        setRefVoltage(inputStr, REFERENCE_VOLTAGE_2, REFERENCE_VOLTAGE_COUNT_2, 2, getRawBatteryVoltage());
#if ANALOG_CHANNELS
      // Expansion header H1. "A0" alone reports the pin, "A01=1.234" and
      // "A02=1.234" store its two calibration points, and the same for A1..A3.
      // isVarCommand() has already had its go at the string, so the three-letter
      // variable commands (ADJ and the rest) can never reach this.
      }else if(inputStr[0]=='A' && inputStr[1]>='0' && inputStr[1]<='3' && inputStr[2]=='\0'){
        byte pin=inputStr[1]-'0';
        if(analogPinEnabled(pin)){
          int count=getRawAnalogPin(pin);
          out << analogPinName(pin) << ':';
          out << '\xB3' << twoPointMv(getInt(analogCalAddr(pin,1,false)), getInt(analogCalAddr(pin,1,true)),
                                       getInt(analogCalAddr(pin,2,false)), getInt(analogCalAddr(pin,2,true)), count)
              << "V (" << count << ")\n";
        }else{
          out << F("Pin not enabled at compile time\n");
        }
      }else if(inputStr[0]=='A' && inputStr[1]>='0' && inputStr[1]<='3' && (inputStr[2]=='1' || inputStr[2]=='2')){
        byte pin=inputStr[1]-'0';
        if(analogPinEnabled(pin)){
          setAnalogRefVoltage(inputStr, pin, inputStr[2]-'0');
        }else{
          out << F("Pin not enabled at compile time\n");
        }
#endif
      // }else if(!strncasecmp("XSIG", inputStr)){
      //   iridiumStart();
      //   byte tries;
      //   if(inputLength==4){
      //     tries=1;
      //   }else{
      //     tries=readInt(inputStr);
      //   }
      //   for(int i=0;i<tries;i++){
      //     SerialPrint(i+1); SerialPrint("> Signal:"); SerialPrint(getIridiumSignal());ln();
      //     getIridiumTime();
      //     delay(1000);
      //   }
      //   iridiumSleep();      
      }else if(!strcasecmp("TUNNEL", inputStr)){// Prints help  
        serialTunnelCall();

      }else if(!strncasecmp("ER", inputStr, 2)){// Prints help  
        //Dismiss errors sent by the HM-10 bluetooth module before establishing a connection
        hiddenCommand=true;
      }else{        
        out << F("Unrecognized command:") << inputStr << NL;
        validCommand=false;
      }
      if(validCommand){
        serialMonitorStart = millis();
        serialMonitorTimeout=120000;//Extend timeout two minutes every time a valid command is received
      }
      if (!hiddenCommand){
        // Some commans corresponds to imputs from the bluetooth module and should not generata a new "waiting for commads" prompt
        // Both are unsigned, so compute the remainder only when it is positive
        unsigned long elapsed=millis()-serialMonitorStart;
        printWaitingCommands(elapsed<serialMonitorTimeout ? serialMonitorTimeout-elapsed : 0);
      }
      
    }
  }

  if(messageSent==false){
    // If there is a pending message we try to send it every time we wake up to take a picture
    iridiumSendMessageCall();
  }
  
  unsigned int DOY=dayOfYear(currentDateVec[0],currentDateVec[1],currentDateVec[2]);
  if (prevDay!=DOY){
    //Code to execute once a day
    out << (char)(ASTERISK_BAR+3) << NL;
    out << (char)(ASTERISK_BAR+1) << F("Daily code") << (char)(ASTERISK_BAR+1) << NL;
    out << (char)(ASTERISK_BAR+3) << NL;
    
    // adjustRTCinterval comes from EEPROM and a zero would divide by zero here
    if (adjustRTCinterval>0 && DOY % adjustRTCinterval == 0){
      //Code to execute every ADJUST_RTC_INTERVAL days
      #if GPS_INSTALLED == 1
        timeUpdateCall();
      #endif
    }
    // If the number of days running since last reset is a multiple of MESSAGE_FREQUENCY_DAYS we set the flag messageSent to false
    // so the message is sent, amd if it fails it will be retried on after each of the folowing picture cycles
    byte messageFreq=EEPROM.read(varAddr[MESSAGE_FREQUENCY_DAYS]);
    if(messageFreq>0 && ((unsigned long)runningDays())%messageFreq==0){
      messageSent=false;
      iridiumSendMessageCall();
    }

    prevDay=DOY;
  }

  //************ preparing to sleep **********************
  startUp = false;
  
  // Seting up the alarm so that the RTC triggers the interrupt pin at the desired wakeup time
  digitalWrite(LED_PIN, LOW);
  digitalWrite(GREEN_LED, LOW);

  SPI.end();
  digitalWrite(11, LOW);
  digitalWrite(13, LOW); 
  // Flash pin levels only; the 0xB9 itself was sent by takeMeasurement() while
  // SPI was still alive. SPI.transfer() with SPE cleared waits forever.
  #if SLEEP_FLASH_POWERED
    digitalWrite(MEM_POWER, HIGH);
    digitalWrite(FLASH_MEMORY_CS, HIGH);
  #else
    digitalWrite(MEM_POWER, LOW);
    digitalWrite(FLASH_MEMORY_CS, LOW);
  #endif

  // The analog comparator is not disabled anywhere else, and the datasheet is
  // explicit that it "will consume power independent of sleep mode" while ACD
  // is clear. Measured below the noise on this board, but it is one bit.
  ACSR |= _BV(ACD);

  setWakeUp();
  out << F("Sleeping...\n");
  out.setVerbose(false);

  // Covers a cycle that took no measurement, such as the start-up one
  tmp119Sleep();

  #if POWER_DEBUG
    dumpPinState();
  #endif

  Serial.end(); //This is needed otherwise current leaks trough the TX pin (RX in the bluetooth module), which causes the led to dimmly light up during sleep and a significat increase in sleep current consumption

  // Phase 3 closes as late as possible, so it includes Serial.end(). Only the
  // few microseconds of goToSleep() itself are left out.
  if(cycleTimed){
    lastPostMs=clampMs(millis()-measureEndMs);
    if(lastPostMs>maxPostMs){
      maxPostMs=lastPostMs;
    }
    cycleTimed=false;
  }

  goToSleep();

  //##################################################
  //##################################################
  // Disable external pin interrupt on wake up pin.
  detachInterrupt(digitalPinToInterrupt(WAKEUP_PIN));

  cycleStartMs=millis();
  //setOutputPins(OUTPUT);
  delay(WAKEUP_DELAY); // Whithout this delay I was getting random restarts
  // Reestarting comms
  //Wire.begin();//Starting I2C interface (RTC)
  SPI.begin();//Starting SPI interface for memory  
  Serial.begin(BAUDRATE);
  getCurrentTime();

  if(currentTime<COMPILATION_TIME){
    // Clock is implausible (RTC lost time): warn with the red LED
    flashRedLED(1);
  }

   

// **** Displaying current info on wake-up ******
  ln();
  out << (char)(ASTERISK_BAR+3) << NL;
  out << (char)(ASTERISK_BAR+3) << NL;
}
