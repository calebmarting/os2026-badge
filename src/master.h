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

// Blinks "hands off" on every badge, then calibrates each one in turn. Runs at
// boot, and on demand from a double tap of the master's button.
void masterRecalibrate();

// Something was played or pressed: leaves attract mode and restarts its idle
// countdown. Touches are noticed on their own; main calls this for the button.
void masterNoteActivity();

// Realistic strumming (a fret change stops a ringing note) against ringing
// mode (notes keep sounding and follow the frets). Left eye lit = ringing.
void masterToggleRealistic();
