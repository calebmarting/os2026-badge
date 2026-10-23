# Task: Fix cross-badge touch flicker in the 12-badge guitar firmware

## Context (read first)

This repo is the firmware for the Open Sauce 2026 badge (SAMD21, effectively an Arduino MKR Zero). I've built a guitar from 12 of these badges, chained through the SAO port so they share 3V3, GND, SDA and SCL. One badge is powered over USB-C and feeds 3V3 to the others through the SAO.

- Each badge has 6 capacitive touch pads (the "teeth"), read by the SAMD21 PTC.
- One badge = one fret (or the open strings). Chords mean touching several badges at once.
- All badges run the same firmware. Each gets its I2C address from a configuration mode; **address 0 is the master**, which collects touch state from the others over I2C.

**Symptom:** several pads touched on the *same* badge read fine. Touching pads on *several badges* at once makes readings flicker, and deltas go negative or nonsensical.

**Working theory:** all 12 PTCs measure at the same time, unsynchronized. The player's hand bridges pads on different badges, so one badge's charge pulses couple into another's measurement. Phase drift between the independent clocks produces the flicker and negative deltas. Within one badge it's fine because the PTC measures one pad at a time and holds the other pads at ground.

Secondary suspects: weak baseline/drift logic, raw signed values corrupted or misinterpreted over I2C, and touch-correlated LED switching sagging the shared 3V3 rail.

## Step 1: Investigate before changing anything

Read the codebase and report back to me **before editing**:

1. Which touch library is used (Adafruit_FreeTouch, Arduino QTouch, custom PTC code), and with what settings: oversampling, series resistor, frequency mode.
2. How the baseline and threshold are computed and updated. Is the baseline captured at boot? Does it drift? Is the math signed?
3. The I2C protocol between master and slaves: what is sent (raw values, deltas, bits?), data types, byte order, bus speed, and whether `Wire.onRequest` / `Wire.onReceive` handlers do heavy work.
4. Whether anything else shares the I2C bus (e.g. the accelerometer) and its address. Flag any collision with the badge address range (0–11 or whatever range the config mode allows).
5. When the LEDs update relative to touch measurement.
6. Whether a sleep/game-over timer could put badges to sleep mid-play.
7. Whether the board definition / schematic in the repo shows I2C pull-up resistors and their values (tell me what you find; I'll handle hardware).

Summarize the findings and propose a plan mapped to Step 2. Wait for my OK.

## Step 2: Firmware changes (in this order, each in its own commit)

### 2a. Time-division scanning (the main fix)

Only one badge's PTC may measure at any moment.

- **Master (addr 0):** loop over addresses 1–11. For each: send `CMD_SCAN`, wait a fixed `SCAN_US` (measured, see below) with no other bus traffic, then read back a 2-byte frame. Scan its own pads in its own slot. Repeat.
- **Slaves:** never measure on their own. On `CMD_SCAN` (set a volatile flag in `onReceive`, do the work in `loop()`):
  1. restore PTC pin mux (e.g. re-run `begin()` for each FreeTouch object, or the equivalent)
  2. measure all 6 pads
  3. set pads back to `OUTPUT` and drive them LOW
  4. update touch bits (2b)
- When idle, pads stay driven LOW so they act as ground for the player's hand.
- No LED updates and no I2C activity during a badge's own measurement window.
- Instrument one full 6-pad scan with `micros()`, print it over serial, and set `SCAN_US` with margin. Target: a full 12-badge cycle ≲ 10 ms. Tell me the measured numbers.
- If a slave misses a token, the master marks it stale and keeps going. The loop must never block on a dead badge (use timeouts).

### 2b. Robust baseline and detection (on each badge, locally)

- **Boot calibration:** the master broadcasts `CMD_CAL` (general call or loop over addresses) after startup and on demand (e.g. a button combo). Each badge averages 32–64 samples per pad. Blink an LED as a "hands off" prompt during calibration.
- **Signed math everywhere:** `int32_t d = (int32_t)raw - baseline;`
- **Drift only when globally idle:** the master includes an "any touch active" flag in each `CMD_SCAN`. Slaves update the baseline only when that flag is clear and their own pads are untouched, with a slow IIR: `base += (raw - base) >> 6` (fixed-point is fine).
- **Negative recalibration:** if `d < -NEG_LIMIT` for N consecutive scans, re-baseline that pad.
- **Hysteresis + debounce:** ON when `d > T_on`, OFF when `d < T_off` (≈ T_on/2). A state change needs 2–3 consecutive agreeing scans.
- Keep thresholds as named constants at the top of a config header so I can tune them.

### 2c. Send booleans over I2C, not raw values

- Slave response frame: `[touchBits (6 bits) | status flags, checksum]`. Use a real check (e.g. CRC-8, or at least XOR with a constant).
- The master discards frames that fail the check and keeps the previous state.
- Add an optional debug mode (compile flag) where a badge prints its raw/baseline/delta values over **its own** USB serial for tuning.
- Set the I2C clock to 100 kHz explicitly.

### 2d. PTC tuning knobs

Expose these as config constants and start with:

- oversampling: 16 or 32 (up from 4), if the scan-time budget allows
- series resistor: 50K or 100K internal
- frequency mode: `FREQ_MODE_SPREAD`, or hopping

Note: in Adafruit_FreeTouch, `setFreqHopping()` is private and the hop/sample-delay value isn't set by the constructor. If you want per-badge sample delays (e.g. based on address), vendor a patched copy of the library in the repo rather than editing the global install, and document it.

### 2e. Housekeeping

- Make sure sleep/idle timeouts can't kick in while in guitar mode.
- Update LEDs only between scan windows, never during a measurement.

## Constraints

- Keep the existing config-mode address setup working. Address 0 stays the master.
- Every badge runs the same binary. Behavior branches on address only.
- Don't break the stock badge features unless they conflict with guitar mode. If they conflict, put guitar mode behind a flag or mode switch.
- Make sure the project still builds with the repo's existing toolchain (PlatformIO/Arduino) after each step, and run the build to confirm.
- Explain any assumptions you had to make about hardware you can't see.

## Done when

- Builds cleanly.
- I can flash all badges and get a serial log from the master showing per-badge touch bits and the cycle time.
- Debug mode on any single badge shows raw/baseline/delta for tuning.
- A short `GUITAR.md` documents the protocol (commands, frame format), timing, and tunable constants.

## Hardware notes (for me, not for you to change)

I'll do these myself. Just mention them if the code findings make any of them more urgent:

- A GND-connected metal strip under my hand while playing (direct contact, not an ESD wrist strap).
- Remove extra I2C pull-ups so only 1–2 badges keep them; aim for ~2.2–4.7 kΩ total.
- Heavier 3V3/GND wiring, or feed 5 V to each badge's own regulator, plus 10–47 µF bulk capacitance per badge.
