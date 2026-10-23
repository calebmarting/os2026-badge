// Easter-egg songs. The note data is generated before every build from
// songs/songs.json (tools/song_convert.py) into songs_data.cpp; the master
// plays it by handing each voice to a group of badges. Add a song by adding an
// entry to the manifest - nothing in here changes.
#pragma once

#include <Arduino.h>

// One step of a song, unpacked by songReadEvent().
struct SongEvent
{
    // Milliseconds after the previous event.
    uint32_t deltaMs;
    // Bit N = badge N's piezo. The converter decides who plays what -
    // including chords that borrow piezos from other notes - so playback just
    // does as it is told.
    uint16_t piezos;
    // MIDI note number for those piezos, 0 = they go silent, or from
    // SONG_DRUM_BASE up a drum hit: kSongDrums[note - SONG_DRUM_BASE].
    uint8_t note;
};

#define SONG_DRUM_BASE 128

// A drum is a burst of pseudo white noise (badge_hw.h noiseStart()).
// The table is generated with the songs (tools/song_convert.py DRUM_SOUNDS).
struct SongDrum
{
    uint16_t centerHz;
    uint16_t spreadHz;
    uint16_t ms;
    // 0 = a square-ish burst; else clicks this many us long (see noiseStart).
    uint8_t pulseUs;
    // Fade by (1 + decayTenths / 10) times by the end (see noiseStart).
    uint8_t decayTenths;
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
    // The events, packed 2-4 bytes each - songs are the biggest thing in
    // flash. Format: encode_events() in tools/song_convert.py.
    uint32_t dataLen;
    const uint8_t *data;
};

extern const Song kSongs[];
extern const uint8_t kSongCount;

uint16_t songNoteHz(uint8_t note);

// Unpacks the event at pos and moves pos past it. False at the end.
bool songReadEvent(const uint8_t *&pos, const uint8_t *end, SongEvent &event);

const Song *songFind(const char *name);

// taps/times are the most recent capo-button taps, oldest first. Returns the
// song whose trigger the newest taps complete within SONG_TRIGGER_WINDOW_MS,
// or nullptr.
const Song *songMatchTrigger(const uint8_t *taps, const uint32_t *times, uint8_t count, uint32_t now);
