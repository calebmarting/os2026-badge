// Badge guitar.
//
// Up to 13 badges chained over the SAO I2C ports. The badge at ID 0 is the
// master: it is the bus controller and its 6 touch pads are the strings. IDs
// 1..12 are fret badges, where badge N is fret N and its pads report which
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
#include "slave.h"

static uint8_t badgeId = 0;

void setup()
{
    Serial.begin(115200);

    hwBegin();
    badgeId = loadBadgeId();

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
    if (buttonTakeRelease())
    {
        if (badgeId == 0)
        {
            masterToggleUkulele();
        }
        else
        {
            slaveToggleCapo();
        }
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
