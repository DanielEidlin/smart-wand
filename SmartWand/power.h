// Battery and power management. STUB -- the low-voltage floor is its own
// task (CLAUDE.md Electrical constraints): a latching ~3.0 V cutoff that
// extinguishes Lumos and refuses to relight, calibrated under LED load.
// Until it exists, never leave the wand lit on battery unattended.
#pragma once

inline void powerBegin() {}

// Whether Lumos may light. TODO: the latching floor. Always true for now.
inline bool lumosAllowed() { return true; }
