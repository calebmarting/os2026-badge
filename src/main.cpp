// Very basic demo for the OS2026 badge. This demo uses the touch sensors, accelerometer, microphone, and button to demonstrate the capabilities of the badge.
// I highly recommend using PlatformIO to build and upload this code to the badge.
// If you have issues uploading, press

#include <Arduino.h>

#include "Adafruit_FreeTouch.h"

#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>

Adafruit_LIS3DH lis = Adafruit_LIS3DH();

#define BUZZER_PIN A0
#define LED_ACCEL 5
#define LED_BUTTON 6
#define LED_MIC 7
#define MIC_IN_PIN A6
#define BUTTON_PIN 14
#define SAO_GPIO_1 8
#define SAO_GPIO_2 9

Adafruit_FreeTouch qt_1 = Adafruit_FreeTouch(A1, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_2 = Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_3 = Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_4 = Adafruit_FreeTouch(ADC_BATTERY, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_5 = Adafruit_FreeTouch(A4, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);
Adafruit_FreeTouch qt_6 = Adafruit_FreeTouch(A5, OVERSAMPLE_4, RESISTOR_100K, FREQ_MODE_NONE);

#define NUM_TOUCH_SENSORS 6
Adafruit_FreeTouch *touchSensors[] = {&qt_1, &qt_2, &qt_3, &qt_4, &qt_5, &qt_6};
#define NUM_SLIDER_LEDS 6
const int sliderLedPins[] = {1, 0, 3, 2, 4, LED_BUILTIN};
int sliderSetPoints[] = {410, 410, 673, 410, 410, 0};
int freqs[] = {440, 523, 622, 698, 740, 880};
#define TOUCH_DEADZONE 30

char buffer[256];

void setup()
{
    Serial.begin(115200);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_ACCEL, OUTPUT);
    pinMode(LED_BUTTON, OUTPUT);
    pinMode(LED_MIC, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);
    pinMode(SAO_GPIO_1, OUTPUT);
    pinMode(SAO_GPIO_2, OUTPUT);

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
        int result = touchSensors[i]->measure();
        sliderSetPoints[i] = result;
    }

    if (!lis.begin(0x18, 0b00010001)) // 0b00010001 for the SC7A20TR
    {
        Serial.println("Couldnt start");
        while (1)
            yield();
    }

    Serial.println("SC7A20TR found!");

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
}

void loop()
{
    int result = 0;
    int current = -1;

    digitalWrite(SAO_GPIO_1, (millis() / 500) % 2 == 0 ? HIGH : LOW);
    digitalWrite(SAO_GPIO_2, (millis() / 1000) % 2 == 1 ? HIGH : LOW);
    static bool lastButtonState = LOW;
    if (digitalRead(BUTTON_PIN) == HIGH)
    {
        digitalWrite(LED_BUTTON, HIGH);
        if (lastButtonState == LOW)
        {
            Serial.println("Button Pressed");
            lastButtonState = HIGH;
        }
    }
    else
    {
        digitalWrite(LED_BUTTON, LOW);
        if (lastButtonState == HIGH)
        {
            Serial.println("Button Released");
            lastButtonState = LOW;
        }
    }

    for (int i = 0; i < NUM_TOUCH_SENSORS; i++)
    {
        result = touchSensors[i]->measure();
        Serial.print(" ");
        Serial.print(result);
        Serial.print(" ");
        if (result < sliderSetPoints[i] - TOUCH_DEADZONE || result > sliderSetPoints[i] + TOUCH_DEADZONE)
        {

            current = i;
            digitalWrite(sliderLedPins[i], HIGH);
        }
        else
        {
            digitalWrite(sliderLedPins[i], LOW);
        }
    }

    if (current != -1)
    {
        Serial.print("Touched: QT ");
        Serial.print(current + 1);
        tone(BUZZER_PIN, freqs[current]); // Play a tone on pin A0 at a frequency based on the current touch
    }
    else
    {
        noTone(BUZZER_PIN);
    }

    int opamp_value = analogRead(MIC_IN_PIN);
    int amplitude = abs(opamp_value - 512);
    if (amplitude > 300)
    {

        digitalWrite(LED_MIC, HIGH);
    }
    else
    {
        digitalWrite(LED_MIC, LOW);
    }
    Serial.print(" Button: ");
    Serial.print(digitalRead(BUTTON_PIN));
    Serial.print(" ");
    sprintf(buffer, "Microphone: %5d", amplitude);
    Serial.print(buffer);

    sensors_event_t event;
    lis.getEvent(&event);

    /* Display the results (acceleration is measured in m/s^2) */
    Serial.print("\tX: ");
    Serial.print(event.acceleration.x);
    Serial.print(" \tY: ");
    Serial.print(event.acceleration.y);
    Serial.print(" \tZ: ");
    Serial.print(event.acceleration.z);

    float scalar = sqrt(event.acceleration.x * event.acceleration.x + event.acceleration.y * event.acceleration.y + event.acceleration.z * event.acceleration.z);
    Serial.print(" \tScalar: ");
    Serial.print(scalar);
    Serial.println(" m/s^2 ");

    if (scalar > 20)
    {
        digitalWrite(LED_ACCEL, HIGH);
    }
    else
    {
        digitalWrite(LED_ACCEL, LOW);
    }

    delay(5);
}
