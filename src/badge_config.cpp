#include "badge_config.h"
#include "badge_hw.h"
#include "guitar_config.h"
#include "nvm.h"

#include <Wire.h>

// Stored in the last flash row, outside the firmware image (see nvm.h). The
// old FlashStorage copy lived inside the image, so every upload of any kind
// reset the badge to ID 0; this one survives an I2C update.
//
// The magic and the inverted copy matter because erased flash reads back as
// all 0xFF, which would otherwise look like a badge configured to ID 255.
struct BadgeConfig
{
    uint32_t magic;
    uint8_t id;
    uint8_t idInverted;
};

#define BADGE_CONFIG_MAGIC 0x31525447UL // "GTR1"

uint8_t loadBadgeId()
{
    BadgeConfig config;
    memcpy(&config, (const void *)CONFIG_ROW, sizeof(config));
    if (config.magic != BADGE_CONFIG_MAGIC || config.idInverted != (uint8_t)~config.id || config.id >= MAX_BADGES)
    {
        return 0;
    }
    return config.id;
}

static void saveBadgeId(uint8_t id)
{
    uint8_t page[NVM_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    BadgeConfig config = {BADGE_CONFIG_MAGIC, id, (uint8_t)~id};
    memcpy(page, &config, sizeof(config));
    nvmEraseRow(CONFIG_ROW);
    nvmWritePage(CONFIG_ROW, page);
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
        saveBadgeId(id);
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
