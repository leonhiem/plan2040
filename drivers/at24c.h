/**
 * drivers/at24c.h — 24C256 / 24C512 I2C EEPROM (32 / 64 KB)
 *
 * The NTC lookup table's home (TODO.md idea 3, "EEPROM with the NTC
 * lookup table"). One I2C address for the whole chip, 2-byte memory
 * address, as with the FRAM. Unlike the FRAM:
 *
 * - writes go in pages (EEPROM_PAGE bytes, 64 on the 24C256, 128 on the
 *   24C512) and each page takes up to ~5 ms to program; eeprom_write()
 *   splits at page boundaries and waits each one out (ACK polling),
 *   so writing 1 KB takes ~40-80 ms, the whole 64 KB ~3 s;
 * - it wears out (~1M writes per cell): for a table written once and
 *   read often, not for a counter or a log (that's the FRAM's job).
 *
 * Reads can start anywhere and be any length; a 1 KB read is ~50 ms at
 * 200 kHz, so periodic work reads in small pieces. Bus, address, size and
 * page size come from board.h (EEPROM_I2C, EEPROM_I2C_ADDR, EEPROM_SIZE,
 * EEPROM_PAGE). Blocking.
 */
#pragma once

#include <stdint.h>

bool eeprom_read(uint32_t addr, uint8_t *buf, int len);

// Writes any length, splitting at pages and waiting out each page's ~5 ms
// programming time. Blocks: for bringup and boot only (drivers/README.md).
bool eeprom_write(uint32_t addr, const uint8_t *buf, int len);

// The same without waiting (2026-11-24), for writing while the system runs:
// eeprom_page_start() sends up to the end of addr's page and returns at
// once; the chip then programs it (~5 ms) and doesn't answer meanwhile;
// eeprom_ready() says whether it's done (true) or still busy (false). The
// caller gives up after a while (a chip that never answers again is gone).
// Failures say why in errstr().
bool eeprom_page_start(uint32_t addr, const uint8_t *buf, int len);
bool eeprom_ready(void);
