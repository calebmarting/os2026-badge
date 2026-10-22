// The touch-detection settings every badge runs with. They start at the
// defaults in guitar_config.h; keeping them in one struct rather than as bare
// defines is what lets them be tuned without a reflash.
#pragma once

#include <Arduino.h>

struct Params
{
    // A pad turns on above onThreshold counts over its baseline and only turns
    // off again below offThreshold. Hysteresis keeps a touch hovering near
    // the line from chattering.
    int16_t onThreshold;
    int16_t offThreshold;
    // How far below its baseline a pad must read to count as "negative".
    int16_t negThreshold;
    // What a negative reading means - see TOUCH_NEG_MODE in guitar_config.h.
    uint8_t negMode;
    // An unexplained negative held this many scans in a row re-baselines the
    // pad: the baseline was captured too high.
    uint16_t negRecalScans;
    // Consecutive scans that must agree before a pad changes state.
    uint8_t debounceScans;
    // Baseline IIR when everything is idle: base += (raw - base) >> driftShift.
    uint8_t driftShift;
    // A pad held on longer than this is re-baselined, 0 = never.
    uint16_t maxOnSeconds;
    // Samples averaged per pad during calibration.
    uint8_t calSamples;
};

extern Params params;
