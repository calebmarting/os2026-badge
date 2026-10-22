// Touch detection: per-pad baseline, thresholds, hysteresis and debounce, on
// top of the raw scans from badge_hw. Every badge runs this locally on its own
// pads; only the resulting on/off bits go over the wire.
#pragma once

#include <Arduino.h>

// What a reading well below the baseline means (Params::negMode).
#define NEG_MODE_IGNORE 0    // never a touch; held long enough, re-baselines
#define NEG_MODE_NEIGHBOUR 1 // a touch only while an adjacent pad reads on
#define NEG_MODE_ALWAYS 2    // always a touch (the old behaviour)

// Full calibration: COMPCAP search, then params.calSamples scans averaged into
// a fresh baseline. Blocking, and it measures throughout, so it must only run
// while this badge holds the token. Returns how long it took in us.
uint32_t touchCalibrate();
bool touchCalibrated();

// One scan plus detection. globalIdle is "nothing anywhere on the neck is
// held", from the master; the baseline only drifts when it is set and this
// badge's own pads are all off. Returns the scan time in us (0 before
// calibration, when nothing is measured at all).
uint32_t touchScan(bool globalIdle);

// Bit 0..5 = pad 1..6 on, after hysteresis and debounce.
uint8_t touchMask();
// Pads currently reading below -negThreshold, whatever that was taken to mean.
uint8_t touchNegMask();
// Pads re-baselined since boot by the negative or max-on rules.
uint16_t touchRecalCount();

uint16_t touchRaw(uint8_t pad);
int32_t touchBaseline(uint8_t pad);
int32_t touchDelta(uint8_t pad);

// One line of raw/baseline/delta per pad plus the masks, for tuning.
void touchPrintDebug(uint32_t scanUs);
