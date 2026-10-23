#include "params.h"
#include "badge_hw.h"
#include "guitar_config.h"

Params params = {
    TOUCH_ON_THRESHOLD,
    TOUCH_OFF_THRESHOLD,
    TOUCH_NEG_THRESHOLD,
    TOUCH_NEG_MODE,
    TOUCH_NEG_RECAL_SCANS,
    TOUCH_DEBOUNCE_SCANS,
    TOUCH_DRIFT_SHIFT,
    TOUCH_MAX_ON_SECONDS,
    TOUCH_CAL_SAMPLES,
    PTC_OVERSAMPLE,
    PTC_RESISTOR,
    PTC_FREQ_MODE,
    PTC_FREQ_HOP,
    PTC_HOP_PER_BADGE,
    SCAN_US,
    ATTRACT_IDLE_SECONDS,
    PTC_PAD_ISOLATE,
    STRUM_READ_EVERY,
    MUTE_LOCKOUT_MS,
    REALISTIC_STRUM,
    FRET_CHANGE_GRACE_MS,
    BUS_TURNAROUND_US,
    BUS_SDA_HOLD,
    BUS_INACTOUT,
};

// Order is the wire index - append only, never reorder.
static const ParamInfo kParams[] = {
    {"on", 1, 1000, false},
    {"off", 0, 1000, false},
    {"neg", 1, 1000, false},
    {"negmode", 0, 2, false},
    {"negrecal", 1, 30000, false},
    {"debounce", 1, 20, false},
    {"drift", 1, 15, false},
    {"maxon", 0, 3600, false},
    {"calsamples", 1, 255, true},
    {"os", 0, 6, true},
    {"res", 0, 3, true},
    {"freq", 0, 3, true},
    {"hop", 0, 15, true},
    {"hopstep", 0, 15, true},
    {"scanus", 0, 30000, false},
    {"attract", 0, 3600, false},
    {"isolate", 0, 1, true},
    {"strumevery", 0, 11, false},
    {"mutelock", 0, 1000, false},
    {"realistic", 0, 1, false},
    {"fretgrace", 0, 1000, false},
    {"turnus", 0, 5000, false},
    {"sdahold", 0, 3, false},
    {"inactout", 0, 3, false},
};
#define NUM_PARAMS (sizeof(kParams) / sizeof(kParams[0]))

uint8_t paramCount()
{
    return NUM_PARAMS;
}

const ParamInfo &paramInfo(uint8_t index)
{
    return kParams[index];
}

int8_t paramFind(const char *name)
{
    for (uint8_t i = 0; i < NUM_PARAMS; i++)
    {
        if (strcmp(name, kParams[i].name) == 0)
        {
            return i;
        }
    }
    return -1;
}

int16_t paramGet(uint8_t index)
{
    switch (index)
    {
    case 0: return params.onThreshold;
    case 1: return params.offThreshold;
    case 2: return params.negThreshold;
    case 3: return params.negMode;
    case 4: return params.negRecalScans;
    case 5: return params.debounceScans;
    case 6: return params.driftShift;
    case 7: return params.maxOnSeconds;
    case 8: return params.calSamples;
    case 9: return params.oversample;
    case 10: return params.resistor;
    case 11: return params.freqMode;
    case 12: return params.freqHop;
    case 13: return params.hopPerBadge;
    case 14: return params.scanUs;
    case 15: return params.attractSeconds;
    case 16: return params.isolate;
    case 17: return params.strumEvery;
    case 18: return params.muteLockMs;
    case 19: return params.realistic;
    case 20: return params.fretGraceMs;
    case 21: return params.turnaroundUs;
    case 22: return params.sdaHold;
    case 23: return params.inactOut;
    default: return 0;
    }
}

bool paramSet(uint8_t index, int16_t value)
{
    if (index >= NUM_PARAMS || value < kParams[index].min || value > kParams[index].max)
    {
        return false;
    }
    switch (index)
    {
    case 0: params.onThreshold = value; break;
    case 1: params.offThreshold = value; break;
    case 2: params.negThreshold = value; break;
    case 3: params.negMode = value; break;
    case 4: params.negRecalScans = value; break;
    case 5: params.debounceScans = value; break;
    case 6: params.driftShift = value; break;
    case 7: params.maxOnSeconds = value; break;
    case 8: params.calSamples = value; break;
    case 9: params.oversample = value; break;
    case 10: params.resistor = value; break;
    case 11: params.freqMode = value; break;
    case 12: params.freqHop = value; break;
    case 13: params.hopPerBadge = value; break;
    case 14: params.scanUs = value; break;
    case 15: params.attractSeconds = value; break;
    case 16: params.isolate = value; break;
    case 17: params.strumEvery = value; break;
    case 18: params.muteLockMs = value; break;
    case 19: params.realistic = value; break;
    case 20: params.fretGraceMs = value; break;
    case 21: params.turnaroundUs = value; break;
    case 22: params.sdaHold = value; break;
    case 23: params.inactOut = value; break;
    }
    return true;
}

void paramsApplyAcquisition(uint8_t badgeId)
{
    uint8_t hop = (params.freqHop + badgeId * params.hopPerBadge) & 0x0F;
    padsConfigure(params.oversample, params.resistor, params.freqMode, hop, params.isolate != 0);
}
