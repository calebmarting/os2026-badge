// Master badge: ID 0, the I2C controller and the strumming hand.
//
// Its 6 touch pads are the strings. It polls every fret badge for held pads,
// resolves the highest fret per string, and writes the resulting pitch out to
// the badges that sound that string.
#pragma once

#include <Arduino.h>

void masterBegin();
void masterLoop();

// Switches between guitar and ukulele. Silences the whole neck on the way
// through, since the badge->string mapping changes with the instrument.
void masterToggleUkulele();
