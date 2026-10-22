// Fret badge: IDs 1..11, where badge N is fret N.
//
// It measures its pads only when the master hands it the scan token, answers
// with a frame of which pads are held, and plays whatever frequency the master
// writes to it.
#pragma once

#include <Arduino.h>

void slaveBegin(uint8_t id);
void slaveLoop();

// Toggles the capo. With it on, this badge reports every string as fretted and
// stops reading its pads at all - exactly what clamping a capo across the neck
// at this fret does. Highest-fret-wins means fretting above it still overrides.
void slaveToggleCapo();
