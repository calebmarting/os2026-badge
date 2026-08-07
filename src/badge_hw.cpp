#include "badge_hw.h"
#include "guitar_config.h"

#include "Adafruit_FreeTouch.h"

const uint8_t sliderLedPins[NUM_SLIDER_LEDS] = {1, 0, 3, 2, 4};

// Pad 4 sits on ADC_BATTERY rather than a normal Ax pin. RESISTOR_50K gives a
// larger swing on touch than the 100K the Bop It game used, which is what makes
// the one-sided TOUCH_THRESHOLD comparison below reliable.
static Adafruit_FreeTouch qt_1 = Adafruit_FreeTouch(A1, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_2 = Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_3 = Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_4 = Adafruit_FreeTouch(ADC_BATTERY, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_5 = Adafruit_FreeTouch(A4, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch qt_6 = Adafruit_FreeTouch(A5, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
static Adafruit_FreeTouch *touchPads[NUM_TOUCH_PADS] = {&qt_1, &qt_2, &qt_3, &qt_4, &qt_5, &qt_6};

// Measured once at boot and then left alone for the whole session. An adaptive
// baseline is the usual approach for capacitive touch, but it is wrong for an
// instrument: any tracking fast enough to follow thermal drift is also fast
// enough to creep up under a held chord and slide fingers back out of the
// detection window. A fixed reference means a pad that reads high stays
// detected for as long as you hold it. Power-cycle the badge to recalibrate.
static int32_t touchBaseline[NUM_TOUCH_PADS];
static uint16_t touchCompCap[NUM_TOUCH_PADS];
static uint8_t cachedTouchMask = 0;

static bool btnRaw = false;
static bool btnStable = false;
static uint32_t btnChangeMs = 0;
static uint32_t btnDownMs = 0;
static bool btnReleaseEvent = false;

#define COMPCAP_MAX 16383

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
// is undocumented. Costs about 20 ms at boot.
static void calibrateCompCap()
{
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
    delay(50);

    calibrateCompCap();

    // This is the only calibration the pads ever get, so average enough samples
    // that noise cannot skew a pad's threshold for the whole session. Keep hands
    // off the slider through here: whatever is being touched at boot becomes
    // that pad's idea of "not touched".
    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        int32_t sum = 0;
        for (uint8_t s = 0; s < TOUCH_CAL_SAMPLES; s++)
        {
            sum += touchPads[i]->measure();
        }
        touchBaseline[i] = sum / TOUCH_CAL_SAMPLES;

        char line[64];
        snprintf(line, sizeof(line), "Pad %u  compcap %5u  baseline %4ld",
                 (unsigned)(i + 1), (unsigned)touchCompCap[i], (long)touchBaseline[i]);
        Serial.print(line);
        // A pad that needs almost the whole register has nothing left to absorb
        // drift. Pad 3 sits here on the reference badge - it carries the most
        // parasitic capacitance and bottoms out around 81 even fully
        // compensated, so it is the first place to look if touch degrades.
        if (touchCompCap[i] > (COMPCAP_MAX - COMPCAP_MAX / 16))
        {
            Serial.print("   <- near compensation limit");
        }
        Serial.println();
    }
}

// Every pad is an independent comparison against its fixed boot baseline, so
// any number of them can be held at once and none of them time out.
void touchUpdate()
{
    int32_t reading[NUM_TOUCH_PADS];
    uint8_t mask = 0;

    for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
    {
        reading[i] = touchPads[i]->measure();
        // Either direction counts as a touch. A pad bridged to a held
        // neighbour drains through it and swings negative rather than
        // positive, and that is still a finger on the pad.
        int32_t delta = reading[i] - touchBaseline[i];
        if (delta > TOUCH_THRESHOLD || delta < -TOUCH_THRESHOLD_NEG)
        {
            mask |= (1 << i);
        }
    }

    cachedTouchMask = mask;

#if TOUCH_DEBUG
    static uint32_t lastTouchPrintMs = 0;
    uint32_t now = millis();
    if (now - lastTouchPrintMs >= 250)
    {
        lastTouchPrintMs = now;
        Serial.print("pad raw/base/delta:");
        for (uint8_t i = 0; i < NUM_TOUCH_PADS; i++)
        {
            Serial.print(' ');
            Serial.print(reading[i]);
            Serial.print('/');
            Serial.print(touchBaseline[i]);
            Serial.print('/');
            Serial.print(reading[i] - touchBaseline[i]);
        }
        Serial.print("  mask ");
        for (int8_t i = NUM_TOUCH_PADS - 1; i >= 0; i--)
        {
            Serial.print((mask & (1 << i)) ? '1' : '0');
        }
        Serial.println();
    }
#endif
}

uint8_t touchMask()
{
    return cachedTouchMask;
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
