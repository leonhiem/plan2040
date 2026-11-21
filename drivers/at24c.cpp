#include "at24c.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <cstring>

#define I2C_TIMEOUT_US 10000
#define WRITE_WAIT_US  20000  // a page takes <= 5 ms (24C256/512 datasheets); generous

bool eeprom_read(uint32_t addr, uint8_t *buf, int len)
{
    if (len <= 0 || addr + len > EEPROM_SIZE)
        return false;
    uint8_t a[2] = { (uint8_t)(addr >> 8), (uint8_t)addr };
    if (i2c_write_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, a, 2, true, I2C_TIMEOUT_US) != 2)
        return false;
    return i2c_read_timeout_us(EEPROM_I2C, EEPROM_I2C_ADDR, buf, len, false,
                               I2C_TIMEOUT_US + len * 100) == len;
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
