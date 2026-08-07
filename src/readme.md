# Badge Guitar

Chains up to 13 badges over the SAO I2C ports into one playable guitar - or a
ukulele, at the press of a button. The same firmware image goes on every badge;
each badge is given an ID that decides its role.

> The original Bop It game that shipped on the badge now lives in `examples/original/`.

## Roles

| ID | Role |
|---|---|
| 0 | **Master.** Bus controller. Its 6 touch pads are the strings - pad 1 is the low E. |
| 1-12 | **Fret badges.** Badge N is fret N. Its pads report which strings are held at that fret. |

Every badge is also a speaker. Badges pair up by ID to share a string:

| String | Badge IDs |
|---|---|
| 1 - low E | 0, 1, 12 |
| 2 - A | 2, 3 |
| 3 - D | 4, 5 |
| 4 - G | 6, 7 |
| 5 - B | 8, 9 |
| 6 - high E | 10, 11 |

Lay the fret badges out in a row and you have a neck. The master resolves the
**highest** fret held per string, the way a real fretboard works, and writes the
resulting pitch out to the badges that sound that string. Standard tuning,
E2 A2 D3 G3 B3 E4, 12 frets.

## Ukulele mode

**Short-press the button on the master** to toggle it. The forehead LED is lit
while ukulele mode is active. The mode is runtime-only - the badges always boot
back into guitar mode, which keeps toggling off the flash entirely.

A ukulele has 4 strings, so **pads 2 and 5 are switched off** and the remaining
four carry the strings. A finger on a disabled pad does nothing at all - it is
masked out of what a badge reports, not quietly folded into a neighbour.

The master broadcasts the mode to every fret badge, so the whole neck changes
together:

```
pads:    1   2   3   4   5   6
         |   x   |   |   x   |
string:  1       2   3       4
         G       C   E       A

slider:  1   2   3   4   5
         G   C   -   E   A
```

Standard reentrant GCEA tuning - the G is pitched *above* the C and E, not below,
which is what gives a ukulele its sound:

| String | Pad | Slider LED | Open | Fret 12 | Badge IDs |
|---|---|---|---|---|---|
| 1 - G | 1 | 1 | G4 392 Hz | 784 Hz | 0, 1, 2, 12 |
| 2 - C | 3 | 2 | C4 262 Hz | 523 Hz | 3, 4, 5 |
| 3 - E | 4 | 4 | E4 330 Hz | 659 Hz | 6, 7, 8 |
| 4 - A | 6 | 5 | A4 440 Hz | 880 Hz | 9, 10, 11 |

Slider LED 3 stays dark, and every badge on the neck shows this same layout -
not just the master. Badges group in threes rather than pairs, so each ukulele
string is louder than a guitar string. Everything else - muting, strumming,
sustain, hammer-ons - behaves identically.

> The whole ukulele range is 262-880 Hz, comfortably inside what the piezo
> reproduces well. If the guitar's low E turns out too quiet to hear, ukulele
> mode will still sound correct regardless of `OCTAVE_SHIFT`.

## Capo

**Short-press the button on any fret badge** to clamp a capo there. Its whole
slider lights up, and it stops reading its pads entirely - it just reports every
string as held, which is exactly what a capo does.

Because the master always takes the *highest* fret per string, this falls out
correctly with no special handling:

| | Result |
|---|---|
| Capo at 3, nothing else | every string plays fret 3 |
| Capo at 3, finger at fret 5 | that string plays fret 5, the rest play 3 |
| Capo at 5, finger at fret 2 | fret 5 - below the capo is inaudible, as on a real neck |

In ukulele mode a capo still respects the disabled pads, so it holds the 4
strings rather than resurrecting pads 2 and 5.

Press the button again to release it. The capo is runtime-only and clears on a
power cycle. The master cannot take a capo - it is not a fret, and its short
press is the instrument toggle.

## Playing

Touching a strum pad **mutes** that string straight away, so resting a finger on
it damps the note. What happens when you let go depends on how long you held it:

- **Quick tap** (up to `STRUM_MAX_HOLD_MS`, default 250 ms) - plucks the string.
- **Hold longer** - it stays muted. This is your palm mute.

A note rings for `SUSTAIN_MS` (default 3 s) and then silences itself. Moving your
fingers while a note is ringing retunes it, which gives you hammer-ons and slides.

LEDs, on every badge:

| LED | Shows |
|---|---|
| Slider 1-5 | pads 1-5 (in ukulele mode, the 4 grouped strings instead) |
| `LED_BUILTIN` | pad 6 |
| Right eye | this badge is sounding a note |
| Forehead | **only** on the master, and **only** in ukulele mode |

## Setting a badge's ID

1. **Long-press** the button (1 s). `LED_BUILTIN` starts blinking to show you are
   in setup, and the slider LEDs show the current ID in binary the way you would
   write it down - most significant bit on the **left**, ones place on the right.

   ```
   ID 5  ->  [ ][ ][*][ ][*]      ID 12 ->  [ ][*][*][ ][ ]
             16  8  4  2  1                 16  8  4  2  1
   ```

   The leftmost LED is the 16s place, so it stays dark for every valid ID.
2. **Short tap** to increment, wrapping 0 -> 12 -> 0.
3. **Long-press** again to save. The LEDs blink three times and the badge reboots
   into its new role.

The ID lives in flash and survives a power cycle. Doing nothing for 30 s leaves
setup without saving. A badge that has never been configured comes up as the master.

Outside setup mode, a **short** press does something different depending on the
badge: on the master it toggles ukulele mode, on a fret badge it toggles a capo.

## Wiring

Bus the badges together over the SAO ports: SDA (D11), SCL (D12), plus GND and 3V3.

Three things will bite you otherwise:

- **Lift the I2C pull-ups on all but two badges.** Each badge carries 5.1 k on SDA
  and SCL. Thirteen in parallel is about 390 ohms, which needs ~7.4 mA of sink per
  pin against a 3 mA spec. The symptom is polls returning garbage as you add badges.
- **Only one badge may be ID 0.** Two controllers on one bus will collide.
- **The firmware never touches 0x18.** Every badge has an accelerometer at that
  address, so a chained neck has 13 devices answering it. That is also why the
  guitar does not use the accelerometer at all.

The bus stays at the default 100 kHz on purpose - with 13 badges plus cabling the
capacitance is near the 400 pF limit and 400 kHz will not make the edges.

## Protocol

Master reads 1 byte from each fret badge:

```
bit 0..5 = pad 1..6 held (pad 1 = low E)
```

In ukulele mode a badge masks pads 2 and 5 out of that byte, so those bits always
read 0 and a touch there is genuinely ignored.

Writes are told apart by **length**. One byte is the instrument:

```
0 = guitar    1 = ukulele
```

Two bytes are a frequency in Hz, little-endian, 0 to silence:

```
byte 0 = freq & 0xFF
byte 1 = freq >> 8
```

The master re-sends the mode once a second. A badge that browns out and restarts
still answers polls, so the master never sees it drop - but it comes back up in
guitar mode, and without the refresh it would sit there reading the wrong pads
until the next time you toggled.

## Tuning the feel

Everything adjustable is at the top of `guitar_config.h`:

| Define | Default | |
|---|---|---|
| `SUSTAIN_MS` | 3000 | how long a note rings |
| `STRUM_MAX_HOLD_MS` | 250 | longer hold = mute instead of strum |
| `OCTAVE_SHIFT` | 0 | **set to 1 if the low strings are inaudible** |
| `PITCH_TRACKS_FRETS` | 1 | 0 latches pitch at the strum, losing hammer-ons |
| `TOUCH_THRESHOLD` | 200 | raise if pads trigger on their own, lower if touches are missed |
| `TOUCH_CAL_SAMPLES` | 16 | samples averaged per pad at boot |
| `MASTER_DEBUG` | 0 | 1 prints resolved frets and pitches over serial |
| `TOUCH_DEBUG` | 0 | 1 prints raw / baseline / delta per pad, plus the touch mask |

## Touch pads

Each pad is calibrated once at boot and then compared against that fixed
reference forever. **Keep your hands off the slider while a badge powers up** -
whatever is being touched during calibration becomes that pad's idea of "not
touched". Power-cycle to recalibrate.

There is deliberately no adaptive baseline. It is the standard approach for
capacitive touch, but it is wrong for an instrument: any tracking fast enough to
follow thermal drift is also fast enough to creep up under a held chord and slide
your fingers back out of the detection window. A fixed reference means a pad
stays detected for exactly as long as you hold it.

Pads are fully independent, so press as many at once as you like - barre chords
and multi-string mutes all work.

To pick a threshold, set `TOUCH_DEBUG 1` and watch the deltas over serial:

```
pad raw/base/delta: 812/640/172 655/641/14 ...  mask 000001
```

Put `TOUCH_THRESHOLD` between what an untouched pad idles at and what a real
touch produces. Check the deltas hold up when you press several pads at once -
if they shrink noticeably as you add fingers, the pads are coupling to each other
electrically and the threshold needs to sit lower than the single-touch numbers
suggest.

`OCTAVE_SHIFT` is the one most likely to need changing. The piezo resonates near
4 kHz and reproduces an 82 Hz low E poorly, so true pitch may be very quiet down
there. Setting it to 1 moves everything up an octave.

## Building

```
pio run              # build
pio run -t upload    # flash the badge that is plugged in
```

Double-tap the small reset button in the centre of the T piece to enter the
bootloader first. Flash the master last so it is the badge left on USB for
serial debugging at 115200.
