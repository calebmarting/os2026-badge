// The touch settings every badge runs with. They start at the defaults in
// guitar_config.h, and the master's console can change any of them at runtime
// ("set NAME VALUE") - it broadcasts the change to every badge, so tuning
// needs no reflash. Runtime changes last until power-off; copy the values you
// settle on into guitar_config.h to keep them.
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

    // PTC acquisition. Changing any of these recalibrates the neck.
    uint8_t oversample; // oversample_t: 0 = 1x ... 4 = 16x, 5 = 32x, 6 = 64x
    uint8_t resistor;   // series_resistor_t: 0 = 0, 1 = 20K, 2 = 50K, 3 = 100K
    uint8_t freqMode;   // freq_mode_t: 0 = none, anything else = spread spectrum
    // PTC sample delay (0..15) used when freqMode is not 0. Each badge adds
    // hopPerBadge x its ID, so neighbours can sample on different cadences.
    uint8_t freqHop;
    uint8_t hopPerBadge;

    // Master only: the silent window per token in us, 0 = derived.
    uint16_t scanUs;
    // Master only: idle seconds before attract mode, 0 = never.
    uint16_t attractSeconds;
    // 1 = while one pad is measured, the badge's other pads stay driven LOW.
    uint8_t isolate;
    // Master only: extra strum-pad reads, one after every this many fret
    // badges. 0 = only the master's own slot.
    uint8_t strumEvery;
};

extern Params params;

// The settings as a numbered table, which is also their index on the wire.
struct ParamInfo
{
    const char *name;
    int16_t min;
    int16_t max;
    // Changing it means the baselines no longer apply.
    bool needsRecal;
};

uint8_t paramCount();
const ParamInfo &paramInfo(uint8_t index);
// -1 if there is no such name.
int8_t paramFind(const char *name);
int16_t paramGet(uint8_t index);
// False, and nothing changes, if the value is outside the param's range.
bool paramSet(uint8_t index, int16_t value);

// Pushes the acquisition settings into the PTC configuration for this badge.
// Takes effect from the next measurement.
void paramsApplyAcquisition(uint8_t badgeId);
