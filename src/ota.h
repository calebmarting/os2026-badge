// Firmware update over I2C: flash the master once over USB, and it updates
// every fret badge at the same time.
//
//   1. tools/ota_upload.py (or `pio run -t ota`) streams firmware.bin to the
//      master over USB. The master stages it in its own upper flash and checks
//      its CRC-32.
//   2. The master general-calls CMD_OTA_BEGIN; every fret badge stops playing
//      and erases its staging area. Then it general-calls every 64-byte page
//      once, so all eleven badges receive the image in a single pass.
//   3. It asks each badge in turn which page it is missing first, and re-sends
//      just those pages to just that badge, until the badge's own CRC-32 of
//      its staged copy matches.
//   4. Verified badges get CMD_OTA_COMMIT and copy the image into place;
//      anything that failed is told to abort and keeps its old firmware. The
//      master installs itself last, once the badges have had time to finish.
//
// Badge IDs live outside the image (nvm.h), so nobody needs re-addressing.
#pragma once

#include <Arduino.h>

// OTA status block, the answer to a read after CMD_OTA_STATUS:
//   0     state, one of OTA_*
//   1..2  first page still missing, 0xFFFF = none
//   3..6  CRC-32 of the firmware this badge is running
//   7     crc8 of bytes 0..6, seeded with the badge address
#define OTA_STATUS_LEN 8
#define OTA_PAGE_PACKET_LEN (1 + 2 + 64 + 1)

#define OTA_IDLE 0
#define OTA_ERASING 1
#define OTA_RECEIVING 2
#define OTA_VERIFYING 3
#define OTA_VERIFIED 4

// Fret badge side. otaSlaveReceive() is called from the I2C receive ISR with
// the command byte already read; it returns false for a non-OTA command.
bool otaSlaveReceive(uint8_t cmd, int count);
// Called from the I2C request ISR; true if it answered the read.
bool otaSlaveRequest();
void otaSlaveBegin(uint8_t address);
// Does the flash work. Returns true while an update owns the badge, in which
// case the caller must not scan, play or touch the LEDs.
bool otaSlaveLoop();

// Master side: the console's "ota SIZE CRC" lands here, with the image about
// to arrive on Serial. Only returns if the update failed; on success the
// master reboots into the new firmware.
void otaMasterRun(uint32_t size, uint32_t crc);
// Asks one badge for the CRC-32 of the firmware it is running.
bool otaQueryRunningCrc(uint8_t id, uint32_t &crc);
