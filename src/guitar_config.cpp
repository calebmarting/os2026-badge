#include "guitar_config.h"

const uint16_t kStringFret[NUM_STRINGS][NUM_FRETS + 1] = {
    // E2 - low E
    {82, 87, 92, 98, 104, 110, 117, 123, 131, 139, 147, 156, 165},
    // A2
    {110, 117, 123, 131, 139, 147, 156, 165, 175, 185, 196, 208, 220},
    // D3
    {147, 156, 165, 175, 185, 196, 208, 220, 233, 247, 262, 277, 294},
    // G3
    {196, 208, 220, 233, 247, 262, 277, 294, 311, 330, 349, 370, 392},
    // B3
    {247, 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494},
    // E4 - high E
    {330, 349, 370, 392, 415, 440, 466, 494, 523, 554, 587, 622, 659},
};

// Standard reentrant GCEA: the G is pitched *above* the C and E rather than
// below them, which is what gives a ukulele its sound. The E4 row is identical
// to the guitar's high-E row, which is a handy sanity check when reading this.
const uint16_t kUkeFret[NUM_UKE_STRINGS][NUM_FRETS + 1] = {
    // G4
    {392, 415, 440, 466, 494, 523, 554, 587, 622, 659, 698, 740, 784},
    // C4
    {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523},
    // E4
    {330, 349, 370, 392, 415, 440, 466, 494, 523, 554, 587, 622, 659},
    // A4
    {440, 466, 494, 523, 554, 587, 622, 659, 698, 740, 784, 831, 880},
};

// One pad per string.
static const uint8_t kGuitarPadMasks[NUM_STRINGS] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20};
// Pads 2 and 5 are disabled entirely, leaving pads 1, 3, 4 and 6 spread across
// the slider as the 4 strings. A pad that appears in no mask is simply never
// consulted, so groupPads() disables it for free.
static const uint8_t kUkePadMasks[NUM_UKE_STRINGS] = {0x01, 0x04, 0x08, 0x20};

const Instrument kGuitar = {kStringFret, kGuitarPadMasks, NUM_STRINGS, 2};
const Instrument kUkulele = {kUkeFret, kUkePadMasks, NUM_UKE_STRINGS, 3};

uint16_t freqForFret(const Instrument *inst, uint8_t stringIndex, uint8_t fret)
{
    if (stringIndex >= inst->stringCount)
    {
        return 0;
    }
    if (fret > NUM_FRETS)
    {
        fret = NUM_FRETS;
    }
    return inst->fretTable[stringIndex][fret] << OCTAVE_SHIFT;
}

uint8_t stringIndexForBadge(const Instrument *inst, uint8_t badgeId)
{
    return (badgeId / inst->badgesPerString) % inst->stringCount;
}

uint8_t groupPads(const Instrument *inst, uint8_t padMask)
{
    uint8_t strings = 0;
    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        if (padMask & inst->padMasks[s])
        {
            strings |= (1 << s);
        }
    }
    return strings;
}
