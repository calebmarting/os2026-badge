#include "master.h"
#include "badge_hw.h"
#include "guitar_config.h"
#include "scan_protocol.h"
#include "touch.h"

#include <Wire.h>

// Which instrument is being played. Runtime only - it always boots to guitar,
// so toggling never costs a flash page erase.
static const Instrument *inst = &kGuitar;

// What the master knows about one fret badge.
struct FretLink
{
    // Pads 1..6 as last measured, from the last frame that checked out. A bad
    // or missing frame leaves this alone, so one glitch cannot drop a fret.
    uint8_t pads;
    bool capo;
    // The badge says it has a baseline. A fresh badge without one gets a
    // CMD_CAL in the master's next slot.
    bool calibrated;
    uint32_t lastCalMs;
    // Answered within the last STALE_SCANS tokens. A stale badge reads as
    // nothing held, so a dead fret can never pin a note on.
    bool fresh;
    uint8_t misses;      // consecutive, resets on a good frame
    uint16_t missesLog;  // since the last stats line
};

static FretLink frets[MAX_BADGES];
static uint8_t scanSeq = 0;

// Longest 6-pad scan the master has timed on itself. Every badge runs the same
// firmware with the same settings, so it is the best available estimate of how
// long a fret badge needs - and it follows any change to the acquisition
// settings without anyone re-measuring.
static uint32_t ownScanMaxUs = 0;
// How long the master's own calibration took. Same reasoning: it sizes the
// exclusive window each fret badge gets for its calibration.
static uint32_t ownCalUs = 0;

// Anything on the neck read as held last cycle. Rides out with every token so
// no badge drifts its baseline while a hand is anywhere on the instrument.
static bool anyTouch = false;

static uint8_t fretForString[NUM_STRINGS];
static uint16_t stringFreq[NUM_STRINGS]; // currently sounding, 0 = silent
static uint32_t stringStartMs[NUM_STRINGS];

static uint32_t padPressMs[NUM_STRINGS];
static bool padWasDown[NUM_STRINGS];

// Cycle timing, reset every stats line.
static uint32_t cycleSumUs = 0;
static uint32_t cycleMaxUs = 0;
static uint32_t cycleCount = 0;
static uint32_t lastStatsMs = 0;
static uint32_t lastRecoverMs = 0;

static void setStringFreq(uint8_t s, uint16_t freq);

// How long the bus stays silent after handing a badge the token.
static uint32_t scanWindowUs()
{
#if SCAN_US > 0
    return SCAN_US;
#else
    return ownScanMaxUs + ownScanMaxUs / 4 + SCAN_MARGIN_US;
#endif
}

// SERCOM2 is the MKR Zero's Wire port. The core's master code busy-waits on
// the bus with no timeout, so a badge that wedges SCL low would hang the whole
// instrument. LOWTOUT aborts any transfer where SCL is held low for 25-35 ms -
// the hardware then sets the very flags those loops wait on, so they fall
// through with an error. INACTOUT returns a bus left "busy" by a glitch to idle.
// Both live in CTRLA, which is only writable with the peripheral disabled.
static void enableBusTimeouts()
{
    SercomI2cm &i2c = SERCOM2->I2CM;
    i2c.CTRLA.bit.ENABLE = 0;
    while (i2c.SYNCBUSY.bit.ENABLE)
    {
    }
    i2c.CTRLA.bit.LOWTOUTEN = 1;
    i2c.CTRLA.bit.INACTOUT = 3; // 20-21 SCL cycles, ~200 us at 100 kHz
    i2c.CTRLA.bit.ENABLE = 1;
    while (i2c.SYNCBUSY.bit.ENABLE)
    {
    }
    // Same as Wire.begin(): the bus state starts unknown until forced idle.
    i2c.STATUS.bit.BUSSTATE = 1;
    while (i2c.SYNCBUSY.bit.SYSOP)
    {
    }
}

static void busBegin()
{
    Wire.begin();
    enableBusTimeouts();
}

// A badge reset mid-byte can be left holding SDA low, waiting for clocks that
// will never come. Nine clocks walk it out of any byte, then a STOP frees the
// bus. Pins are driven open-drain style: LOW, or released to the pull-up.
static void busRecover()
{
    Wire.end();
    pinMode(PIN_WIRE_SDA, INPUT);
    pinMode(PIN_WIRE_SCL, INPUT);
    for (uint8_t i = 0; i < 9 && digitalRead(PIN_WIRE_SDA) == LOW; i++)
    {
        digitalWrite(PIN_WIRE_SCL, LOW);
        pinMode(PIN_WIRE_SCL, OUTPUT);
        delayMicroseconds(5);
        pinMode(PIN_WIRE_SCL, INPUT);
        delayMicroseconds(5);
    }
    digitalWrite(PIN_WIRE_SDA, LOW);
    pinMode(PIN_WIRE_SDA, OUTPUT);
    delayMicroseconds(5);
    pinMode(PIN_WIRE_SDA, INPUT);

    busBegin();
    Serial.println("I2C bus recovered");
}

static void writeFreq(uint8_t id, uint16_t freq)
{
    Wire.beginTransmission(wireAddress(id));
    Wire.write(CMD_FREQ);
    Wire.write((uint8_t)(freq & 0xFF));
    Wire.write((uint8_t)(freq >> 8));
    // A badge that is not listening falls out through the scan path, which is
    // the one place staleness is decided.
    Wire.endTransmission();
}

static void generalCall(uint8_t cmd, uint8_t arg)
{
    Wire.beginTransmission(GENERAL_CALL_ADDR);
    Wire.write(cmd);
    Wire.write(arg);
    Wire.endTransmission();
}

static void printPads(uint8_t pads)
{
    for (uint8_t i = 0; i < NUM_PADS; i++)
    {
        Serial.print((pads & (1 << i)) ? '1' : '.');
    }
}

// One line per change, so a touch log reads as a timeline: every badge's pads,
// pad 1 leftmost. "------" is a stale badge, a trailing 'c' a capo.
static void logPads()
{
#if MASTER_LOG
    Serial.print(millis());
    Serial.print(" 0:");
    printPads(touchMask());
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        Serial.print(' ');
        Serial.print(id);
        Serial.print(':');
        if (frets[id].fresh)
        {
            printPads(frets[id].pads);
            Serial.print(frets[id].capo ? 'c' : ' ');
        }
        else
        {
            Serial.print("------ ");
        }
    }
    Serial.println();
#endif
}

static bool noteMiss(uint8_t id)
{
    FretLink &f = frets[id];
    if (f.misses < 255)
    {
        f.misses++;
    }
    f.missesLog++;

    if (f.fresh && f.misses >= STALE_SCANS)
    {
        f.fresh = false;
        f.pads = 0;
        f.capo = false;
        Serial.print("Fret ");
        Serial.print(id);
        Serial.println(" stale");
        return true;
    }
    return false;
}

// Returns true when anything the touch log shows has changed.
static bool noteFrame(uint8_t id, uint8_t b0)
{
    FretLink &f = frets[id];
    uint8_t pads = b0 & FRAME_PAD_BITS;
    bool capo = (b0 & FRAME_STATUS_CAPO) != 0;
    bool changed = !f.fresh || pads != f.pads || capo != f.capo;

    f.misses = 0;
    f.pads = pads;
    f.capo = capo;
    f.calibrated = (b0 & FRAME_STATUS_CALIBRATED) != 0;

    if (!f.fresh)
    {
        f.fresh = true;
        Serial.print("Fret ");
        Serial.print(id);
        Serial.println(" joined");
        // Bring the newcomer in step with whatever its string is doing. The
        // instrument needs nothing: it rides along with every token.
        writeFreq(id, stringFreq[stringIndexForBadge(inst, id)]);
    }
    return changed;
}

// One badge's turn with the token. Nothing else happens on the bus between the
// CMD_SCAN and the read, and no other badge measures, so this badge's scan is
// the only charge moving anywhere on the neck. Never blocks on a dead badge: a
// NACK skips the window, and the bus timeouts cover a wedged one.
static bool scanFret(uint8_t id, uint8_t flags)
{
    uint8_t addr = wireAddress(id);

    Wire.beginTransmission(addr);
    Wire.write(CMD_SCAN);
    Wire.write(flags);
    Wire.write(scanSeq);
    if (Wire.endTransmission() != 0)
    {
        return noteMiss(id);
    }

    uint32_t sentUs = micros();
    uint32_t windowUs = scanWindowUs();
    while (micros() - sentUs < windowUs)
    {
    }

    if (Wire.requestFrom(addr, (uint8_t)FRAME_LEN) != FRAME_LEN)
    {
        return noteMiss(id);
    }
    uint8_t b0 = Wire.read();
    uint8_t b1 = Wire.read();
    // A badge that had not finished, or never saw this token, still holds the
    // previous frame - keep the last good state rather than trust it.
    if (b1 != scanSeq)
    {
        return noteMiss(id);
    }
    return noteFrame(id, b0);
}

// A long exclusive token. The badge gets as long as the master's own
// calibration took plus margin, then must answer with a calibrated frame.
static bool calibrateFret(uint8_t id)
{
    uint8_t addr = wireAddress(id);
    scanSeq++;

    Wire.beginTransmission(addr);
    Wire.write(CMD_CAL);
    Wire.write(scanSeq);
    if (Wire.endTransmission() != 0)
    {
        return false;
    }

    uint32_t sentUs = micros();
    uint32_t waitUs = ownCalUs + ownCalUs / 4 + (uint32_t)CAL_MARGIN_MS * 1000;
    while (micros() - sentUs < waitUs)
    {
    }

    bool ok = Wire.requestFrom(addr, (uint8_t)FRAME_LEN) == FRAME_LEN;
    uint8_t b0 = ok ? Wire.read() : 0;
    uint8_t b1 = ok ? Wire.read() : 0;
    ok = ok && b1 == scanSeq && (b0 & FRAME_STATUS_CALIBRATED);
    frets[id].calibrated = ok;
    frets[id].lastCalMs = millis();

    Serial.print("Fret ");
    Serial.print(id);
    Serial.println(ok ? " calibrated" : " calibration failed");
    return ok;
}

// The whole neck, one badge at a time: hands-off blink first so the player
// can lift off, then solid while each badge calibrates in its own turn. Every
// note is silenced first so no piezo is toggling under a baseline.
static void calibrateAll()
{
    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        setStringFreq(s, 0);
    }

    Serial.println("Calibrating - hands off the neck");
    generalCall(CMD_PROMPT, PROMPT_HANDS_OFF);
    uint32_t leadStartMs = millis();
    while (millis() - leadStartMs < CAL_LEAD_MS)
    {
        updatePromptLeds(PROMPT_HANDS_OFF);
    }

    generalCall(CMD_PROMPT, PROMPT_CALIBRATING);
    updatePromptLeds(PROMPT_CALIBRATING);

    ownCalUs = touchCalibrate();
    Serial.print("Master calibrated in ");
    Serial.print(ownCalUs);
    Serial.println("us");
    for (uint8_t i = 0; i < NUM_PADS; i++)
    {
        Serial.print("  pad ");
        Serial.print(i + 1);
        Serial.print(" compcap ");
        Serial.print(padCompCap(i));
        Serial.print(" baseline ");
        Serial.println(touchBaseline(i));
    }

    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        calibrateFret(id);
    }

    generalCall(CMD_PROMPT, PROMPT_OFF);
    updatePromptLeds(PROMPT_OFF);

    // Whatever was held going in is gone now; do not let it read as a strum.
    for (uint8_t s = 0; s < NUM_STRINGS; s++)
    {
        padWasDown[s] = false;
    }
    anyTouch = false;
}

void masterRecalibrate()
{
    calibrateAll();
}

void masterBegin()
{
    busBegin();
    // Left at the default 100 kHz on purpose. With 12 badges and the
    // cabling between them, bus capacitance is close to the 400 pF limit and
    // 400 kHz will not make the edges.

    // The fret badges power up with the master and need a moment before they
    // are listening.
    delay(STARTUP_MS);
    calibrateAll();

    // Fret badges never measure unprompted, so the master can time its own
    // scan now to size the first windows.
    ownScanMaxUs = touchScan(false);

    Serial.println("Master ready");
}

// The badge's effective pads: a capo holds every string at its fret.
static uint8_t fretPads(uint8_t id)
{
    const FretLink &f = frets[id];
    if (!f.fresh)
    {
        return 0;
    }
    return f.capo ? FRAME_PAD_BITS : f.pads;
}

// Highest fret wins, which is how a real fretboard behaves: the string only
// vibrates from the highest finger down to the bridge.
static void resolveFrets()
{
    // Collapse each badge's raw pads into string bits once, rather than per
    // string. On a ukulele the grouping runs the whole length of the neck, and
    // the pads it leaves out of every group (2 and 5) are ignored right here.
    uint8_t grouped[MAX_BADGES];
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        grouped[id] = groupPads(inst, fretPads(id));
    }

    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        uint8_t fret = 0;
        for (uint8_t id = 1; id < MAX_BADGES; id++)
        {
            if (grouped[id] & (1 << s))
            {
                fret = id; // ascending scan, so the last hit is the highest
            }
        }
        fretForString[s] = fret;
    }
}

static void setStringFreq(uint8_t s, uint16_t freq)
{
    if (stringFreq[s] == freq)
    {
        return;
    }
    stringFreq[s] = freq;

    for (uint8_t id = 0; id < MAX_BADGES; id++)
    {
        if (stringIndexForBadge(inst, id) != s)
        {
            continue;
        }

        if (id == 0)
        {
            if (freq > 0)
            {
                tone(BUZZER_PIN, freq);
            }
            else
            {
                noTone(BUZZER_PIN);
                digitalWrite(BUZZER_PIN, LOW);
            }
        }
        else if (frets[id].fresh)
        {
            writeFreq(id, freq);
        }
    }
}

// A pad going down always mutes its string, so resting a finger on it damps the
// note. Whether letting go counts as a strum depends on how long it was held:
// a quick tap plucks, a deliberate hold is a mute.
static void updateStrum()
{
    // Grouping the pads first means the machine below works unchanged whether a
    // string is one pad or two.
    uint8_t mask = groupPads(inst, touchMask());
    uint32_t now = millis();

    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        bool down = (mask & (1 << s)) != 0;

        if (down && !padWasDown[s])
        {
            padPressMs[s] = now;
            setStringFreq(s, 0);
        }
        else if (!down && padWasDown[s])
        {
            if (now - padPressMs[s] <= STRUM_MAX_HOLD_MS)
            {
                setStringFreq(s, freqForFret(inst, s, fretForString[s]));
                stringStartMs[s] = now;
            }
        }

        padWasDown[s] = down;
    }
}

static void updateSustain()
{
    uint32_t now = millis();

    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        if (stringFreq[s] == 0)
        {
            continue;
        }

        if (now - stringStartMs[s] >= SUSTAIN_MS)
        {
            setStringFreq(s, 0);
            continue;
        }

#if PITCH_TRACKS_FRETS
        // Retune a note that is still ringing, which is what makes hammer-ons
        // and slides work. setStringFreq() is a no-op when nothing moved, so
        // this does not put traffic on the bus every cycle.
        setStringFreq(s, freqForFret(inst, s, fretForString[s]));
#endif
    }
}

// Silences everything under the OLD instrument before switching. The
// badge->string mapping changes with the instrument, so a note left ringing
// would be stranded on a badge the new mapping never addresses again - it would
// drone until that badge happened to be reassigned a pitch.
void masterToggleUkulele()
{
    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        setStringFreq(s, 0);
    }

    inst = (inst == &kGuitar) ? &kUkulele : &kGuitar;

    // The fret badges hear about it with their next token, since every
    // CMD_SCAN carries the instrument.

    uint32_t now = millis();
    uint8_t strings = groupPads(inst, touchMask());
    for (uint8_t s = 0; s < NUM_STRINGS; s++)
    {
        stringFreq[s] = 0;
        stringStartMs[s] = 0;
        fretForString[s] = 0;
        // Seed the edge state from what is actually being touched right now, so
        // a finger resting across the toggle does not read as a fresh strum.
        padWasDown[s] = (strings & (1 << s)) != 0;
        padPressMs[s] = now;
    }

    Serial.println(inst == &kUkulele ? "Ukulele mode" : "Guitar mode");
}

// The slider layout is shared with every fret badge so the whole neck reads the
// same. The only thing the master does differently is the forehead LED.
static void renderLeds(bool sounding)
{
    uint8_t pads = touchMask();

    // The forehead LED means exactly one thing on exactly one badge: this is
    // the master and it is in ukulele mode. Nothing else ever lights it.
    digitalWrite(LED_ACCEL, inst == &kUkulele ? HIGH : LOW);

    if (inst == &kUkulele)
    {
        updateUkePlayLeds(pads, sounding);
    }
    else
    {
        updatePlayLeds(pads, sounding);
    }
}

static void logStats()
{
    uint32_t now = millis();
    if (now - lastStatsMs < LOG_STATS_MS)
    {
        return;
    }
    lastStatsMs = now;

#if MASTER_LOG
    char line[96];
    snprintf(line, sizeof(line), "cycle avg %lu us max %lu us | scan %lu us window %lu us | misses",
             (unsigned long)(cycleCount ? cycleSumUs / cycleCount : 0), (unsigned long)cycleMaxUs,
             (unsigned long)ownScanMaxUs, (unsigned long)scanWindowUs());
    Serial.print(line);
    bool any = false;
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (frets[id].missesLog > 0)
        {
            any = true;
            Serial.print(' ');
            Serial.print(id);
            Serial.print('=');
            Serial.print(frets[id].missesLog);
            if (!frets[id].fresh)
            {
                Serial.print("(stale)");
            }
        }
    }
    Serial.println(any ? "" : " none");
#endif

    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        frets[id].missesLog = 0;
    }
    cycleSumUs = 0;
    cycleMaxUs = 0;
    cycleCount = 0;
}

// One full cycle: every fret badge's window in turn, then the master's own.
// Everything that puts traffic on the bus or switches current - frequency
// writes, LEDs, serial - happens in the master's slot, after its own pads are
// back to driven-LOW, so none of it can land inside anyone's measurement.
void masterLoop()
{
    uint32_t cycleStartUs = micros();

    // A bus that is still busy between transactions has been left wedged.
    uint32_t now = millis();
    if (SERCOM2->I2CM.STATUS.bit.BUSSTATE == 3 && now - lastRecoverMs >= 1000)
    {
        lastRecoverMs = now;
        busRecover();
    }

    scanSeq++;
    uint8_t flags = (inst == &kUkulele) ? SCAN_FLAG_UKULELE : 0;
    if (anyTouch)
    {
        flags |= SCAN_FLAG_ANY_TOUCH;
    }
    uint8_t ownBefore = touchMask();
    bool changed = false;
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        changed |= scanFret(id, flags);
    }

    // The master's own window. The last fret badge has already answered, so
    // its scan is done and the neck is quiet.
    uint32_t ownUs = touchScan(!anyTouch);
    if (ownUs > ownScanMaxUs)
    {
        ownScanMaxUs = ownUs;
    }
    changed |= touchMask() != ownBefore;

#if TOUCH_DEBUG
    static uint32_t lastDebugMs = 0;
    if (now - lastDebugMs >= TOUCH_DEBUG_MS)
    {
        lastDebugMs = now;
        touchPrintDebug(ownUs);
    }
#endif

    // Measured pads only: a capo is a clamp, not a hand, and must not freeze
    // every baseline on the neck for as long as it is on.
    anyTouch = touchMask() != 0;
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        anyTouch |= frets[id].fresh && frets[id].pads != 0;
    }

    resolveFrets();
    updateStrum();
    updateSustain();

    bool sounding = false;
    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        if (stringFreq[s] > 0)
        {
            sounding = true;
            break;
        }
    }
    renderLeds(sounding);

    if (changed)
    {
        logPads();
    }

    // A badge that answers without a baseline has just booted - a brown-out,
    // or a late arrival. Calibrate it in the master's slot, one per cycle, and
    // back off a badge that keeps failing so it cannot stall the instrument.
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (frets[id].fresh && !frets[id].calibrated && millis() - frets[id].lastCalMs >= CAL_RETRY_MS)
        {
            calibrateFret(id);
            break;
        }
    }

#if MASTER_DEBUG
    static uint32_t lastPrintMs = 0;
    if (now - lastPrintMs >= 250)
    {
        lastPrintMs = now;
        Serial.print(inst == &kUkulele ? "uke  frets:" : "gtr  frets:");
        for (uint8_t s = 0; s < inst->stringCount; s++)
        {
            Serial.print(' ');
            Serial.print(fretForString[s]);
        }
        Serial.print("  hz:");
        for (uint8_t s = 0; s < inst->stringCount; s++)
        {
            Serial.print(' ');
            Serial.print(stringFreq[s]);
        }
        Serial.println();
    }
#endif

    uint32_t cycleUs = micros() - cycleStartUs;
    cycleSumUs += cycleUs;
    cycleCount++;
    if (cycleUs > cycleMaxUs)
    {
        cycleMaxUs = cycleUs;
    }
    logStats();
}
