#include "master.h"
#include "badge_hw.h"
#include "guitar_config.h"

#include <Wire.h>

// Which instrument is being played. Runtime only - it always boots to guitar,
// so toggling never costs a flash page erase.
static const Instrument *inst = &kGuitar;

// Ukulele strings land on slider LEDs 1, 2, 4 and 5, leaving the middle one
// dark so the display mirrors the pads merging at each end of the slider.
static const uint8_t kUkeStringLeds[NUM_UKE_STRINGS] = {0, 1, 3, 4};

static bool slavePresent[MAX_BADGES];
static uint8_t slavePads[MAX_BADGES];

static uint8_t fretForString[NUM_STRINGS];
static uint16_t stringFreq[NUM_STRINGS]; // currently sounding, 0 = silent
static uint32_t stringStartMs[NUM_STRINGS];

static uint32_t padPressMs[NUM_STRINGS];
static bool padWasDown[NUM_STRINGS];

static uint32_t lastPollMs = 0;
static uint32_t lastProbeMs = 0;
static uint8_t probeCursor = 1;

static void setStringFreq(uint8_t s, uint16_t freq);

static void writeFreq(uint8_t id, uint16_t freq)
{
    Wire.beginTransmission(wireAddress(id));
    Wire.write((uint8_t)(freq & 0xFF));
    Wire.write((uint8_t)(freq >> 8));
    if (Wire.endTransmission() != 0)
    {
        slavePresent[id] = false;
        slavePads[id] = 0;
    }
}

// A 1-byte write is a mode; a 2-byte write is a frequency. Length is what tells
// them apart on the slave, so this must stay exactly one byte.
static void writeMode(uint8_t id)
{
    Wire.beginTransmission(wireAddress(id));
    Wire.write((uint8_t)(inst == &kUkulele ? 1 : 0));
    if (Wire.endTransmission() != 0)
    {
        slavePresent[id] = false;
        slavePads[id] = 0;
    }
}

static void broadcastMode()
{
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (slavePresent[id])
        {
            writeMode(id);
        }
    }
}

static bool probe(uint8_t id)
{
    Wire.beginTransmission(wireAddress(id));
    return Wire.endTransmission() == 0;
}

void masterBegin()
{
    Wire.begin();
    // Left at the default 100 kHz on purpose. With 12 badges and the
    // cabling between them, bus capacitance is close to the 400 pF limit and
    // 400 kHz will not make the edges.

    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        slavePresent[id] = probe(id);
        Serial.print("Fret ");
        Serial.print(id);
        Serial.println(slavePresent[id] ? ": present" : ": absent");
    }

    Serial.println("Master ready");
}

// Reads every present slave's pad mask. An absent badge NACKs its address and
// requestFrom returns immediately, so a missing fret costs a few hundred
// microseconds rather than stalling the strum.
static void pollSlaves()
{
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (!slavePresent[id])
        {
            continue;
        }

        if (Wire.requestFrom(wireAddress(id), (uint8_t)1) != 1)
        {
            slavePresent[id] = false;
            slavePads[id] = 0;
            continue;
        }
        slavePads[id] = Wire.read();
    }
}

// Highest fret wins, which is how a real fretboard behaves: the string only
// vibrates from the highest finger down to the bridge.
static void resolveFrets()
{
    // Collapse each badge's raw pads into string bits once, rather than per
    // string. On a ukulele the grouping runs the whole length of the neck, so
    // pads 5 and 6 on any fret badge both mean "the A string".
    uint8_t grouped[MAX_BADGES];
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        grouped[id] = slavePresent[id] ? groupPads(inst, slavePads[id]) : 0;
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
        else if (slavePresent[id])
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
        // this does not put traffic on the bus every poll.
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

    // Tell the neck before anything else. Every fret badge has to change which
    // pads it reads and how it lights its slider, and until it hears this it is
    // still reporting under the old instrument.
    broadcastMode();

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

// Retries one absent ID at a time so a neck that is missing several badges does
// not spend a long blocking scan between polls.
static void probeAbsent()
{
    for (uint8_t attempt = 0; attempt < MAX_BADGES; attempt++)
    {
        uint8_t id = probeCursor;
        probeCursor++;
        if (probeCursor >= MAX_BADGES)
        {
            probeCursor = 1;
        }

        if (slavePresent[id])
        {
            continue;
        }

        if (probe(id))
        {
            slavePresent[id] = true;
            // Bring the newcomer in step: instrument first, since it decides
            // what the badge reads, then whatever its string is doing.
            writeMode(id);
            writeFreq(id, stringFreq[stringIndexForBadge(inst, id)]);
            Serial.print("Fret ");
            Serial.print(id);
            Serial.println(" joined");
        }
        return;
    }
}

void masterLoop()
{
    uint32_t now = millis();
    if (now - lastPollMs < POLL_INTERVAL_MS)
    {
        return;
    }
    lastPollMs = now;

    touchUpdate();
    pollSlaves();
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

    if (now - lastProbeMs >= PROBE_INTERVAL_MS)
    {
        lastProbeMs = now;
        probeAbsent();
        // Re-assert the instrument once a second. A badge that browns out and
        // restarts still answers polls, so the master never notices it is gone
        // - but it comes back up in guitar mode. Without this it would sit
        // there reading the wrong pads until the next time you toggled.
        broadcastMode();
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
}
