HOW TO BUILD WITH THE I2C TIMEOUT (-DWIRE_TIMEOUT)

The Wire library already contains a timeout (libraries/Wire/src/utility/twi.c), but
MiniCore compiles it out unless WIRE_TIMEOUT is defined for EVERY translation unit,
libraries included. Without it, Wire::endTransmission() spins forever if a device
holds SDA low, and the logger hangs in the field with nothing to recover it.
setup() arms it with Wire.setWireTimeout(25000, true) when the define is present.

ARDUINO IDE 2 (tested 2026-10-07 with MiniCore 3.1.3)
------------------------------------------------------
build_opt.h (in this folder) is NOT read by the IDE for this core. Instead, create

  ~/.arduino15/packages/MiniCore/hardware/avr/3.1.3/platform.local.txt

with these two lines -- the ones in MiniCore's platform.txt, with -DWIRE_TIMEOUT added:

  compiler.c.flags=-c -g {compiler.optimization_flags} {compiler.warning_flags} -std=gnu11 -ffunction-sections -fdata-sections -MMD -DWIRE_TIMEOUT
  compiler.cpp.flags=-c -g {compiler.optimization_flags} {compiler.warning_flags} -std=gnu++17 -fpermissive -fno-exceptions -ffunction-sections -fdata-sections -fno-threadsafe-statics -MMD -DWIRE_TIMEOUT

and restart the IDE. Check: the sketch grows by ~736 bytes (TMP119 + A0 build:
27.982 -> 28.718 B). After a MiniCore update, copy the two lines again from the new
version's platform.txt into its own folder.

Why not compiler.c.extra_flags / compiler.cpp.extra_flags? MiniCore implements its
"Compiler LTO" menu (on by default) through exactly those properties in boards.txt,
which take precedence over platform.local.txt. Setting them either does nothing (in
platform.local.txt) or, when forced from the command line, REPLACES -flto: the build
loses LTO and grows by ~2 kB. build.extra_flags is also taken by boards.txt
({build.clkpr}).

The IDE does not show the sketch's "#warning WIRE_TIMEOUT not defined" with
Compiler warnings = None (it compiles with -w). Set it to Default to see it.

ARDUINO-CLI
-----------
  arduino-cli compile --fqbn MiniCore:avr:328:clock=7_3728MHz_external,BOD=2v7,variant=modelP \
    --libraries ~/Documents/sketchbook/libraries \
    --build-property "build.extra_flags={build.clkpr} -DWIRE_TIMEOUT" \
    GlacierTemp_1_cell_v02_claude

(or rely on the same platform.local.txt). Keep {build.clkpr}: it is what that
property holds by default.
