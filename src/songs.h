// Easter-egg songs. The note data is generated before every build from
// songs/songs.json (tools/song_convert.py) into songs_data.cpp; the master
// plays it by handing each voice to a group of badges. Add a song by adding an
// entry to the manifest - nothing in here changes.
#pragma once

#include <Arduino.h>

// Packed to 5 bytes: songs are the biggest thing in flash.
struct __attribute__((packed)) SongEvent
{
    // Milliseconds after the previous event.
    uint16_t deltaMs;
    // Bit N = badge N's piezo. 0 = an event that only passes time. The
    // converter decides who plays what - including chords that borrow
    // piezos from other notes - so playback just does as it is told.
    uint16_t piezos;
    // MIDI note number for those piezos, 0 = they go silent, or from
    // SONG_DRUM_BASE up a drum hit: kSongDrums[note - SONG_DRUM_BASE].
    uint8_t note;
};

#define SONG_DRUM_BASE 128

// A drum is a burst of pseudo white noise: the piezo toggles at random
// intervals between centerHz - spreadHz and centerHz + spreadHz for ms.
// The table is generated with the songs (tools/song_convert.py DRUM_SOUNDS).
struct SongDrum
{
    uint16_t centerHz;
    uint16_t spreadHz;
    uint16_t ms;
};

extern const SongDrum kSongDrums[];
extern const uint8_t kSongDrumCount;

struct Song
{
    const char *name;
    // Capo-button taps that start it, as fret badge IDs in order.
    const uint8_t *trigger;
    uint8_t triggerLen;
    // As written in the manifest; informational only.
    uint8_t voices;
    uint16_t eventCount;
    const SongEvent *events;
};

extern const Song kSongs[];
extern const uint8_t kSongCount;

uint16_t songNoteHz(uint8_t note);

const Song *songFind(const char *name);

// taps/times are the most recent capo-button taps, oldest first. Returns the
// song whose trigger the newest taps complete within SONG_TRIGGER_WINDOW_MS,
// or nullptr.
const Song *songMatchTrigger(const uint8_t *taps, const uint32_t *times, uint8_t count, uint32_t now);
