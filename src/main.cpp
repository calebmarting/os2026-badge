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
        // One tap toggles the ukulele, two quick taps recalibrate the neck. The
        // toggle has to wait out the double-tap window to know which it was.
        static bool tapPending = false;
        static uint32_t tapMs = 0;
        if (tapped && tapPending && millis() - tapMs <= DOUBLE_TAP_MS)
        {
            tapPending = false;
            masterRecalibrate();
        }
        else if (tapped)
        {
            tapPending = true;
            tapMs = millis();
        }
        else if (tapPending && millis() - tapMs > DOUBLE_TAP_MS)
        {
            tapPending = false;
            masterToggleUkulele();
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
