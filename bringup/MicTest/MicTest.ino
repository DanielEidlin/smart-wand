// Bring-up sketch: capture short utterances from the onboard PDM mic
// (pulse-density modulation -- the 1-bit-per-sample format this MEMS mic
// outputs; the library below decimates it into normal 16-bit PCM before
// we ever see it) and dump them over serial as raw audio for a host
// script to wrap into WAV files.
//
// Exists to gather incantation samples now, while the board is still on
// the bench -- see CLAUDE.md Roadmap step 1 ("this is nearly free now and
// annoying to redo once the wand is epoxied shut"). Incantation
// *recognition* is Phase 4; this sketch only records raw audio, it
// doesn't listen for anything.
//
// PDM pins and the mic's power-enable pin are onboard and wired
// automatically: the bundled PDM library's global `PDM` object is
// constructed from the board variant's PIN_PDM_DIN/CLK/PWR, and
// PDM.begin() drives the power pin itself -- unlike the IMU, there's no
// manual pinMode/digitalWrite step needed here. See CLAUDE.md
// "Board gotchas" for both pins.
//
// Sample rate is 16 kHz mono to match the KWS (keyword spotting) model
// CLAUDE.md's "Designing for voice" section plans for Phase 4, so these
// recordings double as real training/test data later, not just a bench
// smoke test.
//
// Serial protocol:
//   - Idle: prints "LEVEL,<millis>,<peakAbs>" a few times a second so you
//     can confirm the mic is live (talk/clap near it, watch the number
//     move) without doing a full capture.
//   - Send 'r' to start recording a fixed MAX_CAPTURE_MS window (auto-stops
//     at that length; send 's' to stop earlier -- useful for short words).
//     On stop, dumps:
//       "CAPTURE,<numSamples>,<sampleRate>,<channels>\n"
//       followed by numSamples raw little-endian int16 PCM samples,
//       BINARY not text (unlike ImuTest's CSV -- 16 kHz audio is too much
//       data to print as decimal text and stay anywhere near real time),
//       followed by "\n# CAPTURE END\n".
//   - tools/capture_audio.py drives the 'r'/'s' commands interactively and
//     writes each capture out as a WAV file.

#include <Adafruit_TinyUSB.h>  // pulls in the USB-CDC Serial object on this core
#include <PDM.h>

const uint32_t SAMPLE_RATE_HZ = 16000;
const int CHANNELS = 1;

// Mic gain, in the PDM peripheral's own units: 0x00 = -20 dB, 0x28 (40) =
// 0 dB, 0x50 (80) = +20 dB, in 0.5 dB steps (nrf_pdm.h NRF_PDM_GAIN_*).
// The Arduino PDM library's own DEFAULT_PDM_GAIN is 20 -- i.e. -10 dB, a
// meaningful attenuation. That's fine for a mic held near your face, but
// incantations get spoken with the wand at arm's length and the mic
// pointed away, so the hardware 0 dB point is the floor to start from.
//
// 50 (+5 dB) is the bench-chosen value: at arm's length it put speech at
// p90 ~5700 with ~15 dB of headroom before clipping. Raising it further
// would only eat headroom -- this gain is DIGITAL, applied after the mic's
// bitstream is decimated, so it scales signal and noise by the same factor
// and cannot improve SNR. Among values that don't clip, prefer the lower.
//
// Re-run `capture_audio.py <port> calibrate` after any change to this
// value or to the recording room: it emits the matching level thresholds,
// which are keyed to the noise floor and go stale when the gain moves.
const int MIC_GAIN = 50;
const unsigned long MAX_CAPTURE_MS = 2000;  // covers "Expecto Patronum" (longest incantation) plus margin
const size_t CAPTURE_MAX_SAMPLES = (SAMPLE_RATE_HZ * MAX_CAPTURE_MS) / 1000;

int16_t captureBuffer[CAPTURE_MAX_SAMPLES];
volatile size_t captureCount = 0;
volatile bool capturing = false;
volatile bool captureDone = false;  // set inside the PDM callback when the buffer fills

volatile int16_t levelPeak = 0;  // shared with the PDM callback; only touched elsewhere with PDM_IRQn masked
unsigned long lastLevelPrintMs = 0;
unsigned long captureStartMs = 0;

// Runs in interrupt context (called from the PDM peripheral's IRQ handler) --
// no Serial calls here, keep it to memory copies only.
void onPDMdata() {
  int bytesAvailable = PDM.available();
  static int16_t chunk[256];  // matches the PDM library's DEFAULT_PDM_BUFFER_SIZE (512 bytes)

  while (bytesAvailable > 0) {
    int toRead = min((int)sizeof(chunk), bytesAvailable);
    PDM.read(chunk, toRead);
    int samples = toRead / 2;

    for (int i = 0; i < samples; i++) {
      int16_t s = chunk[i];
      int16_t mag = s < 0 ? -s : s;
      if (mag > levelPeak) levelPeak = mag;
      if (capturing && captureCount < CAPTURE_MAX_SAMPLES) {
        captureBuffer[captureCount++] = s;
      }
    }

    if (capturing && captureCount >= CAPTURE_MAX_SAMPLES) {
      capturing = false;
      captureDone = true;
    }

    bytesAvailable -= toRead;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  PDM.onReceive(onPDMdata);
  if (!PDM.begin(CHANNELS, SAMPLE_RATE_HZ)) {
    Serial.println("# PDM init failed");
    while (1) {}
  }
  // MUST come after begin(): begin() itself calls setGain(DEFAULT_PDM_GAIN)
  // partway through, so setting gain beforehand (as the library's own
  // PDMSerialPlotter example shows) is silently overwritten.
  PDM.setGain(MIC_GAIN);

  Serial.println("# MicTest ready");
  Serial.print("# sampleRate=");
  Serial.print(SAMPLE_RATE_HZ);
  Serial.print("Hz channels=");
  Serial.print(CHANNELS);
  Serial.print(" maxCaptureMs=");
  Serial.print(MAX_CAPTURE_MS);
  Serial.print(" gain=");
  Serial.println(MIC_GAIN);
  Serial.println("# send 'r' to start recording, 's' to stop early");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' && !capturing) {
      captureCount = 0;
      capturing = true;
      captureDone = false;
      captureStartMs = millis();
      Serial.println("# RECORDING");
    } else if (c == 's' && capturing) {
      capturing = false;
      captureDone = true;
    }
  }

  if (captureDone) {
    captureDone = false;

    NVIC_DisableIRQ(PDM_IRQn);
    size_t n = captureCount;
    NVIC_EnableIRQ(PDM_IRQn);

    Serial.print("CAPTURE,");
    Serial.print(n);
    Serial.print(',');
    Serial.print(SAMPLE_RATE_HZ);
    Serial.print(',');
    Serial.println(CHANNELS);
    Serial.write((uint8_t*)captureBuffer, n * sizeof(int16_t));
    Serial.println();
    Serial.println("# CAPTURE END");
    return;
  }

  if (!capturing) {
    unsigned long now = millis();
    if (now - lastLevelPrintMs >= 200) {
      lastLevelPrintMs = now;

      NVIC_DisableIRQ(PDM_IRQn);
      int16_t peak = levelPeak;
      levelPeak = 0;
      NVIC_EnableIRQ(PDM_IRQn);

      Serial.print("LEVEL,");
      Serial.print(now);
      Serial.print(',');
      Serial.println(peak);
    }
  } else if (millis() - captureStartMs >= MAX_CAPTURE_MS) {
    // Safety net -- the callback should already have stopped us exactly at
    // CAPTURE_MAX_SAMPLES, but stop here too in case fewer samples than
    // expected arrived in the window.
    capturing = false;
    captureDone = true;
  }
}
