#include "touch.h"
#include "badge_hw.h"
#include "guitar_config.h"
#include "params.h"

struct PadState
{
    // 8 fractional bits, so the slow drift IIR keeps moving on sub-count
    // differences instead of stalling on integer rounding.
    int32_t baseQ8;
    uint16_t raw;
    int32_t delta;
    bool on;
    // Consecutive scans that wanted the opposite of `on`.
    uint8_t agree;
    // Consecutive scans spent unexplained-negative.
    uint16_t negScans;
    uint32_t onSinceMs;
};

static PadState pads[NUM_TOUCH_PADS];
static bool calibrated = false;
static uint8_t onMask = 0;
static uint8_t negMask = 0;
static uint16_t recalCount = 0;
static uint32_t lastScanUs = 0;
static uint32_t lastCalUs = 0;

static int32_t baseline(const PadState &p)
{
    return (p.baseQ8 + 128) >> 8;
}

static void rebaseline(PadState &p)
{
    p.baseQ8 = (int32_t)p.raw << 8;
    p.on = false;
    p.agree = 0;
    p.negScans = 0;
    recalCount++;
}

uint32_t touchCalibrate()
{
    uint32_t startUs = micros();

    padsCalibrateCompCap();

    uint16_t raw[NUM_TOUCH_PADS];
    int32_t sum[NUM_TOUCH_PADS] = {0};
    uint8_t samples = params.calSamples > 0 ? params.calSamples : 1;
    for (uint8_t s = 0; s < samples; s++)
    {
        padsScan(raw);
        for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
        {
            sum[i] += raw[i];
        }
    }

    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        PadState &p = pads[i];
        p.raw = sum[i] / samples;
        p.baseQ8 = (sum[i] << 8) / samples;
        p.delta = 0;
        p.on = false;
        p.agree = 0;
        p.negScans = 0;
    }
    onMask = 0;
    negMask = 0;
    calibrated = true;

    lastCalUs = micros() - startUs;
    return lastCalUs;
}

bool touchCalibrated()
{
    return calibrated;
}

// Debounce: a pad changes state only after debounceScans scans in a row agree
// that it should. One noisy scan cannot press or release a fret.
static void settle(PadState &p, bool want, uint32_t now)
{
    if (want == p.on)
    {
        p.agree = 0;
        return;
    }
    if (++p.agree >= params.debounceScans)
    {
        p.on = want;
        p.agree = 0;
        p.onSinceMs = now;
    }
}

uint32_t touchScan(bool globalIdle)
{
    if (!calibrated)
    {
        return 0;
    }

    uint16_t raw[NUM_TOUCH_PADS];
    uint32_t scanUs = padsScan(raw);
    uint32_t now = millis();

    bool pos[NUM_TOUCH_PADS];
    bool neg[NUM_TOUCH_PADS];
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        PadState &p = pads[i];
        p.raw = raw[i];
        p.delta = (int32_t)raw[i] - baseline(p);
        // Hysteresis: once on, a pad stays on down to the lower threshold -
        // and the same margin applies on the negative side, so a firm finger
        // hovering around -negThreshold cannot chatter either.
        pos[i] = p.delta > (p.on ? params.offThreshold : params.onThreshold);
        int32_t negLine = params.negThreshold;
        if (p.on)
        {
            negLine -= params.onThreshold - params.offThreshold;
            if (negLine < 1)
            {
                negLine = 1;
            }
        }
        neg[i] = p.delta < -negLine;
    }

    uint8_t newNeg = 0;
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        PadState &p = pads[i];
        if (neg[i])
        {
            newNeg |= (1 << i);
        }

        // A finger bridging two adjacent pads drains the measured one through
        // its neighbour and swings it negative - but only while that neighbour
        // is itself held. A negative with no positive neighbour has no finger
        // behind it, which means the baseline is wrong rather than the touch.
        bool neighbourOn = (i > 0 && pos[i - 1]) || (i + 1 < NUM_TOUCH_PADS && pos[i + 1]);
        bool negTouch = false;
        bool negExplained = false;
        switch (params.negMode)
        {
        case NEG_MODE_ALWAYS:
            negTouch = neg[i];
            negExplained = true;
            break;
        case NEG_MODE_NEIGHBOUR:
            negTouch = neg[i] && neighbourOn;
            negExplained = neighbourOn;
            break;
        default:
            break;
        }

        settle(p, pos[i] || negTouch, now);

        // Only with nothing held anywhere. During play a hand near the pad can
        // pull it negative with no neighbour on - typically while a weak
        // shared-charge touch next door sits just under the on threshold.
        // Re-zeroing it then would leave it reading "held" once the hand
        // lifts, and with a pad held nothing drifts, so it would stay stuck.
        if (neg[i] && !negExplained && globalIdle)
        {
            if (++p.negScans >= params.negRecalScans)
            {
                rebaseline(p);
            }
        }
        else
        {
            p.negScans = 0;
        }

        // A safety net for a baseline captured under a finger: a pad that
        // reads "held" for longer than anyone holds a note gets re-zeroed.
        if (p.on && params.maxOnSeconds > 0 && now - p.onSinceMs >= (uint32_t)params.maxOnSeconds * 1000)
        {
            rebaseline(p);
        }
    }

    uint8_t newOn = 0;
    bool settling = false;
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        if (pads[i].on)
        {
            newOn |= (1 << i);
        }
        settling |= pads[i].agree > 0;
    }

    // Drift only when the whole neck is idle and this badge has nothing on or
    // even half-decided. A hand resting anywhere freezes every baseline, so a
    // held chord can never be tracked away. Pads near a threshold, or
    // negative, are left for the rules above.
    if (globalIdle && newOn == 0 && !settling)
    {
        for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
        {
            PadState &p = pads[i];
            if (!neg[i] && p.delta < params.offThreshold)
            {
                p.baseQ8 += (((int32_t)p.raw << 8) - p.baseQ8) >> params.driftShift;
            }
        }
    }

    onMask = newOn;
    negMask = newNeg;
    lastScanUs = scanUs;
    return scanUs;
}

uint8_t touchMask()
{
    return onMask;
}

uint8_t touchNegMask()
{
    return negMask;
}

uint16_t touchRecalCount()
{
    return recalCount;
}

uint16_t touchRaw(uint8_t pad)
{
    return pads[pad].raw;
}

int32_t touchBaseline(uint8_t pad)
{
    return baseline(pads[pad]);
}

int32_t touchDelta(uint8_t pad)
{
    return pads[pad].delta;
}

static void putU16(uint8_t *out, uint32_t value)
{
    if (value > 0xFFFF)
    {
        value = 0xFFFF;
    }
    out[0] = value & 0xFF;
    out[1] = value >> 8;
}

void touchPackDebug(uint8_t *out)
{
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        int32_t base = baseline(pads[i]);
        putU16(out + 2 * i, pads[i].raw);
        putU16(out + 12 + 2 * i, base < 0 ? 0 : base);
    }
    out[24] = onMask;
    out[25] = negMask;
    putU16(out + 26, recalCount);
    putU16(out + 28, lastScanUs);
    putU16(out + 30, lastCalUs / 1000);
}

static void printMask(uint8_t mask)
{
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        Serial.print((mask & (1 << i)) ? '1' : '.');
    }
}

void touchPrintDebug(uint32_t scanUs)
{
    Serial.print("pad raw/base/delta:");
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        Serial.print(' ');
        Serial.print(pads[i].raw);
        Serial.print('/');
        Serial.print(baseline(pads[i]));
        Serial.print('/');
        Serial.print(pads[i].delta);
    }
    Serial.print("  on ");
    printMask(onMask);
    Serial.print(" neg ");
    printMask(negMask);
    Serial.print("  recal ");
    Serial.print(recalCount);
    Serial.print("  scan ");
    Serial.print(scanUs);
    Serial.println("us");
}
