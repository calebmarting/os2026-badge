// Line-based commands over a badge's own USB serial. Type "help" on the master
// for the list; a fret badge only knows "debug" and "help".
#pragma once

#include <Arduino.h>

// Collects characters without blocking. Returns true once a whole line has
// arrived, with it in `line`: trimmed, lower-cased, NUL-terminated.
bool consoleReadLine(char *line, size_t size);
