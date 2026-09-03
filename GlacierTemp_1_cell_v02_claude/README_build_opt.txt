build_opt.h adds compiler flags to EVERY translation unit, including libraries.
It is read automatically by the Arduino IDE 2.x and arduino-cli; you do not
include it from anywhere.

-DWIRE_TIMEOUT
    Enables the timeout code that already exists inside the Wire library
    (libraries/Wire/src/utility/twi.c), but which MiniCore leaves compiled out
    by default. Without it, Wire::endTransmission() spins forever if any device
    holds SDA low, and the logger hangs in the field with nothing to recover it.
    setup() then calls Wire.setWireTimeout(25000, true) to arm it.

If you delete this file the sketch still builds, but the Wire.setWireTimeout()
call in setup() will not compile. Keep them together.
