/*
 * TapMinimal — a literal, byte-for-byte port of ST's own "Single-tap example"
 * from the LSM6DS3(TR-C) datasheet/application note, transcribed directly
 * from a screenshot of the real datasheet page (not a PDF-to-text summary —
 * those proved unreliable earlier in this debugging session and got at least
 * one register value wrong).
 *
 * Why this rewrite: earlier attempts (SparkFun's InterruptHWTapConfig.ino
 * sequence, then +FDS, then +FUNC_EN/+BW_SCAL_ODR) all verified every
 * register write with a readback, confirmed the accelerometer was live and
 * reactive to real motion, and still saw zero tap events, ever. The
 * datasheet screenshot revealed why: TWO registers need a bit 7 that no
 * prior sequence set, and that the Seeed_Arduino_LSM6DS3 library either
 * mislabels or doesn't name at all:
 *
 *   - TAP_CFG1 bit 7: the library calls this "TIMER_EN". The datasheet's own
 *     comment on this exact write says "Enable interrupts and tap detection
 *     on X, Y, Z axis" — it's the master interrupt-enable for the whole
 *     6D/tap/wake-up/free-fall block. Every prior sketch left it at 0.
 *   - TAP_THS_6D bit 7: not named in the library's header at all (only
 *     TAP_THS[4:0] and SIXD_THS[6:5] are documented there). The datasheet's
 *     own example sets it. Every prior sketch left it at 0 too.
 *
 * Datasheet reference values, single-tap only (X/Y/Z, INT1) — CONFIRMED
 * WORKING on real hardware, 2026-08-15:
 *   1. CTRL1_XL    = 60h  // ODR_XL = 416 Hz, FS_XL = +/-2g
 *   2. TAP_CFG1    = 8Eh  // enable interrupts + tap detection on X,Y,Z
 *   3. TAP_THS_6D  = 89h  // TAP_THS[4:0]=01001b -> 562.5 mg threshold
 *   4. INT_DUR2    = 06h  // SHOCK=10b (~38.5ms), QUIET=01b (~9.6ms), DUR=0
 *   5. WAKE_UP_THS = 00h  // single-tap only (SINGLE_DOUBLE_TAP = 0)
 *   6. MD1_CFG     = 40h  // single-tap interrupt routed to INT1
 *
 * This build extends that confirmed baseline to double-tap, per the arming
 * design in CLAUDE.md ("double-tap the wand to arm a cast"): WAKE_UP_THS ->
 * 80h (single+double both enabled — see the bit-7 gotcha in CLAUDE.md, the
 * library's own enum name for this is backwards), MD1_CFG -> 48h (route
 * both single- and double-tap events to INT1, so both are visible here for
 * tuning). Everything else stays byte-identical to the proven single-tap
 * sequence above.
 */

#include "LSM6DS3.h"
#include "Wire.h"

LSM6DS3 imu(I2C_MODE, 0x6A);

volatile uint8_t int1Status = 0;

void int1ISR() {
    int1Status++;
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000);
    Serial.println();
    Serial.println(F("=== TapMinimal — literal ST datasheet single-tap example ==="));

    pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
    digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
    delay(50);

    if (imu.begin() != 0) {
        Serial.println(F("[FATAL] IMU not responding. Check Wire1 / power pin."));
        while (1) delay(1000);
    }
    Serial.println(F("[ok] IMU online"));

    uint8_t err = 0;

    err += imu.writeRegister(LSM6DS3_ACC_GYRO_CTRL1_XL,    0x60);
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_CFG1,    0x8E);
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_TAP_THS_6D,  0x89);
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_INT_DUR2,    0x06);
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_WAKE_UP_THS, 0x80);  // single+double
    err += imu.writeRegister(LSM6DS3_ACC_GYRO_MD1_CFG,     0x48);  // route both to INT1

    if (err) {
        Serial.println(F("[FATAL] tap configuration failed"));
        while (1) delay(1000);
    }
    Serial.println(F("[ok] tap configured (literal datasheet sequence)"));

    // Readback every register against the exact datasheet value.
    {
        struct { const char* name; uint8_t reg; uint8_t wrote; } regs[] = {
            {"CTRL1_XL",     LSM6DS3_ACC_GYRO_CTRL1_XL,     0x60},
            {"TAP_CFG1",     LSM6DS3_ACC_GYRO_TAP_CFG1,     0x8E},
            {"TAP_THS_6D",   LSM6DS3_ACC_GYRO_TAP_THS_6D,   0x89},
            {"INT_DUR2",     LSM6DS3_ACC_GYRO_INT_DUR2,     0x06},
            {"WAKE_UP_THS",  LSM6DS3_ACC_GYRO_WAKE_UP_THS,  0x80},
            {"MD1_CFG",      LSM6DS3_ACC_GYRO_MD1_CFG,      0x48},
        };
        for (auto& r : regs) {
            uint8_t readback = 0;
            imu.readRegister(&readback, r.reg);
            Serial.print(F("[diag] ")); Serial.print(r.name);
            Serial.print(F(" wrote 0x")); Serial.print(r.wrote, HEX);
            Serial.print(F(" read back 0x")); Serial.print(readback, HEX);
            Serial.println(readback == r.wrote ? F("  OK") : F("  MISMATCH"));
        }
    }

    // Liveness check: is the accelerometer actually producing new samples?
    // XLDA (new-data-available) is level-latched, not edge-pulsed — it stays
    // set until the output registers are read, so reading via
    // readFloatAccelX() (which reads and clears it) is required for an
    // accurate count. Also prints the last X reading directly.
    Serial.println(F("[diag] measuring accel sample rate for 1 s..."));
    uint32_t sampleCount = 0;
    float lastX = imu.readFloatAccelX();
    const uint32_t diagStart = millis();
    while (millis() - diagStart < 1000) {
        uint8_t status = 0;
        imu.readRegister(&status, LSM6DS3_ACC_GYRO_STATUS_REG);
        if (status & LSM6DS3_ACC_GYRO_XLDA_DATA_AVAIL) {
            lastX = imu.readFloatAccelX();
            sampleCount++;
        }
    }
    Serial.print(F("[diag] ~")); Serial.print(sampleCount);
    Serial.print(F(" accel samples/sec (expect ~416)"));
    Serial.print(F("  last X reading: ")); Serial.print(lastX, 3); Serial.println(F(" g"));

    pinMode(PIN_LSM6DS3TR_C_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_LSM6DS3TR_C_INT1), int1ISR, RISING);

    Serial.println(F("Tap or double-tap the board."));
}

void loop() {
    // Not latched (LIR=0 — TAP_CFG1's LIR bit is 0 in the datasheet value
    // 0x8E), so INT1 pulses once per tap event (once for a single, twice in
    // quick succession for a double) instead of holding.
    if (int1Status > 0) {
        delay(300);  // Throwaway bring-up sketch — delay() is fine here.
        if (int1Status == 1) Serial.println(F("Single-tap event (via INT1/ISR)"));
        if (int1Status > 1)  Serial.println(F("Double-tap event (via INT1/ISR)"));
        int1Status = 0;
    }

    // Ground-truth check independent of the ISR/INT1 path entirely: poll
    // TAP_SRC directly. If this fires but the ISR above never does, the tap
    // engine is working and the bug is in INT1 routing or attachInterrupt.
    uint8_t src = 0;
    imu.readRegister(&src, LSM6DS3_ACC_GYRO_TAP_SRC);
    if (src & LSM6DS3_ACC_GYRO_TAP_EV_STATUS_DETECTED) {
        Serial.print(F("[diag] TAP_SRC=0x")); Serial.print(src, HEX);
        Serial.print(F("  (polled directly, INT1 pin currently reads "));
        Serial.print(digitalRead(PIN_LSM6DS3TR_C_INT1));
        Serial.println(F(")"));
    }

    // Raw-motion ground truth, independent of the tap engine entirely.
    static uint32_t lastSpikePrint = 0;
    const float ax = imu.readFloatAccelX();
    const float ay = imu.readFloatAccelY();
    const float az = imu.readFloatAccelZ();
    const float magG = sqrtf(ax * ax + ay * ay + az * az);
    if (fabsf(magG - 1.0f) > 0.3f && millis() - lastSpikePrint > 100) {
        Serial.print(F("[diag] accel spike: |a|=")); Serial.print(magG, 2);
        Serial.println(F(" g"));
        lastSpikePrint = millis();
    }

    delay(5);
}
