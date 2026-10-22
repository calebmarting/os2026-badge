#include "nvm.h"

// From the linker script: the end of code and constants in flash, and the
// initialised-data block that is stored right after them.
extern uint32_t __etext;
extern uint32_t __data_start__;
extern uint32_t __data_end__;

static void nvmCommand(uint32_t cmd)
{
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | cmd;
    while (!NVMCTRL->INTFLAG.bit.READY)
    {
    }
}

void nvmEraseRow(uint32_t addr)
{
    NVMCTRL->ADDR.reg = addr / 2; // ADDR counts 16-bit words
    nvmCommand(NVMCTRL_CTRLA_CMD_ER);
}

void nvmWritePage(uint32_t addr, const uint8_t *data)
{
    // Manual write: the page is committed by the WP command, not by the last
    // word landing in the page buffer.
    NVMCTRL->CTRLB.bit.MANW = 1;
    nvmCommand(NVMCTRL_CTRLA_CMD_PBC);
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    for (uint8_t i = 0; i < NVM_PAGE_SIZE / 4; i++)
    {
        uint32_t word;
        memcpy(&word, data + 4 * i, 4); // data need not be word-aligned
        dst[i] = word;
    }
    nvmCommand(NVMCTRL_CTRLA_CMD_WP);
}

uint32_t crc32Update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    crc = ~crc;
    while (len--)
    {
        crc ^= *data++;
        for (uint8_t b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
        }
    }
    return ~crc;
}

uint32_t runningImageSize()
{
    uint32_t text = (uint32_t)&__etext - APP_START;
    uint32_t data = (uint32_t)&__data_end__ - (uint32_t)&__data_start__;
    return text + data;
}

uint32_t runningImageCrc()
{
    // The data block's initial values are stored in flash at __etext, so the
    // image is one contiguous run of flash - exactly firmware.bin.
    static uint32_t crc = 0;
    static bool done = false;
    if (!done)
    {
        crc = crc32Update(0, (const uint8_t *)APP_START, runningImageSize());
        done = true;
    }
    return crc;
}

bool imageLooksValid(const uint8_t *image, uint32_t size)
{
    if (size < 8 || size > APP_MAX_SIZE)
    {
        return false;
    }
    uint32_t sp;
    uint32_t reset;
    memcpy(&sp, image, 4);
    memcpy(&reset, image + 4, 4);
    bool spInRam = sp > HMCRAMC0_ADDR && sp <= HMCRAMC0_ADDR + HMCRAMC0_SIZE;
    bool resetInImage = (reset & 1) && (reset & ~1UL) >= APP_START && (reset & ~1UL) < APP_START + size;
    return spInRam && resetInImage;
}

// Lives in .data, so the startup code copies it to RAM with the rest of the
// initialised data. Everything it touches is a register or a volatile pointer
// - no calls - so nothing it needs is in the flash it is erasing.
__attribute__((section(".data.ramfunc_install"), noinline, noreturn, used))
static void ramInstall(uint32_t size)
{
    __asm volatile("cpsid i" ::: "memory");

    NVMCTRL->CTRLB.bit.MANW = 1;
    for (uint32_t row = APP_START; row < APP_START + size; row += NVM_ROW_SIZE)
    {
        NVMCTRL->ADDR.reg = row >> 1;
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
        while (!NVMCTRL->INTFLAG.bit.READY)
        {
        }

        for (uint32_t page = row; page < row + NVM_ROW_SIZE; page += NVM_PAGE_SIZE)
        {
            NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
            while (!NVMCTRL->INTFLAG.bit.READY)
            {
            }
            volatile uint32_t *dst = (volatile uint32_t *)page;
            const volatile uint32_t *src = (const volatile uint32_t *)(page - APP_START + STAGING_START);
            for (uint32_t i = 0; i < NVM_PAGE_SIZE / 4; i++)
            {
                dst[i] = src[i];
            }
            NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
            while (!NVMCTRL->INTFLAG.bit.READY)
            {
            }
        }
    }

    SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
    __asm volatile("dsb 0xF" ::: "memory");
    while (true)
    {
    }
}

void nvmInstallStaged(uint32_t size)
{
    // Called through a pointer: RAM is too far from flash for a direct BL.
    void (*volatile install)(uint32_t) = ramInstall;
    install(size);
    while (true)
    {
    }
}
