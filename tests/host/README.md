# Host tests (PC)

The PC-side tests of plan2040, built up during the cpu_rp2040 sessions
(2026-11-10 .. 2026-11-21) and committed with the restructure into one
tree for all boards (TODO.md idea 3, step 1). They compile the REAL
sources (kernel/, fs/, dev/, prog/) with g++ on the PC, against fakes:

- `fake/board.h`: the FRAM's size and layout (FRAM_SIZE, SYSAREA_*), in
  place of a real `boards/<board>/board.h`.
- `fake/pico/stdlib.h`: a fake clock (`fake_now`, `sleep_ms()` moves it)
  and keyboard input from a string (`getchar_timeout_us`).
- `fake/hardware/gpio.h`, `pwm.h`, `i2c.h`: pin levels in `fake_gpio[]`
  (with a hook on every `gpio_put()`), PWM levels, just enough I2C for a
  real `board.h` (a test that runs a driver implements the two I2C calls:
  `adctest`'s fake ADS1115); `spi.h` counts the bytes sent. These are C-compatible:
  the TFT code is C, built with `$(CC)`. `fake_bw/board.h` is the real
  babywarmer_v1 pin map (for the babywarmer app's tests).
- `fakefram.cpp`, `fakefram2.cpp`: the FRAM as an 8 KB array, with an
  optional power cut after N writes, and read failures.

Run them from this directory:

    make check      # build, run each test, diff its output against expected/
    make expected   # rewrite expected/ after a deliberate change
    make clean

`make check` prints `ok` or `FAIL` per test. On a FAIL, `diff
expected/<test>.out <test>.out` shows what changed. When the change is
intended (e.g. a new message), run `make expected` and check `git diff
expected/` before committing: the expected files are the record of what
the shell did, line by line.

## The tests

| Test | What it covers |
|---|---|
| `fstest` | framfs: mkfs, create/write/read/append, full disk, full directory, a power cut at every write (always old or new contents, never half) |
| `hourstest` | hour counters in the system area: A/B records, torn writes, never backwards, mkfs leaves them alone |
| `fscktest` | fsck on damaged filesystems: leaked blocks, crossed chains, what it may and may not fix |
| `mvcut` | `mv` (rename) with a power cut at every write |
| `sim` | a whole shell session: pipes, redirects, ls, grep, wc, head/tail, tee, rm, df |
| `bootsim` | a firmware built with `FRAM_BOOT=0` (the babywarmer): at power-up the built-in boot script runs although a `/fram/boot` exists, and says so; `run /fram/boot` still runs it by hand |
| `quotesim` | shell quotes `"..."` / `'...'`, `#` comments, too many arguments, empty pipelines |
| `cronsim` | cron minute by minute with a fake RTC, `/dev/cons` |
| `catsim` | `cat > file` / `cat >> file` typing mode |
| `cpsim` | `cp` and `mv`, onto mounts and onto themselves |
| `remsim` | `remember` with a fake lamp and button |
| `hexsim` | `hexdump -s -n` on files and `/dev/fram` |
| `rmsim` | `rm` walkthrough, with hexdumps of what stays behind |
| `fscksim` | `fsck /fram` from the shell |
| `helpsim` | `help` output |
| `progtest` | `wc`, `grep`, `head`, `tail` on their own, self-checking |
| `outtest` | babywarmer's outputs on fake GPIO/PWM with the real babywarmer_v1 pin map: `/dev/heater`'s SSR ticks per 15 s window and the bargraph, `/dev/relay`, `/dev/alarm`, and the exact 56 bits `/dev/seg7big`, `/dev/seg7small`, `/dev/leds` shift into the 74HC595s |
| `bwsim` | the whole babywarmer controller for 110 simulated minutes: the real scheduler, shell, jobs, babywarmer boot script, devices and programs; only the sensors are fakes, fed by a rough thermal model. the guard's cases: the sensor out from the start, falling off while warm, `kill` of the control job, heater faults, a dead ADS1115 (none, manual) and back; step 5's field-warmer control: the feed-forward, a +2 °C setpoint re-boost, the room NTC down, manual at 10 % and 5 % per press, `monitor`'s field line; also START and `run tftmon` (the TFT code runs too, on a fake SPI). The thermal model is calibrated to the room table (39 % at 24 °C keeps 36 °C). The status lines show both 7-segment displays, decoded from the 74HC595 bits (`~` blinking, `-` blank), and the CHK LED. What the jobs print while the ADS1115 is dead is counted, not shown |
| `adctest` | our ADS1115 driver and the babywarmer's background sampling (`ntcadc.cpp`, the real scheduler) against a fake ADS1115 with a real conversion time: how fast a change shows, CPU/bus time normally and with each fault (not answering, stuck bus, conversion never finishing), when and with what text the devices fail, how fast they recover, and that one glitch never shows |
| `luttest` | the NTC table generator (`apps/babywarmer/ntclut.cpp`) against the physics computed independently: the built-in MF58 table, a Steinhart-Hart calibration from exact and from slightly wrong points, the agnostic interpolation, the round trip, and every error a calibration text can have |
| `btntest` | `drivers/buttons.cpp` with a consumer polling every 150 ms as `adjust` does: a press is one step, UP/DOWN held repeat (3 s: 17 steps of 0.1 °C), MANUAL and MUTE held never repeat |
| `drvtest` | the FRAM and RTC drivers' reasons (drivers/README.md rule 3): the real `fm24cl64`, `pcf8563`, `dev/rtc`, `dev/fram`, `framfs` on a fake I2C bus whose chips can stop answering: `RTC (0x51, local bus) not answering`, wrong dates refused with what's wrong, `/fram` mounted again by itself when the FRAM answers again; a boot with both chips silent (a fresh process) |
| `eetest` | `/dev/ntc/*` with the real EEPROM driver against a fake 24C512 (pages, ~5 ms busy): writing a calibration in the background, verifying, booting again (a fresh process, the same EEPROM image), a power cut halfway, a damaged byte, refused texts |

## Notes

- The sims register only the devices and programs they need, by their
  real names (`dev_fram_register()`, `prog_cat_register()`, ...), not
  through a config's generated `conf.cpp`.
- `quotesim` starts with `kill 1`: the built-in boot script starts
  `toggle ... &`, and `toggle` isn't in that sim (it would print "not
  found" every tick).
- The PC is not a Cortex-M0+: a PC test won't show an unaligned 32-bit
  load (that faults on the RP2040). Use memcpy from byte buffers.
- Re-run the sims after any change to the shell or the parser.
