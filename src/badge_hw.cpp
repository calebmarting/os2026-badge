#include "badge_hw.h"
#include "guitar_config.h"
#include "scan_protocol.h"

#include "Adafruit_FreeTouch.h"
#include <wiring_private.h>

const uint8_t sliderLedPins[NUM_SLIDER_LEDS] = {1, 0, 3, 2, 4};

// Pad 4 sits on ADC_BATTERY rather than a normal Ax pin. The acquisition
// settings here are placeholders: padsConfigure() replaces them from the
// params before anything is measured.
static Adafruit_FreeTouch qt_1 = Adafruit_FreeTouch(A1, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_2 = Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_3 = Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_4 = Adafruit_FreeTouch(ADC_BATTERY, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_5 = Adafruit_FreeTouch(A4, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_6 = Adafruit_FreeTouch(A5, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch *touchPads[NUM_TOUCH_PADS] = {&qt_1, &qt_2, &qt_3, &qt_4, &qt_5, &qt_6};
// Must match the constructors above.
static const uint8_t kPadPins[NUM_TOUCH_PADS] = {A1, A2, A3, ADC_BATTERY, A4, A5};

static uint16_t touchCompCap[NUM_TOUCH_PADS];

static bool btnRaw = false;
static bool btnStable = false;
static uint32_t btnChangeMs = 0;
static uint32_t btnDownMs = 0;
static bool btnReleaseEvent = false;

#define COMPCAP_MAX 16383

// Hands the pads back to the PTC. FreeTouch muxes a pad to peripheral B in
// begin() and never touches the pin again, so after padsIdle() has turned them
// into plain outputs this is all a measurement needs - the PTC's own Y-line
// configuration is untouched by pinMode(). PIO_ANALOG is peripheral B.
static void padMux(uint8_t pad)
{
    pinPeripheral(kPadPins[pad], PIO_ANALOG);
}

// Drives a pad hard LOW. Between measurements the pads are then just more
// ground for the player's hand, instead of floating PTC inputs that another
// badge's charge pulses can couple through.
static void padIdle(uint8_t pad)
{
    digitalWrite(kPadPins[pad], LOW);
    pinMode(kPadPins[pad], OUTPUT);
}

static void padsIdle()
{
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        padIdle(i);
    }
}

// Isolated: only the pad being measured is connected to the PTC; its five
// neighbours stay driven LOW, so a finger resting on one of them grounds the
// hand instead of sharing charge with the measured pad. Not isolated: all six
// are handed to the PTC for the whole scan, as FreeTouch normally leaves them.
static bool isolatePads = false;

// Brackets the measurements of one pad.
static void padBegin(uint8_t pad)
{
    if (isolatePads)
    {
        padMux(pad);
    }
}

static void padEnd(uint8_t pad)
{
    if (isolatePads)
    {
        padIdle(pad);
    }
}

// Brackets a whole scan.
static void scanBegin()
{
    if (!isolatePads)
    {
        for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
        {
            padMux(i);
        }
    }
}

static int32_t avgMeasure(uint8_t pad, uint8_t samples)
{
    int32_t sum = 0;
    for (uint8_t s = 0; s < samples; s++)
    {
        sum += touchPads[pad]->measure();
    }
    return sum / samples;
}

// Cancels each pad's standing parasitic capacitance so a touch has room to
// register. Without this a pad idles at 400-680 of a ~1016 full scale, one
// finger pegs the ceiling, and a second finger pushes the conversion out of
// range - both readings then collapse and neither crosses the threshold. That
// is what made chords and barres impossible.
//
// Binary search rather than a fixed value because the parasitic load differs
// per pad, and direction-detecting rather than assumed because the SAMD21 PTC
// is undocumented.
void padsCalibrateCompCap()
{
    scanBegin();
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        padBegin(i);
        touchPads[i]->setCompCap(0);
        int32_t atLow = avgMeasure(i, 4);
        touchPads[i]->setCompCap(COMPCAP_MAX);
        bool risesWithCompCap = avgMeasure(i, 4) > atLow;

        uint16_t lo = 0;
        uint16_t hi = COMPCAP_MAX;
        while (lo < hi)
        {
            uint16_t mid = lo + (hi - lo) / 2;
            touchPads[i]->setCompCap(mid);
            if ((avgMeasure(i, 2) > TOUCH_CAL_TARGET) == risesWithCompCap)
            {
                hi = mid;
            }
            else
            {
                lo = mid + 1;
            }
        }

        touchCompCap[i] = lo;
        touchPads[i]->setCompCap(lo);
        padEnd(i);
    }
    padsIdle();
}

uint16_t padCompCap(uint8_t pad)
{
    return touchCompCap[pad];
}

void padsConfigure(uint8_t oversample, uint8_t resistor, uint8_t freqMode, uint8_t hop, bool isolate)
{
    isolatePads = isolate;
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        touchPads[i]->setOversampling((oversample_t)oversample);
        touchPads[i]->setSeriesResistor((series_resistor_t)resistor);
        touchPads[i]->setFreqHopping((freq_mode_t)freqMode, (freq_hop_t)hop);
    }
}

uint32_t padsScan(uint16_t raw[NUM_TOUCH_PADS])
{
    uint32_t startUs = micros();
    scanBegin();
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        padBegin(i);
        raw[i] = touchPads[i]->measure();
        padEnd(i);
    }
    padsIdle();
    return micros() - startUs;
}

void hwBegin()
{
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT);
    pinMode(LED_ACCEL, OUTPUT);
    pinMode(LED_BUTTON, OUTPUT);
    pinMode(LED_MIC, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_ACCEL, LOW);
    digitalWrite(LED_BUTTON, LOW);
    digitalWrite(LED_MIC, LOW);
    digitalWrite(LED_BUILTIN, LOW);

    for (uint8_t i = 0; i < NUM_SLIDER_LEDS; i++)
    {
        pinMode(sliderLedPins[i], OUTPUT);
        digitalWrite(sliderLedPins[i], LOW);
    }

    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        if (!touchPads[i]->begin())
        {
            Serial.print("Failed to begin touch pad ");
            Serial.println(i + 1);
        }
    }
    // No calibration here: on a chained neck that would put every badge's PTC
    // to work at once, which is the very interference the scan token exists to
    // prevent. Each badge calibrates in its own turn instead, see touch.h.
    padsIdle();
}

void buttonUpdate()
{
    bool raw = digitalRead(BUTTON_PIN) == HIGH;
    uint32_t now = millis();

    if (raw != btnRaw)
    {
        btnRaw = raw;
        btnChangeMs = now;
    }

    if (btnRaw != btnStable && now - btnChangeMs >= BUTTON_DEBOUNCE_MS)
    {
        btnStable = btnRaw;
        if (btnStable)
        {
            btnDownMs = now;
        }
        else
        {
            btnReleaseEvent = true;
        }
    }
}

bool buttonIsDown()
{
    return btnStable;
}

uint32_t buttonHeldMs()
{
    return btnStable ? millis() - btnDownMs : 0;
}

bool buttonTakeRelease()
{
    bool event = btnReleaseEvent;
    btnReleaseEvent = false;
    return event;
}

void setSliderLeds(uint8_t mask)
{
    for (uint8_t i = 0; i < NUM_SLIDER_LEDS; i++)
    {
        digitalWrite(sliderLedPins[i], (mask & (1 << i)) ? HIGH : LOW);
    }
}

void setSliderLedsBinary(uint8_t value)
{
    uint8_t mask = 0;
    for (uint8_t i = 0; i < NUM_SLIDER_LEDS; i++)
    {
        if (value & (1 << i))
        {
            mask |= (1 << (NUM_SLIDER_LEDS - 1 - i));
        }
    }
    setSliderLeds(mask);
}

void updatePlayLeds(uint8_t padMask, bool sounding)
{
    setSliderLeds(padMask); // bits 0-4, the slider only has 5 LEDs
    digitalWrite(LED_BUILTIN, (padMask & (1 << 5)) ? HIGH : LOW);
    digitalWrite(LED_BUTTON, sounding ? HIGH : LOW);
}

// Strings 1-4 land on slider LEDs 1, 2, 4, 5. The middle LED stays dark, which
// lines the display up with the gaps left by the disabled pads 2 and 5.
static const uint8_t kUkeStringLeds[NUM_UKE_STRINGS] = {0, 1, 3, 4};

void updateCapoLeds(bool sounding)
{
    setSliderLeds(0x1F);
    digitalWrite(LED_BUILTIN, HIGH); // pad 6 counts as held too
    digitalWrite(LED_BUTTON, sounding ? HIGH : LOW);
}

void updatePromptLeds(uint8_t mode)
{
    bool on = false;
    if (mode == PROMPT_HANDS_OFF)
    {
        on = (millis() / 125) % 2 == 0;
    }
    else if (mode == PROMPT_CALIBRATING)
    {
        on = true;
    }
    setSliderLeds(on ? 0x1F : 0);
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    digitalWrite(LED_BUTTON, LOW);
}

void updateAttractLeds()
{
    static uint32_t lastStepMs = 0;
    uint32_t now = millis();
    if (now - lastStepMs < ATTRACT_STEP_MS)
    {
        return;
    }
    lastStepMs = now;

    uint8_t slider = 0;
    for (uint8_t i = 0; i < NUM_SLIDER_LEDS; i++)
    {
        if (random(100) < ATTRACT_ON_PERCENT)
        {
            slider |= (1 << i);
        }
    }
    setSliderLeds(slider);
    const uint8_t others[] = {LED_BUILTIN, LED_BUTTON, LED_MIC, LED_ACCEL};
    for (uint8_t pin : others)
    {
        digitalWrite(pin, random(100) < ATTRACT_ON_PERCENT ? HIGH : LOW);
    }
}

void endAttractLeds()
{
    digitalWrite(LED_MIC, LOW);
    digitalWrite(LED_ACCEL, LOW);
}

// Drum noise runs from TC4's interrupt, not the main loop: a cymbal flips
// the piezo every 50-150 us, and a loop that also blinks LEDs and polls USB
// only comes round every 25-40 us, which squashed the random timing onto a
// few fixed steps - a harsh, partly tonal buzz instead of hiss. tone() uses
// TC5; the two share one generic clock, which both set to the same 48 MHz.
#define NOISE_TC TC4
#define NOISE_TICKS_PER_US 3 // 48 MHz / 16

static volatile bool noiseOn = false;
static volatile bool noiseHigh = false;
static volatile uint32_t noiseElapsed = 0;   // ticks
static uint32_t noiseTotal = 0;              // ticks
static uint32_t noiseMin = 0;                // ticks, shortest interval
static uint32_t noiseSpan = 1;               // ticks, longest - shortest + 1
static uint32_t noisePulse = 0;              // ticks high per click, 0 = square
static uint32_t noiseDecayTenths = 0;
static uint32_t noiseGap = 0;                // ticks low after the current click
static uint32_t noiseRng = 0x9E3779B9;
static volatile uint32_t *noiseOutSet;
static volatile uint32_t *noiseOutClr;
static uint32_t noiseMask;
static bool noiseTimerReady = false;

static void noiseSync()
{
    while (NOISE_TC->COUNT16.STATUS.bit.SYNCBUSY)
    {
    }
}

static void noiseTimerBegin()
{
    GCLK->CLKCTRL.reg = (uint16_t)(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID(GCM_TC4_TC5));
    while (GCLK->STATUS.bit.SYNCBUSY)
    {
    }
    NOISE_TC->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
    while (NOISE_TC->COUNT16.CTRLA.bit.SWRST)
    {
    }
    // Match-frequency mode: the counter restarts at CC0, so each interrupt
    // sets how long until the next one.
    NOISE_TC->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV16;
    noiseSync();
    NOISE_TC->COUNT16.INTENSET.reg = TC_INTENSET_MC0;
    NVIC_SetPriority(TC4_IRQn, 1);
    NVIC_EnableIRQ(TC4_IRQn);

    PortGroup &port = PORT->Group[g_APinDescription[BUZZER_PIN].ulPort];
    noiseOutSet = &port.OUTSET.reg;
    noiseOutClr = &port.OUTCLR.reg;
    noiseMask = 1UL << g_APinDescription[BUZZER_PIN].ulPin;
    noiseTimerReady = true;
}

static void noiseTimerStop()
{
    NOISE_TC->COUNT16.CTRLA.bit.ENABLE = 0;
    noiseSync();
    *noiseOutClr = noiseMask;
    noiseHigh = false;
    noiseOn = false;
}

// How far through the hit, 0..255. 32-bit only: 64-bit division is a slow
// library call on the M0+, and this runs in the interrupt.
static uint32_t noiseProgress()
{
    return noiseElapsed / (noiseTotal / 256 + 1);
}

// (1 + decayTenths / 10) at the end of the hit, 1 at the start, x256.
static uint32_t noiseFade256()
{
    return 256 + noiseDecayTenths * noiseProgress() / 10;
}

static uint32_t noiseInterval()
{
    noiseRng ^= noiseRng << 13;
    noiseRng ^= noiseRng >> 17;
    noiseRng ^= noiseRng << 5;
    return noiseMin + noiseRng % noiseSpan;
}

extern "C" void TC4_Handler()
{
    NOISE_TC->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;
    if (!noiseOn || noiseElapsed >= noiseTotal)
    {
        noiseTimerStop();
        return;
    }

    uint32_t next;
    if (noisePulse == 0)
    {
        // Square: flip at every random half-period.
        if (noiseHigh)
        {
            *noiseOutClr = noiseMask;
        }
        else
        {
            *noiseOutSet = noiseMask;
        }
        noiseHigh = !noiseHigh;
        // Square drums fade by slowing down: the pitch falls away, which is
        // what a kick or a tom does anyway.
        next = noiseInterval() * noiseFade256() / 256;
    }
    else if (noiseHigh)
    {
        // Clicks: the end of one short pulse, then the rest of its period low.
        *noiseOutClr = noiseMask;
        noiseHigh = false;
        next = noiseGap;
    }
    else
    {
        // Clicks fade by getting narrower, at an unchanged rate: slowing them
        // down instead would drag a cymbal back through the piezo's ~4 kHz
        // resonance on the way out.
        uint32_t period = noiseInterval();
        uint32_t pulse = noisePulse * 256 / noiseFade256();
        if (pulse < 1)
        {
            pulse = 1;
        }
        noiseGap = period > pulse ? period - pulse : 1;
        *noiseOutSet = noiseMask;
        noiseHigh = true;
        next = pulse;
    }

    if (next > 0xFFFF)
    {
        next = 0xFFFF;
    }
    noiseElapsed += next;
    NOISE_TC->COUNT16.CC[0].reg = (uint16_t)(next - 1);
    noiseSync();
}

void noiseStart(uint16_t centerHz, uint16_t spreadHz, uint16_t durationMs, uint8_t pulseUs, uint8_t decayTenths)
{
    noTone(BUZZER_PIN);
    pinMode(BUZZER_PIN, OUTPUT);
    if (!noiseTimerReady)
    {
        noiseTimerBegin();
    }
    NOISE_TC->COUNT16.CTRLA.bit.ENABLE = 0;
    noiseSync();

    uint32_t low = centerHz > spreadHz + 20 ? centerHz - spreadHz : 20;
    uint32_t high = (uint32_t)centerHz + spreadHz;
    // Square mode flips twice a cycle; click mode clicks once. A higher
    // frequency is a shorter interval, so the bounds swap over.
    uint32_t perSecond = 1000000UL * NOISE_TICKS_PER_US / (pulseUs ? 1 : 2);
    noiseMin = perSecond / high;
    noiseSpan = perSecond / low - noiseMin + 1;
    noisePulse = (uint32_t)pulseUs * NOISE_TICKS_PER_US;
    noiseDecayTenths = decayTenths;
    noiseTotal = (uint32_t)durationMs * 1000 * NOISE_TICKS_PER_US;
    noiseElapsed = 0;
    noiseHigh = false;
    noiseRng ^= micros();
    *noiseOutClr = noiseMask;
    noiseOn = true;

    NOISE_TC->COUNT16.COUNT.reg = 0;
    noiseSync();
    NOISE_TC->COUNT16.CC[0].reg = 10; // first edge almost at once
    noiseSync();
    NOISE_TC->COUNT16.CTRLA.bit.ENABLE = 1;
    noiseSync();
}

void noiseStop()
{
    if (noiseOn)
    {
        noInterrupts();
        noiseTimerStop();
        interrupts();
    }
}

void updateUkePlayLeds(uint8_t padMask, bool sounding)
{
    uint8_t strings = groupPads(&kUkulele, padMask);
    uint8_t leds = 0;
    for (uint8_t s = 0; s < NUM_UKE_STRINGS; s++)
    {
        if (strings & (1 << s))
        {
            leds |= (1 << kUkeStringLeds[s]);
        }
    }
    setSliderLeds(leds);
    // Pad 6 stays on LED_BUILTIN as a raw pad, the same as in guitar mode, even
    // though the slider above also shows it as the A string.
    digitalWrite(LED_BUILTIN, (padMask & (1 << 5)) ? HIGH : LOW);
    digitalWrite(LED_BUTTON, sounding ? HIGH : LOW);
}
