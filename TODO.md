# TODO

Ideas and open items for plan2040 (was picoos), on cpu_rp2040 so far. Nothing here is decided
until it's picked; the notes are first thoughts on how each would fit,
not a design. Newest ideas first.

## Ideas (2026-11-13)

### 1a. Config files

Now that `/fram` survives power cycles and firmware updates, settings
can live in files instead of in the firmware.

- A lot of this already works without new code, because the boot script
  is a file: `/fram/boot` can hold `echo 36.5 > /dev/setpoint`, and
  `cat /fram/setpoint > /dev/setpoint` restores a value saved earlier
  with `cat /dev/setpoint > /fram/setpoint`. Worth trying that way
  first, and seeing what's actually missing.
- What's missing then is probably the *saving*: something that writes a
  value back to a file when it changes (e.g. after UP/DOWN adjusted the
  setpoint), without wearing anything out (FRAM doesn't wear, so writing
  on every change is fine).
- Formats to choose from: one value per file (`/fram/setpoint` holds
  `36.5`; Plan 9 style, works with `cat` and `>` today) or `key=value`
  lines in one file with small `get`/`set` programs. One value per file
  fits picoos best; the 32-file directory limit is the cost.
- No directories yet, so naming carries the grouping: `pid.kp`,
  `pid.ti`, ... (11 characters max).

**`remember <device> <file>`** (name: Leon's choice), run as a background
job. **Built 2026-11-15**, see the README's "Settings that survive a
power cycle". The plan as it was:

- **What it does:** every tick (150 ms) it reads the device and the file;
  if they differ, it writes the device's value to the file. Otherwise
  nothing. One value per file, plain text, as the device speaks it.
- **Two lines per setting in `/fram/boot`, in this order:**
  ```
  cat /fram/setpoint > /dev/setpoint       # restore what was saved
  remember /dev/setpoint /fram/setpoint &  # from now on, keep saving changes
  ```
  Restore **first**: started the other way round, `remember` would save
  the device's default over the stored value before it's restored.
  (Only the very first time, when `/fram/setpoint` doesn't exist yet,
  the restore line fails harmlessly and `remember` creates the file.)
- **Why a program and not inside `/dev/setpoint`:** saving would then be a
  hidden fact of one driver. As a job it's visible wiring, like
  `adjust`/`follow`, and works for every device without new code:
  `/dev/percent`, `/dev/pid/kp`, `/dev/heaterauto`, `/dev/lamp`, ...
- **It saves every change, whatever made it:** the UP/DOWN buttons
  (through `adjust`), `echo 36.5 > /dev/setpoint`, a script.
- **Stateless on purpose:** it compares with the file, not with a value
  it remembers, so any number of `remember` jobs never share state (the
  trap `hyst` has). Cost: a few bytes read over I2C per job per tick.
- **Safe and free:** FRAM doesn't wear, so writing on every change is
  fine; a power cut mid-write leaves the old or the new value (the
  filesystem's write order), never half of one.
- **Settings are files, so `mkfs` erases them:** that works as a factory
  reset. Different from the hour counters on purpose: settings may be
  reset, operating hours may not.
- **Testable on cpu_rp2040 now**, before babywarmer: let the lamp
  remember whether it was on across a power cycle:
  ```
  cat /fram/lamp > /dev/lamp
  remember /dev/lamp /fram/lamp &
  ```

### 1b. cron — built 2026-11-15

Built as planned below: a kernel task + `/dev/cron` (on/off), lines from
`/fram/crontab`, plus `/dev/cons` for text on the terminal. See the
README's "cron". Decided: pipelines only (no builtins), nothing on an
untrusted clock, no catching up of skipped minutes. Original notes:

#### (original idea)

A crontab on the FRAM, driven by the RTC: run commands at set times.

- `/fram/crontab`, classic five fields plus a command:
  `min hour day month weekday command`, with `*`, numbers, lists
  (`1,15`) and steps (`*/5`). Write it with `cat > /fram/crontab` (`echo`
  can't write lines with `>`, `|`, `&` in them until the shell has quotes).
- A kernel task, not a job: checks `/dev/rtc` once a second, and on
  each new minute reads the crontab and runs the matching lines through
  the same pipeline machinery as the shell. Unkillable by `kill`, like
  the TFT's flush task was.
- Solves a known limit: a logger today needs `loop` and holds the
  shell. With cron, `*/1 * * * * date >> /fram/log` runs in the
  background.
- Questions to decide: where output goes (nowhere unless redirected, as
  real cron mails it; or a `/fram/cron.log`); what to do while
  `/dev/rtc/vl` says the time can't be trusted (probably: run nothing);
  shell builtins (`sleep`, `run`) in cron lines or only programs (a
  line could be `run <script>` to get both).
- Minute changes missed while the CPU was busy (a long I2C transfer) are
  impossible at a 1 s check, but a reboot at :59 could skip a minute;
  fine for logging, worth a note.

### 2. hexdump — done 2026-11-13

Built as below: `hexdump [-s] [-n] [path]`, `fs_pread()`, read-only
`/dev/fram` and `/dev/mem`, STAGE_BUF 4 KB. See the README. Original notes:

#### (original idea)

`hexdump` in the classic `hexdump -C` layout: offset, 16 bytes in hex,
the same bytes as ASCII:

```
00000000  70 66 73 31 01 00 40 00  80 00 20 00 00 00 00 00  |pfs1..@... .....|
```

- For files and anything else, as a filter: `cat /fram/boot | hexdump`.
- For the raw FRAM and for memory, devices are needed: a read-only
  `/dev/fram` (the whole 8 KB, under the filesystem) and maybe
  `/dev/mem`. Read-only on purpose: a stray `>` must not wreck the
  filesystem.
- The catch: picoos devices have no offsets (one read = the whole
  value) and pipes carry 1 KB. A hexdump line turns 16 bytes into ~78
  characters, so one pipe holds a dump of only ~200 bytes. Options:
  `hexdump <path> <offset> <length>` reading the device itself (simple);
  or a window device, `echo 4096 > /dev/fram/offset` then
  `cat /dev/fram | hexdump`, the same seek-then-read pattern as the TFT's
  `/dev/tft/seek` (`lseek()` + `read()`). Also a reason to look at
  streaming pipes (see below).
- Nice first use: look at the superblock (`pfs1`), the directory
  entries, and a file's chain, to see the filesystem the README
  describes.

### 3. Choosing what to build in — one plan2040 for all boards (planned 2026-11-20)

**Step 1 done 2026-11-21** (board test pending, see the README's Test
log): the tree is restructured into `kernel/` (now with the shell and
jobs), `fs/`, `dev/`, `prog/`, `drivers/`, `boards/cpu_rp2040/`,
`configs/cpu_rp2040.cmake`, `tests/host/`, and renamed plan2040 (banner,
CMake target `plan2040` → `plan2040.uf2`, README). The config lists
devices and programs by name; CMake generates `conf.cpp` with the
`dev_<name>_register()` / `prog_<name>_register()` calls. Sizes
(`MAX_JOBS`, `STAGE_BUF`, `PROG_MAX`, `FS_MAX_DEVICES`, `TASK_MAX`) come
from the config. Decided 2026-11-21: the shell and jobs go in `kernel/`;
the I2C bus/chip table waits for step 3; the PC tests are committed
with a Makefile (`make -C tests/host check`, outputs diffed against
`expected/`). The firmware came out byte-for-byte the same size, and the
tests' output was the same apart from the banner. Step 2 was done
differently: the new repo itself is the merge point. Next: step 3.

**Decided 2026-11-21: the babywarmer app comes from `../picoos`, not
from `../babywarmer`.** `../babywarmer` hasn't changed since 2026-08-15
(6e8418c, the "stable release line" split off picoos): PID and heater
run inside the firmware's own tasks there, not as jobs. `../picoos`
carried on to 2026-09-08 (89c16e5): heater control copied from
babywarmer and redone Plan 9 style as devices and programs (`dev/heater`,
`heaterauto`, `percent`, `phase`, `pid`, `relay`, `setpoint`,
`skintemp`, `ambient`, `current`, `alarm`, `alarmcheck`, `seg7`, `tft`;
`prog/pid`, `phase`, `safelut`, `alarm`, `alarmcheck`, `ledwire`,
`tftwire`, `monitor`), plus the ST7735 TFT (`tft/`) and `ntc.cpp` /
`ntc_lut.h`. So `apps/babywarmer/` is built from picoos @ 89c16e5, and
each commit says where its parts came from. `babywarmer_v1` stays the
name of the *board* (the old babywarmer PCB) and its config.
`../picogateway` (2026-09-08, d514ba0: wifi, LoRa, ThingSpeak, OLED,
EEPROM) is the source for the gateway app, step 4. **Postponed
(Leon, 2026-11-21): babywarmer first.** The gateway needs a Pico W
build (`PICO_BOARD=pico_w`, the cyw43 wifi driver, lwIP, its own
`lwipopts.h`), different SDK libraries and options from everything
else; the config can set `PICO_BOARD` already, the rest waits.

**The babywarmer_v1 hardware hack** (decided 2026-11-21, Leon; plan
changed the same day): **the cpu_rp2040 board becomes the CPU of the
old babywarmer PCB.** The babywarmer's own RP2040 is no longer used; the
cpu_rp2040 board is wired in its place, the way it will sit on the
future carrier board. The babywarmer pin map carries over unchanged
(buttons, buzzer, LEDs, 7-segment, lamp, SSR, relay, TFT, ADS1115). So
the babywarmer app runs on real hardware with everything plan2040 has
(files on `/fram`, `remember`, cron, the hour counter, `date`), long
before the new carrier exists, and on the very CPU board it will have
there. Two I2C buses, as on the future carrier:

| Bus | Pins | Chip | Address | Where |
|---|---|---|---|---|
| local (i2c1) | GPIO14/15 | FM24CL64B FRAM | 0x50 | on cpu_rp2040, as now |
| local (i2c1) | GPIO14/15 | PCF8563T RTC | 0x51 | on cpu_rp2040, as now (supercap backup) |
| header (i2c0) | GPIO12/13 | ADS1115 ADC (NTCs) | 0x48 | on the babywarmer PCB |
| header (i2c0) | GPIO12/13 | 24C512 EEPROM, the NTC lookup table | 0x52 | added by Leon, next to the ADS1115 |

The EEPROM sits on the ADS1115's bus on purpose: the same place it will
have on the future ADC module (ADS1115 + EEPROM), so the software never
has to move it. A2 A1 A0 = 010; WP to GND, or to a free GPIO to guard it
in hardware; 128-byte pages, ~5 ms per page write. Wiring: Leon keeps the
wires short and neat; 3.3 V only (the RP2040 isn't 5 V tolerant, the
LESSON in `boards/cpu_rp2040/board.h`); the header bus's pull-ups come
from the babywarmer PCB / the added parts, ~2-5 kOhm in total.

**The old RP2040 stays on the PCB, held in reset** (Leon, 2026-11-22):
not desoldered; its RUN pin tied to GND, and it **stays powered** (an
unpowered RP2040 would clamp every shared net through its ESD diodes
and be back-powered through its pins). In reset its pads are in their
reset state: input, output driver off, and (per the RP2040 datasheet,
to confirm with a meter) the **internal pull-down on, ~50-80 kOhm, on
every GPIO**. So the nets aren't floating; each gets a weak pull-down:
- Outputs (SSR, relay, buzzer, LEDs, 7-segment, TFT, lamp): fine, ~66 uA
  extra load for the new chip.
- I2C GPIO12/13: fine, the 4k7 pull-ups still give ~3.0 V high.
- **The six buttons (GPIO 2, 7, 8, 9, 28, 29): a problem.** plan2040 uses
  only the RP2040's internal pull-ups (~50 kOhm); against the old chip's
  pull-down a released button sits around 1.65 V, undefined: random
  presses. **Fix: an external ~10 kOhm pull-up from each button line to
  3V3** (never 5 V), giving ~2.75 V. Unless the babywarmer PCB has
  pull-ups there already (picoos used only the internal ones, so
  probably not).
- GPIO26 (current sense): the extra ~50 kOhm to ground loads the sense
  circuit a little; `/dev/current` may read lower than on the old board.
- RUN straight to GND (a pull-up/reset button on RUN only costs a few
  hundred uA). One 3V3 source for the shared rail, never two regulator
  outputs in parallel.
- Check after soldering, before plan2040: `bringup` with the buttons
  untouched must report no presses; a released button line measures
  close to 3.3 V.
- **Done (Leon, 2026-11-23):** 3k3 pull-ups to 3V3 on the six button
  lines (a released button then sits at ~3.1 V against the old chip's
  ~50 k; 1 mA when pressed); every pull-up checked, any to +5 V moved to
  3V3. The 24C512 is soldered: its datasheet marks A2 as NC (that chip
  has only A1 and A0; the A2 address bit is 0 inside), so A0 and the NC
  pin to GND, A1 to 3V3: address `1010 0 1 0` = **0x52**, as in
  `board.h`. (A part whose A2 does work, like Microchip's 24LC512, gives
  the same address with A2 on GND.) Hack not finished yet.

Software for this combination, in step 3:
- `boards/babywarmer_v1/`: cpu_rp2040's local bus (FRAM, RTC) plus the
  babywarmer pin map (`../picoos/warmer.h`) and the header bus i2c0
  12/13 (ADS1115, EEPROM). Two buses in one board: the first real use for
  the bus/chip description below.
- `bringup.cpp` for it: local scan 0x50 0x51, header scan 0x48 0x52, the
  FRAM and RTC tests as on cpu_rp2040, an EEPROM test (read, and a
  write-and-restore of one page), the buttons. First thing to run after
  the hack.
- The EEPROM is the first place to build `/dev/eeprom`, `/dev/lut` and
  the calibration workflow (`cat > /fram/ntc.pts`, `cp /fram/ntc.pts
  /dev/lut`, see below). Until a table is written, picoos's `ntc_lut.h`
  in flash is the fallback.
- A whole-table EEPROM write (512 pages, ~3 s) holds the header bus,
  and with it the ADS1115: bench only, not while the heater runs (as
  noted below).

**Step 3: decided 2026-11-21, and the order.** From the picoos survey
(picoos @ 89c16e5: ~45 devices, 8 programs, the ST7735 code, `ntc_lut.h`,
a 13-line boot script that wires the whole controller):
- The babywarmer's own parts live in `apps/babywarmer/{dev,prog,tft}`;
  the config names them like any other device or program (looked up in
  the app first, then in `dev/`/`prog/`). Chip drivers in `drivers/`
  (ADS1115, ST7735), the babywarmer screen in `apps/babywarmer/tft`.
- I2C buses and each chip's bus + address in `board.h` (a fact of the
  board, not of the firmware), not in the config. The config table above
  stays possible later.
- `../pico-ads1115` (BSD-3, Antonio González) copied into
  `drivers/ads1115/` with its LICENSE and where it came from.
  (Replaced 2026-11-23 by our own driver, see step 3e below.)
- The built-in boot script is a plain file in the app
  (`apps/babywarmer/boot`, and `tftmon`), embedded by CMake; `/fram/boot`
  still overrides it.
- Kept from plan2040 where both have one (newer): `lamp`, `buttons`,
  `cat`, `echo`, `select`, ... `/dev/alarm` owns GPIO1, so the
  babywarmer config drops `led`. Dropped from picoos: `pico-onewire`
  (linked, never used) and C++ exceptions (switched on, never used).
- ~66 devices: the babywarmer config sets `FS_MAX_DEVICES` higher.

Order, one commit each, tested on the PC and then the board:
- **3a** `boards/babywarmer_v1/` (two buses), the shared
  `boards/bringup.cpp` (EEPROM test), `drivers/at24c`,
  `configs/babywarmer_v1.cmake` with the generic devices only: the
  first test after the hack. **Built 2026-11-21**, board test pending.
- **3b** apps in CMake (`apps/<app>/dev,prog,scripts`), the built-in
  scripts as files (`scripts/boot`). **Built 2026-11-21** (saves 4 KB
  RAM: the scripts stay in flash), board test pending.
- **3c** sensors: `drivers/ads1115`, ntc, skintemp, ambient, current.
  **Built 2026-11-21**, board test pending. The ADS1115 library is
  copied in as it was, then changed in its own commit: I2C timeouts, a
  limit on the conversion wait, true/false results (a missing ADS1115
  could otherwise spin until the watchdog reset the board, every boot
  again), and an unset `fsr` for two PGA codes. **Open questions
  (2026-11-21), to decide with the control step (3e):**
  - `ntc_convert()` (unchanged from picoos): codes 9744..21742 clamp to
    the coldest entry, but codes from 21743 up wrap around (`short`
    arithmetic) to the *hottest*, 91.8 C. What does a broken or
    unplugged NTC give on this circuit, near full scale or near zero?
    Out-of-table readings should probably be an error (cat fails, the
    alarm logic sees it) rather than any temperature. `ntctest` shows
    the mapping.
  - The recalibrated table (2026-08-30, substitute NTC) bottoms out at
    22.9 C: a colder room reads 22.9 (picoos's alarmcheck puts its low
    alarm at 23 C for exactly that reason, so it can still fire;
    corrected 2026-11-22, an earlier note here said a 10 C alarm).
    The EEPROM table (3g) is the real fix; until then, worth knowing.
  - **Decided 2026-11-21 (Leon):** the NTC table is messy and
    experimental; both points get fixed with the full EEPROM lookup
    table (3g), not in `ntc.cpp` now.
- **3d** outputs: heater + its tpo task, relay, seg7 + `/dev/leds`, alarm.
  **Built 2026-11-21**, board test pending. Ported unchanged apart from
  the includes and init: `/dev/heater` registers the `tpo` task and the
  bargraph PWM itself, `/dev/seg7`'s register does the 1 s lamp test (both
  were in picoos's main()). The config drops `/dev/led` (`/dev/alarm`
  owns GPIO1). `tests/host/outtest` checks them on fake GPIO/PWM.
- **3e** control: setpoint, heaterauto, percent, pid, phase, safelut,
  alarmcheck, alarm, ledwire, monitor, and the boot script.
  **Built 2026-11-22**, board test pending. The 13 files ported
  unchanged apart from includes and register names; the boot script is
  picoos's, plus `date` and the LAMP button, minus `tftwire` (3f).
  `tests/host/bwsim` runs the whole controller on the PC. **Open
  points it showed (2026-11-22), to decide:**
  - **A dead ADS1115 keeps the heater running.** Every control program
    reads a failed device as a fallback value (skin 0.0): `alarmcheck`
    raises the low alarm (buzzer) but keeps the relay on whenever the
    power is > 0; `phase` ends in `safe`; and `cat /dev/ambient | safelut`
    now fails every time, so `/dev/safepower` keeps its last value: the
    heater runs at, e.g., 39 % with no working sensor. (In picoos a dead
    ADS1115 hung the board instead, not better.) A sensor fault should
    probably be an alarm of its own that switches the heater off and the
    relay off: e.g. `alarmcheck` treating an unreadable `/dev/skintemp`
    as a fault, and `phase` going to a state with 0 % power.
  - **The heater-fail check can false-alarm between 50 and 99 % power.**
    It reads `/dev/current` once every 15 s, 8 samples within
    microseconds; the TPO has the SSR off for part of every 15 s window,
    and a check landing there sees "heating hard, no current". bwsim
    shows it during boost (80 %). babywarmer's original kept a rolling
    average over time (`curr_sense_table`); picoos's 8 instant samples
    don't. On the board it depends on whether the current-sense circuit
    filters (an RC). Fixes: average over a TPO window, or only judge
    while the SSR is on.
  - **A failing background job prints its error every 150 ms** (bwsim:
    11250 `cat: error` lines in 10 minutes from the two NTC pipelines):
    unusable console exactly when something's wrong. Print once per job
    until it works again?
  - In bwsim, safe mode at 39 % never reached the recovery temperature,
    so it stayed in `safe`; that's the rough thermal model, not a
    finding, but worth watching on the real warmer.
  - **Decided 2026-11-23, the principle (Leon):** "machines work for us,
    we not for them, so try to do at least something for - in this case
    the baby". Never (1) a sluggish, delayed user interface, (2) a machine
    giving up its work because one subsystem fails, (3) a cryptic error
    message followed by giving up. So: **degrade, keep retrying, explain**;
    switch off only where carrying on would harm (overtemp, a stuck SSR),
    and say so.
  - **Decided 2026-11-23, the design for the three points:**
    1. **A fallback ladder for the sensors**, instead of a stale value:
       | What works | What the warmer does | What it says |
       |---|---|---|
       | skin + room NTC | PID, as now | - |
       | room NTC only | safe mode, power from `safelut` (as now) | SENSOR alarm: skin sensor failed, heating from room temperature |
       | neither (ADS1115 dead) | **manual mode** (MAN), power = `safelut` for the last room temperature it saw, capped; `/dev/percent`'s 10 % if it never saw one; UP/DOWN adjust it, as in normal manual mode | SENSOR alarm: no temperature sensors, heating at N % |
       | any | keeps retrying; back up the ladder by itself when sensors answer (auto again) | sensor OK again |
       The relay still goes off on overtemp or a stuck SSR. To check:
       what IEC 60601-2-21 (radiant warmers) requires on a sensor fault.
    2. **Heater check only while the SSR is on**: no current while it's
       switched on = heater alarm ("no current while switched on:
       element, fuse or SSR?"); current while it's off = stuck SSR (relay
       off, as now). No more false alarms between 50 and 99 %.
    3. **Errors in plain words, once, without slowing anything down**
       (a kernel change, for every board):
       - `errstr`, as in Plan 9: a device or program that fails sets a
         short reason text; `cat` prints `cat: /dev/skintemp: ADS1115
         (0x48, header bus) not answering` instead of `cat: error`.
       - A background job prints its error once when it starts failing
         and once when it works again (`job 7: ok again`); `jobs` shows
         each job's last error.
       - Backoff: after a failed transfer a device answers "still
         failing" at once for ~1 s before trying the chip again, so a
         stuck bus (10 ms per timed-out transfer) can't make the buttons,
         displays or console sluggish.
    Order: 3 first (kernel, all boards, the others use it), then 2,
    then 1; each with PC tests (bwsim) before the board.
  - **Decided 2026-11-23: our own ADS1115 driver, and a driver standard.**
    The copied pico-ads1115 (BSD-3: legally fine to keep) goes, for
    technical reasons: four of its functions needed fixing already
    (timeouts, results, the conversion wait, the PGA bug), it takes bus
    and address as arguments instead of from `board.h`, keeps a shadow
    of the config register, can't say why it failed, and above all it
    **busy-waits ~8 ms per reading** (start, poll until done): with the
    boot script's three ADC pipelines plus `phase` and `alarmcheck`
    that's ~25 ms of a blocked CPU per 150 ms, buttons/displays/console
    waiting (Leon's principle 1). Instead:
    - `drivers/ads1115.cpp`, ~80 lines from the datasheet, plan2040
      style; the library credited in a comment (picoos started with it;
      git history keeps both).
    - **Background sampling**: a small kernel task starts a conversion,
      reads it on a later tick, next channel (skin, room, ...), never
      waiting. `/dev/skintemp`, `/dev/ambient` answer at once with the
      latest value, or fail with an errstr when it's too old or the chip
      stopped answering. Retry/backoff in one place; the fallback ladder
      gets a reliable "sensor dead"; the EEPROM table (3g) plugs in there.
    - **`drivers/README.md`, the contract every driver follows:** bus and
      address from `board.h`; a timeout on every transfer; true/false
      results with an errstr text; no call blocks longer than ~1 ms
      (conversions, EEPROM page writes split across ticks); the plain
      C++ subset (no heap, exceptions, STL, templates); a header comment
      with the chip's facts. `pcf8563`, `fm24cl64`, `at24c` mostly meet
      it already (`at24c`'s ~5 ms page wait is the next to split).
    **New order:** (1) errstr in the kernel, (2) the driver standard and
    our own ADS1115 with background sampling, (3) the heater check, (4)
    the sensor ladder.
  - **(1) built 2026-11-23**, board test pending: `kernel/errstr.{h,cpp}`
    (`werrstr()`, `errstr()`), `kernel/fs.cpp` puts the path in front and
    a general reason when a device gives none, `fs/framfs.cpp` says why
    at every failure, `kernel/jobs.cpp`: `pipeline_run_once()` returns
    ok/failed with `pipeline_error()`; a background job prints its reason
    once (again if it changes) and "ok again"; `jobs` shows `failing:`;
    cron prints a failing line's reason. `/dev/skintemp`, `/dev/ambient`
    say "ADS1115 (0x48) not answering" (until the new driver). Not yet:
    the 1 s backoff (came with the new ADS1115 driver, step 2), and
    more reasons in other devices and programs (gradually). A job's
    message can still land right after the `% ` prompt (the prompt
    redraw item below).
  - **(2) built 2026-11-23**, board test pending: `drivers/ads1115.cpp`
    from the datasheet (start / ready / result, never waiting;
    `ads1115_convert()` for bringup and boot), `drivers/README.md` (the
    contract, and where each driver stands: `fm24cl64`, `pcf8563` lack
    errstr reasons; `at24c` waits ~5 ms per page, to split with 3g),
    `apps/babywarmer/ntcadc.cpp`: the "adc" task, each channel every
    ~100 ms, 1 s backoff, fresh for 1.5 s. `/dev/skintemp`, `/dev/ambient`
    answer at once. `tests/host/adctest` against a fake ADS1115: ~1.3 %
    CPU normally, ~1 % with a stuck bus, a failure shows after 1.5 s with
    its reason, recovery within a second, one glitch never shows.
    `bringup` reads all ADS1115 inputs (`adc`).
  - **(3) built 2026-11-23**, board test pending. While building it, a
    second problem with picoos's check showed: it ran every 15 s
    against a 15 s TPO window, so it landed at nearly the same point of
    the cycle every time, and at low power a dead heater could go
    unnoticed for minutes. Now `alarmcheck` judges the current **every
    second** against the SSR's real state (new `/dev/heater/ssr`: `on
    1300`, ms in that state; `/dev/relay` readable): no current after
    the SSR and relay have been on for 0.5 s, or current after the SSR
    has been off for 0.5 s (stuck SSR), each after 3 such seconds in a
    row. `/dev/alarm/heater` keeps its reason (`on: no current while
    switched on (element, fuse or SSR?)`). A stuck SSR opens the relay
    and keeps it open (latched) until `echo off > /dev/alarm/heater` or
    a reboot. bwsim: no false alarms in boost; broken element found in
    ~6 s, stuck SSR in ~9 s.
    **Decided 2026-11-23 (Leon): a stuck SSR is the exception to "keep
    doing something for the baby".** An SSR that's always on means the
    heater is out of control; letting the mechanical relay click away as
    a stand-in switch is too dangerous. So: relay off and held off,
    heater-failure alarm, until the repair (`echo off >
    /dev/alarm/heater`) or a reboot. (That's what step 3 does.)
  - **Step 4, discussed 2026-11-24: the guard.** Leon: the fallback
    ladder is the most important ruling task, and it isn't only about
    hardware: a skin sensor that falls out of the bed still *works* but
    measures the wrong thing, and the PID then drives the heater up.
    Three scenarios with a working sensor: (1) it falls out while the
    baby is warm, (2) it's out of the bed from the start, during
    warm-up, (3) it's wrongly placed. What picoos's `phase` watchdogs
    catch today: (1) yes (fast drop, or > 3 C below setpoint for 20 s),
    but the PID ramps up until then; (2) yes ("no rise" in 5 min at
    80 %), **but not when the sensor lies on the mattress under the
    heater** and warms up; (3) "too low" partly, "too high" (under the
    baby, in direct radiation: the baby gets cold) not at all. And
    `phase` is a job: `kill` stops it, a `/fram/boot` without it has no
    watchdogs at all.
    **Decided 2026-11-24, the guard as a kernel task:**
    - `apps/babywarmer/guard.cpp`, a kernel task, once a second,
      registered by the app itself: can't be killed, doesn't depend on a
      boot script.
    - It reads only **hardware facts and the user's intent** (skin, room,
      current, the SSR, setpoint, auto/manual), never what a job computed
      (`/dev/state`, `/dev/pidout`): those are what it guards. In auto
      mode a `/dev/pidout` not updated for ~10 s means the control jobs
      are gone: fall back.
    - **Scope: trust + power limit + all of alarmcheck's safety**:
      overtemp -> relay off, the heater-current check, stuck SSR, writing
      `/dev/alarm/*`. `alarmcheck` (the job) goes. `alarm`, `ledwire`,
      `tftwire` stay jobs: they only show what the guard decided.
    - **Three levels of trust** in the skin reading, in `/dev/guard`
      with the reason: `ok`, `safe: <why>`, `none: <why>` (`suspect` in
      the first draft, see the decision below).
    - **In safe and none the guard provides the power itself**
      (safe: safelut from the room temperature; none: the
      conservative manual %, adjustable with UP/DOWN), whatever the jobs
      do: the ladder works with every control job dead, and also when the
      sensor reads too high (a cap alone would let the baby get cold).
      In ok it caps (a sane maximum).
    - **Enforced where the hardware is:** `tpo_apply()` drives the SSR
      with the guard's decision (cap, or the fallback power), so it acts
      within 100 ms even if no job writes `/dev/heater` again.
      `/dev/heater/limit` shows it.
    - **Manual mode: hardware safety only** (overtemp, heater current,
      stuck SSR, the skin temperature alarms); no plausibility power
      limit: in manual the nurse decides the power.
    - **phase's watchdogs move into the guard** (one owner of "can the
      skin reading be trusted"). `phase` keeps only the warm-up sequence
      (idle, boost, coast, pid), follows `/dev/guard` (shows safe,
      reseeds the PID on recovery).
    - Test: bwsim kills the control jobs and takes the sensor off; the
      heater stays within the guard's limits.
    **Decided 2026-11-24 (Leon): no "suspect" plausibility detection.**
    Proposed were extra checks (heating effort much above safelut for
    minutes, skin rising faster than a baby can, skin high on ~0 %
    power, a 15 s TPO ripple test). Leon: too hard to detect reliably,
    too many false alarms; e.g. a ceiling fan also makes the heater work
    harder than normal. A warmer that alarms for nothing gets ignored.
    So the guard keeps **only picoos's proven checks, for two
    scenarios**:
    - (1) the sensor falls out while warm: **fast drop** (> 0.08 C/s for
      3 s, more than 2 C below setpoint) or **sustained low** (more than
      3 C below setpoint for 20 s) -> safe;
    - (2) the sensor is out during warm-up: **no rise** (< 0.3 C in 5 min
      of boost) -> safe;
    - plus the hardware cases (no skin reading -> safe; no sensors at all
      -> none).
    **Recovery: by itself** (Leon), when the skin is within 2 C of the
    setpoint (picoos's rule) for 2 minutes, so it doesn't flip back and
    forth.
    **Known limit, accepted:** a sensor lying on the mattress under the
    heater warms up, passes "no rise", and the PID then controls the
    mattress spot (scenario 3, wrongly placed, isn't detected).
    **Decided 2026-11-24, what the nurse sees:**
    | | ok | safe (sensor off the baby, no rise, skin unreadable) | none (no sensors) |
    |---|---|---|---|
    | the warmer does | PID | heats from the room temperature (safelut) | manual at a conservative %, UP/DOWN adjust it |
    | CHK LED | off | on | on |
    | TFT | normal | SENSOR icon + yellow triangle blink | same, MODE icon shows manual |
    | big display | skin temperature | **the reading, blinking** (shown, not trusted: it may be the room) | blank (no reading) |
    | small display | setpoint | the power it heats with now (e.g. `39`) | the manual % |
    | buzzer | - | **sounds; MUTE silences it for 1 min**, then again until fixed | the same |
    | the nurse | - | puts the sensor back: skin control again by itself after 2 min plausible; or takes over with MANUAL | fixes the sensor/cable; meanwhile UP/DOWN |
    | console | - | once: `guard: safe: <why>, heating N % from room T`; later `guard: ok again` | once, with the reason |
    A text line on the TFT's warmer screen ("SENSOR OFF BABY? heating
    39 %"): **later**, designed in the st7735 repo (display.h) and
    re-synced; for now icons, LEDs, displays, buzzer.
    **Still to look at:** what IEC 60601-2-21 (radiant warmers) requires
    (sensor fault alarm, temperature deviation alarm, output limits in
    baby control mode).
  - **4a built 2026-11-24** (the safety core), board test pending:
    `apps/babywarmer/dev/guard.cpp` (the task, `/dev/guard`), the
    guard's say in `heater.cpp` (`heater_guard()`: cap, or its own
    power; applied in `tpo_apply()`; `/dev/heater/limit`), the room
    table shared (`safelut.cpp`), `/dev/alarm/sensor`, `prog/alarmcheck`
    gone (into the guard), `phase` slimmed to the warm-up sequence.
    **Bumpless handover (Leon, 2026-11-24):** back from safe, `phase`
    seeds the PID integral so its first output is the power in use
    (picoos seeded 0.0: bwsim showed the power dropping from 39 % to
    ~3 %, the baby cooling, and a false "sensor off the baby" every ~10
    minutes). bwsim covers every case (see its story).
    **Open, the 39 % bench check (Leon):** the room table is a guess
    ("helps the baby a bit better than nothing"). It conflicts with
    picoos's note that the real PID integrator settled at ~6.6 %. Which
    is right decides two things: (a) if ~7-10 % is the real steady
    power, safe mode's 39 % is 3-4x too much, and safe mode is open
    loop (with the sensor off the baby nothing sees the baby getting
    too warm): **too low is safer than too high there**; (b) if ~39 % is
    right, picoos's coast seed (3 %) is far too low (bwsim: after `none`
    the fresh start-up coasts, the PID starts from 3 %, the baby cools,
    a false "safe" at 108:58).
  - **4b built 2026-11-24**, board test pending: `ledwire` (CHK; the
    big display blinks in safe, blank in none) and `tftwire` (SENSOR
    icon) from `/dev/guard`; `/dev/seg7big/blink` with a "seg7" task;
    the boot script's small display line is a `select` on `/dev/state`
    (manual %, setpoint, or the power in safe). bwsim decodes both
    displays: every state as decided. Left for later: the TFT text line
    (st7735 repo).
  - **Step 5: the field warmer's lessons back, before the first
    hardware test** (evaluated 2026-11-24). `../warmer/warmer.cpp`
    (LH20260427, 2026-04-27) is the **only firmware tested for real**
    (real heater, real NTC, a real room in Vietnam); `../babywarmer`
    and `../picoos` came later and were bench tests only. Compared line
    by line with plan2040's babywarmer app. Leon: "otherwise we would be
    wasting time and re-inventing what we already did before."
    **The decisive difference: plan2040 has no feed-forward.** In the
    field warmer the power in PID mode is `ff_table(room)` (30 % at
    24 C, 25 % at 28 C) + PID, the PID part clamped to -35..+25 %, the
    total to 0..75 % (`DUTY_MAX`). picoos cut the feed-forward on
    purpose ("PID alone", `prog/pid.cpp`), so the integrator has to
    carry the whole ~27-30 % by itself: at Kp/Ti = 0.015 %/(C s) that's
    ~1800 C s of error, e.g. 1 C too cold for half an hour. The coast
    seed of 3 % was meant *on top of* the feed-forward; alone it's far
    too low. That is what bwsim showed (power 3 % after coast, the baby
    cooling, a false "safe" at 108:58).
    **It settles the 39 % question:** picoos's "integrator settled at
    ~6.6 %" was the integrator on top of the feed-forward, not the total
    power. The field log: the heater settled at 27.3 % in a 27.7 C room;
    the safe table (`safelut.cpp` is exactly the field's
    `safe_ff_table`) gives ~32.5 % there, ~5 % above the real steady
    power: a mild warm bias in open loop, field-derived, not a guess.
    The bench check becomes a confirmation.
    **The NTC table is right:** the field's `ntc_lut.h` (650 entries,
    0.1 C) and plan2040's built-in MF58 B3950 table agree within
    +-0.15 C over 30-40 C (+-0.2 C over 14-50 C).
    **Everything compared:**
    | | Field warmer | plan2040 (2026-11-24) | |
    |---|---|---|---|
    | NTC table | 650 entries, 0.1 C, clamps out of range (templow/temphigh) | built-in MF58 table, interpolated; out of range a reading fails, guard to safe | agree +-0.15 C |
    | PID gains | Kp 3, Ti 200, Td 5, dt 1 s, D on measurement, anti-windup | the same | ok |
    | PID output | FF + PID(-35..+25), total cap 75 % | PID(0..100), no cap in ok (`guard.cpp`: 100) | **to fix** |
    | Boost / coast | 80 % to setpoint-3, 0 % for 45 s, seed 3 | the same (the seed wrong without FF) | ok with FF |
    | Setpoint raised >= 2 C | back to boost (commit 2026-04-27) | missing | **to fix** |
    | Manual | step 5 %, max 80 %, back to 10 % on every switch to manual (customer request) | step 0.5 % (`adjust`'s one step), max 100 %, no reset | **to fix** |
    | No-rise | coded as 5 min, but counts 1 s ticks against `300000/TICK_MS`: really **50 min** (a bug) | really 5 min | **to decide**: 5 min never field-tested |
    | Sensor off the baby | fast drop, sustained low -> safe | the same thresholds | ok |
    | Safe mode | safe table capped 55 %; CHK LED only, no buzzer (commented out); back to PID at once at setpoint-2 | the same table; buzzer, blinking display; back after 2 min, bumpless | stricter, decided; nothing above 55 % in safe |
    | Temperature alarms | high: skin > setpoint+2; low: skin < room-2 | fixed 40 C / 23 C (picoos's test thresholds) | **to fix** |
    | Relay off, overtemperature | skin > setpoint+3 | the same | ok |
    | Heater current | every 15 s, 8-sample average (false alarms at 50-99 %) | every 1 s, after the SSR settles, 3 s confirm, stuck-SSR latch | better |
    | Dead ADS1115 | the library waits forever: watchdog reboot every ~8 s, each back into 80 % boost | reason in words, safe, retry every 1 s | much better |
    | Dead control | impossible (one loop) | possible (jobs, `/fram/boot`); the guard catches it in 10 s | new risk, covered |
    | Structure | one 1245-line loop | ~12 jobs from a script + kernel tasks (~0.6 s chain delay, nothing against a 15 s TPO) | flexible, observable, more parts |
    | Telemetry | one line per second (state setp skin amb heat low high curr fail warn pid_i pid_pm): Leon's tuning logs | `monitor`, another format | **to add** |
    **Chances:** as it is now, plan2040 would keep the baby warm *worse*
    than the field warmer (slow undershooting approach, false safe
    alarms) while failing *more* safely. With the field's control back,
    it equals the proven control and exceeds its safety.
    **To do, in this order, before the first hardware test:**
    1. **Feed-forward back.** **Decided 2026-11-24 (Leon):**
       - `fftable`, built in exactly like `safelut`: the field's
         `ff_table` compiled in (reflash to change, as in the field; a
         `/fram` override can come later without redesign):
         `cat /dev/ambient | fftable > /dev/ffpower &`.
       - **`pid` takes the feed-forward as a bias**, an optional 4th
         argument: `pid /dev/state pid /dev/setpoint /dev/ffpower`, and
         computes the field's formula in one place:
         `pidout = clamp(ff + clamp(P + I + D, -35, +25), 0, 75)`, the
         anti-windup on the PID part's clamp, as in `warmer.cpp`. The
         limits are devices (`/dev/pid/min`, `/dev/pid/max`,
         `/dev/pid/outmax`). Without the 4th argument `pid` works as now.
       - **Room sensor down** (decided 2026-11-24, Leon): keep the last
         feed-forward while it's younger than ~10 minutes (a room changes
         slowly), then a conservative 30 % (the table at 24 C); the PID
         corrects the rest, an alarm says the room sensor is down. (The
         field used 50 %: a high bias in a warm room.)
       - `phase`'s seeds include the FF: the coast seed of 3 % is the
         integrator on top of it again, and the bumpless handover from
         safe seeds `integral = power in use - ff - P - D`.
    2. Caps: 75 % in auto (the guard's cap in ok), 80 % in manual.
    3. Manual: 5 % per step; back to 10 % on every switch to manual.
    4. Re-boost when the setpoint goes up by 2 C or more.
    5. Alarms: high at setpoint+2, low at room-2, as in the field.
    6. No-rise: **10 minutes** (decided 2026-11-24, Leon): twice what
       the field code meant (5 min, never run: the tick bug made it 50),
       until the hardware tests show the real rise rate at 80 %.
    7. The field's telemetry line, as an option, so new logs compare
       side by side with the April logs.
    8. Then board and bench tests; the 39 % bench check confirms.
- **3f** the TFT: `drivers/st7735`, `apps/babywarmer/tft`, `/dev/tft`,
  tftwire, tftmon. **Built 2026-11-23**, board test pending. **Decided
  2026-11-23:** the whole `st7735` repo copy stays together, unmodified,
  in `apps/babywarmer/tft/` (from `../st7735/input` @ 4940778, identical
  to picoos's `tft/`), so re-syncing is a plain copy; the chip driver
  moves to `drivers/` when a second user comes (the 4" ST7796S, a
  `/dev/cons` on the screen). `dev/tft.cpp` checks at compile time that
  `st7735.h`'s pins are the board's. `tftwire &` back in the boot script;
  `tftmon` is a built-in script file. With everything: ~200 KB flash,
  ~33 KB RAM, 64 devices, 25 tasks.
- **3g** `/dev/eeprom`, `/dev/lut` and the calibration workflow.

**Later, a name for the Pico W build?** (Leon, 2026-11-21): maybe
**plan2040w**, since the wifi build is so different (cyw43 driver,
lwIP, other SDK libraries and options). Note: the Pico W's CPU is the
same RP2040; the wifi is a separate chip (CYW43439) on SPI. The Pico 2 W
is the one with another CPU (RP2350: that would be plan2350w).

**Decided 2026-11-20:** one source tree instead of three diverging copies
(`../picoos` babywarmer, `../picogateway` on a Pico W, this repo). Layers:
`kernel/` + shell (same for all), generic `dev/` `prog/` `fs/`, `drivers/`
(chips), `boards/<board>/` (pin maps), `apps/<app>/` (babywarmer: heater,
PID, phases, alarms, TFT; gateway: LoRa, wifi, ThingSpeak, OLED), and
`configs/<name>.cmake`, one per firmware, Plan 9 kernel-config style.
Compile time, not detected at start-up (pins can't be probed safely; the
Pico W's wifi needs other SDK libraries). Order: (1) restructure this
repo, the `cpu_rp2040` firmware unchanged, `bringup` per board; (2) move
into the existing `picoos` repo as its next major version, both
histories kept (show the git steps first); (3) the babywarmer app,
first on the old babywarmer board as `babywarmer_v1`, with FRAM + RTC
added by wire on its ADS1115 bus (GPIO12/13: ADS1115 0x48, FRAM 0x50,
RTC 0x51; GPIO14/15 aren't routed there); (4) the gateway app later. (Update 2026-11-21: step 2 became the new
repo itself; for step 3 the cpu_rp2040 board is wired into the old
babywarmer PCB as its CPU instead, see "The babywarmer_v1 hardware hack"
above; step 4 postponed.)
Old boards without FRAM: supported as reduced configs.

**I2C buses and chips in the config** (2026-11-21): the config describes
buses and the chips on them, so any chip can sit on any bus at any
address without code changes; drivers take their bus + address from it
instead of the global `BOARD_I2C`:

```cmake
set(PLAN2040_I2C   "local  i2c1 14 15 400000"      # name controller SDA SCL speed
                   "header i2c0 12 13 400000")
set(PLAN2040_CHIPS "fm24cl64 local  0x50"          # driver bus address [options]
                   "pcf8563  local  0x51"
                   "ads1115  header 0x48"
                   "24c256   header 0x52 size=32768 page=64")
```

**EEPROM with the NTC lookup table** (2026-11-21): a 24C256 (32 KB) or
24C512 (64 KB) holding a table from ADS1115 code to temperature, on a
future **ADC module** (ADS1115 + EEPROM on one small PCB), so the
calibration travels with the sensor electronics and no firmware change
is ever needed for a new sensor or module (unlike `ntc_lut.h` in the
old picoos, edited and rebuilt when the NTC was swapped, 2026-08-30).
- Addresses: 0x50 is the FRAM and 0x51 the RTC (fixed), so strap the
  EEPROM to **0x52** or higher (A2 A1 A0 = 010).
- Single-ended ADS1115 codes are 0..32767: one 2-byte temperature per
  code is exactly 64 KB (24C512, full table); the 24C256 holds every
  second code, linearly interpolated (2 ADC steps = ~0.25 mV, far below
  what the NTC resolves). The table carries its own header (magic,
  step, units) so either chip works.
- Written once (EEPROMs wear, ~1M writes, ~5 ms per page: 64 bytes on
  the 24C256, 128 on the 24C512), read often. Generated on the PC,
  loaded once over USB.
- In picoos: a raw `/dev/eeprom` (hexdump-able, like `/dev/fram`; writing
  guarded), and the temperature device (e.g. `/dev/skintemp`) reads the
  ADC and looks the value up in it.
- Flash would also fit (today ~101 KB + babywarmer/TFT ~100 KB + a 64 KB
  table = ~13 % of 2 MB), but the EEPROM-on-the-module design is better;
  a table in flash stays a possible fallback for boards without one.

- **Reading** (2026-11-21): one I2C address covers the whole chip (2-byte
  memory address, as with the FRAM). Pages only limit *writes*; a read can
  start anywhere and be any length, so one ADC conversion = one 4-byte
  read (the two neighbouring entries, for interpolation), ~0.2 ms at
  400 kHz. Single-ended ADS1115 codes are already 15 bits; with the
  24C256's every-second-code table, interpolation still uses all 15.

**Building and writing the table: needs software + a calibration
procedure** (2026-11-21). The measuring is manual: the NTC (in its real
housing) next to a reference thermometer, in an ice bath, then warmer
water, recording pairs of *(ADC code, reference temperature)*. Leon's
first idea: a point every 0.5 °C from 0.0 to 50.0 °C, with the
temperatures in between interpolated. Notes for when it's built:
- **On the board itself, picoos style**, so no PC tool is needed to read
  the ADC: e.g. `ntccal` shows the live, averaged ADC code (say 64
  samples, and "stable" once it stops drifting); you type the reference
  temperature and it appends the point to a file (`/fram/ntc.pts`, plain
  text, one `code temp` pair per line, editable with `cat >`). Then
  `mklut` builds the table from the points and writes it to the EEPROM
  page by page (64 or 128 bytes, waiting out each ~5 ms write), and
  reads it back to verify.
- **Fewer points can be enough.** An NTC follows a known smooth curve
  (Steinhart-Hart / Beta). If the divider's fixed resistor and reference
  voltage are known, ~3-5 good points (ice bath 0 °C, room, ~37 °C,
  ~50 °C) give a better curve than 101 hand readings (it averages out
  reading errors). If they're not known (as in 2026-08), interpolate
  smoothly and monotonically through all the measured points instead.
  `mklut` could do both; worth comparing on real data.
- **Decided 2026-11-21: a few points by hand + a calculated curve**
  (Steinhart-Hart), not 101 hand readings.
- **Decided 2026-11-21, the workflow: FRAM as the scratch book, the EEPROM
  as the final record.** The full table (32-64 KB) can't be a FRAM file
  (files are at most 1 KB), but the points can, and a device can turn
  points into a table when they're copied into it:
  ```
  cat > /fram/ntc.pts          # a few points by hand, "code temp" per line
  cp /fram/ntc.pts /dev/lut    # fit the curve, write table + points to the EEPROM, verify
  cat /dev/lut                 # what the module carries: points + curve, as text
  cp /dev/lut /fram/ntc.pts    # and back out, to adjust and write again
  ```
  Writing text to `/dev/lut` makes it act (Plan 9 style); reading gives
  the stored points and coefficients from the EEPROM header, so a module
  always carries its own calibration, readable and round-trippable.
  (So rather than a separate `mklut` program, it's the device's write.)
- Writing a whole table is 512 page writes x ~5 ms = ~3 s of busy
  system: fine on the bench, not while a heater runs. Either "bench
  only", or let the write continue in small steps across ticks.
- Outside the measured range (below the first / above the last point):
  clamp, or mark "out of range" in the table, rather than extrapolate.
- The table header records the calibration: date, number of points,
  range, method, step, so `hexdump /dev/eeprom` (or a small `lutinfo`)
  shows what a module carries.

**The NTC circuit as it is (Leon, 2026-11-24)**, on the babywarmer PCB:
a Wheatstone bridge per sensor, both on a separate AMS1117-3V3 LDO that
powers only the bridges and the ADS1115. Skin: reference leg 3V3 - 47k -
**AIN1** - 10k - GND; NTC leg 3V3 - 47k - **AIN0** - NTC - GND; each centre
through 4k7 to the ADS1115. Room: the same on AIN3 (reference) / AIN2
(NTC). Filtering: 2.2 uF across the NTC, 100 nF AIN0-AIN1, 100 nF
AIN0-3V3, 100 nF AIN1-GND. Both NTCs MF58 10k@25 C, B = 3950 (the
substitute of 2026-08 that messed up `ntc_lut.h` was not an MF58).
What it gives (AIN0 - AIN1, the ADS1115 at +-1.024 V, 31.25 uV a code):
| T | R_NTC | AIN0-AIN1 | code |
|---|---|---|---|
| 0 C | 33.6 k | +0.797 V | +25 500 |
| 10 C | 19.9 k | +0.403 V | +12 900 |
| 25 C | 10.0 k | 0 | 0 |
| 37 C | 6.0 k | -0.206 V | -6 600 |
| 50 C | 3.6 k | -0.345 V | -11 025 |
About 14 mV/K near 37 C: **1 code = ~0.002 C**. (picoos's `ntc.cpp`
comment "0V -> 50degC" is wrong: 0 V is 25 C; its table starts at code
-11025 = 50 C, which matches this circuit.)

**3g, decided 2026-11-24 (Leon):**
- **Still open: calculate or look up.** The LUT's strength (Leon): simple,
  and agnostic to circuit and sensor type. So the format keeps both open:
- **The table: the full 16-bit range, one entry per 32 codes**: 2048
  entries x 2 bytes = **4 KB** (~0.07 C per step here, linear in between;
  as `ntc_lut.h`'s step). Entries in 0.01 C; a marker for "not calibrated
  here", so a reading outside the calibrated range fails with a reason
  (`outside the calibrated range (open or shorted?)`) and the guard goes
  to safe. Plus a header: magic, version, step, date, the measured
  points, an optional bridge description, fitted coefficients, a
  checksum: whatever "calculate" would need later is already on the
  module.
- **One table for both NTCs** (same sensor, same bridge): **`/dev/ntc/lut`**
  (write the points: it builds and writes the table; read: the
  calibration as text). `/dev/skintemp`, `/dev/ambient` stay.
- **The EEPROM is the table's home.** A built-in table in flash,
  generated from this circuit and the MF58 datasheet (bridge formula +
  B = 3950), replaces the messed-up `ntc_lut.h` as the fallback while the
  EEPROM has no valid table (said once on the console). **Decided
  (Leon): use the flash fallback, and build the EEPROM side fully now**:
  the driver and the writing tools (`/dev/ntc/lut`, the points file, the
  generator), ready for the calibration later.
- **Built 2026-11-24**, board test pending: `tools/ntclut` (the PC checker) and `apps/babywarmer/ntclut.{h,cpp}`
  (the format, the parser, the generator: Steinhart-Hart through the
  bridge, the Beta curve, or Fritsch-Carlson interpolation; the lookup),
  `dev/ntc.cpp` (`/dev/ntc/lut`, `status`, `skin`, `room`; the table in
  RAM, from the EEPROM or built-in at boot; writing and verifying in the
  background, header invalid first and valid last), `drivers/at24c`
  non-blocking (`eeprom_page_start()`, `eeprom_ready()`). picoos's
  `ntc.cpp`/`ntc_lut.h` are gone (the built-in table replaces them, made
  from the bridge and the MF58 curve: +-0.005 C against the physics).
  RAM +13 KB (the table in use, a pending one, their texts). PC tests:
  `luttest`, `eetest` (and `adctest` now reads correct temperatures:
  code 0 = 25.0 C). Still open: calculate-or-lookup (the format keeps
  both; the coefficients are in the stored text).
- The generator: with a `bridge ...` line in the points file, code ->
  resistance and a Steinhart-Hart fit through the points (3-5 points
  enough); without it, a smooth monotonic interpolation through the
  points (agnostic, needs more points).

**The ADC module, next revision: ratiometric (Leon, 2026-11-24: later).**
Today's bridge isn't ratiometric: it runs on the AMS1117 (initial
accuracy ~+-1-2 %, plus drift with temperature and load), but the ADS1115
measures against its own internal reference, so every change of the 3V3
shows in the reading: **~0.14 C per 1 %** near 37 C. Options, to check
against datasheets before buying:
- **Ratiometric (recommended):** use the bridge supply as the ADC's
  reference, so supply changes affect bridge and reference alike and
  cancel; even the AMS1117 is good enough then. The ADS1115 has no
  reference input; TI's **ADS1219** (24-bit, I2C, 4 inputs = 2
  differential pairs, external REFP/REFN) does, as far as known: same
  header bus, a similar driver (start / ready / result).
- A precision reference (REF30xx, MCP1501 class) for the bridge: a stable
  excitation, but the ADC's own internal reference still drifts on its
  own: two errors that add. Second choice.
- Either way: **0.1 %, 25 ppm/C resistors** for the 47k/10k (the reference
  leg sets the zero point); the NTC's self-heating is no issue (~60 uA,
  ~20 uW). The 4k7 series resistors plus the ADC's finite input
  impedance give a small fixed error (~1 %): a calibration on the real
  circuit absorbs it (another reason for measured points over a formula).
- On the module: the 24C512 with the table (0x52), so the calibration
  travels with the bridge and its supply.

**Name: decided 2026-11-21: plan2040.** The repo is
github.com/leonhiem/plan2040; development continues there.

#### Original notes

picoos grows by many small commands; a project shouldn't have to carry
all of them.

- Plan 9 did exactly this: a kernel configuration file listing which
  devices and parts go into a kernel. picoos could have one list, e.g.
  in `CMakeLists.txt` or a `picoos.conf`, naming the devices and
  programs to build; registration in `picoos.cpp` generated from the
  same list, so adding a program stays a one-line change.
- Measure before cutting: most programs are a few hundred bytes of
  flash, which won't matter with 2 MB (see the README's resources
  section: 4 % used). The real RAM costs are elsewhere: the 20 job slots
  (5.2 KB, was 13 KB), the pipe/script buffers, the FRAM filesystem's caches. Making
  those sizes configurable (`MAX_JOBS`, `STAGE_BUF`, ...) would save
  more than leaving out `grep`.
- Whole features worth a switch: the FRAM filesystem, cron, the TFT,
  `bringup`-style tests.
- Real numbers to design it around are in the README ("If space ever gets
  tight"): job slots 5.2 KB RAM (was 13 KB), pipe buffers 6 KB RAM, hexdump 1.5 KB
  flash, /dev/mem + /dev/fram + fs_pread under 1 KB flash together.

### 4. Hour counter (power-fail safe) — built 2026-11-15

Built as `/dev/hours` (counter 0, power-on hours) in the FRAM's system
area, see the README's "Hour counter". Decided 2026-11-15: `mkfs` never
erases it; system area 256 bytes = 7 counters; first version counts
power-on hours only (heater hours etc. later, in the spare slots).
Original notes:

#### (original idea)

An operating-hours counter, like a car's odometer: babywarmer needs one
(service intervals, warranty), and so will other systems. It must never
go corrupt, never go backwards, and survive anything: power cuts at any
moment, firmware updates, and `mkfs /fram`.

Why counters go corrupt elsewhere, and what that means here:

- **Wear.** Many counters live in EEPROM or flash, rated ~100k–1M writes
  per cell. Saving every minute wears out a cell within a few years; then
  the value goes bad. **The FRAM doesn't have this problem**: ~10^14
  writes, so saving every minute for thousands of years is fine. No
  wear-leveling needed.
- **Torn writes.** A power cut halfway through writing a multi-byte
  value leaves half old, half new bytes: a random number. This one
  *does* apply to FRAM (it writes byte by byte as the I2C transfer
  arrives). The classic fix:
  - Keep **two copies** (A and B), each a record of
    `{sequence number, minutes, CRC}`.
  - Write them **alternately**: A, then B, then A, ...
  - At power-up, check both CRCs and take the valid record with the
    highest sequence number.
  - A cut can only tear the copy being written; the other copy is intact
    and at most one step older. Worst case: one minute lost, never a
    corrupt or reset counter.
- **Formatting.** If the counter were a file, `mkfs /fram` would erase
  it. So it should live in a small **reserved area of the FRAM outside
  the filesystem** (e.g. the last block), which `mkfs` never touches.
  That takes one block from the filesystem: a format change, so best
  decided before there are files worth keeping. (A `/fram` file would
  otherwise be safe against power cuts: the filesystem's write order
  already guarantees old-or-new.)

Further thoughts:

- **Count with the RP2040's own timer, not the RTC.** Setting the clock,
  or a flat RTC backup, must not change operating hours.
- **Resolution:** minutes in a 32-bit number covers ~8000 years. Saved
  once a minute, a power cut loses at most the minute in progress.
- **As files:** e.g. a read-only `/dev/hours` showing `1234.5`, or
  `1234:30` (hours:minutes). Setting it (a replaced board taking over
  the old counter, or a factory reset) should be deliberate and hard to
  do by accident: a separate `/dev/hours/ctl` taking `set 1234`, in
  Plan 9 style, rather than a writable `/dev/hours`.
- **More than one counter?** Board powered on vs. heater actually
  heating are different numbers for babywarmer. A counter could be gated
  by a device ("count while `/dev/heater` > 0"), the same gate idea as
  `follow`. Possibly a few counters: `/dev/hours/power`,
  `/dev/hours/heater`.
- **Proving it:** the same PC test as the filesystem (cut power at every
  write, check the value after remount), and on the board a script or
  relay that cuts power hundreds of times, then checks the counter only
  ever went up.

## Wishlist

### 5. A bigger display: 4" ST7796S, 320×480, touch (2026-11-14)

A replacement for the 1.8" ST7735 (128×160): same kind of SPI display
controller, 7.5× the pixels, plus a touchscreen. Doable; notes:

- **Same wiring idea as the ST7735.** SPI (SCK, MOSI) plus CS, DC, RST
  and backlight, so the existing TFT pins (GPIO 20–25) carry over. The
  ST7796S speaks the same basic commands as the ST7735 (set column/row
  window, write pixels, RGB565), so a new low-level driver is mostly a
  different init sequence and size; `gfx.c` and up keep their shape.
- **No framebuffer, as now.** A full frame is 320 × 480 × 2 bytes =
  300 KB, more than the RP2040's 264 KB of RAM. The current driver
  already draws straight to the display (dirty regions, no framebuffer),
  so that's no change in approach. (An RP2350 with 520 KB could hold
  one, if that ever matters.)
- **Speed.** RP2040 SPI goes up to ~62.5 MHz; a *full* screen at 40 MHz
  takes ~60 ms (~16 frames/s), at 62.5 MHz ~40 ms. The babywarmer UI
  redraws small parts (icons, a number, the APGAR clock), so that's
  plenty. Worth doing with DMA on core 1 so the shell and jobs never
  wait for pixels (the stack fix of 2026-11-12 made core 1 usable).
- **The UI needs a new layout, not just a new driver.** Icons, faces and
  digits are bitmaps sized for 128×160. The `st7735` repo renders them
  from source art with Python/cairo, so they can be re-rendered bigger.
  Flash is no problem: a 128×128 icon in RGB565 is 32 KB, and there are
  ~1.9 MB free.
- **Touch: XPT2046** (resistive, on SPI), decided 2026-11-14. Needs
  MISO, its own CS and an IRQ line. Since the touchscreen takes over
  from the push buttons, **their GPIOs (2, 7, 8, 9, 28, 29) are free for
  it**. One catch: MISO needs a pin with hardware SPI *receive*, and
  those are fixed:
  - SPI0 RX (the TFT's bus, SCK 22 / MOSI 23): GPIO 0, 4, 16, 20 (buzzer,
    7-segment, SSR bargraph, TFT backlight: none is a button pin).
  - SPI1 RX: GPIO 8, 12, 24, 28: **8 and 28 are button pins.**

  Two clean ways, for the breadboard to settle:
  - **Touch on its own bus, SPI1**: MISO on GPIO 28 (or 8), SCK on
    GPIO 10 (free), MOSI on GPIO 27 (free), CS and IRQ on two other
    button pins. The XPT2046 wants a slow clock (~2 MHz) and the display
    a fast one (40+ MHz); separate buses mean no switching speeds
    between them, and display DMA never blocks a touch read.
  - **Touch sharing SPI0 with the display** (how most of these modules
    are wired): move the backlight from GPIO 20 to a button pin (it's a
    plain on/off pin), use GPIO 20 as MISO, CS and IRQ on button pins.
    Fewer pins, but the bus has to change speed for every touch read.
- **Calibration.** A resistive touch gives raw 12-bit X/Y readings, not
  pixels: it needs a calibration (touch the corners once) to map them to
  screen coordinates. The calibration values belong in a config file on
  `/fram` (idea 1a): measured once, kept across power cycles.
- **Backlight.** A 4" backlight draws ~100 mA or more: switched through a
  transistor on the carrier board, never straight from a GPIO (see the
  README's GPIO notes).
- **What it would give picoos:**
  - Room for a much richer babywarmer screen (graphs: skin temperature
    over the last hour, setpoint, heater power).
  - A real local console: with the 6×10 font, 53 × 48 characters. A
    `/dev/cons` on the display, in Plan 9 spirit.
  - Touch as devices: `/dev/touch` (`x y`, or nothing), and on-screen
    buttons appearing as `/dev/buttons/<name>` exactly like the
    physical ones, so `toggle`/`adjust` work with either unchanged.
- **A keyboard for the console: an external one.** A full on-screen
  keyboard would cover most of the 320×480 screen, leaving little room
  for the console it types into. Better an external keyboard, with the
  touchscreen for buttons and maybe a small pop-up number pad (e.g. to
  enter a setpoint). Plan 9's shape fits either way: `/dev/cons` reads
  the keyboard and writes the screen, and the shell talks to it the same
  way it talks to USB serial now.

  No experience here yet with either keyboard type, so first notes on
  the extra hardware each needs (to check against datasheets and a
  breadboard try before any PCB):

  - **1. USB keyboard through a converter chip** (preferred). Small parts exist that are
    a USB host for keyboards and deliver the keys as plain serial (UART),
    e.g. WCH's CH9350L (to be checked). The RP2040 then just reads a UART
    at 3.3 V: no USB stack, no core taken. Still needs the 5 V VBUS
    supply with current limiting and the ESD protection at the USB
    connector, as above. Costs 1–2 GPIOs (UART RX, maybe TX). Probably
    the least software and the least risk.
  - **2. Anything that sends keys over a UART.** Same picoos side as the
    converter chip: a 3.3 V serial line into an RP2040 UART, read by a
    small driver behind `/dev/cons`. The source could be a ready-made
    keyboard-to-serial module, or another device or terminal on a serial
    cable. A TTL UART on a connector still wants ESD protection; a real
    RS-232 cable (±12 V) needs a transceiver (MAX3232 type), never a
    direct connection to the pins.
  - **3. PS/2 keyboard.** The simplest protocol: two lines, clock and data,
    11-bit frames clocked by the keyboard, easy to read with PIO or a
    GPIO interrupt, almost no CPU.
    - *Needs real level shifting*: the lines are open-collector, pulled
      up to 5 V inside the keyboard. Two bidirectional shifters, the
      same MOSFET circuit as an I2C level shifter (e.g. BSS138 with
      pull-ups on both sides), or the RP2040 pins see 5 V (the README's
      +5 V lesson).
    - *5 V supply* for the keyboard, current-limited, and *ESD
      protection* (TVS) on clock and data at the connector.
    - Downside: it's old-fashioned (IBM, 1987; USB replaced it in the
      2000s). Still found on industrial and some gaming keyboards, but
      most keyboards on sale are USB-only, and a USB-to-PS/2 adapter only
      works with keyboards that still speak both protocols.

  - **4. (last) USB keyboard on a second, PIO-driven USB port** (the Pico-PIO-USB
    library; the RP2040's own USB port stays the serial console and
    power input; it can be a USB host too, but then the console is gone).
    - *Data lines need no level shifting*: USB full/low speed signals
      at ~3.3 V, which RP2040 pins can drive directly, through the usual
      series resistors (~22–27 Ω) on D+ and D-. A host also needs
      ~15 kΩ pull-downs on D+/D- (check whether the library uses the
      RP2040's internal ones).
    - *5 V for the keyboard (VBUS)*: from the carrier board, through a
      current limiter (load switch or polyfuse), so a shorted cable or
      faulty keyboard can't take the board's supply down. Keyboards draw
      up to ~100 mA.
    - *ESD protection*: a USB TVS array on D+/D- and VBUS at the
      connector (e.g. a USBLC6-2 type part), since a connector is where
      people touch.
    - *Software cost, the real catch*: PIO-USB needs a lot of CPU, with
      tight 1 ms timing, and typically runs on core 1 by itself; that's
      where the TFT was going. It also needs the CPU clock at a multiple
      of 12 MHz (e.g. 120 MHz instead of the SDK's 125). Possible, but
      it changes the plan for core 1.
    - Works with any USB keyboard, which is the big plus.
    - **Ranked last (Leon, 2026-11-14):** spending a core and much of
      the RP2040's processing power on USB work is not wanted.
  **Order of preference (2026-11-14):** 1. converter chip, 2. UART,
  3. PS/2, 4. PIO-USB. Try the converter chip on the breadboard first,
  before the carrier PCB is designed.

- **How to start:** the same path as the ST7735: a standalone
  experimental repo first, on a breadboard (driver, colors, speed,
  touch and its calibration, the XPT2046 bus choice above), then a
  handoff into picoos. Display and breadboard ordered 2026-11-14.

### 6. picoos ideas on the CDP1802 (2026-11-21, future, a separate project)

Leon's CDP1802 CPU in VHDL should get the ideas of picoos: devices as
files (open/read/write, no ioctl), a cooperative scheduler of small
resumable tasks, a shell with pipes/redirects, small programs, scripts
as files. A separate project and discussion; noted here for the one
consequence for *this* code:
- **No rewrite to C** (decided 2026-11-21). The 1802 needs the ideas
  more than this exact code (its drivers and memory handling would be
  new anyway), and its known C compiler (the LCC port, lcc1802) has no
  C++.
- **Guideline instead: keep `kernel/`, `shell.cpp` and `jobs.cpp` in a
  plain C++ subset**: no heap, no exceptions, no STL, no templates, no
  RTTI. Almost true today (two small lambdas, in the parser and in
  `fsck`, would be quick to turn into functions). Then the core can be
  carried to a C compiler later without a rewrite.
- **Size:** the 1802's 64 KB is the whole machine (like a PDP-11
  process). Today's picoos (~100 KB code, ~39 KB RAM incl. 8 KB pipe
  buffers) won't fit as is; a core config with small buffers would
  (another reason for idea 3's configurable sizes).
- **Later topic: memory beyond 64 KB**, banked memory and locking (an
  expansion register in the FPGA switching which RAM bank appears in
  part of the address space), as many 8-bit machines grew.
- Perspective: Unix grew up on PDP-11s (64 KB per process, < 1 MIPS)
  and Plan 9 on early-1990s 386/486/MIPS machines; the RP2040 is faster
  than most of them. "Small" is relative.

### 7. An array of CPUs: one large computer from many small ones (2026-11-21, future)

Leon's idea (and long dream): since every small CPU has a memory limit
(the CDP1802's 64 KB; the RP2040 has no memory bus to expand), build an
**array of CPUs**, each working with its own memory block, plus one
extra CPU (N+1) as **supervisor** that allocates and delegates. A large
computer made of many small ones, while staying with small micro CPUs.

It's a known, respected architecture:
- **INMOS transputers** (1980s): arrays of small CPUs with their own
  memory, joined by fast serial links, a root node loading and directing
  the others; their language Occam used channels between processes
  (the same idea as pipes).
- **Plan 9 was designed for exactly this**: CPU servers, file servers and
  terminals, each with its own memory, joined into one namespace by
  **9P**; the `cpu` command runs work on a CPU server that still sees
  your files. So this idea and the wish for 9P (see the name entry under
  idea 3) are really one idea.

Two ways to build it:

| | Shared memory | Message passing |
|---|---|---|
| How | One big memory; each CPU gets its own window/block; a bus arbiter decides who may access | Each CPU has its own memory; CPUs talk over links (UART, SPI, PIO, or in VHDL your own) |
| Supervisor (N+1) | Allocates memory blocks (sets each CPU's window/bank registers) | Allocates *work*: run this pipeline stage, serve this device |
| Hard part | Locking and arbitration: two CPUs must never use the same data at once (the topic Leon wants to learn, see idea 6) | Latency; designing the messages |
| Fits | The CDP1802 in VHDL: the bus, arbiter and bank registers are self-designed | RP2040s: no external memory bus, but fast links (PIO: several Mbit/s) |

How picoos carries over:
- **Devices as files make distribution natural**: a node offers its
  devices as files; the supervisor mounts them over the link (9P, or
  something simpler first): `cat /node2/dev/skintemp` like a local file.
- **Pipes across nodes**: `cat /node1/dev/adc | pid ... > /node2/dev/heater`,
  each stage possibly on another node.
- The supervisor holds the namespace and hands out work, Plan 9 style.
- First step whenever it starts: two RP2040 boards and one link, one
  remote device file read through it.

## Known open items

- **babywarmer back, later**: cpu_rp2040 (73 × 54 mm, 2 × 20-pin
  headers, USB-C powered) will be the *daughter board* of the next
  babywarmer PCB. Buzzer, LEDs, 7-segment, TFT, ADC front end and power
  supply live on that carrier board, so the heater, NTC/ADS1115,
  7-segment and PID/phase/alarm code from picoos waits until it exists.
  Pins stay as on babywarmer (buttons, TFT, heater, 7-segment). The
  ADS1115 goes on GPIO12 (SDA) / GPIO13 (SCL), which the headers export;
  the RTC/FRAM bus on GPIO14/15 stays local to this board.
- **ST7735 TFT, on core 1**: the stack fix (2026-11-12) made core 1 safe
  to use. `klog`'s "no locks needed" assumption ends there (see
  `kernel/klog.h`); anything shared between cores needs a look.
- **Quotes in the shell** — **done 2026-11-16**: `"..."` and `'...'`, see
  the README's shell rules. `echo` can write any line into a file now,
  crontabs included.
- **Streaming or larger pipes**: pipes pass whole buffers (4 KB since
  hexdump, was 1 KB), which limits long output (a big `hexdump` pages
  with `-s`). Files are at most 1 KB, separately.
- **More arguments per stage** — **done 2026-11-16**, with the quotes:
  each pipeline keeps the line's text once and refers to words by
  position, so `JOB_MAX_ARGS` went 6 → 16 (15 arguments) and the job
  slots shrank from 13 KB to 5.2 KB RAM.
- **`shred` / wiping `rm`**: `rm` frees blocks but leaves the bytes (see
  the README's "What `rm` does").
- **Adding to or editing a file with `>`, `|`, `&` in it** (2026-11-16):
  - Adding: **done 2026-11-16**, as `cat >> <path>` (and `cat > <path>` to
    replace); the `script` builtin is gone. Not `script -a` after all:
    `cat >`/`cat >>` is the Unix way, with `>`/`>>` meaning what they
    always mean. To change or delete a line: write the file again.
  - Still open, bigger, the classic Unix answer: `ed`, the line editor
    (`ed /fram/crontab`, then `3d`, `2a`, `p`, `w`). Fits with the TFT
    console and a keyboard.
- **`cp <from> <to>`** (2026-11-16, Leon), next to `rm`. **Done 2026-11-16**,
  with `mv`, as described below (see the README's programs table). Today a copy works
  as `cat /fram/a > /fram/b`, but `cp` says what it means. Notes:
  - Small: a program that reads `<from>` (file or device) and writes
    `<to>`, as `cat x > y` does, so it inherits the filesystem's
    power-cut safety (old or new, never half).
  - Unix habits worth keeping: `cp a /fram` (a mount as target) means
    "same name, there"; refuse copying a file onto itself.
  - Its sibling **`mv <from> <to>`** (rename) is worth doing at the same
    time: on `/fram` it only rewrites the name in the 16-byte directory
    entry (one write, so power-cut safe), no data copied. That's a new
    `rename` operation for mounts (`kernel/fs.h`), like `remove`.
- **Internal commands can't be piped** (2026-11-16, noted, not to fix
  now): `help | grep hours` prints all of `help`, because builtins print
  straight to the terminal and the shell ignores the rest of the line.
  Same for `jobs | grep ...`. Marked as "internal commands" in `help`.
  Possible fixes later: make the read-only ones (`help`, `jobs`, `scripts`)
  programs that output text, like `ls` became one in picoos; and/or warn
  (`help: builtins can't be piped`) instead of silently ignoring `|`/`>`
  after a builtin. `sleep`, `run`, `loop`, `watch`, `kill` change the
  shell itself, so they stay builtins.
- **Prompt redraw after `/dev/cons` output?** (2026-11-16) Text written to
  `/dev/cons` (a cron `tick`) lands right after the `% ` prompt, and the
  next typed line appears without a prompt: nothing is lost, it only looks
  mixed. bash (readline) and zsh redraw: fresh line, the message, then
  the prompt and the half-typed line again. Plain Unix terminals and Plan
  9's `rc` don't, and picoos doesn't either, for now (decided: leave the
  code as is). Decide together with the TFT console (wishlist 5), where
  the screen handling gets written anyway.
  **Update 2026-11-16:** in practice a `tick` every minute *was* really
  annoying (it split typed commands on screen several times). Worked
  around by taking `tick` out of the crontab (cron output now only goes
  to devices/files). So the redraw is wanted, not just nice to have, as
  soon as anything prints to `/dev/cons` regularly; it's a small change
  (`/dev/cons` asks the shell to redraw prompt + typed line).
- **Per-job poll rate**: all background jobs run every 150 ms.
- **`hyst` shares one state** between all its users (from picoos).
- **`fsck /fram`** — **done 2026-11-16** (see the README): reclaim blocks leaked by a power cut mid-write
  (today only `mkfs` does, by wiping everything).
- **Directories on `/fram`**: not yet, by choice.
- **Next PCB**: bigger FRAM in the same SOIC-8 footprint (32 KB, no
  driver change), a flash footprint for bigger W25Q parts; RP2350 only
  if more GPIO/RAM/FPU is needed. See the README's resources section.
