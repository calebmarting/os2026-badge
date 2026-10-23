# Badge guitar: protocol, timing and tuning

The reference for the 12-badge guitar firmware in `src/`. For how to *play* it
(strumming, capo, ukulele mode, setting IDs) see [src/readme.md](src/readme.md).

- [Why one badge measures at a time](#why-one-badge-measures-at-a-time)
- [Timing](#timing)
- [Wire protocol](#wire-protocol)
- [Touch detection](#touch-detection)
- [Tunable constants](#tunable-constants)
- [Console commands](#console-commands)
- [Collecting touch logs](#collecting-touch-logs)
- [Updating the firmware](#updating-the-firmware)
- [Hardware assumptions](#hardware-assumptions)

## Why one badge measures at a time

Each badge's PTC is clocked from its own internal 8 MHz RC oscillator (GCLK3 =
OSC8M), so twelve badges measuring at once run on twelve slightly different,
drifting clocks. A hand resting across two badges couples one badge's charge
pulses into the other's measurement, and the phase drift between them shows up
as flicker and as deltas that go negative. Within one badge none of that happens,
because its PTC measures one pad at a time.

So the master (ID 0) hands out a **scan token**. Only the badge holding it
measures; everyone else keeps their pads **driven LOW**, which also makes them
extra ground for the hand. A fret badge never measures on its own, not even to
calibrate.

## Timing

One cycle:

```
| fret 1 | fret 2 | ... | fret 11 | master's slot                                   |
  CMD_SCAN -> silent window -> read frame       own scan, then strum logic, frequency
                                                writes, LEDs, serial, console, debug
```

- **Window.** After `CMD_SCAN` the bus stays silent for the scan window, then
  the master reads the frame. By default the window is derived: the master times
  its own 6-pad scan, takes the longest seen since the last calibration, and uses
  `1.25 x that + SCAN_MARGIN_US` (250 us). Every badge runs the same code with the
  same settings, so the master's own scan is a good stand-in. `set scanus N`
  fixes it instead.
- **Strum reads.** The fret badges are read once a cycle, but the master
  also reads its own strum pads after every `strumevery` (2) fret badges, so
  6 times a cycle and ~3.5 ms apart. Each read runs the strum logic straight
  away. It's still one PTC at a time: the extra reads sit between fret badges'
  windows. Only the read in the master's own slot may drift its baseline.
- **Where switching happens.** A fret badge updates its LEDs after its pads are
  back to LOW but *before* it publishes its frame, so the switching stays inside
  its own window. Frequency writes, the master's LEDs and all serial output
  happen in the master's slot, after its own scan.
- **Bus cost.** At 100 kHz each fret costs roughly 0.65 ms on the bus (a
  4-byte write plus a 2-byte read), so about 7 ms per cycle before any
  measuring. A missing badge NACKs its address and costs about 0.1 ms, with no
  window.
- **Your numbers.** I couldn't measure scan times without the hardware. The
  master prints a stats line every second. The figures below show the format
  only; they are made up:

  ```
  cycle avg 15230 us max 16010 us | scan 610 us window 1012 us | missed none | bad none | stale none
  ```

  `scan` is the master's own measured 6-pad scan and `window` is what each badge
  gets. Measured on the 12-badge neck: 16x gave a 1301 us scan and a 29.9 ms
  cycle; 4x (now the default) gives 436 us and 17.2 ms. About 725 us per badge
  is I2C, so this protocol cannot reach 10 ms at 100 kHz whatever the
  oversampling.

**Missed tokens.** A frame that doesn't arrive or fails its check counts as a
miss, and the master keeps that badge's last good state. After `STALE_SCANS` (4)
misses in a row the badge is *stale*: it reads as nothing held, so a dead fret
can never pin a note on. Its next good frame brings it back. Missed and bad
counts are in the stats line.

**Never blocking.** A badge that's absent NACKs and is skipped. A badge that
wedges SCL low trips the SERCOM's SCL-low timeout (25–35 ms), which ends the
transfer with an error. A bus left busy gets a 9-clock recovery, at most once a
second. The SERCOM's ~200 us inactivity timeout is deliberately off: it fired
whenever a USB interrupt delayed the master between bytes, cutting long reads
short.

## Wire protocol

Fret badge N answers at **I2C address 0x30 + N** (0x31–0x3B). The offset keeps
clear of the reserved 0x00–0x07 block and of the accelerometers at 0x18, which
the firmware never addresses. "GC" means general call (address 0x00, every badge
at once). The bus runs at 100 kHz. Multi-byte values are little-endian.

| Cmd | Name | Args | To | Effect |
|---|---|---|---|---|
| 0x51 | `CMD_SCAN` | flags, seq | badge | Measure now, then publish a frame |
| 0x52 | `CMD_FREQ` | u16 Hz | badge | Play this pitch, 0 = silent |
| 0x53 | `CMD_CAL` | seq | badge | Calibrate (an exclusive long token), then publish a frame |
| 0x54 | `CMD_PROMPT` | mode | GC | 0 off, 1 blink "hands off", 2 solid "calibrating" |
| 0x55 | `CMD_PARAM` | index, i16 | GC or badge | Set one runtime param (the table below) |
| 0x56 | `CMD_DEBUG` | – | badge | The next read returns the debug block |
| 0x60 | `CMD_OTA_BEGIN` | u32 size, u32 crc | GC | Stop playing, erase staging |
| 0x61 | `CMD_OTA_DATA` | u16 page, 64 bytes, crc8 | GC or badge | One page of the new image |
| 0x62 | `CMD_OTA_STATUS` | – | badge | The next read returns the OTA status block |
| 0x63 | `CMD_OTA_COMMIT` | u32 crc | badge | Install the verified image, reboot |
| 0x64 | `CMD_OTA_ABORT` | – | GC or badge | Drop the update, resume playing |

The command values avoid 0x00, 0x04 and 0x06, which general-call-aware devices
treat as reset and address-programming commands.

**`CMD_SCAN` flags:** bit 0 `ANY_TOUCH` means something on the neck read as held
last cycle; baselines only drift while it's clear. Bit 1 `UKULELE` is the current
instrument; it rides along with every token, so a badge that reboots is back on
the right instrument within a cycle. Bit 2 `ATTRACT` means attract mode: the
badge twinkles its LEDs at random instead of showing its pads, switching them
inside its own window like any other LED update. It still measures, so a touch
anywhere ends attract mode on the next cycle.

**Frame**, which a badge returns on a normal read (2 bytes):

```
byte 0   bits 0-5  pads 1-6 on (after hysteresis and debounce)
         bit 6     CALIBRATED - the badge has a baseline
         bit 7     CAPO       - its capo is on (pads still report the real hand)
byte 1   CRC-8 (poly 0x07, init 0xFF) over [badge address, token seq, byte 0]
```

The seq isn't sent back, only folded into the check. A frame left over from a
missed token was computed with the old seq, so it fails the check exactly like a
corrupted byte. An idle SAMD slave answers `FF FF`, which also fails, except for
a 1-in-256 coincidence. A badge that isn't calibrated measures nothing, sends
`CALIBRATED = 0`, and gets a `CMD_CAL` in the master's next slot. That's how a
badge that reboots mid-song recovers on its own.

**Debug block** (33 bytes, after `CMD_DEBUG`): 6 x u16 raw reading, 6 x u16
baseline, on mask, negative mask, u16 re-baseline count, u16 last scan us, u16
last calibration ms, then a CRC-8 seeded with the badge address.

**OTA status block** (8 bytes, after `CMD_OTA_STATUS`): state (0 idle,
1 erasing, 2 receiving, 3 verifying, 4 verified), u16 first missing page (0xFFFF
= none), u32 CRC-32 of the running firmware, then a CRC-8 seeded with the
address.

## Touch detection

Each badge processes its own pads locally (`src/touch.cpp`); only on/off bits
cross the bus.

1. **Calibration** (in its own token): a per-pad COMPCAP binary search parks the
   idle reading near `TOUCH_CAL_TARGET`, then `TOUCH_CAL_SAMPLES` scans are
   averaged into the baseline. It runs at boot, on a double tap of the master's
   button, on `cal`, and automatically for any badge that reports itself
   uncalibrated. Every badge first blinks for `CAL_LEAD_MS` (hands off), then
   lights solid while the badges calibrate one by one. Ringing notes are
   silenced first.
2. **Delta:** `d = (int32_t)raw - baseline`, signed throughout. The baseline is
   kept with 8 fractional bits.
3. **Hysteresis:** a pad turns on at `d > on` (35) and stays on until `d < off`
   (30). `off` sits just under `on`, not at half of it, because a released pad
   next to a held one still carries up to +30 of crosstalk.
4. **Negative readings** (`d < -neg`, 40, with the same hysteresis margin as
   the positive side once on), set by `negmode`:
   - `0`: never a touch.
   - `2` (default): always a touch, never re-baselines. A firm finger that
     spills onto the next pad reads strongly negative (-110 to -118 measured on
     a ukulele C-string finger), and with one badge measuring at a time,
     untouched pads sit within ±1, so a negative means a finger.
   - `1`: a touch only while an **adjacent** pad reads on. That's a
     finger bridging two pads, which drains the measured one negative. A
     negative with no such neighbour, held for `negrecal` (50) scans **while
     the whole neck reads idle**, means the baseline is wrong and the pad is
     re-baselined. Never during play: re-zeroing a pad that a hand pulled
     negative leaves it stuck on once the hand lifts. It drops a finger whose
     neighbour reads only partly on, which is why it is no longer the default.
5. **Debounce:** a pad changes state only after `debounce` (2) scans in a row
   agree.
6. **Drift:** `base += (raw - base) >> drift` (6, about a second), but only when
   `ANY_TOUCH` is clear *and* this badge has nothing on or pending. A hand
   anywhere freezes every baseline, so a held chord is never tracked away.
7. **Max on:** a pad held on for `maxon` seconds (30) is re-baselined. This is a
   safety net against a baseline captured under a finger. `set maxon 0` turns it
   off.

## Tunable constants

These are the defaults at the top of `src/guitar_config.h`. The "Console" column
is the name for `set NAME VALUE` on the master, which changes it on every badge
until power-off. Copy values you settle on back into the header. Settings marked
**R** trigger a recalibration when changed.

| Define | Console | Default | Meaning |
|---|---|---|---|
| `TOUCH_ON_THRESHOLD` | `on` | 35 | counts over baseline to turn on |
| `TOUCH_OFF_THRESHOLD` | `off` | 30 | counts over baseline to stay on |
| `TOUCH_NEG_THRESHOLD` | `neg` | 40 | counts under baseline that count as negative |
| `TOUCH_NEG_MODE` | `negmode` | 2 | 0 ignore, 1 neighbour-gated, 2 always a touch |
| `TOUCH_NEG_RECAL_SCANS` | `negrecal` | 50 | unexplained-negative scans before re-baselining |
| `TOUCH_DEBOUNCE_SCANS` | `debounce` | 2 | agreeing scans needed to change state |
| `TOUCH_DRIFT_SHIFT` | `drift` | 6 | idle baseline IIR shift |
| `TOUCH_MAX_ON_SECONDS` | `maxon` | 30 | re-baseline a pad held this long, 0 = never |
| `TOUCH_CAL_SAMPLES` | `calsamples` **R** | 32 | samples averaged per pad |
| `PTC_OVERSAMPLE` | `os` **R** | 2 (4x) | 2 = 4x, 3 = 8x, 4 = 16x, 5 = 32x. Measured cycle: ~17 ms at 4x, ~30 ms at 16x |
| `PTC_RESISTOR` | `res` **R** | 2 (50K) | 0 none, 1 20K, 2 50K, 3 100K |
| `PTC_FREQ_MODE` | `freq` **R** | 2 | 0 fixed frequency, anything else spread spectrum |
| `PTC_FREQ_HOP` | `hop` **R** | 0 | spread-spectrum sample delay, 0..15 |
| `PTC_HOP_PER_BADGE` | `hopstep` **R** | 0 | delay += this x badge ID, to stagger badges |
| `PTC_PAD_ISOLATE` | `isolate` **R** | 1 | only the measured pad is on the PTC, the other five held LOW. 0 = all six for the whole scan |
| `SCAN_US` | `scanus` | 0 | fixed scan window in us, 0 = derived |
| `STRUM_READ_EVERY` | `strumevery` | 2 | extra strum-pad read after every N fret badges, 0 = once a cycle |
| `MUTE_LOCKOUT_MS` | `mutelock` | 75 | after a mute is let go, a touch starting this soon is a bounce and cannot pluck |
| `REALISTIC_STRUM` | `realistic` | 1 | 1 = a fret change stops a ringing note; 0 = ringing mode (master's left eye lit). Triple-tap the master to switch |
| `FRET_CHANGE_GRACE_MS` | `fretgrace` | 30 | how long a fret change must last before it stops a note |
| `ATTRACT_IDLE_SECONDS` | `attract` | 10 | idle seconds before the LEDs twinkle, 0 = never |
| `ATTRACT_STEP_MS` | – | 150 | how often each badge reshuffles its LEDs |
| `ATTRACT_ON_PERCENT` | – | 40 | chance each LED is lit per reshuffle |
| `SCAN_MARGIN_US` | – | 250 | added to the derived window |
| `STALE_SCANS` | – | 4 | misses in a row before a badge reads as gone |
| `TOUCH_CAL_TARGET` | – | 120 | where COMPCAP parks the idle reading |
| `CAL_LEAD_MS` | – | 1500 | hands-off blink before calibrating |
| `DOUBLE_TAP_MS` | – | 350 | master tap window: 1 tap ukulele, 2 recalibrate, 3 strum mode |
| `GUITAR_OCTAVE_SHIFT` | – | 1 | guitar plays an octave above concert pitch |
| `UKE_OCTAVE_SHIFT` | – | 0 | ukulele at concert pitch |

Oversampling doesn't change the count scale (FreeTouch divides it back out), so
the thresholds stay valid. More oversampling means less noise and a
proportionally longer window.

About the spread-spectrum modes: in this library `FREQ_MODE_HOP`, `SPREAD` and
`SPREAD_MEDIAN` all behave the same. Any non-zero mode enables spread spectrum
with `hop` as the PTC sample delay. The vendored copy in `lib/Adafruit_FreeTouch`
differs from upstream 1.1.3 in two documented ways: its setters are public, and
`config.hops` is initialised.

## Console commands

Open the master's USB serial at 115200 (`pio device monitor`).

| Command | |
|---|---|
| `help` | the list |
| `log on\|off` | pad-change timeline (on at boot) |
| `stats on\|off` | the once-a-second cycle/miss line (on at boot) |
| `watch N` / `watch all` / `watch off` | raw/baseline/delta for badge N (0 = master), pulled over I2C; repeat `watch N` to add or remove badges |
| `cal` | recalibrate the whole neck (hands off) |
| `info` | which badges are present or calibrated, plus each one's firmware CRC |
| `id` / `id N` | show this badge's ID / make it ID N and reboot (works on every badge's console) |
| `params` | every tunable with its current value and range |
| `set NAME VALUE` | change one on every badge |
| `ota SIZE CRC` | used by the update tool, not typed by hand |

A fret badge's own USB console only knows `debug` (toggle raw/baseline/delta on
that badge's serial), `id` and `help`. A fret badge only measures when it gets the
token, so its debug output needs the master running. `watch` on the master shows
the same numbers without moving the cable.

## Collecting touch logs

With the cable on the master:

```
log on
watch 3          (the badge you're testing; add more with watch 4, ...)
```

The **timeline** prints one line per change, pad 1 leftmost:

```
48211 0:...... 1:...... 2:..11.. 3:...... 4:...... ...   <- fret 2, pads 3+4 held
```

The **watch** lines show what's behind it (made-up values):

```
dbg  3 raw/base/delta: 121/120/1 119/120/-1 178/120/58 61/120/-59 ...  on ..11.. neg ...1..  recal 0  scan 610us  cal 152ms
```

For the negative-delta question (decision B), the cases worth capturing:

1. A finger bridging two adjacent pads on one badge. Expect one pad positive,
   its neighbour negative, and both **on**.
2. A chord across several badges. With the scan token there should now be
   **no** negatives on pads nobody is touching. If there are, note which badges.
3. Releasing a bridged pair. Both should go off within a couple of scans, with
   `recal` unchanged.

If case 1 drops a fret, try `set negmode 2`. If phantom frets appear from
negatives, try `set negmode 0`. Either way, the logs show which.

## Updating the firmware

**Once, over USB.** Every badge needs this firmware flashed the normal way the
first time (`pio run -t upload`, double-tapping reset into the bootloader if
needed), because the old firmware doesn't know the update protocol. A USB upload
erases the stored ID (`bossac --erase` wipes all flash above the bootloader), so
**set each fret badge's ID once more** afterwards: with it still on USB, type
`id N` in its serial console. An unconfigured badge comes
up as the master, so the master itself needs nothing.

**After that, over I2C.** With only the master on USB:

```
pio run -t ota          # build, then update all 12 badges
```

or `python tools/ota_upload.py .pio/build/mkrzero/firmware.bin [--port COMx]`.
The master stages the image and checks its CRC-32. It sends every page to all
eleven badges at once, then re-sends only missing pages per badge until each
one's CRC-32 matches. Then the badges install, and the master installs itself
last. 30–40 KB takes roughly 10–15 seconds in all (an estimate; nothing measured
yet). IDs are kept because they live in the last flash row, outside the image.
Afterwards, `info` on the master should show the same firmware CRC on every
badge.

**If something goes wrong:** a badge that fails verification is told to abort
and keeps its old firmware; the update tool and `info` both report it. If power
drops during the final copy, that badge's app is half-written. The bootloader is
never touched, so double-tap reset and flash it over USB. Each master boot
general-calls an abort, which frees any badge left waiting mid-update.

Flash map: bootloader `0x0000`–`0x1FFF`, firmware `0x2000`–`0x1FFFF` (at most
120 KB, checked at build time), staging `0x20000`–`0x3FEFF`, badge config
`0x3FF00`.

## Hardware assumptions

These come from the schematics and the code. I couldn't check them on the
badges themselves:

- All six SAO pins are bussed through the chain. The firmware only needs SDA,
  SCL, 3V3 and GND.
- The USB-powered badge is ID 0, so its serial port is the one with the logs and
  the update path.
- I2C pull-ups: each T-piece has 5.1 kΩ on SDA (R14) and SCL (R9). With only the
  master's kept, the bus has a single 5.1 kΩ pair. That's within spec for a
  short, lightly loaded bus. If `bad` frames show up in the stats line as more
  badges are added, slow edges are the first suspect: a second pull-up pair
  (2.55 kΩ total) is the fix.
- The accelerometers sit at 0x18 on every badge and are never addressed.
- The piezo keeps running through scan windows (muting it per scan would chop
  the note). It's on PA02, a PTC-capable pin beside the pad pins, so if a ringing
  note shows up in the `watch` deltas, that's the cause.
