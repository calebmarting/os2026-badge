# Badge Guitar

Chains 12 badges over the SAO I2C ports into one playable guitar - or a
ukulele, at the press of a button. The same firmware image goes on every badge;
each badge is given an ID that decides its role.

> The original Bop It game that shipped on the badge now lives in `examples/original/`.

## Roles

| ID | Role |
|---|---|
| 0 | **Master.** Bus controller. Its 6 touch pads are the strings - pad 1 is the low E. |
| 1-11 | **Fret badges.** Badge N is fret N. Its pads report which strings are held at that fret. |

Every badge is also a speaker. Badges pair up by ID to share a string:

| String | Badge IDs |
|---|---|
| 1 - low E | 0, 1 |
| 2 - A | 2, 3 |
| 3 - D | 4, 5 |
| 4 - G | 6, 7 |
| 5 - B | 8, 9 |
| 6 - high E | 10, 11 |

Lay the fret badges out in a row and you have a neck. The master resolves the
**highest** fret held per string, the way a real fretboard works, and writes the
resulting pitch out to the badges that sound that string. Standard tuning,
E2 A2 D3 G3 B3 E4, 11 frets - played **an octave up** (E3 to E5) because the
piezo cannot make a true 82 Hz low E audible.

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

| String | Pad | Slider LED | Open | Fret 11 | Badge IDs |
|---|---|---|---|---|---|
| 1 - G | 1 | 1 | G4 392 Hz | 740 Hz | 0, 1, 2 |
| 2 - C | 3 | 2 | C4 262 Hz | 494 Hz | 3, 4, 5 |
| 3 - E | 4 | 4 | E4 330 Hz | 622 Hz | 6, 7, 8 |
| 4 - A | 6 | 5 | A4 440 Hz | 831 Hz | 9, 10, 11 |

Slider LED 3 stays dark, and every badge on the neck shows this same layout -
not just the master. Badges group in threes rather than pairs, so each ukulele
string is louder than a guitar string. Everything else - muting, strumming,
sustain, hammer-ons - behaves identically.

> The ukulele plays at true pitch: its whole range is 262-831 Hz, comfortably
> inside what the piezo reproduces well. Only the guitar is shifted up an
> octave (`GUITAR_OCTAVE_SHIFT` / `UKE_OCTAVE_SHIFT`).

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
| Everything, fast blink | calibrating soon - hands off the neck |
| Everything, solid | calibrating, one badge at a time |
| Slider filling up | receiving a firmware update |
| Everything twinkling at random | attract mode: nothing touched for 10 s. Touch anything, or press a button, to play |

## Setting a badge's ID

1. **Long-press** the button (1 s). `LED_BUILTIN` starts blinking to show you are
   in setup, and the slider LEDs show the current ID in binary the way you would
   write it down - most significant bit on the **left**, ones place on the right.

   ```
   ID 5  ->  [ ][ ][*][ ][*]      ID 11 ->  [ ][*][ ][*][*]
             16  8  4  2  1                 16  8  4  2  1
   ```

   The leftmost LED is the 16s place, so it stays dark for every valid ID.
2. **Short tap** to increment, wrapping 0 -> 11 -> 0.
3. **Long-press** again to save. The LEDs blink three times and the badge reboots
   into its new role.

The ID lives in the last row of flash, outside the firmware, so it survives a
power cycle and an I2C firmware update - but not a USB upload, which erases all
of flash. Doing nothing for 30 s leaves setup without saving. A badge that has
never been configured comes up as the master.

Outside setup mode, a **short** press does something different depending on the
badge: on the master it toggles ukulele mode (and a double tap recalibrates), on
a fret badge it toggles a capo.

## Wiring

Bus the badges together over the SAO ports: SDA (D11), SCL (D12), plus GND and 3V3.

- **Keep the I2C pull-ups on one badge only** (or two). Each badge carries 5.1 k on
  SDA and SCL; twelve in parallel is about 425 ohms, far more sink current than
  the pins are rated for. This build keeps them on the master.
- **Only one badge may be ID 0.** Two controllers on one bus will collide - and a
  badge fresh off a USB upload comes up as ID 0 until you set it.
- **The firmware never touches 0x18.** Every badge has an accelerometer at that
  address, which is why fret badges answer at 0x30 + ID instead, and why the
  guitar does not use the accelerometer at all.

The bus runs at 100 kHz - with 12 badges plus cabling the capacitance is near the
400 pF limit and 400 kHz will not make the edges.

## Calibration

Every badge calibrates at power-up, one badge at a time: the whole neck **blinks
fast - take your hands off** - then lights solid while each badge in turn
measures its idle pads. Whatever is touched during calibration becomes that pad's
idea of "not touched".

**Double-tap the master's button** to recalibrate whenever the neck starts acting
up (a single tap is the ukulele toggle, so it now lands a moment later). A badge
that reboots on its own - a brown-out, or plugged in late - is recalibrated
automatically when it reappears.

## Protocol, timing and tuning

See [GUITAR.md](../GUITAR.md): the scan token, the wire protocol and frame format,
the timing, every tunable constant, the master's serial console (live touch logs,
raw/baseline/delta for any badge, changing settings without a reflash), and the
firmware update path.

The playing-feel settings stay in `guitar_config.h`:

| Define | Default | |
|---|---|---|
| `SUSTAIN_MS` | 3000 | how long a note rings |
| `STRUM_MAX_HOLD_MS` | 250 | longer hold = mute instead of strum |
| `GUITAR_OCTAVE_SHIFT` | 1 | guitar plays an octave above concert pitch |
| `UKE_OCTAVE_SHIFT` | 0 | ukulele plays at concert pitch |
| `PITCH_TRACKS_FRETS` | 1 | 0 latches pitch at the strum, losing hammer-ons |
| `MASTER_DEBUG` | 0 | 1 prints resolved frets and pitches over serial |
| `TOUCH_DIAG` | 0 | 1 boots the touch bench instead of the instrument |

`GUITAR_OCTAVE_SHIFT` defaults to 1 because the piezo resonates near 4 kHz and
reproduces an 82 Hz low E poorly. Set it to 2 for brighter still, or 0 for
concert pitch.

## Touch pads

### COMPCAP calibration

Each pad carries a standing parasitic capacitance that idles it at **400-680 of
a ~1016 full scale**, so 40-67% of the range is gone before a finger arrives.
One touch then pegs the ceiling, and a second touch pushes the conversion out of
range entirely - both readings collapse and *neither* crosses the threshold.
That is what made chords and barres impossible.

Calibration binary-searches the PTC's compensation capacitor per pad to park
every idle reading near `TOUCH_CAL_TARGET` (120), which restores ~900 counts of
headroom. Measured before and after on one badge:

| | Before | After |
|---|---|---|
| Idle baseline | 402-681, uneven | 120 on all six |
| Single touch | pegged at 1016 | 167-900, unclipped |
| Two adjacent pads | collapsed, mask `000000` | both detected |

> This needs `setCompCap()`, which is private upstream, so `lib/Adafruit_FreeTouch`
> is a lightly forked copy. Its header documents the changes. The upstream package
> is deliberately absent from `platformio.ini` - two copies of `adafruit_ptc.c`
> would collide at link time.

### Baseline and threshold

The single-badge numbers the thresholds were picked from, measured before the
scan token existed:

| | Crosstalk from an untouched neighbour | Real touch |
|---|---|---|
| Positive | up to **+30** | as weak as **+36** |
| Negative | none from the neighbour alone | **-56 to -110** on a pad bridged to a held neighbour |

A pad turns on above 35 and off below 30, needs two agreeing scans to change, and
its baseline only drifts while nothing anywhere on the neck is held. A negative
reading counts as a touch only while an adjacent pad is held - the bridged-finger
case above - and otherwise means the baseline is wrong, so it gets re-zeroed.
[GUITAR.md](../GUITAR.md#touch-detection) has the full rules and the knobs.

### Re-tuning

On the master's console, `watch N` streams raw/baseline/delta for badge N and
`set` changes a threshold on every badge at once - no reflash. For anything deeper
set `TOUCH_DIAG 1`, which boots one badge into a bench instead of the instrument:
short-press steps through acquisition configs, long-press sweeps COMPCAP and
INTCAP and then recalibrates. It tracks the largest positive **and negative**
excursion per pad.

Already ruled out by measurement on a single badge: the **series resistor does
almost nothing here** (R0 and R50K baselines differ by ~1 count).

## Building and updating

```
pio run              # build
pio run -t upload    # flash the one badge plugged in over USB
pio run -t ota       # update ALL badges through the master (see GUITAR.md)
```

The first time, every badge has to be flashed over USB - double-tap the small
reset button in the centre of the T piece to enter the bootloader - and then have
its ID set again, since a USB upload erases it. From then on, plug in only the
master and run `pio run -t ota`: it updates the whole neck over I2C, and every
badge keeps its ID.
