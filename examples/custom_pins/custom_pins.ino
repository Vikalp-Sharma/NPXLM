/*
 * custom_pins.ino
 * ───────────────
 * Example 1: explicit pin configuration + rgb() with a time limit
 *
 * Hardware
 * --------
 *  p1 – WS2812B NeoPixel:         power=11, data=12
 *  p2 – common-anode PWM RGB LED: r=17, g=16, b=25
 *
 * What this sketch does
 * ─────────────────────
 *  setup():
 *    - Configure pins for both LEDs
 *    - Show red on the NeoPixel for ~3 seconds of rainbow,
 *      then switch to green — demonstrating that write() works
 *      normally after rgb() returns
 *    - Show "purple" on the RGB LED for ~2 seconds of rainbow,
 *      then switch to a custom colour
 *
 *  loop():
 *    - NeoPixel: rainbow 4 s → cyan → rainbow 2 s → red → repeat
 *    - RGB LED:  rainbow 3 s → orange → rainbow 2 s → blue → repeat
 *
 * The key point: setColor() and write() work perfectly after
 * rgb() returns because rgb() is BLOCKING — it runs for exactly
 * the requested number of seconds and then stops on its own.
 */

#include "npxlm.h"

pxl  p1;   // WS2812B NeoPixel  (PIO-driven on RP2040)
pxlp p2;   // PWM common-anode RGB LED

void setup() {
  // ── Set pins ──────────────────────────────────────────────
  p1.pin(11, 12);    // power = 11, data = 12
  p2.pin(17, 16, 25); // r = 17, g = 16, b = 25

  // ── NeoPixel: rainbow for 3 s, then green ─────────────────
  p1.rgb(4, 3);          // rainbow at speed 4 for 3 seconds
  p1.setColor("green");  // works immediately — rgb() has returned

  // ── RGB LED: rainbow for 2 s, then a custom colour ─────────
  p2.rgb(6, 2);          // rainbow at speed 6 for 2 seconds
  p2.write(80, 10, 50);  // 80% red, 10% green, 50% blue
}

void loop() {
  // ── NeoPixel cycle ────────────────────────────────────────
  p1.rgb(4, 4);          // rainbow for 4 s
  p1.setColor("cyan");   // show cyan
  delay(500);
  p1.rgb(7, 2);          // rainbow for 2 s
  p1.setColor("red");    // show red
  delay(500);

  // ── RGB LED cycle ─────────────────────────────────────────
  p2.rgb(5, 3);           // rainbow for 3 s
  p2.setColor("orange");  // show orange
  delay(500);
  p2.rgb(8, 2);           // rainbow for 2 s
  p2.setColor("blue");    // show blue
  delay(500);
}
