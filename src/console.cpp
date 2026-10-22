#include "console.h"

static char pending[64];
static size_t pendingLen = 0;

bool consoleReadLine(char *line, size_t size)
{
    while (Serial.available() > 0)
    {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n')
        {
            if (pendingLen == 0)
            {
                continue; // the other half of a CRLF, or a blank line
            }
            size_t start = 0;
            while (start < pendingLen && pending[start] == ' ')
            {
                start++;
            }
            size_t n = 0;
            for (size_t i = start; i < pendingLen && n + 1 < size; i++)
            {
                line[n++] = (char)tolower(pending[i]);
            }
            while (n > 0 && line[n - 1] == ' ')
            {
                n--;
            }
            line[n] = '\0';
            pendingLen = 0;
            return n > 0;
        }
        if (pendingLen + 1 < sizeof(pending))
        {
            pending[pendingLen++] = c;
        }
    }
    return false;
}
