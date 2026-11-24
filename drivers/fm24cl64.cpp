// drivers/fm24cl64.cpp — see fm24cl64.h. A failure says why (errstr,
// drivers/README.md rule 3, 2026-11-24): "FRAM (0x50, local bus) not
// answering".
#include "fm24cl64.h"
#include "board.h"
#include "kernel/errstr.h"
#include <cstring>

#define I2C_TIMEOUT_US 10000
#define WRITE_CHUNK    64

static bool no_answer(void)
{
    werrstr("FRAM (0x%02x, %s) not answering", FRAM_I2C_ADDR, FRAM_I2C_NAME);
    return false;
}

static bool outside(uint16_t addr, int len)
{
    werrstr("FRAM: %d bytes at 0x%04x are outside the chip (%d bytes)", len, addr, FRAM_SIZE);
    return false;
}

bool fram_read(uint16_t addr, uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > FRAM_SIZE)
        return outside(addr, len);
    uint8_t a[2] = { (uint8_t)(addr >> 8), (uint8_t)addr };
    if (i2c_write_timeout_us(FRAM_I2C, FRAM_I2C_ADDR, a, 2, true, I2C_TIMEOUT_US) != 2)
        return no_answer();
    if (i2c_read_timeout_us(FRAM_I2C, FRAM_I2C_ADDR, buf, len, false,
                            I2C_TIMEOUT_US + len * 100) != len)
        return no_answer();
    return true;
}

bool fram_write(uint16_t addr, const uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > FRAM_SIZE)
        return outside(addr, len);
    uint8_t tmp[2 + WRITE_CHUNK];
    while (len > 0) {
        int n = len < WRITE_CHUNK ? len : WRITE_CHUNK;
        tmp[0] = addr >> 8;
        tmp[1] = addr;
        memcpy(tmp + 2, buf, n);
        if (i2c_write_timeout_us(FRAM_I2C, FRAM_I2C_ADDR, tmp, n + 2, false,
                                 I2C_TIMEOUT_US + n * 100) != n + 2)
            return no_answer();
        addr += n;
        buf += n;
        len -= n;
    }
    return true;
}
