// Badge guitar.
//
// 12 badges chained over the SAO I2C ports. The badge at ID 0 is the
// master: it is the bus controller and its 6 touch pads are the strings. IDs
// 1..11 are fret badges, where badge N is fret N and its pads report which
// strings are held down at that fret. Every badge is also a speaker; they pair
// up by ID so {0,1} sound the low E, {2,3} the A, and so on.
//
// One image is flashed to every badge. Long-press the button to set this
// badge's ID; it is stored in flash and picks the role at boot.
#include <Arduino.h>

#include "badge_config.h"
#include "badge_hw.h"
#include "guitar_config.h"
#include "master.h"
#include "nvm.h"
#include "params.h"
#include "slave.h"
#include "touch_diag.h"

static uint8_t badgeId = 0;

void setup()
{
    Serial.begin(115200);

    hwBegin();

#if TOUCH_DIAG
    touchDiagRun(); // never returns; the badge plays nothing in this build
#endif

    badgeId = loadBadgeId();
    // The mic input floats enough to differ per badge, so every badge
    // twinkles differently in attract mode.
    randomSeed(analogRead(ENTROPY_PIN) ^ (badgeId << 10) ^ micros());
    paramsApplyAcquisition(badgeId);

    Serial.print("Firmware ");
    Serial.print(runningImageCrc(), HEX);
    Serial.print(", ");
    Serial.print(runningImageSize());
    Serial.println(" bytes");
    if (runningImageSize() > APP_MAX_SIZE)
    {
        // It would overlap the staging area, so an I2C update would overwrite
        // the very code doing it.
        Serial.println("WARNING: firmware too large for I2C updates");
    }

    if (badgeId == 0)
    {
        masterBegin();
    }
    else
    {
        slaveBegin(badgeId);
    }
}

void loop()
{
    buttonUpdate();
    if (buttonHeldMs() >= LONG_PRESS_MS)
    {
        runSetupMode(badgeId); // reboots, never returns
    }

    // A long press enters setup mode above and never comes back, so a release
    // reaching here is always a short press.
    bool tapped = buttonTakeRelease();
    if (badgeId == 0)
    {
        if (tapped)
        {
            masterNoteActivity();
        }
        // Taps are counted until DOUBLE_TAP_MS passes with no new one: one
        // toggles the ukulele, two recalibrate the neck, three toggle
        // realistic strumming. Each has to wait out the window to know which
        // it was.
        static uint8_t taps = 0;
        static uint32_t tapMs = 0;
        if (tapped)
        {
            taps++;
            tapMs = millis();
        }
        else if (taps > 0 && millis() - tapMs > DOUBLE_TAP_MS)
        {
            if (taps == 1)
            {
                masterToggleUkulele();
            }
            else if (taps == 2)
            {
                masterRecalibrate();
            }
            else
            {
                masterToggleRealistic();
            }
            taps = 0;
        }
    }
    else if (tapped)
    {
        slaveToggleCapo();
    }

    if (badgeId == 0)
    {
        masterLoop();
    }
    else
    {
        slaveLoop();
    }
}
