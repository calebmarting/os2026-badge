#include "slave.h"
#include "badge_hw.h"
#include "guitar_config.h"

#include <Wire.h>

// Written in loop(), read from the I2C request ISR. A byte is a single aligned
// access on Cortex-M0+, so no guarding is needed - and measuring the pads
// inside the ISR would be far too slow to hold the bus for.
static volatile uint8_t cachedMask = 0;

static volatile uint16_t pendingFreq = 0;
static volatile bool freqDirty = false;
static uint16_t activeFreq = 0;

// Set by the master. Decides which pads this badge reads and how it lights its
// slider - the pitch it plays is still just whatever it is told.
static volatile bool ukuleleMode = false;

static void onRequest()
{
    Wire.write((uint8_t)cachedMask);
}

// Messages are told apart by length: 1 byte is a mode, 2 bytes are a frequency.
static void onReceive(int count)
{
    if (count == 1)
    {
        ukuleleMode = Wire.read() != 0;
    }
    else if (count >= 2)
    {
        uint8_t lo = Wire.read();
        uint8_t hi = Wire.read();
        pendingFreq = (uint16_t)lo | ((uint16_t)hi << 8);
        freqDirty = true;
    }

    while (Wire.available())
    {
        Wire.read();
    }
}

// Set by a short press of this badge's button. Runtime only, like the mode.
static bool capoOn = false;

#define ALL_PADS 0x3F

void slaveToggleCapo()
{
    capoOn = !capoOn;
    Serial.println(capoOn ? "Capo on" : "Capo off");
}

// A capo reports every string held. Feeding ALL_PADS through the same ukulele
// mask keeps the disabled pads disabled - a capo should not resurrect pads 2
// and 5 that the instrument does not have.
static uint8_t reportableMask()
{
    uint8_t raw = capoOn ? ALL_PADS : touchMask();
    return ukuleleMode ? (raw & UKE_ACTIVE_PADS) : raw;
}

void slaveBegin(uint8_t id)
{
    touchUpdate();
    cachedMask = reportableMask();

    Wire.begin(id);
    Wire.onRequest(onRequest);
    Wire.onReceive(onReceive);

    // Which string this badge sounds depends on the instrument the master is
    // currently playing, so it is not something this badge can report - it just
    // plays whatever pitch it is handed.
    Serial.print("Fret badge, ID ");
    Serial.println(id);
}

void slaveLoop()
{
    // With a capo on, every pad's answer is fixed, so there is nothing to
    // measure - skip the scan entirely rather than sampling for a result we
    // are going to override anyway.
    if (!capoOn)
    {
        touchUpdate();
    }
    cachedMask = reportableMask();

    if (freqDirty)
    {
        // tone() reconfigures TC5, which is too much work to do inside the
        // SERCOM ISR that received the value - so it lands here instead.
        noInterrupts();
        uint16_t freq = pendingFreq;
        freqDirty = false;
        interrupts();

        if (freq != activeFreq)
        {
            activeFreq = freq;
            if (activeFreq > 0)
            {
                tone(BUZZER_PIN, activeFreq);
            }
            else
            {
                noTone(BUZZER_PIN);
                digitalWrite(BUZZER_PIN, LOW);
            }
        }
    }

    if (capoOn)
    {
        updateCapoLeds(activeFreq > 0);
    }
    else if (ukuleleMode)
    {
        updateUkePlayLeds(cachedMask, activeFreq > 0);
    }
    else
    {
        updatePlayLeds(cachedMask, activeFreq > 0);
    }
}
