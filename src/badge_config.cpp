#include "badge_config.h"
#include "badge_hw.h"
#include "guitar_config.h"

#include <FlashStorage.h>
#include <Wire.h>

// The `valid` flag matters because erased flash reads back as all 0xFF, which
// would otherwise look like a badge configured to ID 255.
struct BadgeConfig
{
    bool valid;
    uint8_t id;
};

FlashStorage(badge_config_store, BadgeConfig);

uint8_t loadBadgeId()
{
    BadgeConfig config = badge_config_store.read();
    if (!config.valid || config.id >= MAX_BADGES)
    {
        return 0;
    }
    return config.id;
}

static void blinkSaved()
{
    for (uint8_t i = 0; i < 3; i++)
    {
        setSliderLeds(0x1F);
        delay(120);
        setSliderLeds(0);
        delay(120);
    }
}

void runSetupMode(uint8_t id)
{
    // Go quiet and drop off the bus: a badge being re-addressed must not keep
    // answering to its old address while the player edits it.
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    Wire.end();
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(LED_ACCEL, LOW);

    // The long press that got us here is still down. Wait it out and swallow
    // its release, otherwise it would immediately register as an increment.
    while (buttonIsDown())
    {
        buttonUpdate();
    }
    buttonTakeRelease();

    bool saved = false;
    uint32_t lastInputMs = millis();

    while (millis() - lastInputMs < SETUP_TIMEOUT_MS)
    {
        buttonUpdate();
        setSliderLedsBinary(id);
        // Heartbeat so setup mode is unmistakable - otherwise a badge showing
        // a low ID looks a lot like one that is simply idle.
        digitalWrite(LED_BUILTIN, ((millis() / SETUP_BLINK_MS) % 2) == 0 ? HIGH : LOW);

        if (buttonHeldMs() >= LONG_PRESS_MS)
        {
            saved = true;
            break;
        }

        if (buttonTakeRelease())
        {
            id = (id + 1) % MAX_BADGES;
            lastInputMs = millis();
        }
    }

    // Stop the heartbeat first, so the save confirmation is the only thing
    // blinking and cannot be misread.
    digitalWrite(LED_BUILTIN, LOW);

    if (saved)
    {
        BadgeConfig config = {true, id};
        badge_config_store.write(config);
        blinkSaved();
    }

    setSliderLeds(0);
    // A reset is the honest way back: master and slave initialise Wire in
    // different modes, so re-running init in place would not be enough.
    NVIC_SystemReset();
    while (true)
    {
        // NVIC_SystemReset() does not return; this just keeps the compiler
        // from warning about falling off a non-returning path.
    }
}
