// Every tunable for the badge guitar, plus the pitch table.
#pragma once

#include <Arduino.h>

// ---- Touch tuning ----------------------------------------------------------
// The detection constants, all in FreeTouch counts relative to a pad's
// baseline. These are the boot defaults for the Params struct (params.h).
//
// The measured window, from one badge before time-division scanning: idle
// noise is +/-1 count, pressing one pad puts up to +30 of crosstalk on its
// neighbour, and a real touch can be as weak as +36 when three pads are held
// and share the charge. Re-measure with the debug output before nudging these.
//
// Turn on above TOUCH_ON_THRESHOLD, turn off again below TOUCH_OFF_THRESHOLD.
// OFF sits just under ON rather than at half of it: a released pad next to a
// held one still carries up to +30 of crosstalk, and an OFF line below that
// would leave it stuck on.
#define TOUCH_ON_THRESHOLD 35
#define TOUCH_OFF_THRESHOLD 30

// A pad bridged to a neighbour by one finger drains through it and swings
// *below* its baseline. On the neck, a firm ukulele C-string finger (pad 3)
// spilling onto pad 4 read -110 to -118 while pad 4 only reached +21..+34.
#define TOUCH_NEG_THRESHOLD 40
// 0 = a negative is never a touch.
// 1 = a negative is a touch only while an adjacent pad reads positive; with no
//     such neighbour, TOUCH_NEG_RECAL_SCANS in a row *while the whole neck is
//     idle* re-baselines the pad. Never during play: a hand on the neck can
//     pull a pad negative, and re-zeroing it there leaves it stuck on when
//     the hand lifts.
// 2 = a negative is always a touch and never re-baselines.
// 2 is the default: with one badge measuring at a time, untouched pads sit
// within +/-1 of their baseline, so a negative now means a finger - and mode 1
// threw away that C-string finger because its neighbour was under +35.
#define TOUCH_NEG_MODE 2
#define TOUCH_NEG_RECAL_SCANS 50

// Consecutive scans that must agree before a pad turns on or off.
#define TOUCH_DEBOUNCE_SCANS 2
// Baseline drift when the whole neck is idle: base += (raw - base) >> SHIFT.
// 6 settles in ~64 scans, around a second.
#define TOUCH_DRIFT_SHIFT 6
// A pad reading "held" this long is re-baselined, in case its baseline was
// captured under a finger. 0 = never.
#define TOUCH_MAX_ON_SECONDS 30

// PTC acquisition, as Adafruit_FreeTouch enum values.
// Oversampling: 2 = 4x, 3 = 8x, 4 = 16x, 5 = 32x. FreeTouch divides it back
// out, so counts - and every threshold above - keep the same scale; more just
// means less noise and a proportionally longer scan window. On the 12-badge
// neck, 4x gives a ~17 ms cycle against ~30 ms at 16x.
#define PTC_OVERSAMPLE 2
// Series resistor: 0 = none, 1 = 20K, 2 = 50K, 3 = 100K.
#define PTC_RESISTOR 2
// 0 = fixed frequency. Any other value turns on the PTC's spread spectrum
// with PTC_FREQ_HOP as its sample delay (0..15) - the library treats HOP,
// SPREAD and SPREAD_MEDIAN identically. Each badge adds PTC_HOP_PER_BADGE x
// its ID to the delay, so neighbours can be made to sample on different
// cadences; 0 gives every badge the same one.
#define PTC_FREQ_MODE 2
#define PTC_FREQ_HOP 0
#define PTC_HOP_PER_BADGE 0
// 0 = all six pads are handed to the PTC for the whole scan.
// 1 = only the pad being measured is; the other five stay driven LOW, so a
//     finger resting on one grounds the hand instead of sharing charge with
//     the pad being read. On hardware this stopped a ukulele G chord losing
//     its fret-3 finger when the fret-2 fingers went down.
#define PTC_PAD_ISOLATE 1

// Samples averaged per pad during calibration.
#define TOUCH_CAL_SAMPLES 32
// Where per-pad COMPCAP calibration aims to park each idle reading. Full scale
// is ~1016, and a pad left uncalibrated idles at 400-680, so most of the range
// is gone before a finger arrives. Parking low leaves room for the touch.
#define TOUCH_CAL_TARGET 120

// Calibration: every badge blinks "hands off" for CAL_LEAD_MS, then lights
// solid while each one calibrates in turn. A fret badge gets as long as the
// master's own calibration took, plus 25% and CAL_MARGIN_MS.
#define CAL_LEAD_MS 1500
#define CAL_MARGIN_MS 20
// A badge whose calibration failed is retried no sooner than this.
#define CAL_RETRY_MS 2000
// How long the master waits at power-up for the fret badges to boot.
#define STARTUP_MS 500
// Taps on the master's button are counted until this long passes with no new
// one: 1 toggles the ukulele, 2 recalibrate, 3 toggle realistic strumming.
// Each lands this much after the last tap.
#define DOUBLE_TAP_MS 350

// Attract mode: after ATTRACT_IDLE_SECONDS with nothing touched and no button
// pressed, every badge twinkles its LEDs at random until something is touched.
// 0 = never. Each badge reshuffles every ATTRACT_STEP_MS, with each LED lit
// ATTRACT_ON_PERCENT of the time.
#define ATTRACT_IDLE_SECONDS 10
#define ATTRACT_STEP_MS 150
#define ATTRACT_ON_PERCENT 40

// Easter-egg songs (songs/songs.json): a song's capo-tap trigger must be
// completed within this window, first tap to last.
#define SONG_TRIGGER_WINDOW_MS 3000

// 1 = a badge prints raw/baseline/delta per pad over its own USB serial after
// every scan, rate-limited to TOUCH_DEBUG_MS. Only the boot state: "debug" on
// a badge's console toggles it, and "watch" on the master shows the same
// numbers for any badge without moving the cable.
#define TOUCH_DEBUG 0
#define TOUCH_DEBUG_MS 100

// Firmware update over I2C (ota.h). The row times are deliberately pessimistic
// - the SAMD21 erases a row in a few ms and programs a page in ~2.5 ms.
#define OTA_HOST_TIMEOUT_MS 5000
#define OTA_ROW_ERASE_MS 8
#define OTA_ROW_INSTALL_MS 20
#define OTA_BADGE_TIMEOUT_MS 30000
// A badge that has not answered a status read for this long is given up on.
#define OTA_BADGE_SILENT_MS 3000
// ---------------------------------------------------------------------------

// The largest string count of any instrument, so it also sizes the master's
// per-string state arrays. A 4-string instrument just uses the first 4 slots.
#define NUM_STRINGS 6
#define NUM_UKE_STRINGS 4
#define NUM_FRETS 12
// Exactly 12 badges: ID 0 is the master, 1..11 are frets 1..11. That also
// splits evenly into both instruments - 6 strings x 2 badges, 4 strings x 3.
#define MAX_BADGES 12
#define NUM_PADS 6

// A fret badge answers at I2C_ADDR_BASE + its ID. Raw IDs 1..7 would land in
// the 0x00-0x07 block the I2C spec reserves (general call, CBUS, Hs-mode
// master codes), and the offset keeps clear of the accelerometers at 0x18.
#define I2C_ADDR_BASE 0x30
inline uint8_t wireAddress(uint8_t badgeId) { return I2C_ADDR_BASE + badgeId; }

// Pads 1, 3, 4 and 6. Pads 2 and 5 are not read at all in ukulele mode, so a
// badge masks them out of what it reports rather than just ignoring them later.
#define UKE_ACTIVE_PADS 0x2D

// A note rings this long before it silences itself. Strumming the same string
// again, or just touching its pad, cuts it short.
#define SUSTAIN_MS 3000
// A strum pad held longer than this is a mute-and-hold, not a strum, so
// releasing it stays silent. Raise it if quick strums are being swallowed.
#define STRUM_MAX_HOLD_MS 250
// After a mute is let go of, a touch that starts within this window is the
// finger bouncing off the pad, not a new strum: its release stays silent.
#define MUTE_LOCKOUT_MS 75
// A palm mute that misses a pad or two still mutes everything: with all but
// MUTE_TOLERANCE strings held (1 = 5 of 6 on the guitar, 3 of 4 on the
// ukulele), every string is silenced and lifting the palm cannot pluck.
// 0 = off; at least two strings must be held either way.
#define MUTE_TOLERANCE 1
// 1 = realistic strumming: a strummed note rings for SUSTAIN_MS, but changing
// that string's fret stops it and it has to be strummed again. 0 = ringing
// mode, where notes keep sounding and follow the frets (PITCH_TRACKS_FRETS),
// shown by the master's left eye. Triple-tap the master's button to switch.
#define REALISTIC_STRUM 1
// A fret change must hold this long before it stops a note, so one glitchy
// frame cannot cut a ringing string.
#define FRET_CHANGE_GRACE_MS 30
// The master reads its strum pads after every STRUM_READ_EVERY fret badges as
// well as in its own slot: 2 gives 6 reads per cycle, ~3.5 ms apart, against
// one per ~17 ms cycle. A quick strum then lasts several reads instead of
// slipping between two, and registers in ~7 ms. Each extra read costs one
// scan (~0.45 ms at 4x) on the cycle. 1 = after every badge, 0 = only once.
#define STRUM_READ_EVERY 2
// Doubles every pitch per step, per instrument. The piezo resonates near 4 kHz
// and reproduces a true low E (82 Hz) poorly, so the guitar plays an octave up
// (164-1319 Hz). The ukulele's 262-880 Hz is already fine at true pitch.
#define GUITAR_OCTAVE_SHIFT 1
#define UKE_OCTAVE_SHIFT 0
// Ringing mode only (REALISTIC_STRUM 0): 1 = fretting a ringing string
// retunes it, giving hammer-ons and slides; 0 = pitch is latched at the strum.
#define PITCH_TRACKS_FRETS 1

// How long the bus stays silent after the master hands a badge the scan token
// (see scan_protocol.h). 0 = derive it: the master times its own 6-pad scan,
// takes the longest seen, and adds 25% plus SCAN_MARGIN_US for the fret badge
// to notice the token. Set a fixed value here only to override that.
#define SCAN_US 0
#define SCAN_MARGIN_US 250
// Between a command write and the read that answers it, after the STOP is on
// the wire: time for the fret badge's I2C interrupt to take the command in.
#define BUS_TURNAROUND_US 200
// The master's SERCOM SDA hold time (0 off, 1 ~75 ns, 2 ~450 ns, 3 ~600 ns)
// and inactivity timeout (0 off, else 5/10/20 SCL cycles). Runtime-settable
// for bus experiments: "set sdahold N", "set inactout N".
#define BUS_SDA_HOLD 2
#define BUS_INACTOUT 0
// kHz for the long command-then-read blocks (debug, OTA status); 0 = the same
// 100 kHz as everything else. These reads are not timing-critical, so they can
// trade speed for margin on a long, lightly pulled-up chain. 16..100.
#define BUS_READ_KHZ 0
// Missed tokens in a row before a fret badge counts as gone. Until then the
// master keeps its last good frame; after, it reads as nothing held.
#define STALE_SCANS 4

// 1 = the master logs every badge's pads whenever any of them changes, plus a
// stats line every LOG_STATS_MS with the cycle time, scan window and misses.
// Both are only the boot state: "log" and "stats" on the master's console
// switch them at runtime.
#define MASTER_LOG 1
#define LOG_STATS_MS 1000
// How often "watch" prints one badge's raw/baseline/delta.
#define WATCH_MS 50

// 1 = the master prints the resolved fret and pitch per string over serial,
// which is how you check a fret badge is being read correctly.
#define MASTER_DEBUG 0

// 1 = boot into the touch bench instead of the instrument, to sweep the analog
// settings on real hardware. See touch_diag.h. Never plays a note.
#define TOUCH_DIAG 0
// Long enough to outlast a tactile switch's bounce (a few ms), short enough
// that a quick triple tap is still three taps. The master also samples the
// button on every strum read, so it is not limited to once per cycle.
#define BUTTON_DEBOUNCE_MS 25
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
    // Octaves added on top of the table, so the tables stay true concert pitch.
    uint8_t octaveShift;
};

extern const Instrument kGuitar;
extern const Instrument kUkulele;

// Pitch for a string/fret pair, with the instrument's octaveShift applied.
// Out-of-range frets clamp rather than read past the table.
uint16_t freqForFret(const Instrument *inst, uint8_t stringIndex, uint8_t fret);

// Which string a badge sounds. Badges are handed out in consecutive runs, so a
// guitar gives {0,1} the low E and {2,3} the A, while a ukulele gives {0,1,2}
// the G and {3,4,5} the C. The modulo wraps any leftover badge back to string 0.
uint8_t stringIndexForBadge(const Instrument *inst, uint8_t badgeId);

// Collapses a raw 6-bit pad mask into a string mask of stringCount bits. Needed
// both for the master's own strum pads and for every fret badge's reported
// mask, since a ukulele's grouping runs the whole length of the neck.
uint8_t groupPads(const Instrument *inst, uint8_t padMask);
