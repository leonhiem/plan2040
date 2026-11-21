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
  real `board.h`. `fake_bw/board.h` is the real babywarmer_v1 pin map
  (for the babywarmer app's tests).
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
| `bwsim` | the whole babywarmer controller for 110 simulated minutes: the real scheduler, shell, jobs, babywarmer boot script, devices and programs; only the sensors are fakes, fed by a rough thermal model. MANUAL → auto (boost, coast, pid/safe), UP twice, the sensor off the baby and back, then a dead ADS1115. What the jobs print while the ADS1115 is dead is counted, not shown |
| `ntctest` | babywarmer's NTC conversion (`apps/babywarmer/ntc.cpp`): degrees at the interesting ADC codes, incl. the wrap above 21742 |

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
