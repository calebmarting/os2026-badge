// Raw flash access, and the flash map that lets a badge be updated over I2C.
//
//   0x00000 - 0x01FFF  bootloader (never touched)
//   0x02000 - 0x1FFFF  running firmware, at most APP_MAX_SIZE
//   0x20000 - 0x3FEFF  staging: a new image is collected and verified here
//   0x3FF00 - 0x3FFFF  this badge's config (its ID) - outside both images, so
//                      an update keeps it. A USB upload erases it, though:
//                      bossac --erase wipes everything above the bootloader.
#pragma once

#include <Arduino.h>

#define NVM_PAGE_SIZE 64
#define NVM_ROW_SIZE 256
#define APP_START 0x00002000UL
#define STAGING_START 0x00020000UL
#define CONFIG_ROW 0x0003FF00UL
#define APP_MAX_SIZE (STAGING_START - APP_START)

void nvmEraseRow(uint32_t addr);
// One page, which must be page-aligned and inside an erased row.
void nvmWritePage(uint32_t addr, const uint8_t *data);

// Standard CRC-32 (the zlib / Python zlib.crc32 one). Chain calls by passing
// the previous result back in; start from 0.
uint32_t crc32Update(uint32_t crc, const uint8_t *data, uint32_t len);

// Size and CRC-32 of the firmware this badge is running, exactly as the
// firmware.bin it was built from. The CRC is what the master's "info" shows,
// so a mixed neck is easy to spot.
uint32_t runningImageSize();
uint32_t runningImageCrc();

// A firmware image starts with its vector table: an initial stack pointer in
// RAM and a Thumb reset vector inside the image. Refuses anything else.
bool imageLooksValid(const uint8_t *image, uint32_t size);

// Copies the staged image over the running firmware and resets into it. Runs
// from RAM with interrupts off, since it erases the code that called it.
// Never returns. Losing power partway through leaves a half-written app; the
// bootloader is untouched, so a double-tap of reset and a USB upload recover.
[[noreturn]] void nvmInstallStaged(uint32_t size);
