#include "slave.h"
#include "badge_hw.h"
#include "guitar_config.h"
#include "scan_protocol.h"

#include <Wire.h>

static uint8_t myAddress = 0;

// Set by the I2C receive ISR, consumed by loop(). Measuring inside the ISR
// would hold the bus for the whole scan, so the ISR only raises the flag.
static volatile bool scanPending = false;
static volatile uint8_t scanFlags = 0;
static volatile uint8_t scanSeq = 0;

// Built in loop() after a scan, read from the I2C request ISR. Rebuilt with
// interrupts off so a read can never see half an old frame and half a new one.
static volatile uint8_t frame[FRAME_LEN] = {0, 0};

static volatile uint16_t pendingFreq = 0;
static volatile bool freqDirty = false;
static uint16_t activeFreq = 0;

// From the last token. Decides only how this badge lights its slider - the
// master does all the pad grouping, and the pitch is just whatever it is told.
static bool ukuleleMode = false;

// Set by a short press of this badge's button. Runtime only, like the mode.
static bool capoOn = false;

static void onRequest()
{
    Wire.write((const uint8_t *)frame, FRAME_LEN);
}

// Every write starts with a command byte; see scan_protocol.h.
static void onReceive(int count)
{
    if (count >= 1)
    {
        uint8_t cmd = Wire.read();
        if (cmd == CMD_SCAN && count >= 3)
        {
            scanFlags = Wire.read();
            scanSeq = Wire.read();
            scanPending = true;
        }
        else if (cmd == CMD_FREQ && count >= 3)
        {
            uint8_t lo = Wire.read();
            uint8_t hi = Wire.read();
            pendingFreq = (uint16_t)lo | ((uint16_t)hi << 8);
            freqDirty = true;
        }
    }

    while (Wire.available())
    {
        Wire.read();
    }
}

void slaveToggleCapo()
{
    capoOn = !capoOn;
    Serial.println(capoOn ? "Capo on" : "Capo off");
}

void slaveBegin(uint8_t id)
{
    myAddress = wireAddress(id);
    Wire.begin(myAddress);
    Wire.onRequest(onRequest);
    Wire.onReceive(onReceive);

    // Which string this badge sounds depends on the instrument the master is
    // currently playing, so it is not something this badge can report - it just
    // plays whatever pitch it is handed.
    Serial.print("Fret badge, ID ");
    Serial.print(id);
    Serial.print(", I2C 0x");
    Serial.println(myAddress, HEX);
}

// Runs only while this badge holds the token. Everything that switches current
// - the LEDs - happens after the pads are back to driven-LOW but before the
// frame is published, so it is still inside this badge's window: the master
// cannot hand the token on before it has read a frame with this seq.
static void runScan()
{
    noInterrupts();
    uint8_t flags = scanFlags;
    uint8_t seq = scanSeq;
    scanPending = false;
    interrupts();

    ukuleleMode = (flags & SCAN_FLAG_UKULELE) != 0;

    // Measured even with the capo on, so the master always sees the real hand.
    touchUpdate();
    uint8_t pads = touchMask() & FRAME_PAD_BITS;

    if (capoOn)
    {
        updateCapoLeds(activeFreq > 0);
    }
    else if (ukuleleMode)
    {
        updateUkePlayLeds(pads, activeFreq > 0);
    }
    else
    {
        updatePlayLeds(pads, activeFreq > 0);
    }

    uint8_t status = capoOn ? FRAME_STATUS_CAPO : 0;
    noInterrupts();
    frame[0] = pads | status;
    frame[1] = seq;
    interrupts();
}

void slaveLoop()
{
    if (scanPending)
    {
        runScan();
    }

    if (freqDirty)
    {
        // tone() reconfigures TC5, which is too much work to do inside the
        // SERCOM ISR that received the value - so it lands here instead. The
        // master only sends frequencies between scan windows, so this cannot
        // fall inside anyone's measurement. The piezo itself keeps toggling
        // through every window; muting it per scan would chop the note.
        noInterrupts();
        uint16_t freq = pendingFreq;
        freqDirty = false;
        interrupts();

        if (freq != activeFreq)
        {
            activeFreq = freq;
            if (activeFreq > 0)
            {
                tone(BUZZER_PIN, activeFreq);
            }
            else
            {
                noTone(BUZZER_PIN);
                digitalWrite(BUZZER_PIN, LOW);
            }
        }
    }
}
