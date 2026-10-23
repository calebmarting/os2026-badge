// Local badge hardware: pin map, touch pads, button and LEDs.
//
// Deliberately does NOT touch the accelerometer. Every badge has an SC7A20 at
// 0x18, so once the badges are bussed together over the SAO ports there are up
// to 13 devices answering that address. Slaves could not reach it anyway -
// Wire.begin(addr) puts the SERCOM into peripheral mode.
#pragma once

#include <Arduino.h>

#define BUZZER_PIN A0 // piezo, also the SAMD21 DAC pin
#define BUTTON_PIN 14 // tactile switch, active HIGH via an external pulldown
#define LED_ACCEL 5   // forehead
#define LED_BUTTON 6  // right eye
#define LED_MIC 7     // left eye
#define ENTROPY_PIN A6

#define NUM_TOUCH_PADS 6
#define NUM_SLIDER_LEDS 5

// Out of numeric order on purpose so index 0..4 reads physically left to right.
extern const uint8_t sliderLedPins[NUM_SLIDER_LEDS];

// pinModes and the touch pads' PTC setup. Leaves the pads driven LOW and does
// not measure anything - calibration is the touch layer's job (touch.h).
void hwBegin();

// One raw 6-pad scan: hands the pads to the PTC, measures each, then drives
// them all LOW again. Returns how long that took in us. This is the
// measurement window, so nothing else may happen on the bus or the LEDs while
// it runs.
uint32_t padsScan(uint16_t raw[NUM_TOUCH_PADS]);
// Binary-searches each pad's compensation capacitor to park its idle reading
// near TOUCH_CAL_TARGET. Measures continuously, so it needs the token too.
void padsCalibrateCompCap();
uint16_t padCompCap(uint8_t pad);
// Acquisition settings for every pad, as FreeTouch enum values (see the PTC_*
// defaults in guitar_config.h). They are read at the start of each
// conversion, so they apply from the next measurement.
void padsConfigure(uint8_t oversample, uint8_t resistor, uint8_t freqMode, uint8_t hop);

void buttonUpdate();
bool buttonIsDown();
// Milliseconds the button has been held, or 0 if it is up.
uint32_t buttonHeldMs();
// Consumes a pending release edge; returns false if there was none.
bool buttonTakeRelease();

// Bit i lights sliderLedPins[i], i.e. bit 0 is the leftmost LED. Used to mirror
// the touch pads, where pad 1 is physically on the left.
void setSliderLeds(uint8_t mask);
// Renders a number with the most significant bit on the LEFT, so it reads the
// way you would write it down. Note this is the opposite order to
// setSliderLeds().
void setSliderLedsBinary(uint8_t value);
// Pads 1-5 on the slider LEDs, pad 6 on LED_BUILTIN, and the right-eye LED
// while the badge is sounding a note. Deliberately does not touch LED_ACCEL:
// the forehead LED is the master's ukulele-mode indicator and nothing else.
void updatePlayLeds(uint8_t padMask, bool sounding);

// The ukulele layout, identical on the master and every fret badge: the 4
// strings light slider LEDs 1, 2, 4 and 5, leaving the middle one dark where
// the disabled pads are. Takes a raw pad mask and does the grouping itself.
void updateUkePlayLeds(uint8_t padMask, bool sounding);

// The calibration prompt (PROMPT_* in scan_protocol.h): the whole slider and
// LED_BUILTIN blinking fast means hands off, solid means calibrating. Call it
// every loop while blinking.
void updatePromptLeds(uint8_t mode);

// Attract mode: every LED on this badge on or off at random, reshuffled at
// most every ATTRACT_STEP_MS. Call it once per scan while attract mode is on.
void updateAttractLeds();
// Turns off the two LEDs only attract mode uses (left eye, forehead). The play
// displays never write them, so leaving attract mode has to.
void endAttractLeds();

// The whole slider lit: this badge is acting as a capo. Deliberately the same
// in both instruments - in ukulele mode the middle LED is otherwise impossible
// to light, so a full slider is unmistakably a capo rather than a chord.
void updateCapoLeds(bool sounding);
