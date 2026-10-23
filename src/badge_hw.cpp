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
static void padsMux()
{
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        pinPeripheral(kPadPins[i], PIO_ANALOG);
    }
}

// Drives every pad hard LOW. Between measurements the pads are then just more
// ground for the player's hand, instead of floating PTC inputs that another
// badge's charge pulses can couple through.
static void padsIdle()
{
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        digitalWrite(kPadPins[i], LOW);
        pinMode(kPadPins[i], OUTPUT);
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
    padsMux();
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
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
    }
    padsIdle();
}

uint16_t padCompCap(uint8_t pad)
{
    return touchCompCap[pad];
}

void padsConfigure(uint8_t oversample, uint8_t resistor, uint8_t freqMode, uint8_t hop)
{
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
    padsMux();
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        raw[i] = touchPads[i]->measure();
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
