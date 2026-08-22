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
//   - Idle: prints "LEVEL,<millis>,<peakAbs>,<rms>" five times a second so
//     you can confirm the mic is live (talk/clap near it, watch the number
//     move) without doing a full capture. Both level figures are of the
//     HIGH-PASSED signal -- see the level-meter block below for why raw
//     amplitude is the wrong thing to report.
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

#include <Adafruit_TinyUSB.h> // pulls in the USB-CDC Serial object on this core
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
// 40 (0 dB) is the bench-chosen value (2026-08-22): at arm's length it puts
// speech at p90 ~2500 with ~22 dB of headroom before clipping. An earlier
// session ran at 50 (+5 dB) and was walked back, because this gain is
// DIGITAL, applied after the mic's bitstream is decimated: it scales signal
// and noise by the same factor and cannot improve SNR, so among values that
// don't clip, the lower one buys headroom for free. Note that no value in
// the register's range clips speech at this distance -- reaching full scale
// from p90 ~2500 needs +22 dB and the register stops at +20 -- so there is
// nothing to find by sweeping the gain. Levels at any other gain are just
// these numbers times 10^(dB/20).
//
// Re-run `capture_audio.py <port> calibrate` after any change to this
// value: it emits the matching level thresholds, which are keyed to the
// noise floor and go stale when the gain moves. A change of ROOM does not
// require a re-run -- two runs in the same room with the fan and door
// deliberately changed moved the floor by well under a dB, because it is
// the mic's self-noise being measured, not the room.
const int MIC_GAIN = 40;
// This is a SAFETY CEILING, not the intended take length. The host ends a
// capture with 's' when the operator says they're done, mirroring the cast
// button's press/release -- see tools/capture_audio.py.
//
// It works this way because no fixed window is correct. Measured 2026-08-22:
// "Expecto Patronum" spoken standing still runs 1350-1650 ms, but performed
// with the continuous circle gesture it stretches to 2150-3000+ ms, and since
// the circle accepts any number of revolutions (see CLAUDE.md **Spells**) there
// is no upper bound to design to. Every fixed window tried -- 2000 then 3000 ms
// -- truncated every single Patronum take, while those takes still scored
// 42-51 dB SNR, so nothing downstream flags the failure.
//
// 5000 ms of buffer costs 160 KB of RAM (71%). That is affordable only because
// this is a throwaway bring-up sketch; the real firmware streams or windows
// rather than buffering a whole utterance.
const unsigned long MAX_CAPTURE_MS = 5000;
const size_t CAPTURE_MAX_SAMPLES = (SAMPLE_RATE_HZ * MAX_CAPTURE_MS) / 1000;

int16_t captureBuffer[CAPTURE_MAX_SAMPLES];
volatile size_t captureCount = 0;
volatile bool capturing = false;
volatile bool captureDone = false; // set inside the PDM callback when the buffer fills

// --- level-meter high-pass -------------------------------------------------
// The LEVEL line reports the HIGH-PASSED signal, not raw samples. Raw sample
// amplitude on this mic is dominated by sub-100 Hz rumble -- hand tremor, air
// movement, body motion, the mic's own 1/f noise -- which carries none of the
// speech and none of what an MFCC front-end will look at in Phase 4. Measured
// 2026-08-22 at arm's length: broadband noise floor 375 counts RMS, but only
// 4.9 counts RMS across 300-3400 Hz, and 100% of the noise energy below
// 100 Hz. Reporting raw amplitude made real speech look 0-4 dB above "noise"
// when it is actually 24-38 dB above it, and sent a whole afternoon chasing a
// room-acoustics problem that did not exist.
//
// 2nd-order Butterworth high-pass, 300 Hz at 16 kHz (RBJ cookbook, Q=0.7071),
// coefficients pre-normalised by a0. The float work runs in the PDM interrupt,
// which is fine: ~10 flops per sample at 16 kHz on an M4F with a hardware FPU.
//
// The CAPTURE path is deliberately NOT filtered -- recordings stay raw so the
// Phase 4 training pipeline owns its own preprocessing.
const float HP_B0 = 0.92006616f;
const float HP_B1 = -1.84013232f;
const float HP_B2 = 0.92006616f;
const float HP_A1 = -1.83373266f;
const float HP_A2 = 0.84653197f;
static float hpX1 = 0, hpX2 = 0, hpY1 = 0, hpY2 = 0; // touched only in the ISR

volatile int32_t levelPeak = 0;     // shared with the PDM callback; only touched elsewhere with PDM_IRQn masked
volatile uint64_t levelSumSq = 0;   // running sum of squares, for the window RMS
volatile uint32_t levelCount = 0;
unsigned long lastLevelPrintMs = 0;
unsigned long captureStartMs = 0;

// Runs in interrupt context (called from the PDM peripheral's IRQ handler) --
// no Serial calls here, keep it to memory copies only.
void onPDMdata()
{
  int bytesAvailable = PDM.available();
  static int16_t chunk[256]; // matches the PDM library's DEFAULT_PDM_BUFFER_SIZE (512 bytes)

  while (bytesAvailable > 0)
  {
    int toRead = min((int)sizeof(chunk), bytesAvailable);
    PDM.read(chunk, toRead);
    int samples = toRead / 2;

    for (int i = 0; i < samples; i++)
    {
      int16_t s = chunk[i];

      float f = HP_B0 * s + HP_B1 * hpX1 + HP_B2 * hpX2 - HP_A1 * hpY1 - HP_A2 * hpY2;
      hpX2 = hpX1;
      hpX1 = s;
      hpY2 = hpY1;
      hpY1 = f;

      int32_t fi = (int32_t)f;
      int32_t mag = fi < 0 ? -fi : fi;
      if (mag > levelPeak)
        levelPeak = mag;
      levelSumSq += (uint64_t)((int64_t)fi * fi);
      levelCount++;
      if (capturing && captureCount < CAPTURE_MAX_SAMPLES)
      {
        captureBuffer[captureCount++] = s;
      }
    }

    if (capturing && captureCount >= CAPTURE_MAX_SAMPLES)
    {
      capturing = false;
      captureDone = true;
    }

    bytesAvailable -= toRead;
  }
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
  }

  PDM.onReceive(onPDMdata);
  if (!PDM.begin(CHANNELS, SAMPLE_RATE_HZ))
  {
    Serial.println("# PDM init failed");
    while (1)
    {
    }
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

void loop()
{
  if (Serial.available())
  {
    char c = Serial.read();
    if (c == 'r' && !capturing)
    {
      captureCount = 0;
      capturing = true;
      captureDone = false;
      captureStartMs = millis();
      Serial.println("# RECORDING");
    }
    else if (c == 's' && capturing)
    {
      capturing = false;
      captureDone = true;
    }
  }

  if (captureDone)
  {
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
    // Dump in small chunks, flushing each, rather than one big
    // Serial.write(). A single 96 KB write stalls permanently at ~30 KB on
    // this core's USB-CDC: the host receives 30,208 bytes at 14.7 KB/s and
    // then nothing, forever, on every attempt (measured 2026-08-22). The
    // symptom on the host is a truncated take -- which still sounds fine and
    // still scores 40+ dB SNR, so nothing downstream catches it. Chunking
    // also lets a short write be retried instead of silently dropped:
    // Serial.write() returns how much it accepted, and the original code
    // ignored that return value.
    const uint8_t *dump = (const uint8_t *)captureBuffer;
    size_t total = n * sizeof(int16_t);
    size_t sent = 0;
    unsigned long dumpStart = millis();
    while (sent < total)
    {
      size_t chunk = min((size_t)512, total - sent);
      sent += Serial.write(dump + sent, chunk);
      Serial.flush();
      yield();
      if (millis() - dumpStart > 30000) // host vanished; don't hang forever
        break;
    }
    Serial.println();
    Serial.println("# CAPTURE END");
    return;
  }

  if (!capturing)
  {
    unsigned long now = millis();
    if (now - lastLevelPrintMs >= 200)
    {
      lastLevelPrintMs = now;

      NVIC_DisableIRQ(PDM_IRQn);
      int32_t peak = levelPeak;
      uint64_t sumSq = levelSumSq;
      uint32_t count = levelCount;
      levelPeak = 0;
      levelSumSq = 0;
      levelCount = 0;
      NVIC_EnableIRQ(PDM_IRQn);

      uint32_t levelRms = count ? (uint32_t)sqrtf((float)sumSq / (float)count) : 0;

      // LEVEL,<ms>,<peak>,<rms> -- both of the high-passed signal. The third
      // field is newer than tools/capture_audio.py's original two-field
      // format; that script treats it as optional and warns if it is missing.
      Serial.print("LEVEL,");
      Serial.print(now);
      Serial.print(',');
      Serial.print(peak);
      Serial.print(',');
      Serial.println(levelRms);
    }
  }
  else if (millis() - captureStartMs >= MAX_CAPTURE_MS)
  {
    // Safety net -- the callback should already have stopped us exactly at
    // CAPTURE_MAX_SAMPLES, but stop here too in case fewer samples than
    // expected arrived in the window.
    capturing = false;
    captureDone = true;
  }
}
