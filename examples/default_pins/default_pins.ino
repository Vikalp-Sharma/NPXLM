/*
 * default_pins.ino
 * ────────────────
 * Example 2: default pins (no pin() call) + rgb() with time limit
 *
 * No pin() call is needed because the defaults already match
 * the XIAO RP2040 onboard LEDs:
 *   pxl  → power=11, data=12  (onboard NeoPixel)
 *   pxlp → r=17, g=16, b=25  (onboard RGB LED)
 *
 * What this sketch does
 * ─────────────────────
 *  setup():
 *    - Both LEDs show blue immediately
 *
 *  loop():
 *    - NeoPixel: rainbow 3 s → white 1 s → rainbow 5 s → blue 1 s
 *    - RGB LED:  rainbow 3 s → yellow 1 s → rainbow 5 s → purple 1 s
 *    (They run sequentially here because rgb() is blocking;
 *     if you need them to run simultaneously, use a hardware
 *     timer or the RP2040's second core.)
 */

#include "npxlm.h"

pxl  p1;   // WS2812B NeoPixel  (uses default pins: pwr=11, data=12)
pxlp p2;   // PWM common-anode RGB LED (uses default pins: 17, 16, 25)

void setup() {
  // No pin() call needed — the library uses the XIAO defaults.

  p1.setColor("blue");   // NeoPixel: blue at startup
  p2.setColor("blue");   // RGB LED:  blue at startup (same API!)
}

void loop() {
  // ── NeoPixel ──────────────────────────────────────────────
  p1.rgb(3, 3);          // slow rainbow for 3 s
  p1.setColor("white");  // white for 1 s
  delay(1000);
  p1.rgb(7, 5);          // fast rainbow for 5 s
  p1.setColor("blue");   // blue for 1 s
  delay(1000);

  // ── RGB LED ───────────────────────────────────────────────
  p2.rgb(4, 3);           // rainbow for 3 s
  p2.setColor("yellow");  // yellow for 1 s
  delay(1000);
  p2.rgb(8, 5);           // fast rainbow for 5 s
  p2.setColor("purple");  // purple for 1 s
  delay(1000);
}
