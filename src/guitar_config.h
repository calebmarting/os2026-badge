// Every tunable for the badge guitar, plus the pitch table.
#pragma once

#include <Arduino.h>

// The largest string count of any instrument, so it also sizes the master's
// per-string state arrays. A 4-string instrument just uses the first 4 slots.
#define NUM_STRINGS 6
#define NUM_UKE_STRINGS 4
#define NUM_FRETS 12
// Badge IDs 0..12: ID 0 is the master, 1..12 are frets 1..12.
#define MAX_BADGES 13
#define NUM_PADS 6

// Pads 1, 3, 4 and 6. Pads 2 and 5 are not read at all in ukulele mode, so a
// badge masks them out of what it reports rather than just ignoring them later.
#define UKE_ACTIVE_PADS 0x2D

// A note rings this long before it silences itself. Strumming the same string
// again, or just touching its pad, cuts it short.
#define SUSTAIN_MS 3000
// A strum pad held longer than this is a mute-and-hold, not a strum, so
// releasing it stays silent. Raise it if quick strums are being swallowed.
#define STRUM_MAX_HOLD_MS 250
// Doubles every pitch per step. The piezo resonates near 4 kHz and reproduces
// low E (82 Hz) poorly, so set this to 1 if the bottom strings are inaudible.
#define OCTAVE_SHIFT 0
// 1 = fretting a ringing string retunes it, giving hammer-ons and slides.
// 0 = pitch is latched at the moment of the strum.
#define PITCH_TRACKS_FRETS 1

// Master poll period. 12 slaves at 100 kHz is ~3 ms, so this is near the floor.
#define POLL_INTERVAL_MS 4
// How often the master retries one absent ID, so badges can be hot-added.
#define PROBE_INTERVAL_MS 1000

// 1 = the master prints the resolved fret and pitch per string over serial,
// which is how you check a fret badge is being read correctly.
#define MASTER_DEBUG 0

// One-sided, in raw FreeTouch counts above the pad's boot baseline. The
// baseline is measured once at startup and never moves, so a pad stays detected
// for as long as you hold it - power-cycle the badge to recalibrate.
// This window is genuinely tight and was measured, not guessed. Idle noise is
// +/-1 count, but pressing one pad puts up to **+30** of crosstalk on its
// neighbour, while a real touch can be as weak as **+36** when three pads are
// held and the charge is shared. Six counts of daylight, so 35 splits it with
// about 5 either way.
//
// This is the weakest part of the touch system. Too high and light multi-finger
// presses vanish; too low and a neighbour's crosstalk invents a fret. If it
// misbehaves, re-measure with TOUCH_DIAG rather than nudging blind.
#define TOUCH_THRESHOLD 35

// A held pad can also read *below* its baseline. When two pads are bridged by
// one finger, the measured pad drains through its neighbour and swings
// negative - pad 3 reaches -56 to -110 while pad 4 reads positive. That is a
// real touch, so it has to count as one.
//
// This is a separate define because it is a different physical effect with a
// different margin: crosstalk from an untouched neighbour is always *positive*
// (pressing pad 4 without pad 3 leaves pad 3 at 0-1), so nothing pushes a pad
// negative except being touched. The floor is how far a pad could drift down on
// its own - pad 3 bottoms out around 81 against a baseline of 120.
#define TOUCH_THRESHOLD_NEG 40
// Samples averaged per pad during that one calibration pass.
#define TOUCH_CAL_SAMPLES 16
// Where per-pad COMPCAP calibration aims to park each idle reading. Full scale
// is ~1016, and a pad left uncalibrated idles at 400-680, so most of the range
// is gone before a finger arrives. Parking low leaves room for the touch.
#define TOUCH_CAL_TARGET 120
// 1 = every badge prints raw reading / baseline / delta per pad plus the touch
// mask. Use it to pick TOUCH_THRESHOLD: watch the delta a real touch produces
// against the delta an untouched pad sits at, and put the threshold between.
#define TOUCH_DEBUG 0
// 1 = boot into the touch bench instead of the instrument, to sweep the analog
// settings on real hardware. See touch_diag.h. Never plays a note.
#define TOUCH_DIAG 0
#define BUTTON_DEBOUNCE_MS 75
#define LONG_PRESS_MS 1000
// Setup mode gives up and reboots unchanged after this long with no input.
#define SETUP_TIMEOUT_MS 30000
// Half-period of the LED_BUILTIN heartbeat that marks setup mode.
#define SETUP_BLINK_MS 500

// Frequency in Hz for [string][fret], fret 0 being the open string. String 0 is
// the lowest-numbered pad group, not necessarily the lowest pitch - the ukulele
// is tuned reentrant, so its G sits above its C and E.
//
// Stored as rounded whole Hz because that is exactly the 2-byte wire value, so
// playback needs no float or pow() math at runtime. Rounding costs at most ~9
// cents on the open low E and less everywhere else - well inside what the piezo
// itself smears. No PROGMEM: the SAMD21 is ARM with a unified address space, so
// a plain const array already lives in flash and reads directly.
extern const uint16_t kStringFret[NUM_STRINGS][NUM_FRETS + 1];  // E2 A2 D3 G3 B3 E4
extern const uint16_t kUkeFret[NUM_UKE_STRINGS][NUM_FRETS + 1]; // G4 C4 E4 A4

// An instrument is data rather than a mode flag, so switching between them is a
// pointer swap instead of a branch at every site that cares about tuning,
// string count, pad grouping or badge assignment.
struct Instrument
{
    const uint16_t (*fretTable)[NUM_FRETS + 1];
    // padMasks[s] is which of the 6 physical pads count as string s. A pad in
    // no mask is disabled - that is how the ukulele drops pads 2 and 5 to get
    // 4 strings from 6 pads.
    const uint8_t *padMasks;
    uint8_t stringCount;
    uint8_t badgesPerString;
};

extern const Instrument kGuitar;
extern const Instrument kUkulele;

// Pitch for a string/fret pair, with OCTAVE_SHIFT applied. Out-of-range frets
// clamp rather than read past the table.
uint16_t freqForFret(const Instrument *inst, uint8_t stringIndex, uint8_t fret);

// Which string a badge sounds. Badges are handed out in consecutive runs, so a
// guitar gives {0,1} the low E and {2,3} the A, while a ukulele gives {0,1,2}
// the G and {3,4,5} the C. The modulo wraps any leftover badge back to string 0.
uint8_t stringIndexForBadge(const Instrument *inst, uint8_t badgeId);

// Collapses a raw 6-bit pad mask into a string mask of stringCount bits. Needed
// both for the master's own strum pads and for every fret badge's reported
// mask, since a ukulele's grouping runs the whole length of the neck.
uint8_t groupPads(const Instrument *inst, uint8_t padMask);
