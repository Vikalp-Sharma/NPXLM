/*
 * ============================================================
 *  npxlm.h  –  NeoPixel & PWM LED library
 *  Target  : Seeed XIAO RP2040 (also supports AVR and others)
 *  No external libraries required.
 * ============================================================
 *
 *  WHY PIO IS USED ON RP2040 (and not NOP bit-bang)
 *  ─────────────────────────────────────────────────
 *  The Cortex-M0+ on RP2040 runs code from XIP flash via a
 *  cache.  Cache misses stall the CPU 10–50+ ns unpredictably
 *  — enough to corrupt WS2812B timing and produce the wrong
 *  colours.  The RP2040 PIO co-processor runs a 4-instruction
 *  loop in its own dedicated SRAM with a hardware clock divider,
 *  giving cycle-exact bit timing (±8 ns) independent of the CPU.
 *  This is the same approach used by Adafruit NeoPixel and
 *  FastLED on the RP2040.
 *
 *  Classes
 *  -------
 *  pxl   – Single WS2812B NeoPixel LED
 *  pxlp  – PWM common-anode RGB LED
 *
 *  Quick-start
 *  ───────────
 *  pxl p1;                      // defaults: power=11, data=12
 *  p1.pin(11, 12);              // optional – override pins
 *  p1.setColor("red");          // named colour
 *  p1.write(50, 0, 0);          // r g b  (0–100)
 *  p1.rgb(5, 3);                // rainbow for 3 seconds, then returns
 *  p1.setColor("green");        // works normally after rgb() returns
 *
 *  pxlp p2;                     // defaults: r=17, g=16, b=25
 *  p2.pin(17, 16, 25);          // optional – override pins
 *  p2.setColor("purple");       // named colour
 *  p2.write(100, 0, 0);         // full red
 *  p2.rgb(5, 3);                // rainbow for 3 seconds, then returns
 *  p2.write(0, 100, 0);         // works normally after rgb() returns
 */

#ifndef NPXLM_H
#define NPXLM_H

#include <Arduino.h>

// RP2040 PIO and clock headers (bundled with the arduino-pico
// core by Earle Philhower — no extra installation needed).
#ifdef ARDUINO_ARCH_RP2040
  #include "hardware/pio.h"
  #include "hardware/clocks.h"
#endif

// ─────────────────────────────────────────────────────────────
//  Named colour table — shared by both pxl and pxlp
//
//  Supported names (case-insensitive):
//    red  green  blue  white  yellow  purple  cyan  orange
//
//  The table is a plain C struct array so it can be stored in
//  flash (const / PROGMEM compatible) without heap allocation.
// ─────────────────────────────────────────────────────────────
struct _ColorEntry {
    const char* label;
    uint8_t r, g, b;
};

// Declared here, defined once in npxlm.cpp
extern const _ColorEntry _colorTable[];
extern const uint8_t     _colorTableLen;

// ────────────────────────────────────────────────────────────────
//  pxl  –  WS2812B NeoPixel controller  (single LED)
// ────────────────────────────────────────────────────────────────
class pxl {
public:
    /**
     * Constructor.
     * Defaults: power = 11, data = 12  (XIAO RP2040 onboard NeoPixel)
     * You do not need to call pin() if you are using these defaults.
     */
    pxl();

    /**
     * Override the GPIO pins.
     *   powerPin – driven HIGH to enable the NeoPixel's VCC rail.
     *   dataPin  – WS2812B 1-wire data line.
     */
    void pin(uint8_t powerPin, uint8_t dataPin);

    /**
     * Show a named colour (case-insensitive).
     *   Supported: red  green  blue  white  yellow  purple  cyan  orange
     *   Unknown names: LED turns off.
     */
    void setColor(const char* name);

    /**
     * Write a custom colour.
     *   r, g, b : 0–100 (internally scaled to 0–255).
     *   Values outside this range are clamped automatically.
     */
    void write(uint8_t r, uint8_t g, uint8_t b);

    /**
     * Run a smooth rainbow cycle for a fixed number of seconds,
     * then return.  After rgb() returns you can call setColor()
     * or write() normally — no state is left behind.
     *
     *   speed   : 1 (slowest) – 10 (fastest).  0 returns immediately.
     *   seconds : how many seconds to run the rainbow before returning.
     *             Use a large number (e.g. 3600) for "forever".
     *
     * Example:
     *   p1.rgb(5, 3);         // rainbow for 3 s, then stops
     *   p1.setColor("red");   // works immediately after
     */
    void rgb(uint8_t speed, uint16_t seconds);

private:
    uint8_t  _pwrPin;
    uint8_t  _dataPin;
    bool     _ready;
    uint16_t _hue;

#ifdef ARDUINO_ARCH_RP2040
    PIO  _pio;
    uint _sm;
#endif

    void _ensureInit();
    void _sendColor(uint8_t r, uint8_t g, uint8_t b);

#ifndef ARDUINO_ARCH_RP2040
    void _sendByte(uint8_t b);
#endif
};

// ────────────────────────────────────────────────────────────────
//  pxlp  –  PWM common-anode RGB LED
// ────────────────────────────────────────────────────────────────
class pxlp {
public:
    /**
     * Constructor.
     * Defaults: r = 17, g = 16, b = 25  (XIAO RP2040 onboard RGB LED)
     * You do not need to call pin() if you are using these defaults.
     */
    pxlp();

    /**
     * Override the PWM output pins for each colour channel.
     */
    void pin(uint8_t rPin, uint8_t gPin, uint8_t bPin);

    /**
     * Show a named colour (case-insensitive).
     *   Supported: red  green  blue  white  yellow  purple  cyan  orange
     *   Unknown names: LED turns off.
     * Identical API to pxl::setColor().
     */
    void setColor(const char* name);

    /**
     * Write a custom colour.
     *   r, g, b : 0–100 (scaled to PWM and inverted for common-anode).
     *   Values outside this range are clamped automatically.
     */
    void write(uint8_t r, uint8_t g, uint8_t b);

    /**
     * Run a smooth rainbow cycle for a fixed number of seconds,
     * then return.  After rgb() returns you can call setColor()
     * or write() normally.
     *
     *   speed   : 1 (slowest) – 10 (fastest).  0 returns immediately.
     *   seconds : how many seconds to run the rainbow before returning.
     */
    void rgb(uint8_t speed, uint16_t seconds);

private:
    uint8_t  _rPin, _gPin, _bPin;
    bool     _ready;
    uint16_t _hue;

    void _ensureInit();
    void _writeRaw(uint8_t r, uint8_t g, uint8_t b);
};

#endif  // NPXLM_H
