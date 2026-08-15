/*
 * TapTest — verify tap detection on the LSM6DS3TR-C and calibrate it live.
 *
 * Load-bearing for the arming decision: the wand is armed by double-tapping it.
 * If tap detection is unreliable through the finished 20 mm bore, the cast
 * trigger needs revisiting, so run this before building the state machine.
 *
 * Needs no external hardware — USB-C only. Uses the onboard IMU.
 *
 * The Seeed_Arduino_LSM6DS3 library defines every tap register but exposes no
 * tap API, so this configures them by hand. See CLAUDE.md "Board gotchas".
 *
 * "Double-tap" here is decided in software (two single-tap hardware events
 * within doubleTapWindowMs), not by the chip's own DOUBLE_TAP_EV_STATUS bit
 * — bench testing (2026-08-15) found that bit never asserts on this
 * part/config even for two deliberate taps well inside a generous hardware
 * DUR window, while single-tap detection itself is completely reliable. See
 * CLAUDE.md "Board gotchas" for the two register bits that were the actual
 * fix to get single-tap working at all.
 *
 * Thresholds and the double-tap window are adjustable over serial (send '?'
 * for the menu) so they can be retuned through an enclosure without
 * recompiling.
 */

#include "LSM6DS3.h"
#include "Wire.h"

// ---------------------------------------------------------------------------
// Tunables — starting points, expect to retune these over serial.
// ---------------------------------------------------------------------------

// Tap threshold, 5 bits (0-31). 1 LSB = full-scale/32 = 62.5 mg at +/-2 g.
constexpr uint8_t kTapThresholdInit = 8;      // ~500 mg

// Max duration of the over-threshold spike, 2 bits. 1 LSB = 8/ODR.
constexpr uint8_t kTapShockInit = 3;          // ~58 ms at 416 Hz

// Quiet time after a spike before a second tap is accepted, 2 bits. 1 LSB = 4/ODR.
constexpr uint8_t kTapQuietInit = 3;          // ~29 ms at 416 Hz

// Max gap between the two taps of a double-tap, 4 bits. 1 LSB = 32/ODR.
// This still configures the hardware DUR field (harmless to leave set), but
// bench testing found the chip's own DOUBLE_TAP_EV_STATUS bit never actually
// asserts on this part/config even for two taps well inside this window —
// every genuine double-tap shows up as two separate single-tap events. See
// kDoubleTapWindowMsInit below, which is what actually drives double-tap
// classification now.
constexpr uint8_t kTapDurInit = 7;            // ~538 ms at 416 Hz

// Software double-tap window: two single-tap events (any axis) closer
// together than this count as one double-tap, decided here rather than
// trusting the chip's own (non-firing) DOUBLE_TAP_EV_STATUS bit. Adjustable
// over serial with 'w'. Bench-measured genuine double-taps landed at
// 158-185 ms, so 400 ms already comfortably covers a natural cadence.
uint32_t doubleTapWindowMs = 400;

// Debounce floor: any tap event arriving sooner than this after the last
// one is ignored outright — not counted as a single, not eligible for
// pairing. Added after bench data showed a single physical strike's
// cross-axis ringing (9 ms apart, X then Z from one tap) getting accepted
// as a "double" just as readily as a genuine ~160-185 ms double-tap. A real
// double-tap gesture requires physically lifting and re-striking, which
// takes far longer than mechanical ringing settles. Adjustable with 'g'.
uint32_t debounceMs = 80;

// Accelerometer output data rate. ST requires >= 416 Hz for tap detection —
// the impulse is too short to catch at a slower rate. This is the single
// source of truth: it's written straight to CTRL1_XL, and printConfig() reads
// the real Hz back out of it (via OdrHzFromReg) rather than tracking a
// separate number that could drift out of sync.
constexpr uint8_t kOdrXlReg = LSM6DS3_ACC_GYRO_ODR_XL_416Hz;

// Accelerometer full scale. Sets the threshold LSB size below. Same
// single-source-of-truth reasoning as kOdrXlReg — see FsGFromReg.
constexpr uint8_t kFsXlReg = LSM6DS3_ACC_GYRO_FS_XL_2g;

// Human-readable Hz for the ODR register value above. Used only for display
// math in printConfig() — the hardware write uses kOdrXlReg directly.
float OdrHzFromReg(uint8_t reg) {
    switch (reg) {
        case LSM6DS3_ACC_GYRO_ODR_XL_13Hz:   return 13.0f;
        case LSM6DS3_ACC_GYRO_ODR_XL_26Hz:   return 26.0f;
        case LSM6DS3_ACC_GYRO_ODR_XL_52Hz:   return 52.0f;
        case LSM6DS3_ACC_GYRO_ODR_XL_104Hz:  return 104.0f;
        case LSM6DS3_ACC_GYRO_ODR_XL_208Hz:  return 208.0f;
        case LSM6DS3_ACC_GYRO_ODR_XL_416Hz:  return 416.0f;
        case LSM6DS3_ACC_GYRO_ODR_XL_833Hz:  return 833.0f;
        default:                             return 0.0f;
    }
}

// Human-readable g for the full-scale register value above. Display only —
// see OdrHzFromReg.
float FsGFromReg(uint8_t reg) {
    switch (reg) {
        case LSM6DS3_ACC_GYRO_FS_XL_2g:  return 2.0f;
        case LSM6DS3_ACC_GYRO_FS_XL_4g:  return 4.0f;
        case LSM6DS3_ACC_GYRO_FS_XL_8g:  return 8.0f;
        case LSM6DS3_ACC_GYRO_FS_XL_16g: return 16.0f;
        default:                         return 0.0f;
    }
}

// ---------------------------------------------------------------------------

// TAP_CFG1 (0x58) bit fields. All three axes plus LIR (latch the interrupt
// until the source register is read, so polling can't miss a fast event).
//
// Bit 7 is the real fix, found 2026-08-15 from a screenshot of ST's own
// datasheet page (not the library header, which mislabels this bit
// "TIMER_EN"). The datasheet's comment on this exact write is "Enable
// interrupts and tap detection on X, Y, Z axis" — it's the master
// interrupt-enable for the whole 6D/tap/wake-up/free-fall block. Without it,
// TAP_SRC never asserts DETECTED no matter how correct every other tap
// register is — confirmed by hours of bench testing: every other register
// (threshold, shock/quiet/duration, WAKE_UP_THS, MD1_CFG) verified correct
// via readback, a live and motion-reactive accelerometer confirmed
// independently, and still zero tap events until this bit was set.
constexpr uint8_t kTapCfg1InterruptsEnable = 0x80;

constexpr uint8_t kTapAxesAll = LSM6DS3_ACC_GYRO_TAP_X_EN_ENABLED |
                                 LSM6DS3_ACC_GYRO_TAP_Y_EN_ENABLED |
                                 LSM6DS3_ACC_GYRO_TAP_Z_EN_ENABLED |
                                 LSM6DS3_ACC_GYRO_LIR_ENABLED |
                                 kTapCfg1InterruptsEnable;

// TAP_THS_6D (0x59) bit 7 — the second half of the same fix. Not named
// anywhere in the library's header (only TAP_THS[4:0] and SIXD_THS[6:5] are
// documented there), but present and set in ST's own worked example. Same
// symptom if left clear: total silence from the tap engine.
constexpr uint8_t kTapThs6dRequiredBit = 0x80;

// MD1_CFG (0x5E) routing. Bit 3 is named INT1_TAP_ENABLED but routes *double*
// tap per the ST datasheet; bit 6 is single tap. Route both so the test can
// distinguish them.
constexpr uint8_t kMd1RouteBothTaps = LSM6DS3_ACC_GYRO_INT1_TAP_ENABLED |
                                       LSM6DS3_ACC_GYRO_INT1_SINGLE_TAP_ENABLED;

LSM6DS3 imu(I2C_MODE, 0x6A);

uint8_t tapThreshold = kTapThresholdInit;
uint8_t tapShock     = kTapShockInit;
uint8_t tapQuiet     = kTapQuietInit;
uint8_t tapDur       = kTapDurInit;
uint8_t tapAxes      = kTapAxesAll;

uint32_t singleCount = 0;
uint32_t doubleCount = 0;
uint32_t int1Count   = 0;
uint32_t startedAt   = 0;

// Timestamp of the most recent lone single-tap event not yet paired into a
// software double-tap. 0 means "no pending single."
uint32_t lastSingleTapAt = 0;

// Timestamp of the most recent event accepted past the debounce floor
// (single or double). 0 means "none yet."
uint32_t lastEventAt = 0;

// Set from the INT1 handler. Proves the interrupt routing works, which is what
// task 2 depends on — polling TAP_SRC alone wouldn't verify it.
volatile bool int1Fired = false;

void onInt1() {
    int1Fired = true;
    int1Count++;
}

// Write the tap configuration. Returns accumulated error count, 0 = success.
int configureTap() {
    uint8_t err = 0;

    // Accelerometer: bandwidth, full scale, ODR. Tap needs a high ODR.
    const uint8_t ctrl1 = LSM6DS3_ACC_GYRO_BW_XL_400Hz | kFsXlReg | kOdrXlReg;
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_CTRL1_XL, ctrl1);

    // CTRL4_C BW_SCAL_ODR, CTRL10_C FUNC_EN, and CTRL8_XL FDS were all tried
    // here at various points as candidate fixes for "tap never detects
    // anything" and are deliberately NOT set. They're not needed: a build
    // using ST's literal datasheet example (which touches none of these
    // three registers) detected single- and double-taps reliably on real
    // hardware. Keeping this code minimal rather than re-adding disproven
    // "just in case" writes.

    // Which axes detect taps, latch the interrupt, and enable the interrupt
    // generator (kTapCfg1InterruptsEnable above — this is the actual fix).
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1, tapAxes);

    // Threshold lives in bits 4:0; bit 7 (kTapThs6dRequiredBit) is the other
    // half of the fix above.
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_THS_6D,
                             (tapThreshold & 0x1F) | kTapThs6dRequiredBit);

    // INT_DUR2: DUR in bits 7:4, QUIET in 3:2, SHOCK in 1:0.
    const uint8_t dur2 = ((tapDur & 0x0F) << 4) | ((tapQuiet & 0x03) << 2) | (tapShock & 0x03);
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_INT_DUR2, dur2);

    // Bit 7 SET enables double-tap reporting (both single and double reported).
    // The library's enum names are backwards here — SINGLE_DOUBLE_TAP_DOUBLE_TAP
    // is 0x00 and SINGLE_DOUBLE_TAP_SINGLE_TAP is 0x80. Trust the ST datasheet's
    // bit meaning (0 = single-tap only, 1 = both enabled), not the enum name.
    // Confirmed against the SparkFun reference sequence, which writes 0x80.
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_THS,
                             LSM6DS3_ACC_GYRO_SINGLE_DOUBLE_TAP_SINGLE_TAP);

    // Route tap events to the INT1 pin.
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG, kMd1RouteBothTaps);

    return err;
}

void printConfig() {
    const float odrHz   = OdrHzFromReg(kOdrXlReg);
    const float fsG     = FsGFromReg(kFsXlReg);
    const float thsMg   = tapThreshold * (fsG * 1000.0f / 32.0f);
    const float shockMs = tapShock * (8.0f  / odrHz) * 1000.0f;
    const float quietMs = tapQuiet * (4.0f  / odrHz) * 1000.0f;
    const float durMs   = tapDur   * (32.0f / odrHz) * 1000.0f;

    Serial.println();
    Serial.println(F("--- tap config ---------------------------------------"));
    Serial.print(F("  threshold  t = ")); Serial.print(tapThreshold);
    Serial.print(F("\t~")); Serial.print(thsMg, 0); Serial.println(F(" mg"));
    Serial.print(F("  shock      s = ")); Serial.print(tapShock);
    Serial.print(F("\t~")); Serial.print(shockMs, 1); Serial.println(F(" ms  (max spike length)"));
    Serial.print(F("  quiet      q = ")); Serial.print(tapQuiet);
    Serial.print(F("\t~")); Serial.print(quietMs, 1); Serial.println(F(" ms  (settle before 2nd tap)"));
    Serial.print(F("  duration   d = ")); Serial.print(tapDur);
    Serial.print(F("\t~")); Serial.print(durMs, 0); Serial.println(F(" ms  (max gap between taps)"));
    Serial.print(F("  axes       a = 0x")); Serial.print(tapAxes, HEX);
    Serial.print(F("\tX:")); Serial.print(tapAxes & LSM6DS3_ACC_GYRO_TAP_X_EN_ENABLED ? 1 : 0);
    Serial.print(F(" Y:"));  Serial.print(tapAxes & LSM6DS3_ACC_GYRO_TAP_Y_EN_ENABLED ? 1 : 0);
    Serial.print(F(" Z:"));  Serial.println(tapAxes & LSM6DS3_ACC_GYRO_TAP_Z_EN_ENABLED ? 1 : 0);
    Serial.print(F("  dbl window w = ")); Serial.print(doubleTapWindowMs);
    Serial.println(F(" ms  (software double-tap pairing, any axis)"));
    Serial.print(F("  debounce   g = ")); Serial.print(debounceMs);
    Serial.println(F(" ms  (reject events closer together than this)"));
    Serial.println(F("-----------------------------------------------------"));
    Serial.println(F("  t/s/q/d/a/w/g <n>  set value      ?  show config"));
    Serial.println(F("  r                  reset counters c  show counters"));
    Serial.println();
}

void printCounters() {
    const uint32_t mins = (millis() - startedAt) / 60000;
    Serial.print(F("[counters] double=")); Serial.print(doubleCount);
    Serial.print(F("  single="));          Serial.print(singleCount);
    Serial.print(F("  int1="));            Serial.print(int1Count);
    Serial.print(F("  over "));            Serial.print(mins);
    Serial.println(F(" min"));
}

// Handle one serial command line. Format: <letter> [number]
void handleCommand() {
    const char cmd = Serial.read();
    if (cmd == '\r' || cmd == '\n' || cmd == ' ') return;

    if (cmd == '?') { printConfig();   return; }
    if (cmd == 'c') { printCounters(); return; }
    if (cmd == 'r') {
        singleCount = doubleCount = int1Count = 0;
        lastSingleTapAt = 0;
        lastEventAt = 0;
        startedAt = millis();
        Serial.println(F("[counters reset]"));
        return;
    }

    const long value = Serial.parseInt();

    switch (cmd) {
        case 't': tapThreshold = constrain(value, 0, 31); break;
        case 's': tapShock     = constrain(value, 0, 3);  break;
        case 'q': tapQuiet     = constrain(value, 0, 3);  break;
        case 'd': tapDur       = constrain(value, 0, 15); break;
        case 'a': tapAxes      = (value & 0x0E) | LSM6DS3_ACC_GYRO_LIR_ENABLED
                                                 | kTapCfg1InterruptsEnable; break;
        case 'w': doubleTapWindowMs = constrain(value, 50, 2000);
                  lastSingleTapAt = 0;
                  break;
        case 'g': debounceMs = constrain(value, 0, 500);
                  lastEventAt = 0;
                  break;
        default:
            Serial.print(F("[unknown command '")); Serial.print(cmd);
            Serial.println(F("' — send ? for help]"));
            return;
    }

    if (configureTap() != 0) {
        Serial.println(F("[ERROR] reconfigure failed"));
    } else {
        printConfig();
    }
}

void setup() {
    Serial.begin(115200);
    // Don't hang forever if there's no host attached.
    while (!Serial && millis() < 5000);

    Serial.println();
    Serial.println(F("=== TapTest — LSM6DS3TR-C hardware double-tap ==="));

    // The IMU has a power-enable pin that must be HIGH before it responds.
    // begin() does this too, but being explicit costs nothing.
    pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
    digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
    delay(50);

    if (imu.begin() != 0) {
        Serial.println(F("[FATAL] IMU not responding. Check Wire1 / power pin."));
        while (1) delay(1000);
    }
    Serial.println(F("[ok] IMU online"));

    if (configureTap() != 0) {
        Serial.println(F("[FATAL] tap configuration failed"));
        while (1) delay(1000);
    }
    Serial.println(F("[ok] tap configured"));

    // Verify the INT1 routing works — task 2 needs the interrupt, not polling.
    pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), onInt1, RISING);

    startedAt = millis();
    printConfig();
    Serial.println(F("Tap the board. Handle it normally to check false positives."));
}

void loop() {
    // Report INT1 independently of TAP_SRC, so a physically-toggling pin
    // with a broken/mismatched TAP_SRC read is distinguishable from an
    // interrupt generator that never fires at all. Snapshot-and-clear so
    // the TAP_SRC block below still gets an accurate int1= correlation
    // instead of always reading NO because this already reset the flag.
    if (int1Fired) {
        Serial.print(millis());
        Serial.println(F("\t[INT1] rising edge"));
    }
    const bool int1Snapshot = int1Fired;
    int1Fired = false;

    // Reading TAP_SRC also clears the latched interrupt.
    uint8_t src = 0;
    imu.readRegister(&src, LSM6DS3_ACC_GYRO_TAP_SRC);

    // Unconditional raw dump on every INT1 edge, regardless of what the
    // TAP_EV_STATUS_DETECTED bit check below says. This library has already
    // mislabeled two other bits in this exact register block (TAP_CFG1 bit 7,
    // TAP_THS_6D bit 7) — don't trust bit 0x40 here either without seeing the
    // raw byte first. If INT1 fires but this print shows 0x00, or shows bits
    // that don't match what the check below expects, that's the next bug.
    if (int1Snapshot) {
        Serial.print(F("[diag] INT1 fired, raw TAP_SRC=0x")); Serial.println(src, HEX);
    }

    // Gate on the per-type status bits directly, not TAP_EV_STATUS_DETECTED
    // (0x40) — bench data from real taps shows bit 5 (single) reliably set
    // alongside axis/sign, but bit 6 never once set across 11 genuine tap
    // captures. That's the same pattern as the two other mislabeled bits in
    // this register block (TAP_CFG1 bit 7, TAP_THS_6D bit 7): don't trust a
    // library-named "master" bit without confirming it against raw data.
    //
    // Also require int1Snapshot: bench data showed TAP_SRC holding the same
    // stale byte across multiple polls with no new INT1 edge in between
    // (LIR isn't clearing it as promptly as its own comment assumed), which
    // was double-printing/double-counting the same physical tap. Gating on
    // a genuine new interrupt edge, not just "the bits happen to be set
    // right now," fixes that.
    const bool hwDouble = src & LSM6DS3_ACC_GYRO_DOUBLE_TAP_EV_STATUS_DETECTED;
    const bool isSingle = src & LSM6DS3_ACC_GYRO_SINGLE_TAP_EV_STATUS_DETECTED;
    if (int1Snapshot && (hwDouble || isSingle)) {
        const uint32_t now = millis();

        // Debounce floor: reject anything too soon after the last accepted
        // event outright, before it can pollute pairing state at all. This
        // is what actually distinguishes "one tap's ringing crossed two
        // axes" from "two separate deliberate taps" — the software window
        // below has no lower bound and would happily pair either.
        if (lastEventAt != 0 && (now - lastEventAt) < debounceMs) {
            Serial.print(now);
            Serial.print(F("\t[debounced, "));
            Serial.print(now - lastEventAt);
            Serial.println(F(" ms since last accepted event]"));
            if (Serial.available()) handleCommand();
            delay(5);
            return;
        }
        lastEventAt = now;

        // Software double-tap classification: pair this single-tap event
        // with a pending one from within the window, regardless of which
        // axis either landed on (a real double-tap gesture won't reliably
        // hit the same axis twice). hwDouble is kept as a passthrough in
        // case this part/config ever does assert it, but bench testing
        // found it never has, on any axis, even for taps well inside a
        // generous window.
        bool reportDouble = hwDouble;
        if (isSingle) {
            if (lastSingleTapAt != 0 && (now - lastSingleTapAt) <= doubleTapWindowMs) {
                reportDouble = true;
                lastSingleTapAt = 0;
            } else {
                lastSingleTapAt = now;
            }
        }

        if (reportDouble) doubleCount++;
        else               singleCount++;

        Serial.print(now);
        Serial.print(F("\t"));
        Serial.print(reportDouble ? F("DOUBLE") : F("single"));
        if (hwDouble) Serial.print(F(" (hw)"));

        // Which axis dominated, and which direction.
        Serial.print(F("\taxis="));
        if (src & LSM6DS3_ACC_GYRO_X_TAP_DETECTED) Serial.print(F("X"));
        if (src & LSM6DS3_ACC_GYRO_Y_TAP_DETECTED) Serial.print(F("Y"));
        if (src & LSM6DS3_ACC_GYRO_Z_TAP_DETECTED) Serial.print(F("Z"));

        Serial.print(F("\tsign="));
        Serial.print(src & LSM6DS3_ACC_GYRO_TAP_SIGN_NEG_SIGN ? F("-") : F("+"));

        Serial.print(F("\tint1="));
        Serial.print(int1Snapshot ? F("yes") : F("NO"));

        Serial.print(F("\tsrc=0x"));
        Serial.println(src, HEX);
    }

    if (Serial.available()) handleCommand();

    // Events are latched (LIR), so a slow poll can't miss one.
    delay(5);
}
