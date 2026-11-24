#include "at24c.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "kernel/errstr.h"
#include <cstring>

#define I2C_TIMEOUT_US 10000
#define WRITE_WAIT_US  20000  // a page takes <= 5 ms (24C256/512 datasheets); generous

static bool no_answer(void)
{
    werrstr("EEPROM (0x%02x, %s) not answering", EEPROM_I2C_ADDR, EEPROM_I2C_NAME);
    return false;
}

bool eeprom_read(uint32_t addr, uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > EEPROM_SIZE) { werrstr("EEPROM: read outside the chip"); return false; }
    uint8_t a[2] = { (uint8_t)(addr >> 8), (uint8_t)addr };
    if (i2c_write_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, a, 2, true, I2C_TIMEOUT_US) != 2)
        return no_answer();
    return i2c_read_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, buf, len, false,
                               I2C_TIMEOUT_US + len * 100) == len || no_answer();
}

bool eeprom_page_start(uint32_t addr, const uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > EEPROM_SIZE) { werrstr("EEPROM: write outside the chip"); return false; }
    int room = EEPROM_PAGE - (int)(addr % EEPROM_PAGE);
    if (len > room) len = room;                    // never across a page boundary
    uint8_t tmp[2 + EEPROM_PAGE];
    tmp[0] = addr >> 8;
    tmp[1] = addr;
    memcpy(tmp + 2, buf, len);
    return i2c_write_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, tmp, len + 2, false,
                                I2C_TIMEOUT_US + len * 100) == len + 2 || no_answer();
}

// Programming a page, the chip doesn't ACK its address: a 1-byte read is
// the simplest transfer that tests the ACK.
bool eeprom_ready(void)
{
    uint8_t b;
    return i2c_read_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, &b, 1, false, 1000) == 1;
}

// While the chip programs a page it doesn't ACK its address; poll until
// it does (a 1-byte read is the simplest transfer that tests the ACK).
static bool wait_ready(void)
{
    absolute_time_t deadline = make_timeout_time_us(WRITE_WAIT_US);
    uint8_t b;
    do {
        if (i2c_read_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, &b, 1, false, 1000) == 1)
            return true;
    } while (!time_reached(deadline));
    return false;
}

bool eeprom_write(uint32_t addr, const uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > EEPROM_SIZE)
        return false;
    uint8_t tmp[2 + EEPROM_PAGE];
    while (len > 0) {
        // Never cross a page boundary: the chip would wrap around to the
        // start of the same page and overwrite it.
        int room = EEPROM_PAGE - (int)(addr % EEPROM_PAGE);
        int n = len < room ? len : room;
        tmp[0] = addr >> 8;
        tmp[1] = addr;
        memcpy(tmp + 2, buf, n);
        if (i2c_write_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, tmp, n + 2, false,
                                 I2C_TIMEOUT_US + n * 100) != n + 2)
            return false;
        if (!wait_ready())
            return false;
        addr += n;
        buf += n;
        len -= n;
    }
    return true;
}
