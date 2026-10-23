#include "songs.h"
#include "guitar_config.h"

// C8..B8 in Hz; every lower octave is a right shift, so no float maths.
static const uint16_t kTopOctaveHz[12] = {4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902};

uint16_t songNoteHz(uint8_t note)
{
    if (note == 0)
    {
        return 0;
    }
    if (note > 119)
    {
        note = 119;
    }
    uint8_t shift = 9 - note / 12;
    uint16_t hz = kTopOctaveHz[note % 12];
    return shift ? (hz + (1 << (shift - 1))) >> shift : hz;
}

uint8_t songVoiceForBadge(const Song *song, uint8_t badgeId)
{
    return (uint16_t)badgeId * song->voices / MAX_BADGES;
}

const Song *songFind(const char *name)
{
    for (uint8_t i = 0; i < kSongCount; i++)
    {
        if (strcmp(kSongs[i].name, name) == 0)
        {
            return &kSongs[i];
        }
    }
    return nullptr;
}

const Song *songMatchTrigger(const uint8_t *taps, const uint32_t *times, uint8_t count, uint32_t now)
{
    for (uint8_t s = 0; s < kSongCount; s++)
    {
        const Song &song = kSongs[s];
        uint8_t k = song.triggerLen;
        if (k == 0 || k > count || now - times[count - k] > SONG_TRIGGER_WINDOW_MS)
        {
            continue;
        }
        if (memcmp(taps + count - k, song.trigger, k) == 0)
        {
            return &song;
        }
    }
    return nullptr;
}
