// White-noise drum: an accelerometer whack triggers a bit-banged noise burst
// on the piezo. The button steps through 5 pitches, shown on the slider LEDs.
#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

Adafruit_LIS3DH lis = Adafruit_LIS3DH();

#define BUZZER_PIN A0
#define LED_ACCEL 5
#define BUTTON_PIN 14
#define ENTROPY_PIN A6
#define ACCEL_THRESHOLD 25

const uint8_t sliderLedPins[] = {1, 0, 3, 2, 4};
#define NUM_MODES 5

// Per-mode noise color: freqHz is the fundamental pitch, varianceHz is how
// far each toggle's instantaneous frequency is allowed to randomly land on
// either side of it - narrow -> more of a pure tone, wide -> buzzy/broadband
// like a real snare or hi-hat. Kept in Hz (not raw pulse widths) because
// freq = 1e6 / (2 * pulseUs) is nonlinear: a symmetric range in pulse-width
// space is NOT a symmetric range in frequency space, so deriving min/max
// pulse widths from a symmetric Hz range is what actually keeps the
// fundamental centered.
struct DrumPitch
{
    float freqHz;
    float varianceHz;
    unsigned long durationUs;
};

const DrumPitch drumPitches[NUM_MODES] = {
    {82.41f, 16.0f, 220000},   // Bass Drum, ~E2, moderate spread
    {146.83f, 7.0f, 180000},   // Tom Drum, ~D3, narrow - closer to a true tone
    {196.00f, 118.0f, 150000}, // Snare Drum, ~G3, wide - buzzy/noisy
    {440.00f, 88.0f, 120000},  // Crash Cymbal, ~A4, moderate spread
    {880.00f, 528.0f, 90000},  // Closed Hi Hat, ~A5, wide - buzzy/noisy
};

// Bit-bangs BUZZER_PIN full rail-to-rail for one hit - much louder on a
// piezo than the DAC's weak analog swing. Blocks for the hit's duration
// (~90-220ms), same tradeoff the startup jingle already makes with delay().
// No amplitude envelope - just steady randomized toggling for the whole
// duration, then a hard stop.
void triggerDrum(uint8_t mode)
{
    const DrumPitch &pitch = drumPitches[mode];
    float minFreq = pitch.freqHz - pitch.varianceHz;
    float maxFreq = pitch.freqHz + pitch.varianceHz;
    // Higher frequency = shorter pulse, so the min/max flip across the divide.
    unsigned long minPulseUs = (unsigned long)(1000000.0f / (2.0f * maxFreq));
    unsigned long maxPulseUs = (unsigned long)(1000000.0f / (2.0f * minFreq));

    unsigned long startUs = micros();
    unsigned long elapsedUs = 0;
    unsigned long total_pulses = 0;
    bool pinState = false;
    while (true)
    {
        elapsedUs = micros() - startUs;
        if (elapsedUs >= pitch.durationUs)
            break;

        pinState = !pinState;
        if (pinState)
            total_pulses++;
        digitalWrite(BUZZER_PIN, pinState ? HIGH : LOW);

        delayMicroseconds(random(minPulseUs, maxPulseUs + 1));
    }
    digitalWrite(BUZZER_PIN, LOW);
    Serial.print("Drum: ");
    Serial.print(mode);
    Serial.print(", Total pulses: ");
    Serial.print(total_pulses);
    Serial.print(", Hertz: ");
    Serial.println((float)total_pulses / ((float)elapsedUs / 1000000.0f));
}

void setLedMode(uint8_t mode)
{
    for (uint8_t i = 0; i < NUM_MODES; i++)
    {
        digitalWrite(sliderLedPins[i], i == mode ? HIGH : LOW);
    }
}

void setup()
{
    Serial.begin(115200);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);

    for (uint8_t i = 0; i < NUM_MODES; i++)
    {
        pinMode(sliderLedPins[i], OUTPUT);
    }
    setLedMode(0);

    if (!lis.begin(0x18, 0b00010001)) // 0b00010001 for the SC7A20TR
    {                                 // change this to 0x19 for alternative i2c address
        Serial.println("Couldnt start accelerometer");
        while (1)
        {
            Serial.println("Couldnt start accelerometer");
            delay(1000);
            yield();
        }
    }
    lis.setRange(LIS3DH_RANGE_16_G);

    randomSeed(analogRead(ENTROPY_PIN));
}

void loop()
{
    static uint8_t mode = 0;

    static bool lastButtonState = LOW;
    if (digitalRead(BUTTON_PIN) == HIGH)
    {
        if (lastButtonState == LOW)
        {
            lastButtonState = HIGH;
            mode = (mode + 1) % NUM_MODES;
            setLedMode(mode);
        }
    }
    else
    {
        lastButtonState = LOW;
    }

    sensors_event_t event;
    lis.getEvent(&event);
    float scalar = sqrt(event.acceleration.x * event.acceleration.x + event.acceleration.y * event.acceleration.y + event.acceleration.z * event.acceleration.z);
    static bool lastAccelState = false;
    if (scalar > ACCEL_THRESHOLD)
    {
        if (!lastAccelState)
        {
            lastAccelState = true;
            Serial.println("Whack!");
            digitalWrite(LED_BUILTIN, HIGH);
            triggerDrum(mode);
            digitalWrite(LED_BUILTIN, LOW);
        }
    }
    else
    {
        lastAccelState = false;
    }

    // static unsigned long lastPrintTime = 0;
    // if (millis() - lastPrintTime >= 10)
    // {
    //     lastPrintTime = millis();

    //     Serial.print("Accel: ");
    //     Serial.println(scalar);
    // }
}
