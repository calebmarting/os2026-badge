// Easter-egg songs. The note data is generated before every build from
// songs/songs.json (tools/song_convert.py) into songs_data.cpp; the master
// plays it by handing each voice to a group of badges. Add a song by adding an
// entry to the manifest - nothing in here changes.
#pragma once

#include <Arduino.h>

struct SongEvent
{
    // Milliseconds after the previous event.
    uint16_t deltaMs;
    // 0..voices-1, or SONG_WAIT for an event that only passes time.
    uint8_t voice;
    // MIDI note number, 0 = this voice goes silent.
    uint8_t note;
};

#define SONG_WAIT 0xFF

struct Song
{
    const char *name;
    // Capo-button taps that start it, as fret badge IDs in order.
    const uint8_t *trigger;
    uint8_t triggerLen;
    uint8_t voices;
    uint16_t eventCount;
    const SongEvent *events;
};

extern const Song kSongs[];
extern const uint8_t kSongCount;

uint16_t songNoteHz(uint8_t note);

// Badges split into `voices` consecutive groups, the same way the strings are
// shared out: 6 voices give pairs, 4 give threes.
uint8_t songVoiceForBadge(const Song *song, uint8_t badgeId);

const Song *songFind(const char *name);

// taps/times are the most recent capo-button taps, oldest first. Returns the
// song whose trigger the newest taps complete within SONG_TRIGGER_WINDOW_MS,
// or nullptr.
const Song *songMatchTrigger(const uint8_t *taps, const uint32_t *times, uint8_t count, uint32_t now);
