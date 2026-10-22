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
//   byte 1: the seq of the CMD_SCAN this frame answers
// A badge that missed its token still holds the previous frame, so its seq no
// longer matches and the master can tell it is stale rather than trusting it.
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
