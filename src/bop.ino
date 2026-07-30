#include <Arduino.h>

#include "bop_state.h"
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_FreeTouch.h"

Adafruit_LIS3DH lis = Adafruit_LIS3DH();

#define BUZZER_PIN A0
#define LED_BUTTON 6
#define LED_MIC 7
#define LED_ACCEL 5
#define MIC_IN_PIN A6
#define BUTTON_PIN 14
#define MIC_THRESHOLD 300
#define ACCEL_THRESHOLD 60
#define SLIDE_DELTA 2 // pads the touch has to cross to count as a slide
#define BUTTON_CHIRP_FREQ 1200
#define MIC_CHIRP_FREQ 1800
#define SLIDE_LEFT_CHIRP_FREQ 900
#define SLIDE_RIGHT_CHIRP_FREQ 1100
#define WHACK_CHIRP_FREQ 600

Adafruit_FreeTouch qt_1 = Adafruit_FreeTouch(A1, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_2 = Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_3 = Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_4 = Adafruit_FreeTouch(ADC_BATTERY, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_5 = Adafruit_FreeTouch(A4, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_6 = Adafruit_FreeTouch(A5, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch *touchSensors[] = {&qt_1, &qt_2, &qt_3, &qt_4, &qt_5, &qt_6};
#define NUM_TOUCH_SENSORS 6
#define TOUCH_DEADZONE 60
int touchSetPoints[NUM_TOUCH_SENSORS];

const uint8_t sliderLedPins[] = {1, 0, 3, 2, 4};
#define NUM_SLIDER_LEDS 5

ButtonPromptState buttonState("Button", LED_BUTTON, BUZZER_PIN, BUTTON_CHIRP_FREQ);
MicrophonePromptState micState("Microphone", LED_MIC, BUZZER_PIN, MIC_CHIRP_FREQ);
// reverse = slide left reverse = slide right
// If the physical direction looks backwards on the board, swap these two `reverse` values.
SlideLeftPromptState slideLeftState("Slide Left", sliderLedPins, NUM_SLIDER_LEDS, true, BUZZER_PIN, SLIDE_LEFT_CHIRP_FREQ);
SlideRightPromptState slideRightState("Slide Right", sliderLedPins, NUM_SLIDER_LEDS, false, BUZZER_PIN, SLIDE_RIGHT_CHIRP_FREQ);
WhackPromptState whackState("Whack", LED_ACCEL, BUZZER_PIN, WHACK_CHIRP_FREQ);
RoundGapState roundGapState(300, 900, 2000, 3000, 15); // 15% chance of a 3-5s long pause instead
GameStartState startState(BUZZER_PIN, BUTTON_PIN);
GameOverState gameOverState(BUZZER_PIN);

BopState *const promptStates[] = {&buttonState, &micState, &slideLeftState, &slideRightState, &whackState};
#define NUM_PROMPT_STATES 5
BopState *const gapNextStates[] = {&roundGapState};
BopState *const startNextStates[] = {&startState};

void setup()
{
    Serial.begin(115200);
    // while (!Serial)
    //     ;

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_BUTTON, OUTPUT);
    pinMode(LED_MIC, OUTPUT);
    pinMode(LED_ACCEL, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);

    for (int i = 0; i < NUM_SLIDER_LEDS; i++)
    {
        pinMode(sliderLedPins[i], OUTPUT);
        digitalWrite(sliderLedPins[i], LOW);
    }

    for (int i = 0; i < NUM_TOUCH_SENSORS; i++)
    {
        if (!touchSensors[i]->begin())
            Serial.print("Failed to begin qt on pin A");
    }
    delay(50);
    for (int i = 0; i < NUM_TOUCH_SENSORS; i++)
    {
        touchSetPoints[i] = touchSensors[i]->measure();
    }

    if (!lis.begin(0x18, 0b00010001)) // 0b00010001 for the SC7A20TR
    {                                 // change this to 0x19 for alternative i2c address
        Serial.println("Couldnt start");
        while (1)
            yield();
    }
    Serial.println("LIS3DH found!");

    lis.setRange(LIS3DH_RANGE_16_G); // 2, 4, 8 or 16 G!

    Serial.print("Range = ");
    Serial.print(2 << lis.getRange());
    Serial.println("G");

    // lis.setPerformanceMode(LIS3DH_MODE_LOW_POWER);
    Serial.print("Performance mode set to: ");
    switch (lis.getPerformanceMode())
    {
    case LIS3DH_MODE_NORMAL:
        Serial.println("Normal 10bit");
        break;
    case LIS3DH_MODE_LOW_POWER:
        Serial.println("Low Power 8bit");
        break;
    case LIS3DH_MODE_HIGH_RESOLUTION:
        Serial.println("High Resolution 12bit");
        break;
    }

    // lis.setDataRate(LIS3DH_DATARATE_50_HZ);
    Serial.print("Data rate set to: ");
    switch (lis.getDataRate())
    {
    case LIS3DH_DATARATE_1_HZ:
        Serial.println("1 Hz");
        break;
    case LIS3DH_DATARATE_10_HZ:
        Serial.println("10 Hz");
        break;
    case LIS3DH_DATARATE_25_HZ:
        Serial.println("25 Hz");
        break;
    case LIS3DH_DATARATE_50_HZ:
        Serial.println("50 Hz");
        break;
    case LIS3DH_DATARATE_100_HZ:
        Serial.println("100 Hz");
        break;
    case LIS3DH_DATARATE_200_HZ:
        Serial.println("200 Hz");
        break;
    case LIS3DH_DATARATE_400_HZ:
        Serial.println("400 Hz");
        break;

    case LIS3DH_DATARATE_POWERDOWN:
        Serial.println("Powered Down");
        break;
    case LIS3DH_DATARATE_LOWPOWER_5KHZ:
        Serial.println("5 Khz Low Power");
        break;
    case LIS3DH_DATARATE_LOWPOWER_1K6HZ:
        Serial.println("1.6 Khz Low Power");
        break;
    }

    randomSeed(analogRead(MIC_IN_PIN));
    for (BopState *state : promptStates)
    {
        state->set_next_states(gapNextStates, 1);
        state->set_fail_state(&gameOverState);
        // No set_timeout_millis() here - PromptState manages its own
        // response-time ramp (see PromptState::next_timeout_ms).
    }
    roundGapState.set_next_states(promptStates, NUM_PROMPT_STATES);
    gameOverState.set_next_states(startNextStates, 1);
    gameOverState.set_fail_state(&startState);
    gameOverState.set_timeout_millis(4000);
    startState.set_next_states(promptStates, NUM_PROMPT_STATES);
    startState.set_state();
}

void loop()
{
    static bool lastButtonState = LOW;
    if (digitalRead(BUTTON_PIN) == HIGH)
    {
        if (lastButtonState == LOW)
        {
            lastButtonState = HIGH;
            current_state->on_button();
        }
    }
    else
    {
        lastButtonState = LOW;
    }

    int opamp_value = analogRead(MIC_IN_PIN);
    int amplitude = abs(opamp_value - 512);
    static bool lastMicState = false;
    if (amplitude > MIC_THRESHOLD)
    {
        if (!lastMicState)
        {
            lastMicState = true;
            current_state->on_microphone();
        }
    }
    else
    {
        lastMicState = false;
    }

    int touchedPad = -1;
    for (int i = 0; i < NUM_TOUCH_SENSORS; i++)
    {
        int result = touchSensors[i]->measure();
        if (result < touchSetPoints[i] - TOUCH_DEADZONE || result > touchSetPoints[i] + TOUCH_DEADZONE)
        {
            touchedPad = i;
        }
    }
    // Tracks one continuous touch across the pad row by the lowest and highest pad indices seen
    // Fast slides can skip over pads in between without missing the gesture.
    // Resets once the finger lifts (touchedPad == -1).
    static int slideMinPad = -1;
    static int slideMaxPad = -1;
    static bool slideFired = false;
    if (touchedPad == -1)
    {
        slideMinPad = -1;
        slideMaxPad = -1;
        slideFired = false;
    }
    else
    {
        if (slideMinPad == -1 || touchedPad < slideMinPad)
        {
            slideMinPad = touchedPad;
        }
        if (slideMaxPad == -1 || touchedPad > slideMaxPad)
        {
            slideMaxPad = touchedPad;
        }

        if (!slideFired && (slideMaxPad - slideMinPad) >= SLIDE_DELTA)
        {
            slideFired = true;
            if (touchedPad == slideMaxPad)
            {
                current_state->on_slide_right();
            }
            else
            {
                current_state->on_slide_left();
            }
        }
    }

    sensors_event_t event;
    lis.getEvent(&event);
    float scalar = sqrt(event.acceleration.x * event.acceleration.x + event.acceleration.y * event.acceleration.y + event.acceleration.z * event.acceleration.z);
    static bool lastAccelState = false;
    Serial.print("Accel: ");
    Serial.println(scalar);
    if (scalar > ACCEL_THRESHOLD)
    {
        if (!lastAccelState)
        {
            lastAccelState = true;
            current_state->on_whack();
        }
    }
    else
    {
        lastAccelState = false;
    }

    current_state->render();

    delay(5);
}
