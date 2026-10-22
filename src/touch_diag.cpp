#include "touch_diag.h"
#include "badge_hw.h"
#include "guitar_config.h"

#include "Adafruit_FreeTouch.h"

// Must match the constructors in badge_hw.cpp. Pad 4 is on ADC_BATTERY rather
// than a normal Ax pin.
static const int kPadPins[NUM_TOUCH_PADS] = {A1, A2, A3, ADC_BATTERY, A4, A5};

struct DiagConfig
{
    oversample_t oversample;
    series_resistor_t resistor;
    const char *name;
};

// The series resistor is the primary axis - it is the one setting that scales
// how hard the PTC drives the pad, and the current firmware differs from the
// stock basic.ino here (50K vs 100K). The two oversample entries at the end
// exist to show its effect on noise and on scan time, which Stage 3 needs.
//
// FREQ_MODE_SPREAD is deliberately absent. adafruit_ptc_get_config_default()
// never initialises config.hops, which becomes SAMPLEDELAY whenever freqhop is
// not NONE, and neither the constructor nor the private setFreqHopping() lets
// us set it. Sweeping it would produce numbers we could not trust.
static const DiagConfig kConfigs[] = {
    {OVERSAMPLE_4, RESISTOR_0, "OS4  R0"},
    {OVERSAMPLE_4, RESISTOR_20K, "OS4  R20K"},
    {OVERSAMPLE_4, RESISTOR_50K, "OS4  R50K  <- current"},
    {OVERSAMPLE_4, RESISTOR_100K, "OS4  R100K <- basic.ino"},
    {OVERSAMPLE_1, RESISTOR_50K, "OS1  R50K"},
    {OVERSAMPLE_16, RESISTOR_50K, "OS16 R50K"},
};
#define NUM_DIAG_CONFIGS (sizeof(kConfigs) / sizeof(kConfigs[0]))

// Static so it is zero-initialised, which matters given the library leaves
// parts of its config struct untouched.
static Adafruit_FreeTouch pads[NUM_TOUCH_PADS];

static int32_t baseline[NUM_TOUCH_PADS];
static int32_t reading[NUM_TOUCH_PADS];
static int32_t maxDelta[NUM_TOUCH_PADS];
static int32_t minDelta[NUM_TOUCH_PADS];
static uint32_t scanUs = 0;
static uint8_t configIndex = 0;

static int32_t avgMeasure(uint8_t pad, uint8_t samples)
{
    int32_t sum = 0;
    for (uint8_t s = 0; s < samples; s++)
    {
        sum += pads[pad].measure();
    }
    return sum / samples;
}

// Prints what each pad idles at across the full range of one register, so the
// direction and usable span can be read straight off rather than assumed. Both
// registers were private upstream, so nobody has characterised them here.
static void sweepRegister(bool compcap)
{
    Serial.println();
    Serial.print(compcap ? "COMPCAP sweep (default 8192)" : "INTCAP sweep (default 63)");
    Serial.println(" - idle reading per pad");
    Serial.println("   val     p1    p2    p3    p4    p5    p6");

    const uint16_t maxVal = compcap ? 16383 : 63;
    const uint8_t steps = 8;
    for (uint8_t s = 0; s <= steps; s++)
    {
        uint16_t val = (uint32_t)maxVal * s / steps;
        char line[64];
        int n = snprintf(line, sizeof(line), "  %5u", (unsigned)val);
        for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
        {
            if (compcap)
            {
                pads[i].setCompCap(val);
            }
            else
            {
                pads[i].setIntCap((uint8_t)val);
            }
            n += snprintf(line + n, sizeof(line) - n, "  %4ld", (long)avgMeasure(i, 4));
        }
        Serial.println(line);
    }

    // Put the defaults back so the sweep does not leave the pads mistuned.
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        if (compcap)
        {
            pads[i].setCompCap(0x2000);
        }
        else
        {
            pads[i].setIntCap(0x3F);
        }
    }
}

// Binary-searches COMPCAP per pad to park its idle reading near the target.
// Detects which way the register runs rather than assuming, since the PTC is
// undocumented and the direction has never been verified on this hardware.
static void calibrateCompCap()
{
    Serial.println();
    Serial.print("COMPCAP calibration, target ");
    Serial.println(TOUCH_CAL_TARGET);

    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        pads[i].setCompCap(0);
        int32_t atLow = avgMeasure(i, 4);
        pads[i].setCompCap(16383);
        int32_t atHigh = avgMeasure(i, 4);
        bool risesWithCompCap = atHigh > atLow;

        uint16_t lo = 0;
        uint16_t hi = 16383;
        while (lo < hi)
        {
            uint16_t mid = lo + (hi - lo) / 2;
            pads[i].setCompCap(mid);
            if ((avgMeasure(i, 2) > TOUCH_CAL_TARGET) == risesWithCompCap)
            {
                hi = mid;
            }
            else
            {
                lo = mid + 1;
            }
        }
        pads[i].setCompCap(lo);

        char line[72];
        snprintf(line, sizeof(line), "  pad %u  compcap %5u  idle %4ld  (span %ld..%ld)",
                 (unsigned)(i + 1), (unsigned)lo, (long)avgMeasure(i, 8),
                 (long)atLow, (long)atHigh);
        Serial.println(line);
    }
}

static void scan()
{
    uint32_t start = micros();
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        reading[i] = pads[i].measure();
    }
    scanUs = micros() - start;
}

static void resetStats()
{
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        maxDelta[i] = 0;
        minDelta[i] = 0;
    }
}

static void rebaseline()
{
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        int32_t sum = 0;
        for (uint8_t s = 0; s < TOUCH_CAL_SAMPLES; s++)
        {
            sum += pads[i].measure();
        }
        baseline[i] = sum / TOUCH_CAL_SAMPLES;
    }
    resetStats();
}

static void applyConfig(uint8_t index)
{
    configIndex = index;
    const DiagConfig &cfg = kConfigs[index];

    // Reconstructing resets COMPCAP to its 8192 default, so stepping configs
    // always starts from an uncalibrated pad.
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        pads[i] = Adafruit_FreeTouch(kPadPins[i], cfg.oversample, cfg.resistor, FREQ_MODE_NONE);
        if (!pads[i].begin())
        {
            Serial.print("Failed to begin pad ");
            Serial.println(i + 1);
        }
    }
    delay(50);
    rebaseline();

    setSliderLedsBinary(index);

    Serial.println();
    Serial.print("=== config ");
    Serial.print(index + 1);
    Serial.print('/');
    Serial.print((int)NUM_DIAG_CONFIGS);
    Serial.print(": ");
    Serial.print(cfg.name);
    Serial.println(" ===");
}

static void printTable()
{
    Serial.print(kConfigs[configIndex].name);
    Serial.print("  scan=");
    Serial.print(scanUs);
    Serial.print("us  mask=");

    // The raw threshold test only - no hysteresis, debounce or neighbour rule
    // (see touch.cpp) - with any negative counted, so it shows every pad the
    // instrument could possibly read as held.
    uint8_t mask = 0;
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        int32_t delta = reading[i] - baseline[i];
        if (delta > TOUCH_ON_THRESHOLD || delta < -TOUCH_NEG_THRESHOLD)
        {
            mask |= (1 << i);
        }
    }
    for (int8_t i = NUM_TOUCH_PADS - 1; i >= 0; i--)
    {
        Serial.print((mask & (1 << i)) ? '1' : '0');
    }
    Serial.println();

    Serial.println(" pad   raw  base  delta   max   min");
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        char line[48];
        snprintf(line, sizeof(line), "  %2u  %4ld  %4ld  %5ld %5ld %5ld",
                 (unsigned)(i + 1), (long)reading[i], (long)baseline[i],
                 (long)(reading[i] - baseline[i]), (long)maxDelta[i], (long)minDelta[i]);
        Serial.println(line);
    }
}

void touchDiagRun()
{
    Serial.println();
    Serial.println("Touch bench. Short press = next config, long press = re-baseline.");
    Serial.println("Watch the 'min' column: a pad reading below its own baseline is");
    Serial.println("the multi-touch failure, and no threshold can recover it.");

    applyConfig(0);

    uint32_t lastPrintMs = 0;
    while (true)
    {
        buttonUpdate();

        if (buttonHeldMs() >= LONG_PRESS_MS)
        {
            sweepRegister(true);  // COMPCAP
            sweepRegister(false); // INTCAP
            calibrateCompCap();
            rebaseline();
            Serial.println("-- calibrated, press pads now --");
            // Swallow the release so it does not also step the config.
            while (buttonIsDown())
            {
                buttonUpdate();
            }
            buttonTakeRelease();
        }
        else if (buttonTakeRelease())
        {
            applyConfig((configIndex + 1) % NUM_DIAG_CONFIGS);
        }

        scan();

        for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
        {
            int32_t delta = reading[i] - baseline[i];
            if (delta > maxDelta[i])
            {
                maxDelta[i] = delta;
            }
            if (delta < minDelta[i])
            {
                minDelta[i] = delta;
            }
        }

        uint32_t now = millis();
        if (now - lastPrintMs >= 500)
        {
            lastPrintMs = now;
            printTable();
        }
    }
}
