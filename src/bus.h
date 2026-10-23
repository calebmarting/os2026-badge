// The I2C bus between the badges: setup, timeouts, recovery, and checked
// block reads that survive a glitch.
#pragma once

#include <Arduino.h>

// Master: Wire as controller at 100 kHz, plus the timeouts and SDA hold time.
void busBegin();
// Fret badge: call after Wire.begin(address) - SDA hold time and the SCL-low
// timeout on its side of the bus.
void busSlaveConfigure();

// If a glitch left the bus busy (or its state unknown), clock out whatever a
// badge is stuck in and re-initialise. Returns true if it had to. Cheap enough
// to call before every transaction.
bool busRecoverIfStuck();
// Recoveries since the last call, for the stats line.
uint16_t busTakeRecoveryCount();

// Writes cmd to addr, then reads a len-byte block whose last byte is a CRC-8
// seeded with the address (the debug and OTA status blocks). Up to `attempts`
// tries, recovering the bus between them. True if a block checked out.
bool busReadBlock(uint8_t addr, uint8_t cmd, uint8_t *block, uint8_t len, uint8_t attempts);

// One attempt, saying how it went - for bustest.
enum BusReadResult : uint8_t
{
    BUS_READ_OK,
    BUS_READ_NACK,    // the command write was not acknowledged
    BUS_READ_SHORT,   // the read ended before len bytes
    BUS_READ_BAD_CRC, // len bytes arrived but failed the check
};
BusReadResult busReadBlockOnce(uint8_t addr, uint8_t cmd, uint8_t *block, uint8_t len);
// After busReadBlockOnce(): how many bytes requestFrom() delivered, and the
// SERCOM's STATUS register right after it (BUSERR bit 0, ARBLOST bit 1, RXNACK
// bit 2, BUSSTATE bits 4-5, LOWTOUT bit 6).
void busLastReadDetail(uint8_t &got, uint16_t &status);
