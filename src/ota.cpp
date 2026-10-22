#include "ota.h"
#include "badge_hw.h"
#include "guitar_config.h"
#include "nvm.h"
#include "scan_protocol.h"

#include <Wire.h>

#define MAX_PAGES (APP_MAX_SIZE / NVM_PAGE_SIZE)

static uint32_t pagesFor(uint32_t size)
{
    return (size + NVM_PAGE_SIZE - 1) / NVM_PAGE_SIZE;
}

static uint32_t rowsFor(uint32_t size)
{
    return (size + NVM_ROW_SIZE - 1) / NVM_ROW_SIZE;
}

// Covers the page index and the data, so a page can never land at the wrong
// offset either.
static uint8_t packetCheck(const uint8_t *packet)
{
    return crc8(packet + 1, 2 + NVM_PAGE_SIZE);
}

static uint32_t getU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void putU32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = v >> 24;
}

// ---- Fret badge ------------------------------------------------------------

static uint8_t myAddress = 0;

static volatile uint8_t state = OTA_IDLE;
static volatile uint32_t imageSize = 0;
static volatile uint32_t imageCrc = 0;
static volatile bool beginPending = false;
static volatile bool commitPending = false;
static volatile bool abortPending = false;
static volatile bool statusNext = false;
static volatile uint8_t statusBlock[OTA_STATUS_LEN];

// A page takes ~6.5 ms on the wire at 100 kHz but only ~2.5 ms to program, so
// a short queue is plenty. A page that does not fit is simply dropped and
// repaired later.
#define PACKET_QUEUE 4
static volatile uint8_t queue[PACKET_QUEUE][OTA_PAGE_PACKET_LEN];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;

static uint8_t received[MAX_PAGES / 8];
static uint16_t totalPages = 0;
static uint16_t receivedPages = 0;

bool otaSlaveReceive(uint8_t cmd, int count)
{
    switch (cmd)
    {
    case CMD_OTA_BEGIN:
        if (count >= 9)
        {
            uint8_t b[8];
            for (uint8_t i = 0; i < 8; i++)
            {
                b[i] = Wire.read();
            }
            imageSize = getU32(b);
            imageCrc = getU32(b + 4);
            state = OTA_ERASING;
            beginPending = true;
        }
        return true;
    case CMD_OTA_DATA:
        if (count >= OTA_PAGE_PACKET_LEN && state == OTA_RECEIVING)
        {
            uint8_t next = (queueHead + 1) % PACKET_QUEUE;
            if (next != queueTail)
            {
                queue[queueHead][0] = cmd;
                for (uint8_t i = 1; i < OTA_PAGE_PACKET_LEN; i++)
                {
                    queue[queueHead][i] = Wire.read();
                }
                queueHead = next;
            }
        }
        return true;
    case CMD_OTA_STATUS:
        statusNext = true;
        return true;
    case CMD_OTA_COMMIT:
        if (count >= 5)
        {
            uint8_t b[4];
            for (uint8_t i = 0; i < 4; i++)
            {
                b[i] = Wire.read();
            }
            if (state == OTA_VERIFIED && getU32(b) == imageCrc)
            {
                commitPending = true;
            }
        }
        return true;
    case CMD_OTA_ABORT:
        abortPending = true;
        return true;
    default:
        return false;
    }
}

bool otaSlaveRequest()
{
    if (!statusNext)
    {
        return false;
    }
    statusNext = false;
    Wire.write((const uint8_t *)statusBlock, OTA_STATUS_LEN);
    return true;
}

static uint16_t firstMissing()
{
    for (uint16_t page = 0; page < totalPages; page++)
    {
        if (!(received[page / 8] & (1 << (page % 8))))
        {
            return page;
        }
    }
    return 0xFFFF;
}

static void refreshStatus()
{
    uint8_t block[OTA_STATUS_LEN];
    block[0] = state;
    uint16_t missing = (state == OTA_RECEIVING) ? firstMissing() : 0xFFFF;
    block[1] = missing & 0xFF;
    block[2] = missing >> 8;
    putU32(block + 3, runningImageCrc());
    block[7] = crc8(block, OTA_STATUS_LEN - 1, crc8(&myAddress, 1));

    noInterrupts();
    for (uint8_t i = 0; i < OTA_STATUS_LEN; i++)
    {
        statusBlock[i] = block[i];
    }
    interrupts();
}

void otaSlaveBegin(uint8_t address)
{
    myAddress = address;
    refreshStatus();
}

static void resetReception()
{
    memset(received, 0, sizeof(received));
    receivedPages = 0;
    noInterrupts();
    queueTail = queueHead;
    interrupts();
}

bool otaSlaveLoop()
{
    if (abortPending)
    {
        abortPending = false;
        beginPending = false;
        // The master general-calls an abort at every boot, so most of these
        // arrive with nothing to abort.
        if (state != OTA_IDLE)
        {
            state = OTA_IDLE;
            refreshStatus();
            setSliderLeds(0);
            Serial.println("Update aborted");
        }
        return false;
    }

    if (beginPending)
    {
        beginPending = false;
        noTone(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, LOW);

        uint32_t size = imageSize;
        if (size == 0 || size > APP_MAX_SIZE)
        {
            state = OTA_IDLE;
            refreshStatus();
            return false;
        }
        Serial.print("Update: erasing for ");
        Serial.print(size);
        Serial.println(" bytes");
        refreshStatus();
        for (uint32_t row = 0; row < rowsFor(size); row++)
        {
            nvmEraseRow(STAGING_START + row * NVM_ROW_SIZE);
        }
        totalPages = pagesFor(size);
        resetReception();
        state = OTA_RECEIVING;
        refreshStatus();
    }

    if (state == OTA_IDLE)
    {
        return false;
    }

    bool progressed = false;
    while (queueTail != queueHead)
    {
        uint8_t packet[OTA_PAGE_PACKET_LEN];
        for (uint8_t i = 0; i < OTA_PAGE_PACKET_LEN; i++)
        {
            packet[i] = queue[queueTail][i];
        }
        queueTail = (queueTail + 1) % PACKET_QUEUE;

        uint16_t page = packet[1] | (packet[2] << 8);
        if (packetCheck(packet) != packet[OTA_PAGE_PACKET_LEN - 1] || page >= totalPages ||
            (received[page / 8] & (1 << (page % 8))))
        {
            continue;
        }
        nvmWritePage(STAGING_START + (uint32_t)page * NVM_PAGE_SIZE, packet + 3);
        received[page / 8] |= (1 << (page % 8));
        receivedPages++;
        progressed = true;
    }

    if (state == OTA_RECEIVING && receivedPages == totalPages)
    {
        state = OTA_VERIFYING;
        refreshStatus();
        uint32_t size = imageSize;
        const uint8_t *staged = (const uint8_t *)STAGING_START;
        if (crc32Update(0, staged, size) == imageCrc && imageLooksValid(staged, size))
        {
            state = OTA_VERIFIED;
            Serial.println("Update: verified");
        }
        else
        {
            // Start over; the master will notice page 0 is missing again.
            Serial.println("Update: CRC mismatch, receiving again");
            resetReception();
            state = OTA_RECEIVING;
        }
        progressed = true;
    }

    if (progressed)
    {
        refreshStatus();
        // Progress bar: nobody is measuring during an update, so the LEDs are
        // free to show it.
        uint8_t lit = totalPages ? (uint32_t)receivedPages * (NUM_SLIDER_LEDS + 1) / (totalPages + 1) : 0;
        setSliderLeds((1 << lit) - 1);
    }

    if (commitPending)
    {
        Serial.println("Update: installing");
        Serial.flush();
        nvmInstallStaged(imageSize);
    }
    return true;
}

// ---- Master ----------------------------------------------------------------

static bool queryStatus(uint8_t id, uint8_t *block)
{
    uint8_t addr = wireAddress(id);
    Wire.beginTransmission(addr);
    Wire.write(CMD_OTA_STATUS);
    if (Wire.endTransmission() != 0 || Wire.requestFrom(addr, (uint8_t)OTA_STATUS_LEN) != OTA_STATUS_LEN)
    {
        return false;
    }
    for (uint8_t i = 0; i < OTA_STATUS_LEN; i++)
    {
        block[i] = Wire.read();
    }
    return crc8(block, OTA_STATUS_LEN - 1, crc8(&addr, 1)) == block[OTA_STATUS_LEN - 1];
}

bool otaQueryRunningCrc(uint8_t id, uint32_t &crc)
{
    uint8_t block[OTA_STATUS_LEN];
    if (!queryStatus(id, block))
    {
        return false;
    }
    crc = getU32(block + 3);
    return true;
}

static void sendCommand(uint8_t addr, uint8_t cmd, const uint8_t *args, uint8_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(cmd);
    Wire.write(args, len);
    Wire.endTransmission();
}

static void sendPage(uint8_t addr, uint16_t page)
{
    uint8_t packet[OTA_PAGE_PACKET_LEN];
    packet[0] = CMD_OTA_DATA;
    packet[1] = page & 0xFF;
    packet[2] = page >> 8;
    memcpy(packet + 3, (const void *)(STAGING_START + (uint32_t)page * NVM_PAGE_SIZE), NVM_PAGE_SIZE);
    packet[OTA_PAGE_PACKET_LEN - 1] = packetCheck(packet);
    Wire.beginTransmission(addr);
    Wire.write(packet, OTA_PAGE_PACKET_LEN);
    Wire.endTransmission();
}

static void fail(const char *why)
{
    Serial.print("OTA FAIL ");
    Serial.println(why);
}

// Stage the image from USB into this badge's own flash. The host sends it in
// 256-byte chunks and waits for an ACK after each, so the CDC buffer never
// overflows while a row is being written.
static bool receiveFromHost(uint32_t size, uint32_t crc)
{
    for (uint32_t row = 0; row < rowsFor(size); row++)
    {
        nvmEraseRow(STAGING_START + row * NVM_ROW_SIZE);
    }
    Serial.println("OTA READY");

    uint8_t chunk[NVM_ROW_SIZE];
    uint32_t offset = 0;
    while (offset < size)
    {
        uint32_t want = min((uint32_t)NVM_ROW_SIZE, size - offset);
        uint32_t got = 0;
        uint32_t lastByteMs = millis();
        while (got < want)
        {
            if (Serial.available() > 0)
            {
                chunk[got++] = Serial.read();
                lastByteMs = millis();
            }
            else if (millis() - lastByteMs > OTA_HOST_TIMEOUT_MS)
            {
                fail("timed out waiting for the image");
                return false;
            }
        }
        memset(chunk + got, 0xFF, sizeof(chunk) - got);
        for (uint32_t p = 0; p < NVM_ROW_SIZE; p += NVM_PAGE_SIZE)
        {
            nvmWritePage(STAGING_START + offset + p, chunk + p);
        }
        offset += want;
        Serial.print("OTA ACK ");
        Serial.println(offset);
    }

    const uint8_t *staged = (const uint8_t *)STAGING_START;
    if (crc32Update(0, staged, size) != crc)
    {
        fail("CRC mismatch in the master's staged copy");
        return false;
    }
    if (!imageLooksValid(staged, size))
    {
        fail("that is not a firmware image for this board");
        return false;
    }
    Serial.println("OTA STAGED");
    return true;
}

// Brings one badge's staged copy to a verified state, re-sending only the
// pages it reports missing.
static bool repairBadge(uint8_t id, uint32_t totalPagesToSend)
{
    uint8_t addr = wireAddress(id);
    uint32_t resends = 0;
    uint32_t startMs = millis();
    while (millis() - startMs < OTA_BADGE_TIMEOUT_MS)
    {
        uint8_t block[OTA_STATUS_LEN];
        if (!queryStatus(id, block))
        {
            delay(10);
            continue;
        }
        uint8_t st = block[0];
        uint16_t missing = block[1] | (block[2] << 8);
        if (st == OTA_VERIFIED)
        {
            if (resends > 0)
            {
                Serial.print("OTA fret ");
                Serial.print(id);
                Serial.print(" needed ");
                Serial.print(resends);
                Serial.println(" pages re-sent");
            }
            return true;
        }
        if (st == OTA_RECEIVING && missing < totalPagesToSend)
        {
            sendPage(addr, missing);
            resends++;
            // Give it time to program the page before asking again.
            delay(4);
        }
        else
        {
            delay(10); // erasing or verifying
        }
    }
    return false;
}

void otaMasterRun(uint32_t size, uint32_t crc)
{
    if (size == 0 || size > APP_MAX_SIZE)
    {
        fail("image too large for the staging area");
        return;
    }
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    if (!receiveFromHost(size, crc))
    {
        return;
    }

    // Everyone stops and erases. Then poll until each badge is ready, which
    // also finds out who is actually on the neck.
    uint8_t begin[8];
    putU32(begin, size);
    putU32(begin + 4, crc);
    sendCommand(GENERAL_CALL_ADDR, CMD_OTA_BEGIN, begin, sizeof(begin));

    bool present[MAX_BADGES] = {false};
    uint32_t eraseDeadlineMs = millis() + rowsFor(size) * OTA_ROW_ERASE_MS + 500;
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        uint8_t block[OTA_STATUS_LEN];
        while (true)
        {
            bool answered = queryStatus(id, block);
            if (answered && block[0] == OTA_RECEIVING)
            {
                present[id] = true;
                break;
            }
            if ((int32_t)(millis() - eraseDeadlineMs) > 0)
            {
                break;
            }
            delay(answered ? 20 : 5);
        }
    }
    Serial.print("OTA updating frets:");
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (present[id])
        {
            Serial.print(' ');
            Serial.print(id);
        }
    }
    Serial.println();

    // One pass to everyone at once.
    uint32_t pages = pagesFor(size);
    for (uint32_t page = 0; page < pages; page++)
    {
        sendPage(GENERAL_CALL_ADDR, page);
        if (page % 64 == 63)
        {
            Serial.print("OTA SENT ");
            Serial.print((page + 1) * 100 / pages);
            Serial.println("%");
        }
    }

    // Then fix up each badge individually.
    bool ok[MAX_BADGES] = {false};
    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (!present[id])
        {
            continue;
        }
        ok[id] = repairBadge(id, pages);
        Serial.print("OTA fret ");
        Serial.print(id);
        Serial.println(ok[id] ? " verified" : " FAILED - keeps its old firmware");
    }

    for (uint8_t id = 1; id < MAX_BADGES; id++)
    {
        if (!present[id])
        {
            continue;
        }
        if (ok[id])
        {
            uint8_t args[4];
            putU32(args, crc);
            sendCommand(wireAddress(id), CMD_OTA_COMMIT, args, sizeof(args));
        }
        else
        {
            sendCommand(wireAddress(id), CMD_OTA_ABORT, nullptr, 0);
        }
    }

    // A badge copying its image has interrupts off and would stretch any
    // general call, so the master stays quiet until they are all done before
    // rebooting into code that starts by talking to them.
    Serial.println("OTA frets installing");
    delay(rowsFor(size) * OTA_ROW_INSTALL_MS + 500);

    Serial.println("OTA DONE - master installing and rebooting");
    Serial.flush();
    delay(50);
    nvmInstallStaged(size);
}
