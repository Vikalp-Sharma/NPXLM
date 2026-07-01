/*
 * ============================================================
 *  npxlm.cpp  –  NeoPixel & PWM LED library implementation
 *  Target  : Seeed XIAO RP2040 (also supports AVR and others)
 * ============================================================
 */

#include "npxlm.h"

// ════════════════════════════════════════════════════════════
//  Shared colour table
//  Defined here once; both pxl and pxlp reference it via the
//  extern declarations in npxlm.h.
// ════════════════════════════════════════════════════════════

const _ColorEntry _colorTable[] = {
    { "red",    255,   0,   0 },
    { "green",    0, 255,   0 },
    { "blue",     0,   0, 255 },
    { "white",  255, 255, 255 },
    { "yellow", 255, 255,   0 },
    { "purple", 128,   0, 128 },
    { "cyan",     0, 255, 255 },
    { "orange", 255, 165,   0 },
};
const uint8_t _colorTableLen = sizeof(_colorTable) / sizeof(_colorTable[0]);


// ════════════════════════════════════════════════════════════
//  Shared helpers
// ════════════════════════════════════════════════════════════

/*
 * hsvToRgb — convert a hue angle (0–359°) to R, G, B (0–255).
 *
 * Saturation and value are both fixed at 100 % so the output
 * is always a pure, fully-saturated, maximum-brightness colour.
 *
 * The 360° circle is split into 6 × 60° sectors.  Within each
 * sector one channel is at full (255), one is off (0), and one
 * ramps linearly between 0 and 255.
 *
 *   0°  red        60°  yellow     120° green
 *  180° cyan       240° blue       300° magenta
 */
static void hsvToRgb(uint16_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
    uint8_t sector  = h / 60;                          // 0-5
    uint8_t frac    = (uint8_t)((h % 60) * 255 / 60);  // 0-255 within sector
    uint8_t falling = 255 - frac;
    uint8_t rising  = frac;

    switch (sector) {
        case 0:  r = 255;    g = rising;  b = 0;       break; // red    → yellow
        case 1:  r = falling; g = 255;   b = 0;       break; // yellow → green
        case 2:  r = 0;      g = 255;    b = rising;  break; // green  → cyan
        case 3:  r = 0;      g = falling; b = 255;    break; // cyan   → blue
        case 4:  r = rising;  g = 0;     b = 255;    break; // blue   → magenta
        default: r = 255;    g = 0;      b = falling; break; // magenta→ red
    }
}

/*
 * strEqI — case-insensitive string comparison.
 * Uses plain ASCII arithmetic; no locale / POSIX overhead.
 */
static bool strEqI(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return false;
        ++a; ++b;
    }
    return (*a == '\0') && (*b == '\0');
}

/*
 * _lookupColor — search _colorTable for `name`.
 * Sets r, g, b and returns true on success.
 * Returns false (and sets r=g=b=0) when name is not found.
 */
static bool _lookupColor(const char* name, uint8_t &r, uint8_t &g, uint8_t &b) {
    for (uint8_t i = 0; i < _colorTableLen; i++) {
        if (strEqI(name, _colorTable[i].label)) {
            r = _colorTable[i].r;
            g = _colorTable[i].g;
            b = _colorTable[i].b;
            return true;
        }
    }
    r = g = b = 0;
    return false;
}

/*
 * _rgbInterval / _rgbStep — speed → timing parameters.
 *
 * These are shared by pxl::rgb() and pxlp::rgb() to keep
 * the feel of the rainbow consistent between the two classes.
 *
 *  speed  interval  step/update    approx cycle time
 *    1      80 ms      1°               ~29 s
 *    5      40 ms      3°                ~5 s
 *   10       8 ms      5°               ~0.6 s
 */
static uint16_t _rgbInterval(uint8_t speed) {
    return (uint16_t)map(speed, 1, 10, 80, 8);
}
static uint8_t _rgbStep(uint8_t speed) {
    return (uint8_t)map(speed, 1, 10, 1, 5);
}


// ════════════════════════════════════════════════════════════
//  RP2040 PIO WS2812B engine
// ════════════════════════════════════════════════════════════
#ifdef ARDUINO_ARCH_RP2040
/*
 * HOW THE PIO PROGRAM WORKS
 * ─────────────────────────
 * The RP2040 PIO co-processor runs a 4-instruction loop in its
 * own SRAM with a hardware clock divider.  Each instruction can
 * simultaneously drive a GPIO ("side-set") with zero extra cycles.
 *
 * PIO assembly (ws2812.pio from the Raspberry Pi Pico SDK):
 *
 *   .program ws2812
 *   .side_set 1
 *   .wrap_target
 *   bitloop:
 *     out  x, 1       side 0 [T3-1]  ; pull 1 bit into X; data LOW
 *     jmp  !x, 3      side 1 [T1-1]  ; if 0, jump to do_zero; data HIGH
 *   do_one:
 *     jmp  bitloop    side 1 [T2-1]  ; bit=1 → stay HIGH, loop
 *   do_zero:
 *     nop             side 0 [T2-1]  ; bit=0 → go LOW, loop
 *   .wrap
 *
 * Timing  (T1=2, T2=5, T3=3; PIO clock = 8 MHz → 125 ns/cycle)
 *   Bit-1  HIGH = (T1+T2)×125 = 7×125 = 875 ns  [spec 650-950] ✓
 *          LOW  =  T3   ×125 = 3×125 = 375 ns  [spec 300-600] ✓
 *   Bit-0  HIGH =  T1   ×125 = 2×125 = 250 ns  [spec 250-550] ✓
 *          LOW  = (T2+T3)×125 = 8×125 = 1000 ns [spec 700-1000] ✓
 *
 * The opcodes below are the pre-assembled output of pioasm
 * (from pico-examples/pio/ws2812/generated/ws2812.pio.h).
 * Embedding them directly means no pioasm tool is needed.
 */
static const uint16_t _ws2812_instrs[] = {
    0x6221,  // out x, 1      side 0 [2]
    0x1123,  // jmp !x, 3     side 1 [1]
    0x1400,  // jmp 0         side 1 [4]
    0xA442,  // nop           side 0 [4]
};
static const pio_program_t _ws2812_program = {
    _ws2812_instrs, 4, -1   // instructions, length, origin (-1 = anywhere)
};

// Shared across all pxl instances on the same RP2040
static bool _pioLoaded  = false;
static uint _pioOffset  = 0;

// Configure one state machine for WS2812B output on `dataPin`.
static void _pioSetup(PIO pio, uint sm, uint offset, uint8_t dataPin) {
    pio_sm_config cfg = pio_get_default_sm_config();

    // Wrap at the last instruction so the SM loops automatically.
    sm_config_set_wrap(&cfg, offset + 0, offset + 3);

    // 1 side-set bit, non-optional, no pindirs mode.
    sm_config_set_sideset(&cfg, 1, false, false);

    // Route the side-set output to the data GPIO.
    sm_config_set_sideset_pins(&cfg, dataPin);

    // OSR: shift left (MSB first), auto-pull after 24 bits.
    // Auto-pull refills the OSR from the TX FIFO automatically;
    // we only need to write 32-bit words into the FIFO.
    sm_config_set_out_shift(&cfg, false, true, 24);

    // Join TX FIFOs for 8-entry depth (not critical for 1 LED).
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);

    // Set clock divider so 1 PIO cycle = 125 ns (8 MHz PIO clock).
    // 10 PIO cycles per WS2812B bit × 800 kbps = 8 MHz.
    float div = (float)clock_get_hz(clk_sys) / (10.0f * 800000.0f);
    sm_config_set_clkdiv(&cfg, div);

    // Hand the data GPIO to PIO and set it as an output.
    pio_gpio_init(pio, dataPin);
    pio_sm_set_consecutive_pindirs(pio, sm, dataPin, 1, true);

    // Initialise and start the state machine.
    pio_sm_init(pio, sm, offset, &cfg);
    pio_sm_set_enabled(pio, sm, true);
}
#endif  // ARDUINO_ARCH_RP2040


// ════════════════════════════════════════════════════════════
//  pxl  –  WS2812B NeoPixel
// ════════════════════════════════════════════════════════════

pxl::pxl()
    : _pwrPin(11), _dataPin(12), _ready(false), _hue(0) {}

void pxl::pin(uint8_t powerPin, uint8_t dataPin) {
    _pwrPin  = powerPin;
    _dataPin = dataPin;
    _ready   = false;
    _ensureInit();
}

// Configures GPIO (and PIO on RP2040) exactly once.
// Called automatically before every colour transmission.
void pxl::_ensureInit() {
    if (_ready) return;

    // Power pin enables VCC to the XIAO RP2040 onboard NeoPixel.
    // On boards where the LED is always powered this is a no-op.
    pinMode(_pwrPin, OUTPUT);
    digitalWrite(_pwrPin, HIGH);
    delay(1);   // let the WS2812B LDO stabilise

#ifdef ARDUINO_ARCH_RP2040
    // Load the PIO program once; subsequent pxl objects reuse it.
    if (!_pioLoaded) {
        _pioOffset = pio_add_program(pio0, &_ws2812_program);
        _pioLoaded = true;
    }
    _pio = pio0;
    _sm  = pio_claim_unused_sm(_pio, true); // panic=true if none free
    _pioSetup(_pio, _sm, _pioOffset, _dataPin);
#else
    pinMode(_dataPin, OUTPUT);
    digitalWrite(_dataPin, LOW);
#endif

    _ready = true;

    // Send black immediately to clear any power-on random colour.
    _sendColor(0, 0, 0);
}

/*
 * _sendColor — transmit one 24-bit WS2812B frame then latch.
 *
 * WS2812B byte order: Green, Red, Blue (GRB).
 * On RP2040 the PIO state machine handles all timing.
 * On other platforms this falls back to interrupt-disabled bit-bang.
 */
void pxl::_sendColor(uint8_t r, uint8_t g, uint8_t b) {
    _ensureInit();

#ifdef ARDUINO_ARCH_RP2040
    /*
     * Pack GRB into the top 24 bits of a 32-bit word, left-aligned.
     * The OSR shifts left (MSB first) and auto-pulls after 24 bits;
     * bits 7..0 are never shifted out and can be zero.
     *
     *   Bit 31 ──────────────── Bit 8    Bits 7..0
     *   [ G[7..0] ][ R[7..0] ][ B[7..0] ][  0×00  ]
     */
    uint32_t grb = ((uint32_t)g << 24)
                 | ((uint32_t)r << 16)
                 | ((uint32_t)b <<  8);

    // Write to TX FIFO. Blocks only if the FIFO is full
    // (never an issue with a single LED but correct practice).
    pio_sm_put_blocking(_pio, _sm, grb);

    /*
     * Wait for the FIFO to empty (word moved into OSR), then hold
     * the data line LOW for the mandatory reset interval.
     * The PIO side-set drives data LOW when the SM stalls, so the
     * reset begins naturally once transmission ends.
     * We add 100 µs: ~30 µs for remaining bit-clock time + ≥50 µs reset.
     */
    while (!pio_sm_is_tx_fifo_empty(_pio, _sm)) { /* wait */ }
    delayMicroseconds(100);

#else
    noInterrupts();
    _sendByte(g);   // GRB byte order
    _sendByte(r);
    _sendByte(b);
    interrupts();
    delayMicroseconds(60);  // >50 µs reset / latch
#endif
}

// ── _sendByte (non-RP2040) ───────────────────────────────────
#ifndef ARDUINO_ARCH_RP2040
void pxl::_sendByte(uint8_t data) {
#if defined(__AVR__)
    // Direct port-register access (SBI/CBI = 2 cycles each at 16 MHz).
    uint8_t  bitM  = digitalPinToBitMask(_dataPin);
    volatile uint8_t* portR = portOutputRegister(digitalPinToPort(_dataPin));
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) {
            *portR |= bitM;
            __asm__ volatile(
                "nop\nnop\nnop\nnop\nnop\n"
                "nop\nnop\nnop\nnop\nnop\nnop\n"   // 11 NOPs → T1H
            );
            *portR &= ~bitM;
        } else {
            *portR |= bitM;
            __asm__ volatile("nop\nnop\nnop\nnop"); // 4 NOPs → T0H
            *portR &= ~bitM;
            __asm__ volatile("nop\nnop\nnop\nnop\nnop"); // 5 NOPs → T0L
        }
        data <<= 1;
    }
#else
    // Generic fallback — approximate only.
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) {
            digitalWrite(_dataPin, HIGH);
            delayMicroseconds(1);
            digitalWrite(_dataPin, LOW);
        } else {
            digitalWrite(_dataPin, HIGH);
            digitalWrite(_dataPin, LOW);
            delayMicroseconds(1);
        }
        data <<= 1;
    }
#endif
}
#endif  // !ARDUINO_ARCH_RP2040

// ── setColor ─────────────────────────────────────────────────
void pxl::setColor(const char* name) {
    uint8_t r, g, b;
    _lookupColor(name, r, g, b);
    _sendColor(r, g, b);
}

// ── write ────────────────────────────────────────────────────
void pxl::write(uint8_t r, uint8_t g, uint8_t b) {
    r = constrain(r, 0, 100);
    g = constrain(g, 0, 100);
    b = constrain(b, 0, 100);
    _sendColor(
        (uint8_t)((uint16_t)r * 255 / 100),
        (uint8_t)((uint16_t)g * 255 / 100),
        (uint8_t)((uint16_t)b * 255 / 100)
    );
}

// ── rgb ──────────────────────────────────────────────────────
/*
 * Runs the rainbow for exactly `seconds` seconds, then returns.
 *
 * This is BLOCKING — the function does not return until the time
 * is up.  After it returns, setColor() and write() work normally;
 * no cleanup or extra call is needed.
 *
 * The hue position is preserved across calls, so if you call
 * rgb() multiple times the rainbow continues smoothly from where
 * it left off rather than jumping back to the start.
 */
void pxl::rgb(uint8_t speed, uint16_t seconds) {
    if (speed == 0 || seconds == 0) return;
    speed = constrain(speed, 1, 10);

    uint16_t      interval = _rgbInterval(speed);   // ms between LED updates
    uint8_t       step     = _rgbStep(speed);        // hue degrees per update
    unsigned long deadline = millis() + (unsigned long)seconds * 1000UL;
    unsigned long lastMs   = 0;   // force an immediate first update

    while (millis() < deadline) {
        unsigned long now = millis();
        if (now - lastMs >= interval) {
            lastMs = now;
            _hue = (_hue + step) % 360;
            uint8_t r, g, b;
            hsvToRgb(_hue, r, g, b);
            _sendColor(r, g, b);
        }
        // Yield a tiny slice so the watchdog (if enabled) doesn't
        // trigger on very long calls (seconds > ~4 s on some cores).
        // yield() is a no-op when no RTOS is present.
        yield();
    }
}


// ════════════════════════════════════════════════════════════
//  pxlp  –  PWM common-anode RGB LED
//
//  Common-anode polarity
//  ─────────────────────
//  The anode (+) is tied to VCC, so:
//    analogWrite(pin, 0)   → pin LOW  → full current → FULL brightness
//    analogWrite(pin, 255) → pin HIGH → no  current  → OFF
//
//  To convert a brightness B (0-255) to a PWM value:
//    analogWrite(pin, 255 - B)
// ════════════════════════════════════════════════════════════

pxlp::pxlp()
    : _rPin(17), _gPin(16), _bPin(25), _ready(false), _hue(0) {}

void pxlp::pin(uint8_t rPin, uint8_t gPin, uint8_t bPin) {
    _rPin = rPin; _gPin = gPin; _bPin = bPin;
    _ready = false;
    _ensureInit();
}

void pxlp::_ensureInit() {
    if (_ready) return;
    pinMode(_rPin, OUTPUT);
    pinMode(_gPin, OUTPUT);
    pinMode(_bPin, OUTPUT);
    // 255 = HIGH = off for common-anode
    analogWrite(_rPin, 255);
    analogWrite(_gPin, 255);
    analogWrite(_bPin, 255);
    _ready = true;
}

// Internal write: r, g, b already in 0-255 range.
void pxlp::_writeRaw(uint8_t r, uint8_t g, uint8_t b) {
    _ensureInit();
    analogWrite(_rPin, 255 - r);
    analogWrite(_gPin, 255 - g);
    analogWrite(_bPin, 255 - b);
}

// ── setColor ─────────────────────────────────────────────────
void pxlp::setColor(const char* name) {
    uint8_t r, g, b;
    _lookupColor(name, r, g, b);
    _writeRaw(r, g, b);
}

// ── write ────────────────────────────────────────────────────
void pxlp::write(uint8_t r, uint8_t g, uint8_t b) {
    r = constrain(r, 0, 100);
    g = constrain(g, 0, 100);
    b = constrain(b, 0, 100);
    _writeRaw(
        (uint8_t)((uint16_t)r * 255 / 100),
        (uint8_t)((uint16_t)g * 255 / 100),
        (uint8_t)((uint16_t)b * 255 / 100)
    );
}

// ── rgb ──────────────────────────────────────────────────────
/*
 * Same blocking behaviour as pxl::rgb().
 * Runs the rainbow for `seconds` seconds, then returns.
 * setColor() and write() work normally after it returns.
 */
void pxlp::rgb(uint8_t speed, uint16_t seconds) {
    if (speed == 0 || seconds == 0) return;
    speed = constrain(speed, 1, 10);

    uint16_t      interval = _rgbInterval(speed);
    uint8_t       step     = _rgbStep(speed);
    unsigned long deadline = millis() + (unsigned long)seconds * 1000UL;
    unsigned long lastMs   = 0;

    while (millis() < deadline) {
        unsigned long now = millis();
        if (now - lastMs >= interval) {
            lastMs = now;
            _hue = (_hue + step) % 360;
            uint8_t r, g, b;
            hsvToRgb(_hue, r, g, b);
            _writeRaw(r, g, b);
        }
        yield();
    }
}
