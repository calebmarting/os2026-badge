#include "bus.h"
#include "params.h"
#include "scan_protocol.h"

#include <Wire.h>

static uint16_t recoveries = 0;

// The SERCOM's timeouts count on its slow clock, which the Arduino core never
// turns on - so without this, LOWTOUT silently does nothing. GCLK1 is the
// 32.768 kHz crystal, which is what the 25-35 ms SCL-low timeout assumes. One
// slow clock is shared by every SERCOM.
static void enableSlowClock()
{
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_SERCOMX_SLOW | GCLK_CLKCTRL_GEN_GCLK1 | GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY)
    {
    }
}

// SERCOM2 is the MKR Zero's Wire port. The core busy-waits on the bus with no
// timeout of its own, so a badge that wedges SCL low would hang the whole
// instrument. LOWTOUT aborts any transfer where SCL is held low for 25-35 ms -
// the hardware then sets the very flags those loops wait on, so they fall
// through with an error.
//
// SDAHOLD 2 (300-600 ns) holds SDA a little after SCL falls before changing
// it. With zero hold, a slow or ringing SCL edge on a long chain can make a
// data change look like a START or STOP.
//
// INACTOUT stays off: it would treat any SCL low over ~200 us - including the
// master pausing between bytes for a USB interrupt - as a dead bus.
//
// All three live in CTRLA, which is only writable with the peripheral off.
static void configureMaster()
{
    enableSlowClock();
    SercomI2cm &i2c = SERCOM2->I2CM;
    i2c.CTRLA.bit.ENABLE = 0;
    while (i2c.SYNCBUSY.bit.ENABLE)
    {
    }
    i2c.CTRLA.bit.LOWTOUTEN = 1;
    i2c.CTRLA.bit.INACTOUT = params.inactOut & 3;
    i2c.CTRLA.bit.SDAHOLD = params.sdaHold & 3;
    i2c.CTRLA.bit.ENABLE = 1;
    while (i2c.SYNCBUSY.bit.ENABLE)
    {
    }
    // Same as Wire.begin(): the bus state starts unknown until forced idle.
    i2c.STATUS.bit.BUSSTATE = 1;
    while (i2c.SYNCBUSY.bit.SYSOP)
    {
    }
}

void busBegin()
{
    Wire.begin();
    // 100 kHz, explicitly. With 12 badges and the cabling between them, bus
    // capacitance is close to the 400 pF limit and 400 kHz will not make the
    // edges. setClock() re-initialises CTRLA, so it must come first.
    Wire.setClock(100000);
    configureMaster();
}

void busSlaveConfigure()
{
    enableSlowClock();
    SercomI2cs &i2c = SERCOM2->I2CS;
    i2c.CTRLA.bit.ENABLE = 0;
    while (i2c.SYNCBUSY.bit.ENABLE)
    {
    }
    i2c.CTRLA.bit.SDAHOLD = 2;
    // A fret badge that stalls mid-byte - its CPU busy with interrupts off,
    // say - releases the bus after 25-35 ms instead of holding it forever.
    i2c.CTRLA.bit.LOWTOUTEN = 1;
    i2c.CTRLA.bit.ENABLE = 1;
    while (i2c.SYNCBUSY.bit.ENABLE)
    {
    }
}

// A badge reset or confused mid-byte can be left holding SDA low, waiting for
// clocks that will never come. Nine clocks walk it out of any byte, then a
// STOP frees the bus. Pins are driven open-drain style: LOW, or released to
// the pull-up.
static void recover()
{
    Wire.end();
    pinMode(PIN_WIRE_SDA, INPUT);
    pinMode(PIN_WIRE_SCL, INPUT);
    for (uint8_t i = 0; i < 9 && digitalRead(PIN_WIRE_SDA) == LOW; i++)
    {
        digitalWrite(PIN_WIRE_SCL, LOW);
        pinMode(PIN_WIRE_SCL, OUTPUT);
        delayMicroseconds(5);
        pinMode(PIN_WIRE_SCL, INPUT);
        delayMicroseconds(5);
    }
    digitalWrite(PIN_WIRE_SDA, LOW);
    pinMode(PIN_WIRE_SDA, OUTPUT);
    delayMicroseconds(5);
    pinMode(PIN_WIRE_SDA, INPUT);

    busBegin();
    if (recoveries < 0xFFFF)
    {
        recoveries++;
    }
}

bool busRecoverIfStuck()
{
    // 1 = idle, 2 = this master owns it. Between transactions it should always
    // be idle; busy or unknown means a glitch was mistaken for a START, or a
    // transfer was cut short without its STOP.
    uint8_t state = SERCOM2->I2CM.STATUS.bit.BUSSTATE;
    if (state == 1 || state == 2)
    {
        return false;
    }
    recover();
    return true;
}

uint16_t busTakeRecoveryCount()
{
    uint16_t n = recoveries;
    recoveries = 0;
    return n;
}

// The core's endTransmission() only queues the STOP. If requestFrom() goes
// straight on, the hardware can turn that STOP into a repeated START - and a
// fret badge's Wire only hands a write to onReceive() on a STOP (or a repeated
// START into another write). The read would then be answered before the badge
// has seen the command, with whatever it last had ready: its scan frame. So:
// wait for the STOP to finish, then give the badge's ISR time to run. Scan
// tokens never had this problem - a whole window sits between write and read.
static void waitForCommandToLand()
{
    uint32_t startUs = micros();
    while (SERCOM2->I2CM.STATUS.bit.BUSSTATE != 1 && micros() - startUs < 500)
    {
    }
    delayMicroseconds(params.turnaroundUs);
}

BusReadResult busReadBlockOnce(uint8_t addr, uint8_t cmd, uint8_t *block, uint8_t len)
{
    busRecoverIfStuck();
    Wire.beginTransmission(addr);
    Wire.write(cmd);
    if (Wire.endTransmission() != 0)
    {
        return BUS_READ_NACK;
    }
    waitForCommandToLand();
    uint8_t got = Wire.requestFrom(addr, len);
    for (uint8_t i = 0; i < len; i++)
    {
        block[i] = i < got ? Wire.read() : 0;
    }
    if (got != len)
    {
        return BUS_READ_SHORT;
    }
    return crc8(block, len - 1, crc8(&addr, 1)) == block[len - 1] ? BUS_READ_OK : BUS_READ_BAD_CRC;
}

bool busReadBlock(uint8_t addr, uint8_t cmd, uint8_t *block, uint8_t len, uint8_t attempts)
{
    for (uint8_t attempt = 0; attempt < attempts; attempt++)
    {
        if (busReadBlockOnce(addr, cmd, block, len) == BUS_READ_OK)
        {
            return true;
        }
    }
    busRecoverIfStuck();
    return false;
}
