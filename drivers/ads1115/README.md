# drivers/ads1115 — TI ADS1115 16-bit ADC

Antonio González's **pico-ads1115** library (BSD 3-Clause, see
[`LICENSE`](LICENSE)), copied in 2026-11-21 from Leon's local copy
`../pico-ads1115/lib` (files dated 2023-12-05), which picoos built
against with `add_subdirectory()`. Copied so plan2040 builds without a
sibling directory. The first commit of these files is the library as it
was; the changes come in a commit of their own.

**Changes for plan2040** (all marked `plan2040:` in the source):

- Every I2C transfer has a timeout (`i2c_*_timeout_us`, 10 ms), and the
  wait for a conversion has a limit (200 ms; one conversion at the
  slowest rate, 8 SPS, is 125 ms).
- `ads1115_init()`, `ads1115_read_adc()`, `ads1115_read_config()` and
  `ads1115_write_config()` return `true`/`false` (were `void`).
- `ads1115_raw_to_volts()`: PGA codes 110 and 111 (both ±0.256 V, like
  101) left `fsr` unset; they fall under the ±0.256 V case now. Found by
  the compiler's `-Wmaybe-uninitialized` (picoos had that warning off).

Why: with the blocking calls, a missing or badly wired ADS1115 (a loose
wire on the header bus) left the config register copy holding garbage,
and the "conversion busy" loop could then spin until the 8 s watchdog
reset the board, after every reboot again. Now a device reading it
reports an error and everything else keeps running.

Bus and address come from `board.h` (`ADS1115_I2C`, `ADS1115_I2C_ADDR`).
