# drivers/ — chips, and the rules every driver follows

A driver knows one chip: its registers, its timing, its quirks. Devices
(`dev/`, `apps/<app>/dev/`) use drivers; programs never do (they only
touch paths). Decided 2026-11-23, after the copied ADS1115 library
showed what goes wrong without rules (TODO.md idea 3, step 3e), and from
Leon's three rules for machines: no sluggish interface, don't give up
because one part fails, no cryptic errors.

## The contract

1. **Bus and address from `board.h`**, per chip: `FRAM_I2C` +
   `FRAM_I2C_ADDR`, `ADS1115_I2C` + `ADS1115_I2C_ADDR` +
   `ADS1115_I2C_NAME`, ... Never a bus as a global in the driver, never
   as an argument either: where a chip sits is a fact of the board.
2. **A timeout on every transfer** (`i2c_*_timeout_us`, 10 ms plus the
   bytes' time). Never `i2c_*_blocking`: a missing chip or a stuck bus
   must never hang the board.
3. **Results are true/false** (or a count, -1 on failure), **and a
   failure says why**, with `werrstr()` (`kernel/errstr.h`), naming the
   chip and where it is: `"ADS1115 (0x48, header bus) not answering"`.
   The file layer puts the device's path in front; the user reads
   `cat: /dev/skintemp: ADS1115 (0x48, header bus) not answering`.
4. **Never wait for the chip.** No polling until it's done, no sleeping:
   work that takes the chip time (an ADC conversion, an EEPROM page
   write) is split into steps on later ticks (start it; is it done?; take
   the result). A transfer itself may take what its bytes need (a 1 KB
   FRAM file is ~25 ms at 400 kHz), so periodic work keeps its transfers
   short. A blocking helper is allowed for `bringup` and for boot only,
   and says so (`ads1115_convert()`).
5. **Retrying and backoff live in one place**, the device or task that
   uses the chip (e.g. `apps/babywarmer/ntcadc.cpp`: after a failure,
   wait 1 s before the next try, answer from what's known meanwhile), so
   a broken chip costs ~one timeout per second, not one per tick.
6. **The plain C++ subset** (as `kernel/`): no heap, no exceptions, no
   STL, no templates, no RTTI.
7. **A header comment with the chip's facts**: what it is, the address,
   timing, quirks, and where the driver came from (datasheet, or what it
   replaced), plus how it's meant to be used.

## Where each driver stands (2026-11-23)

| Driver | Chip | 1 board.h | 2 timeouts | 3 errstr | 4 never waits | Notes |
|---|---|---|---|---|---|---|
| `ads1115` | TI ADS1115 ADC | yes | yes | yes | yes (`convert()` for bringup/boot) | written 2026-11-23 from the datasheet, replacing pico-ads1115 |
| `fm24cl64` | FM24CL64B 8 KB FRAM | yes | yes | not yet | yes (no write delay) | |
| `pcf8563` | PCF8563T RTC | yes | yes | not yet | yes | |
| `at24c` | 24C256/24C512 EEPROM | yes | yes | yes | yes: `eeprom_page_start()` + `eeprom_ready()` (2026-11-24); `eeprom_write()` waits, for bringup and boot | the NTC table writes page by page in the background |
| `buttons` | GPIO push buttons | yes (pins) | - | - | yes (timer) | |

"Not yet" gets done as each driver is touched next.
