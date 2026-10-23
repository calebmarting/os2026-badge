#include "master.h"
#include "badge_hw.h"
#include "guitar_config.h"
#include "badge_config.h"
#include "console.h"
#include "nvm.h"
#include "ota.h"
#include "params.h"
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
    uint16_t badLog;     // frames that answered but failed the check
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

// When a mute (a hold longer than STRUM_MAX_HOLD_MS) was last let go of. A
// finger leaving the pad can flicker it on and off, and that flicker looks
// exactly like a quick strum - so a touch that starts within the lockout is
// treated as more of the same mute, and its release stays silent.
static uint32_t muteReleaseMs[NUM_STRINGS];
static bool pressIsMuteBounce[NUM_STRINGS];

// Realistic mode: the fret each ringing note was plucked at, and since when
// the string's fret has disagreed with it (0 = it agrees).
static uint8_t pluckedFret[NUM_STRINGS];
static uint32_t fretMovedMs[NUM_STRINGS];

// Cycle timing, reset every stats line.
static uint32_t cycleSumUs = 0;
static uint32_t cycleMaxUs = 0;
static uint32_t cycleCount = 0;
static uint32_t lastStatsMs = 0;
static uint32_t lastRecoverMs = 0;

// Attract mode: last time anything was touched or pressed, and whether the
// neck is twinkling now.
static uint32_t lastActivityMs = 0;
static bool attract = false;

// Console-controlled output, see printHelp().
static bool logPadsOn = MASTER_LOG;
static bool statsOn = MASTER_LOG;
static uint16_t watchMask = 0; // bit N = print badge N's debug block

static void setStringFreq(uint8_t s, uint16_t freq);

// How long the bus stays silent after handing a badge the token.
static uint32_t scanWindowUs()
{
    if (params.scanUs > 0)
    {
        return params.scanUs;
    }
    return ownScanMaxUs + ownScanMaxUs / 4 + SCAN_MARGIN_US;
}

// SERCOM2 is the MKR Zero's Wire port. The core's master code busy-waits on
// the bus with no timeout, so a badge that wedges SCL low would hang the whole
// instrument. LOWTOUT aborts any transfer where SCL is held low for 25-35 ms -
// the hardware then sets the very flags those loops wait on, so they fall
// through with an error. CTRLA is only writable with the peripheral disabled.
//
// INACTOUT stays off. It treats any SCL low longer than ~200 us as a dead bus,
// and the master itself holds SCL low between bytes whenever a USB interrupt
// delays it - on hardware that cut 33-byte debug reads off mid-transfer and
// left the bus needing recovery. busRecover() covers a genuinely stuck bus.
static void enableBusTimeouts()
{
    SercomI2cm &i2c = SERCOM2->I2CM;
    i2c.CTRLA.bit.ENABLE = 0;
    while (i2c.SYNCBUSY.bit.ENABLE)
    {
    }
    i2c.CTRLA.bit.LOWTOUTEN = 1;
    i2c.CTRLA.bit.INACTOUT = 0;
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
    // 100 kHz, explicitly. With 12 badges and the cabling between them, bus
    // capacitance is close to the 400 pF limit and 400 kHz will not make the
    // edges. setClock() re-initialises CTRLA, so it must come before the
    // timeouts.
    Wire.setClock(100000);
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

static void sendCommand0(uint8_t cmd)
{
    Wire.beginTransmission(GENERAL_CALL_ADDR);
    Wire.write(cmd);
    Wire.endTransmission();
}

static void sendParam(uint8_t addr, uint8_t index)
{
    uint16_t value = (uint16_t)paramGet(index);
    Wire.beginTransmission(addr);
    Wire.write(CMD_PARAM);
    Wire.write(index);
    Wire.write((uint8_t)(value & 0xFF));
    Wire.write((uint8_t)(value >> 8));
    Wire.endTransmission();
}

// Every param, so a badge that rebooted - back on its compiled-in defaults -
// is brought in line with whatever was tuned at runtime before it calibrates.
static void sendAllParams(uint8_t addr)
{
    for (uint8_t i = 0; i < paramCount(); i++)
    {
        sendParam(addr, i);
    }
}

static void printPads(uint8_t pads)
{
    for (uint8_t i = 0; i < NUM_PADS; i++)
    {
        Serial.print((pads & (1 << i)) ? '1' : '.');
    }
}

static int appendPads(char *line, int n, int size, uint8_t pads)
{
    for (uint8_t i = 0; i < NUM_PADS && n + 1 < size; i++)
    {
        line[n++] = (pads & (1 << i)) ? '1' : '.';
    }
    line[n] = '\0';
    return n;
}

// One line per change, so a touch log reads as a timeline: every badge's pads,
// pad 1 leftmost. "------" is a stale badge, a trailing 'c' a capo. Built in
// RAM and sent as one write: every separate Serial.print is its own USB
// transfer, and forty of them stalled the cycle by ~7 ms.
static void logPads()
{
    if (!logPadsOn)
    {
        return;
    }
    char line[160];
    int n = snprintf(line, sizeof(line), "%lu 0:", (unsigned long)millis());
    n = appendPads(line, n, sizeof(line), touchMask());
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        n += snprintf(line + n, sizeof(line) - n, " %u:", (unsigned)id);
        if (frets[id].fresh)
        {
            n = appendPads(line, n, sizeof(line), frets[id].pads);
            n += snprintf(line + n, sizeof(line) - n, "%c", frets[id].capo ? 'c' : ' ');
        }
        else
        {
            n += snprintf(line + n, sizeof(line) - n, "------ ");
        }
    }
    Serial.println(line);
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
    if (capo != f.capo)
    {
        masterNoteActivity(); // someone pressed that badge's button
    }

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
    // A corrupted frame, or one from a badge that had not finished or never
    // saw this token, fails the check - keep the last good state.
    if (b1 != frameCheck(addr, scanSeq, b0))
    {
        frets[id].badLog++;
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
    ok = ok && b1 == frameCheck(addr, scanSeq, b0) && (b0 & FRAME_STATUS_CALIBRATED);
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
    sendAllParams(GENERAL_CALL_ADDR);
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

    // The acquisition settings may have changed, so re-time the scan from
    // scratch. Fret badges never measure unprompted, so this is safe now.
    ownScanMaxUs = touchScan(false);

    // The idle countdown starts once the neck is ready to play.
    masterNoteActivity();
}

void masterRecalibrate()
{
    calibrateAll();
}

void masterNoteActivity()
{
    lastActivityMs = millis();
    if (attract)
    {
        // The LEDs go back to showing pads in this same slot (renderLeds
        // rewrites the forehead), and the fret badges on their next token.
        attract = false;
        endAttractLeds();
        Serial.println("Attract mode off");
    }
}

void masterBegin()
{
    busBegin();
    // The fret badges power up with the master and need a moment before they
    // are listening.
    delay(STARTUP_MS);
    // Frees any badge left waiting in an update that never finished.
    sendCommand0(CMD_OTA_ABORT);
    calibrateAll();

    Serial.println("Master ready - type help for commands");
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
            pressIsMuteBounce[s] = now - muteReleaseMs[s] < params.muteLockMs;
            setStringFreq(s, 0);
        }
        else if (!down && padWasDown[s])
        {
            if (pressIsMuteBounce[s] || now - padPressMs[s] > STRUM_MAX_HOLD_MS)
            {
                // The end of a mute, or a bounce on the end of one. Every
                // bounce restarts the lockout, so a long chatter stays quiet.
                muteReleaseMs[s] = now;
            }
            else
            {
                setStringFreq(s, freqForFret(inst, s, fretForString[s]));
                stringStartMs[s] = now;
                pluckedFret[s] = fretForString[s];
                fretMovedMs[s] = 0;
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

        if (params.realistic)
        {
            // Like a real string: moving the fretting finger stops the note,
            // and it needs strumming again. The change has to hold for
            // fretGraceMs first, so one bad frame cannot cut a ringing note.
            if (fretForString[s] == pluckedFret[s])
            {
                fretMovedMs[s] = 0;
            }
            else if (fretMovedMs[s] == 0)
            {
                fretMovedMs[s] = now | 1; // 0 means "not moved"
            }
            else if (now - fretMovedMs[s] >= params.fretGraceMs)
            {
                setStringFreq(s, 0);
            }
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

void masterToggleRealistic()
{
    paramSet(paramFind("realistic"), params.realistic ? 0 : 1);
    // Notes already ringing carry on. Going realistic, they are cut by the
    // next fret change like any other.
    for (uint8_t s = 0; s < NUM_STRINGS; s++)
    {
        pluckedFret[s] = fretForString[s];
        fretMovedMs[s] = 0;
    }
    Serial.println(params.realistic ? "Realistic strumming: a fret change stops the note"
                                    : "Ringing mode: notes follow the frets");
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
        pressIsMuteBounce[s] = false;
        pluckedFret[s] = 0;
        fretMovedMs[s] = 0;
    }

    Serial.println(inst == &kUkulele ? "Ukulele mode" : "Guitar mode");
}

// The slider layout is shared with every fret badge so the whole neck reads the
// same. The only thing the master does differently is the forehead LED.
static void renderLeds(bool sounding)
{
    if (attract)
    {
        updateAttractLeds();
        return;
    }

    uint8_t pads = touchMask();

    // The forehead LED means exactly one thing on exactly one badge: this is
    // the master and it is in ukulele mode. Nothing else ever lights it.
    digitalWrite(LED_ACCEL, inst == &kUkulele ? HIGH : LOW);
    // Same for the left eye: the master is in ringing mode (realistic off).
    digitalWrite(LED_MIC, params.realistic ? LOW : HIGH);

    if (inst == &kUkulele)
    {
        updateUkePlayLeds(pads, sounding);
    }
    else
    {
        updatePlayLeds(pads, sounding);
    }
}

static int appendIdCounts(char *line, int n, int size, const char *label, bool (*pick)(const FretLink &),
                          uint16_t FretLink::*count)
{
    n += snprintf(line + n, size - n, "%s", label);
    bool any = false;
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (pick(frets[id]))
        {
            any = true;
            if (count)
            {
                n += snprintf(line + n, size - n, " %u=%u", (unsigned)id, (unsigned)(frets[id].*count));
            }
            else
            {
                n += snprintf(line + n, size - n, " %u", (unsigned)id);
            }
        }
    }
    if (!any)
    {
        n += snprintf(line + n, size - n, " none");
    }
    return n;
}

static void logStats()
{
    uint32_t now = millis();
    if (now - lastStatsMs < LOG_STATS_MS)
    {
        return;
    }
    lastStatsMs = now;

    if (statsOn)
    {
        char line[200];
        int n = snprintf(line, sizeof(line), "cycle avg %lu us max %lu us | scan %lu us window %lu us | ",
                         (unsigned long)(cycleCount ? cycleSumUs / cycleCount : 0), (unsigned long)cycleMaxUs,
                         (unsigned long)ownScanMaxUs, (unsigned long)scanWindowUs());
        // A stale badge misses every token, so its count says nothing new.
        n = appendIdCounts(line, n, sizeof(line), "missed",
                           [](const FretLink &f) { return f.fresh && f.missesLog > 0; }, &FretLink::missesLog);
        n = appendIdCounts(line, n, sizeof(line), " | bad", [](const FretLink &f) { return f.badLog > 0; },
                           &FretLink::badLog);
        appendIdCounts(line, n, sizeof(line), " | stale", [](const FretLink &f) { return !f.fresh; }, nullptr);
        Serial.println(line);
    }

    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        frets[id].missesLog = 0;
        frets[id].badLog = 0;
    }
    cycleSumUs = 0;
    cycleMaxUs = 0;
    cycleCount = 0;
}

static void printDebugBlock(uint8_t id, const uint8_t *blk)
{
    char line[200];
    int n = snprintf(line, sizeof(line), "dbg %2u raw/base/delta:", (unsigned)id);
    for (uint8_t i = 0; i < NUM_PADS; i++)
    {
        int32_t raw = blk[2 * i] | (blk[2 * i + 1] << 8);
        int32_t base = blk[12 + 2 * i] | (blk[13 + 2 * i] << 8);
        n += snprintf(line + n, sizeof(line) - n, " %ld/%ld/%ld", (long)raw, (long)base, (long)(raw - base));
    }
    Serial.print(line);
    Serial.print("  on ");
    printPads(blk[24]);
    Serial.print(" neg ");
    printPads(blk[25]);
    snprintf(line, sizeof(line), "  recal %u  scan %uus  cal %ums",
             (unsigned)(blk[26] | (blk[27] << 8)), (unsigned)(blk[28] | (blk[29] << 8)),
             (unsigned)(blk[30] | (blk[31] << 8)));
    Serial.println(line);
}

// Pulls one badge's debug block over the bus, in the master's slot, so a
// single USB cable on the master shows raw/baseline/delta for any badge.
static void watchOne(uint8_t id)
{
    uint8_t blk[DEBUG_LEN];
    if (id == 0)
    {
        touchPackDebug(blk);
        printDebugBlock(0, blk);
        return;
    }

    uint8_t addr = wireAddress(id);
    Wire.beginTransmission(addr);
    Wire.write(CMD_DEBUG);
    bool ok = Wire.endTransmission() == 0 && Wire.requestFrom(addr, (uint8_t)DEBUG_LEN) == DEBUG_LEN;
    for (uint8_t i = 0; ok && i < DEBUG_LEN; i++)
    {
        blk[i] = Wire.read();
    }
    if (!ok || crc8(blk, DEBUG_LEN - 1, crc8(&addr, 1)) != blk[DEBUG_LEN - 1])
    {
        Serial.print("dbg ");
        Serial.print(id);
        Serial.println(" no answer");
        return;
    }
    printDebugBlock(id, blk);
}

// Round-robins the watched badges, one per WATCH_MS, so watching the whole
// neck costs one extra bus read per cycle at most.
static void serviceWatch()
{
    static uint32_t lastWatchMs = 0;
    static uint8_t cursor = 0;
    if (watchMask == 0 || millis() - lastWatchMs < WATCH_MS)
    {
        return;
    }
    lastWatchMs = millis();

    for (uint8_t step = 0; step < MAX_BADGES; step++)
    {
        cursor = (cursor + 1) % MAX_BADGES;
        if (watchMask & (1 << cursor))
        {
            watchOne(cursor);
            return;
        }
    }
}

static void printInfo()
{
    Serial.print(inst == &kUkulele ? "Ukulele" : "Guitar");
    Serial.print(", window ");
    Serial.print(scanWindowUs());
    Serial.print("us, master cal ");
    Serial.print(ownCalUs / 1000);
    Serial.println("ms");
    char line[96];
    snprintf(line, sizeof(line), "  master          firmware %08lX (%lu bytes)",
             (unsigned long)runningImageCrc(), (unsigned long)runningImageSize());
    Serial.println(line);
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        const FretLink &f = frets[id];
        uint32_t crc = 0;
        bool haveCrc = f.fresh && otaQueryRunningCrc(id, crc);
        char fw[24] = "";
        if (haveCrc)
        {
            snprintf(fw, sizeof(fw), "  firmware %08lX", (unsigned long)crc);
        }
        snprintf(line, sizeof(line), "  fret %2u  I2C 0x%02X  %s%s%s%s%s",
                 (unsigned)id, (unsigned)wireAddress(id), f.fresh ? "present" : "stale",
                 f.fresh && !f.calibrated ? ", uncalibrated" : "", f.capo ? ", capo" : "", fw,
                 haveCrc && crc != runningImageCrc() ? "  <- differs from master" : "");
        Serial.println(line);
    }
}

static void printHelp()
{
    Serial.println("Master commands:");
    Serial.println("  log on|off      pad-change timeline");
    Serial.println("  stats on|off    cycle time, misses, bad frames, once a second");
    Serial.println("  watch N|all|off raw/baseline/delta of badge N (0 = master)");
    Serial.println("  cal             recalibrate the whole neck (hands off)");
    Serial.println("  info            which badges are present");
    Serial.println("  id N            make THIS badge fret N and reboot (it is 0, the master)");
    Serial.println("  params          every tunable and its current value");
    Serial.println("  set NAME VALUE  change one on every badge, until power-off");
    Serial.println("  ota SIZE CRC    firmware update - use tools/ota_upload.py");
}

// The host tool has already sent "ota SIZE CRC"; the image follows on Serial.
static void runUpdate(char *arg)
{
    char *end = nullptr;
    uint32_t size = strtoul(arg, &end, 10);
    uint32_t crc = strtoul(end, nullptr, 10);
    for (uint8_t s = 0; s < inst->stringCount; s++)
    {
        setStringFreq(s, 0);
    }
    setSliderLeds(0);
    otaMasterRun(size, crc); // only returns on failure
    Serial.println("Update abandoned, still playing the old firmware");
}

static void printParams()
{
    for (uint8_t i = 0; i < paramCount(); i++)
    {
        const ParamInfo &p = paramInfo(i);
        char line[64];
        snprintf(line, sizeof(line), "  %-10s %6d   (%d..%d%s)", p.name, paramGet(i), p.min, p.max,
                 p.needsRecal ? ", recalibrates" : "");
        Serial.println(line);
    }
}

static void setParam(char *arg)
{
    char *value = strchr(arg, ' ');
    if (!value)
    {
        Serial.println("usage: set NAME VALUE (see params)");
        return;
    }
    *value++ = '\0';
    int8_t index = paramFind(arg);
    if (index < 0)
    {
        Serial.println("no such param (see params)");
        return;
    }
    if (!paramSet(index, (int16_t)strtol(value, nullptr, 10)))
    {
        Serial.println("out of range (see params)");
        return;
    }

    sendParam(GENERAL_CALL_ADDR, index);
    Serial.print(paramInfo(index).name);
    Serial.print(" = ");
    Serial.println(paramGet(index));
    if (paramInfo(index).needsRecal)
    {
        paramsApplyAcquisition(0);
        calibrateAll();
    }
}

static bool onOff(const char *arg, bool current)
{
    if (strcmp(arg, "on") == 0)
    {
        return true;
    }
    if (strcmp(arg, "off") == 0)
    {
        return false;
    }
    return !current;
}

static void handleCommand(char *line)
{
    char *arg = strchr(line, ' ');
    if (arg)
    {
        *arg++ = '\0';
        while (*arg == ' ')
        {
            arg++;
        }
    }
    else
    {
        arg = line + strlen(line);
    }

    if (strcmp(line, "log") == 0)
    {
        logPadsOn = onOff(arg, logPadsOn);
        Serial.println(logPadsOn ? "log on" : "log off");
    }
    else if (strcmp(line, "stats") == 0)
    {
        statsOn = onOff(arg, statsOn);
        Serial.println(statsOn ? "stats on" : "stats off");
    }
    else if (strcmp(line, "watch") == 0)
    {
        if (strcmp(arg, "all") == 0)
        {
            watchMask = (1 << MAX_BADGES) - 1;
        }
        else if (strcmp(arg, "off") == 0 || *arg == '\0')
        {
            watchMask = 0;
        }
        else
        {
            long id = strtol(arg, nullptr, 10);
            if (id >= 0 && id < MAX_BADGES)
            {
                watchMask ^= (1 << id);
            }
        }
        Serial.print("watching:");
        for (uint8_t id = 0; id < MAX_BADGES; id++)
        {
            if (watchMask & (1 << id))
            {
                Serial.print(' ');
                Serial.print(id);
            }
        }
        Serial.println(watchMask ? "" : " nothing");
    }
    else if (strcmp(line, "cal") == 0)
    {
        calibrateAll();
    }
    else if (strcmp(line, "info") == 0)
    {
        printInfo();
    }
    else if (strcmp(line, "id") == 0)
    {
        consoleIdCommand(arg, 0);
    }
    else if (strcmp(line, "params") == 0)
    {
        printParams();
    }
    else if (strcmp(line, "set") == 0)
    {
        setParam(arg);
    }
    else if (strcmp(line, "ota") == 0)
    {
        runUpdate(arg);
    }
    else
    {
        printHelp();
    }
}

// One full cycle: every fret badge's window in turn, then the master's own.
// Everything that puts traffic on the bus or switches current - frequency
// writes, LEDs, serial - happens in the master's slot, after its own pads are
// back to driven-LOW, so none of it can land inside anyone's measurement.
// Pads the master has seen held at any read this cycle, so a strum that comes
// and goes between the extra reads still counts as a touch for ANY_TOUCH.
static uint8_t ownTouchedThisCycle = 0;

// One read of the master's own strum pads, then the strum logic on it. The
// fret badges are only read once a cycle, but strums are fast and the strum
// pads sit far from the fretting hand, so the master reads its own pads
// several times a cycle - always between fret badges' windows, so it is still
// the only PTC measuring. Only the read in the master's own slot may drift the
// baseline, so drift keeps the same pace however often the pads are read.
// Returns true if the master's pads changed.
static bool strumRead(bool mayDrift, uint32_t &scanUs)
{
    uint8_t before = touchMask();
    scanUs = touchScan(mayDrift && !anyTouch);
    if (scanUs > ownScanMaxUs)
    {
        ownScanMaxUs = scanUs;
    }
    ownTouchedThisCycle |= touchMask();

    // Fret badges answered earlier this cycle, so resolve with what is known
    // now. Mutes and plucks go out on the bus here - nobody is measuring.
    resolveFrets();
    updateStrum();
    return touchMask() != before;
}

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
    if (attract)
    {
        flags |= SCAN_FLAG_ATTRACT;
    }
    bool changed = false;
    ownTouchedThisCycle = 0;
    uint32_t ownUs = 0;
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        changed |= scanFret(id, flags);
        // An extra strum read after every Nth fret badge. The last one is
        // skipped: the master's own slot follows straight after.
        if (params.strumEvery > 0 && id % params.strumEvery == 0 && id + 1 < MAX_BADGES)
        {
            changed |= strumRead(false, ownUs);
        }
    }

    // The master's own window. The last fret badge has already answered, so
    // its scan is done and the neck is quiet.
    changed |= strumRead(true, ownUs);

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
    anyTouch = ownTouchedThisCycle != 0;
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        anyTouch |= frets[id].fresh && frets[id].pads != 0;
    }

    // Any touch ends attract mode at once, and restarts the idle countdown.
    if (anyTouch)
    {
        masterNoteActivity();
    }
    bool wantAttract = params.attractSeconds > 0 &&
                       millis() - lastActivityMs >= (uint32_t)params.attractSeconds * 1000;
    if (wantAttract && !attract)
    {
        attract = true;
        Serial.println("Attract mode");
    }
    else if (attract && params.attractSeconds == 0)
    {
        masterNoteActivity(); // "set attract 0" while twinkling
    }

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

    char line[64];
    if (consoleReadLine(line, sizeof(line)))
    {
        handleCommand(line);
    }
    serviceWatch();

    // A badge that answers without a baseline has just booted - a brown-out,
    // or a late arrival. Calibrate it in the master's slot, one per cycle, and
    // back off a badge that keeps failing so it cannot stall the instrument.
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (frets[id].fresh && !frets[id].calibrated && millis() - frets[id].lastCalMs >= CAL_RETRY_MS)
        {
            sendAllParams(wireAddress(id));
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
