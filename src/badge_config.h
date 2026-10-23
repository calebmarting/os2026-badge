// The badge's ID, stored in flash so one firmware image can be flashed to every
// badge and each one boots into its own role.
#pragma once

#include <Arduino.h>

// 0 on a badge that has never been configured, which makes a fresh badge come
// up as the master.
uint8_t loadBadgeId();

// Blocking address editor. Shows the current ID in binary on the slider LEDs
// (most significant bit leftmost) with LED_BUILTIN blinking to mark the mode;
// a short tap increments the ID, a long press saves it.
// Never returns - it reboots either way, because the master and slave roles
// need Wire configured in different modes.
void runSetupMode(uint8_t id);

// The "id" console command, on any badge: no argument prints this badge's ID,
// "id N" saves N and reboots into that role - the same as finishing setup
// mode, without the button. Only returns if there was nothing to change.
void consoleIdCommand(const char *arg, uint8_t currentId);
