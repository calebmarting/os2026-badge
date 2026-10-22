// The wire protocol between the master and the fret badges.
//
// Only one badge's PTC may measure at any moment: with 12 PTCs free-running on
// independent RC clocks, a hand bridging two badges couples one badge's charge
// pulses into the other's measurement. So the master hands out a token. It
// sends CMD_SCAN to one badge, keeps the bus silent for the scan window while
// that badge measures, reads back its frame, and moves on. A fret badge never
// measures unless it holds the token.
#pragma once

#include <Arduino.h>

// Commands marked "general call" go to address 0 and reach every badge at once.
#define GENERAL_CALL_ADDR 0x00

// The first byte of every master->badge write. The values stay clear of 0x00,
// 0x04 and 0x06, which a general-call-aware device would read as a reset or an
// address-programming command.
enum : uint8_t
{
    // [flags, seq] - measure all 6 pads now, then answer with a frame.
    CMD_SCAN = 0x51,
    // [lo, hi] - play this frequency in Hz, little-endian. 0 = silent.
    CMD_FREQ = 0x52,
    // [seq] - calibrate now (a long exclusive token), then answer with a
    // frame carrying FRAME_STATUS_CALIBRATED.
    CMD_CAL = 0x53,
    // [mode] general call - the calibration prompt, one of PROMPT_*.
    CMD_PROMPT = 0x54,
    // [] - the next read returns this badge's debug block instead of a frame.
    CMD_DEBUG = 0x56,
};

#define PROMPT_OFF 0
#define PROMPT_HANDS_OFF 1 // blinking: lift your hands, nobody is measuring yet
#define PROMPT_CALIBRATING 2 // solid: badges are calibrating one by one

// CMD_SCAN flags.
// Something somewhere on the neck - any badge, master included - read as held
// last cycle. Baselines only drift while this is clear.
#define SCAN_FLAG_ANY_TOUCH 0x01
// The instrument rides along with every token, so a badge that browns out and
// restarts is back on the right instrument one cycle later.
#define SCAN_FLAG_UKULELE 0x02

// A badge's answer to a read, valid for the last CMD_SCAN it handled.
//   byte 0: bits 0..5 = pads 1..6 measured as touched, plus status bits
//   byte 1: frameCheck(badge address, seq of the token it answers, byte 0)
// The seq is never sent back, only folded into the check. A badge that missed
// its token still holds the previous frame, computed with the previous seq, so
// it fails the check exactly like a corrupted byte does - and the master keeps
// its last good state in both cases. An idle SAMD slave answers 0xFF 0xFF,
// which the check also rejects (barring a 1-in-256 coincidence per read).
#define FRAME_LEN 2
#define FRAME_PAD_BITS 0x3F
// The badge has a baseline. Until it does it measures nothing and reports no
// pads, and the master schedules a CMD_CAL for it - which is how a badge that
// reboots mid-session gets recalibrated without anyone asking.
#define FRAME_STATUS_CALIBRATED 0x40
// The badge's capo is on. Its pad bits are still what was measured; the master
// does the capo substitution, so it can tell a real hand on the neck from a
// clamped capo.
#define FRAME_STATUS_CAPO 0x80

// CRC-8, polynomial 0x07, initial value 0xFF.
inline uint8_t crc8(const uint8_t *data, uint8_t len, uint8_t crc = 0xFF)
{
    while (len--)
    {
        crc ^= *data++;
        for (uint8_t b = 0; b < 8; b++)
        {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

inline uint8_t frameCheck(uint8_t addr, uint8_t seq, uint8_t b0)
{
    uint8_t data[3] = {addr, seq, b0};
    return crc8(data, sizeof(data));
}

// The answer to a read after CMD_DEBUG, all little-endian:
//   0..11  raw reading, uint16 per pad
//   12..23 baseline, uint16 per pad
//   24     on mask      25 negative mask
//   26..27 pads re-baselined since boot
//   28..29 last scan time, us
//   30..31 last calibration time, ms
//   32     crc8 of bytes 0..31, seeded with the badge address
#define DEBUG_LEN 33
