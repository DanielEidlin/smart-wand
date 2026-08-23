# Superseded: the 2026-08-22 lumos pilot (5 takes)

Good audio, wrong room. These were recorded with noise sources deliberately
off: measured in-band floor 1.4 counts RMS, against the 8.8 counts of the
2026-08-23 set recorded with the PC fans running. That is a 16 dB gap, and it
would have applied to `lumos` and no other word -- a background-noise
difference perfectly correlated with one class label, which is exactly the
kind of confound that shows up later as unexplained per-class accuracy and
cannot be diagnosed from the model.

Their `inband_snr_db` column is also stale: computed against the old
NOISE_BAND_RMS = 5.0.

Kept rather than deleted because the audio itself is fine. If quiet-room
robustness is ever wanted in the training set, record it as a deliberate
`--tag quiet_room` batch across ALL words, which is strictly better data than
five orphan lumos takes.
