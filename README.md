# plan2040

A Plan 9-flavored little OS for small CPUs: every device a file, small
programs, pipes, background jobs, scripts as files, a `%` shell. The
next version of [picoos](https://github.com/leonhiem/picoos), renamed
on 2026-11-21 (see "The name: plan2040" below). One source tree for all
boards: each firmware is described by a config file (`configs/`), Plan 9
kernel-config style (see "Source tree and configs").

It runs on **cpu_rp2040**, a small, general-purpose RP2040 board (the
first board, and the only config so far). The PCB is derived from the babywarmer board: same RP2040, power
supply, crystal, QSPI flash, CD4093/CD4013 on/off logic and USB, with
two header rows exposing all GPIOs, plus:

- **PCF8563T** real-time clock (I2C1, address `0x51`; `INT`/`CLKOUT` on pads only)
- **FM24CL64B** 8 KB FRAM (I2C1, address `0x50`; `WP` tied to GND)

Pin map: [`boards/cpu_rp2040/board.h`](boards/cpu_rp2040/board.h). Ideas and open items: [`TODO.md`](TODO.md).

## The name: plan2040

This OS started in August 2026 as **picoos**: *Pico* (the Raspberry Pi
Pico) + *OS*, a quick name for a multitasking experiment on the
babywarmer firmware. The experiment worked out better than expected:
the cooperative scheduler stayed simple, and the Plan 9 ideas (every
device a file, small programs, pipes, scripts as files) turned out to
be simple and good. By November it had a filesystem, an hour counter,
cron and a real shell, and three diverging copies (picoos on the old
babywarmer board, picogateway on a Pico W, this repo) were to become
one OS for all boards. That called for a real name, decided on
2026-11-21 in a long discussion:

- **It had to credit the Plan 9 team at Bell Labs**, whose ideas this OS
  re-uses (and whose 9P protocol is on the wish list for networking).
  So the word *plan* stays.
- **Humble, not a successor.** *plan10* was rejected: it would suggest
  improving on Plan 9, and this OS borrows their ideas, it doesn't
  better them. *nextplan* and *nextplan9* came close (new ideas keep
  coming, "the next plan"), but remind of NeXT. *planlet*, *plan8*,
  *plan9jr* and others were considered.
- **Not tied to one chip**, since the same ideas should also run on
  Leon's CDP1802 CPU in VHDL, an RP2350 later, or an array of many small
  CPUs working together.
- Other directions tried: Space Odyssey (*plan2061*, *plan3001*, after
  Clarke's sequels), the Bible (*Selah*, "pause", like a cooperative
  task; *Mustard*, the mustard seed), the Plan 9 bunny (*Warren*, *Kit*).

**plan2040** solves it as a family: *plan* for Plan 9, and the number is
the CPU it runs on. 2040 is the RP2040 this OS was born on, and reads
as a futuristic year too. The CDP1802 version will be **plan1802** (a
historical year, as it happens), an RP2350 build would be **plan2350**:
the plan family, Plan 9's ideas on small CPUs.

The code lives at
[github.com/leonhiem/plan2040](https://github.com/leonhiem/plan2040),
which carries the full history of the cpu_rp2040 repo, where this OS
grew from 2026-11-10 to 2026-11-21 (that repo is archived now).

## Hardware notes

- **Chip revision: RP2040 B2.** Read from the boot ROM's version byte
  (`hexdump -s 0x13 -n 1 /dev/mem` → `03`).
- **GPIOs are 3.3 V only, never wire a load to +5 V.** The first test
  LED had its anode on +5 V (cathode via 220 Ω to GPIO1). It blinked
  fine, but whenever the pin is high or unconfigured (every reset and
  boot) 5 V leaks through the LED into the pin's protection diode. RP2040
  pins tolerate at most IOVDD + 0.5 V. Fixed by moving the anode to 3V3.
  Anything that really needs 5 V gets a transistor or MOSFET in between.
- **Unconfigured pins have a pull-down: an LED to 3V3 on one glows.**
  Before picoos drove GPIO6, a test LED there (3V3 → LED → 220 Ω →
  GPIO6) glowed dimly, with 1.5 V on the pin. At reset every RP2040 GPIO
  is an input with a ~50 kΩ pull-down, which lets a few tens of µA
  through the LED. Outputs like this are now switched off first thing at
  boot (`board_lamp_init()`, also in `bringup`).
- **The push buttons have no external pull-ups:** the RP2040's internal
  pull-ups are switched on in software (`buttons_init()`): 3.3 V
  released, 0 V pressed.

### RTC (PCF8563T)

- **Voltages:** the clock keeps time down to about **1.0 V**. The I2C
  interface needs at least 1.8 V, but that only matters while the board
  is powered, and then VDD comes from 3V3 anyway.
- **VL flag** (bit 7 of the seconds register): set when VDD has dropped
  below about 0.9 V, i.e. the time can't be trusted. Expect it on first
  power-up of a new board, or after the backup cap ran flat. Writing the
  time (`set`) clears it. A clear VL after a power-off means the backup
  held.
- **First power-up contents are garbage:** the first board woke up at
  `2000-12-14 06:02:20` (random registers), and `control_2` read
  `0x80`/`0xc0`: bits 7–6 are unused and come up random. The
  alarm/timer bits (AF, TF, AIE, TIE) were 0. A driver should write
  `control_2` explicitly at init, not trust its power-on value.
- **CLKOUT is on by default** (32.768 kHz square wave on its pad). The
  pad isn't wired to anything, so a driver should switch it off
  (register `0x0D` = `0x00`): it costs extra backup current for nothing.
- **INT and CLKOUT** are on pads only, not connected to a GPIO yet. A
  wire to a free GPIO later makes RTC alarms/timer interrupts possible.
- Bring-up result (2026-11-11): found at `0x51`, ticking, `set` works
  and clears VL. Kept the time over 15 minutes unplugged on the
  supercap (charged to ~1.8 V at the time), with VL still clear.

### RTC backup supercap

- **Circuit:** follows the PCF8563 application note, which uses 1 F. This
  board uses **10 F / 3 V** instead (that's what was at hand): charged
  from 3V3 through **220 Ω + 1N4007** in series, the cap sitting directly
  on the PCF8563's VDD. The diode does two jobs: its drop keeps the cap
  at or below its 3 V rating (it ends up around 2.8–2.9 V, as the drop
  shrinks at low current), and it blocks the cap from discharging back
  into the 3V3 rail when the board is off.
- **Charging takes hours:** RC = 10 F × 220 Ω = 2200 s (~37 min), and the
  tail is slower still as the diode drop falls off. Initial charging
  current is about 15 mA from 3V3. Don't judge the backup until the cap
  is fully charged.
- **The cap is useful well before it's full:** since the clock runs down
  to ~1.0 V, a cap at 1.8 V already backs up the RTC. Each extra volt
  just buys more time.
- **How long it lasts:** the PCF8563 draws about 0.25 µA in backup (with
  CLKOUT off). 10 F from 2.9 V down to 1.0 V is ~19 C, which on paper is
  years. In practice the supercap's own leakage dominates, so expect
  weeks to months. Measure it rather than trusting the estimate: power
  off, wait, power on, `rtc`, check VL.
- 10× the application note's capacitance means 10× the charge time and
  roughly 10× the backup time.

The cpu_rp2040 repo started fresh (2026-11-10). picoos's history up to
there lives in its own repo, at commit `89c16e5`.

## Building

Same toolchain setup as picoos (Slackware paths, adjust for your system):

```sh
export PICO_SDK_PATH=/home/leon/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=/home/leon/pico/pico-sdk/toolchain/gcc-arm-none-eabi-10.3-2021.10
export CMAKE_FIND_ROOT_PATH=$PICO_TOOLCHAIN_PATH
export CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
export CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
export CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY

mkdir -p build && cd build
cmake ..                              # config cpu_rp2040, the default
make -j$(nproc)
```

Another config: `cmake -DPLAN2040_CONFIG=<name> ..` in a build directory
of its own (e.g. `build-babywarmer_v1`); switching configs in one build
directory needs `rm -rf build` first.

Flash a `.uf2` the usual way (hold BOOTSEL, plug in USB, copy the file),
or from a running plan2040: `echo bootsel > /dev/reboot`.

Two firmwares come out of one build:

- `build/plan2040.uf2`: the OS (below)
- `build/bringup.uf2`: the board's test (further below)

The PC tests: `make -C tests/host check` (see
[`tests/host/README.md`](tests/host/README.md)).

## Source tree and configs

One tree for every board and every firmware (TODO.md idea 3, restructured
2026-11-21). The layers, from the same-everywhere core outwards:

| Directory | What | Same on every board? |
|---|---|---|
| `kernel/` | scheduler (`task`), files and devices (`fs`), programs table (`prog`), `klog`, the shell (`shell.cpp`), its pipelines and jobs (`jobs`), `main.cpp` | yes |
| `fs/` | filesystems and stores: `framfs` (the FRAM filesystem), `hours` (hour counters) | generic |
| `dev/` | devices, one file each: `dev/<name>.cpp` | generic, picked per config |
| `prog/` | programs, one file each: `prog/<name>.cpp` | generic, picked per config |
| `drivers/` | chips: `pcf8563` (RTC), `fm24cl64` (FRAM), `at24c` (24C256/512 EEPROM), `ads1115` (ADC), `buttons` (debounce); a driver is `drivers/<name>.cpp` (or `drivers/<name>/<name>.cpp` for one with files of its own). Every driver follows [`drivers/README.md`](drivers/README.md): bus and address from `board.h`, timeouts, a reason on failure, never waiting for the chip | generic, picked per config |
| `boards/<board>/` | `board.h` (pin map, buses, each chip's bus and address, FRAM layout), `board.cpp` (`board_init()`, `board_pins_init()`) | one per board: `cpu_rp2040`, `babywarmer_v1` |
| `boards/bringup.cpp` | the board test, for every board (from its `board.h`) | yes |
| `scripts/` | built-in scripts, plain files: `boot` | generic, picked per config |
| `configs/<name>.cmake` | one firmware: board, devices, programs, sizes: `cpu_rp2040`, `babywarmer_v1` | one per firmware |
| `apps/<app>/` | an application's own `dev/`, `prog/`, `scripts/` and other sources: babywarmer (step 3), gateway (later) | per application |
| `tests/host/` | PC tests | |

A config is a short list, Plan 9 kernel-config style. From
[`configs/cpu_rp2040.cmake`](configs/cpu_rp2040.cmake):

```cmake
set(PLAN2040_BOARD   cpu_rp2040)
set(PLAN2040_DEVS    led lamp buttons rtc uptime cputemp reboot fram mem hours cron cons)
set(PLAN2040_PROGS   cat echo label thresh hyst toggle adjust follow select date uptime
                     wc grep headtail tee hexdump remember cpmv rm fsck mkfs df ls)
set(PLAN2040_SCRIPTS boot)
set(PLAN2040_FS      framfs hours)
set(PLAN2040_DRIVERS pcf8563 fm24cl64 buttons)
set(PLAN2040_SIZES   MAX_JOBS=20 STAGE_BUF=4096 PROG_MAX=48 FS_MAX_DEVICES=64 TASK_MAX=32)
```

How it's built:

- The names are files: `led` in `PLAN2040_DEVS` is `dev/led.cpp`, `cat`
  in `PLAN2040_PROGS` is `prog/cat.cpp` (a file can hold more than one
  program: `headtail` is `head` and `tail`, `cpmv` is `cp` and `mv`). A
  name without a file stops `cmake` with a message.
- **An app** (`set(PLAN2040_APP babywarmer)`) adds `apps/<app>/dev/`,
  `prog/` and `scripts/`: every name is looked up there first, then in
  the generic `dev/`, `prog/`, `scripts/`. An app's file of the same name
  shadows the generic one (cmake says so), like a Plan 9 `bind`. The
  app's other sources (a screen, a table) go in `PLAN2040_APP_SRC`,
  relative to `apps/<app>/`, and its headers are found from there.
- `PLAN2040_SCRIPTS` names the built-in scripts (see "Scripts").
- CMake writes `build/conf.cpp` from the lists, the way Plan 9's
  `mkdevc` builds a kernel's device table from its conf file: one
  `conf_register()` that calls `dev_<name>_register()` and
  `prog_<name>_register()` in the config's order. `main.cpp` calls
  `board_init()`, then `conf_register()`, then starts the shell; it knows
  nothing about any board or device.
- Order matters: it's the order `ls` lists them, and the order they
  start in (`fram` mounts `/fram` before `hours` reads the FRAM's system
  area, `rtc` comes before `cron`).
- The sizes are compile definitions for every file; each header has the
  same value as its default, so a config only needs the ones it changes.
  `MAX_JOBS` is 1..32; every job slot is a task, so `TASK_MAX` must hold
  the job slots plus the other tasks (on cpu_rp2040: shell, hours,
  cron = 23 of 32).
- The banner says which board: `plan2040 on cpu_rp2040`
  (`PLAN2040_BOARD`).
- Chosen at compile time, not detected at start-up: pins can't be probed
  safely, and a Pico W's wifi needs other SDK libraries. A config may set
  `PICO_BOARD` (it's read before the SDK).

**Buses and chips are a fact of the board** (decided 2026-11-21), so
`board.h` names each I2C bus and, per chip, its bus and address:
`FRAM_I2C` + `FRAM_I2C_ADDR`, `RTC_I2C` + `RTC_I2C_ADDR`, and on
babywarmer_v1 also `ADS1115_I2C`, `EEPROM_I2C` (+ `EEPROM_SIZE`,
`EEPROM_PAGE`). Drivers take them from there, so the same driver works
whichever bus a chip sits on. `board_i2c_init()` starts every bus.

| Board | Bus | Pins | Chips |
|---|---|---|---|
| cpu_rp2040 | local, i2c1, 400 kHz | GPIO14/15 | FRAM 0x50, RTC 0x51 |
| babywarmer_v1 | local, i2c1, 400 kHz | GPIO14/15 | FRAM 0x50, RTC 0x51 (on the cpu_rp2040 board) |
| babywarmer_v1 | header, i2c0, 200 kHz | GPIO12/13 | ADS1115 0x48, EEPROM 0x52 (on the babywarmer PCB) |

babywarmer_v1 is the cpu_rp2040 board wired in as the CPU of the old
babywarmer PCB (TODO idea 3, "The babywarmer_v1 hardware hack"). Its
config has the generic devices only for now (step 3a); the babywarmer
app comes next, from picoos (TODO idea 3).
Build it in its own directory:

```sh
mkdir -p build-babywarmer_v1 && cd build-babywarmer_v1
cmake -DPLAN2040_CONFIG=babywarmer_v1 ..
make -j$(nproc)
```

**The babywarmer app** (`apps/babywarmer/`, from picoos @ 89c16e5) comes
in step by step (TODO idea 3, steps 3c-3g). So far the sensors (3c),
the outputs (3d), the control (3e) and the TFT (3f):

| Device | | |
|---|---|---|
| `/dev/skintemp` | read | Skin temperature, °C (`36.5`): the skin NTC on ADS1115 channels 0-1. Answered at once from the background sampling (below). When there's no fresh reading: `/dev/skintemp: ADS1115 (0x48, header bus) not answering`. |
| `/dev/ambient` | read | Room temperature, °C: the room NTC on channels 2-3. Same. |

**Background sampling** (`apps/babywarmer/ntcadc.cpp`, since 2026-11-23):
the "adc" kernel task keeps the ADS1115 converting, skin and room in
turn, each about every 100 ms, and never waits for a conversion (our own
driver, `drivers/ads1115.cpp`). Measured on the PC (`adctest`): ~1.3 % of
the CPU; a change shows within ~70 ms. When the chip stops answering it
tries again once a second (a stuck bus then costs ~1 % of the CPU, the
panel stays quick), the devices fail after 1.5 s with the reason (one
glitch never shows), and they're back within a second of the fix.
picoos converted on every read and waited ~8 ms for it, ~16 % of the CPU
with the boot script's readers.
| `/dev/current` | read | Heater current sense, raw ADC counts 0..4095 (GPIO26, 8 samples averaged). |
| `/dev/heater` | read/write | Heater power 0-100 % (`25`, `50.5`; `on` = 100, `off` = 0; clamped). Time-proportional: the SSR (GPIO11) is on for that share of every 15 s window, switched by the `tpo` kernel task every 100 ms; the bargraph (GPIO16, PWM ~3 kHz) shows it. Reading gives what's applied. Heats only with the safety relay energized. |
| `/dev/heater/ssr` | read | The SSR right now and how long it has been so, in ms: `on 1300`, `off 200`. What the heater-current check judges. |
| `/dev/relay` | read/write | The mechanical safety relay (GPIO17): `on` = energized, the heater may run; `off` = released. |
| `/dev/alarm` | write | Buzzer (GPIO0) + alarm LED (GPIO1): `on` both, `muted` LED only, `off` neither. |
| `/dev/seg7big`, `/dev/seg7small` | write | The two 3-digit displays: a number, up to 3 digits right-aligned, `.` lights the dot after the middle digit (`36.5`). More digits: the first three show. No digits (e.g. `echo off`): blank. |
| `/dev/seg7big/blink` | read/write | `on`: the big display blinks its value (0.5 s on, 0.5 s off; the "seg7" task); `off`: steady. |
| `/dev/leds`, `/dev/leds/<name>` | write; read/write | The 7 front-panel LEDs (`aut warm low high fail chk man`): `echo "man on" > /dev/leds`, or `echo on > /dev/leds/man`. |

Every output is off at power-up (`board_pins_init()`: SSR off, relay
released, buzzer silent) and changes only by a write to its device; the
7-segment displays and LEDs light up for a 1 s lamp test at boot.

The control, as devices that hold values and programs that run as jobs
(all from picoos, see the comments in each file for the details):

| Device | | |
|---|---|---|
| `/dev/setpoint` | read/write | Skin setpoint, 30.0-39.0 °C, default 35.0. |
| `/dev/heaterauto` | read/write | `on` = auto (the PID), `off` = manual (`/dev/percent`). Starts `off`. |
| `/dev/percent` | read/write | Manual heater power, 0-100, default 10. |
| `/dev/pid/kp`, `ti`, `td`, `dt`, `integral`, `prevmeas`; `/dev/pidout` | read/write | The PID's tuning, its state, and its output. |
| `/dev/state` | read/write | The phase: `idle boost coast pid safe` (written by `phase`). |
| `/dev/autopower`, `/dev/safepower` | read/write | Auto mode's power; safe mode's power (from `safelut`). |
| `/dev/alarm/heater`, `temphigh`, `templow`, `sensor` | read/write | The alarm conditions (written by the guard): `on`/`off`. `heater` and `sensor` give their reason too: `on: no current while switched on (element, fuse or SSR?)`, `on: current while switched off (SSR stuck on?), relay held off`, `on: skin 24.0 C, more than 3 C below the setpoint for 20 s (sensor off the baby?)`. |
| `/dev/guard` | read | The guard's verdict on the skin reading, with the reason: `ok`, `safe: <why>`, `none: <why>` (see "The guard" below). |
| `/dev/heater/limit` | read | The guard's say over the heater: `cap 100.0` (the jobs' power, at most that) or `set 39.0` (the guard's own power, whatever the jobs ask). |

| Program | What it does |
|---|---|
| `phase` | once a second: idle (manual) → boost (80 %) → coast (0 %) → pid; shows `safe` while the guard says so, and hands back to the PID bumpless (the PID starts from the power in use) |
| `pid <gate> <state> <setpoint>` | the PID on the skin temperature, while `/dev/state` is `pid` |
| `safelut` | room temperature → a fixed heater power, for safe mode |
| `alarm <cond...>` | buzzer + alarm LED from the conditions; MUTE silences the buzzer for a minute |
| `ledwire` | the front-panel LEDs from mode, phase and alarms |
| `monitor` | one status line, for `watch` |

**The TFT** (ST7735, 128×160): the babywarmer screen, from Leon's
`st7735` repo, kept as a blackbox copy in `apps/babywarmer/tft/` (see
its README). Icons for FAIL, ALARM, SENSOR and the mode, the heater bar,
a baby face (ok, cold, hot), and the APGAR clock (START starts it), or a
21×16 text console.

| Device | | |
|---|---|---|
| `/dev/tft/aut`, `man`, `chk`, `low`, `high`, `fail` | read/write | The screen's conditions, `on`/`off` (`tftwire` sets them from the same sources as the LEDs). |
| `/dev/tft/heater` | read/write | The heater bar, 0-100. |
| `/dev/tft/apgar` | read/write | `on` (re)starts the APGAR clock. |
| `/dev/tft/mode` | read/write | `graphical` (the warmer screen) or `text` (the console). |
| `/dev/tft/seek`, `text`, `clear` | write; read/write; write | Text mode: cursor row 0-15, a line at the cursor (then the next row), clear the screen. `echo Heater: 42 > /dev/tft/text`. |

`tftwire` feeds the screen; the `tftflush` kernel task redraws it every
20 ms (a task, not a job, so `kill` can't freeze the screen). `run tftmon`
(`apps/babywarmer/scripts/tftmon`) shows ten values on the text console
once, `loop tftmon` every second; `echo graphical > /dev/tft/mode` goes
back. `scripts` lists both built-in scripts.

**The guard** (`apps/babywarmer/dev/guard.cpp`, since 2026-11-24): all
of the heater's safety in one kernel task, once a second. `kill` can't
stop it and no boot script can leave it out: the control (phases, PID,
which power goes to the heater) is jobs, the safety isn't. It trusts
only hardware facts (skin, room, current, the SSR, the relay) and the
user's intent (setpoint, auto/manual), never what a job computed.

| `/dev/guard` | When (auto mode) | The heater |
|---|---|---|
| `ok` | the skin reading is used | the PID's power, as the jobs ask |
| `safe: <why>` | the sensor fell off the baby (the skin fell > 0.08 °C/s for 3 s, or stays > 3 °C below the setpoint for 20 s); it was off from the start (< 0.3 °C rise in 5 min of warm-up); the skin reading fails; or the control jobs are gone (nothing wrote `/dev/heater` for 10 s) | **the guard's own power**: the room table (`safelut`) for the room temperature, whatever the jobs ask |
| `none: <why>` | no temperature at all (skin and room unreadable) | the guard switches to **manual** at the room table's power for the last room temperature seen (else 10 %); UP/DOWN adjust it |

Back to `ok` by itself: 2 minutes after the skin is within 2 °C of the
setpoint again (sensor causes), 5 s after a reading or the control is
back (hardware causes); from `none`, back to auto. Manual mode (the
nurse's choice): hardware safety only. The guard also owns what
`alarmcheck` did (that job is gone): the relay (off on overtemp, on when
heating), the temperature alarms, the heater-current check, the stuck
SSR; and it writes `/dev/alarm/sensor`. Each change is said once on the
console: `guard: safe: skin 24.0 C, more than 3 C below the setpoint for
20 s (sensor off the baby?), heating 39 % from the room (24.0 C)`.
Deliberately no cleverer plausibility checks (Leon: too many false
alarms, a ceiling fan already makes the heater work harder). Known
limit: a sensor lying on the mattress under the heater warms up and
isn't caught. The guard's decision acts in `tpo_apply()`, within 100 ms.

**What the nurse sees** (decided with Leon 2026-11-24; `ledwire` and
`tftwire` show it from `/dev/guard`, the boot script's `select` picks the
small display's value from `/dev/state`):

| | ok | safe | none |
|---|---|---|---|
| CHK LED, TFT SENSOR icon + triangle | off | on / blinking | on / blinking |
| big display | the skin temperature | **the reading, blinking**: shown, not trusted (it may be the room) | **blank**: no reading |
| small display | the setpoint (auto) or the manual % | **the power it heats with** | the manual %, UP/DOWN adjust it |
| MODE (LEDs, TFT) | auto/manual | auto | **manual** (the guard switched) |
| buzzer | - | sounds, MUTE silences it for 1 min | the same |
| console | - | `guard: safe: <why>, heating N % from the room (T C)` once | `guard: none: <why>, manual at N %` once |

Putting the sensor back is all a nurse has to do: 2 minutes later the
warmer is back on skin control by itself (or MANUAL, to take over). A
text line on the TFT's warmer screen comes later, in the st7735 repo.

**The boot script** (`apps/babywarmer/scripts/boot`) wires the control
together, 14 lines: MANUAL toggles auto/manual, UP/DOWN adjust the
setpoint or the manual percent, the displays follow, `phase`, `select`
and `follow` choose the power for `/dev/heater`, `alarm` and `ledwire`
show the alarms, and `tftwire` feeds the TFT. `jobs` lists the 13 jobs
it starts. Like any boot script, `/fram/boot` replaces it.

Both NTCs go through the table in `apps/babywarmer/ntc_lut.h` (the
2026-08-30 recalibration for a substitute NTC). Two things to know
about it (both to be fixed by the EEPROM table, step 3g): nothing reads below 22.9 °C
(the table's coldest entry), and ADC codes above 21742 wrap around to
the hottest entry, 91.8 °C (`ntctest` in `tests/host` shows the whole
mapping).

**Open points** (TODO idea 3, step 4): the room table (`safelut`, 39 %
at 24 °C) is a guess, to check on the bench; it conflicts with picoos's
note that the PID integrator settled at ~6.6 %, and that decides the
coast seed too. (Fixed 2026-11-23/24: the heater-current
check's false alarms, a failing job's error every 150 ms, and a dead
ADS1115 leaving the heater at a stale power.)

Size with everything (2026-11-23): ~200 KB flash (10 % of 2 MB, ~88 KB
of it the TFT's bitmaps), ~33 KB RAM; 64 devices, 31 programs, 25 tasks.

Still to come: the EEPROM table (3g); the gateway app (postponed).

## plan2040

Same kernel, shell and generic programs as picoos; the babywarmer-
specific parts (heater, NTC sensors, 7-segment, TFT, PID/phase/alarm
wiring) stayed behind for now (they come back as `apps/babywarmer`).
New: the cpu_rp2040 board's own devices and more small Unix-style
programs. Small commands, many of them, each doing one thing.

Connect a USB serial terminal; type `help` for the builtins, the grammar
and the commands most often looked up (`echo bootsel > /dev/reboot` to
reprogram, setting the clock, `mkfs`, ...), `ls` for every device and
program.

```
stage [| stage ...] [< path] [> path | >> path] [&]
stage := program [arg...]
```

At every power-up the built-in `boot` script prints the date and wires
the LAMP button to the lamp output, as on babywarmer
(`toggle /dev/buttons/lamp /dev/lamp &`).

### Devices (`/dev/...`)

| Device | R/W | What it does |
|---|---|---|
| `/dev/led` | read/write | The on-board LED (GPIO1). `on`/`off`. |
| `/dev/lamp` | read/write | The lamp output (GPIO6, babywarmer's baby-light). `on`/`off`. Active-low for now (`PIN_LAMP_ON` in `boards/cpu_rp2040/board.h`). |
| `/dev/buttons` | read | Names of all buttons pressed since the last read, read-and-clear. |
| `/dev/buttons/<name>` | read | One button (`up`, `down`, `mute`, `manual`, `start`, `lamp`): `1` if pressed since the last read, else `0`. |
| `/dev/rtc` | read/write | The RTC: `YYYY-MM-DD HH:MM:SS`. Write the same format to set it. |
| `/dev/rtc/vl` | read | `on` if the RTC time can't be trusted (backup ran flat), else `off`. |
| `/dev/uptime` | read | Seconds since boot. |
| `/dev/cputemp` | read | RP2040 die temperature, °C (rough, a few degrees). Each read averages 16 ADC samples. |
| `/dev/reboot` | write | `now` reboots, `bootsel` reboots into the USB bootloader. |
| `/fram/<name>` | read/write | Files on the FRAM, see [Files on the FRAM](#files-on-the-fram-fram). |
| `/dev/fram` | read | The raw FRAM chip, all 8 KB, filesystem structures included. Read-only. Use `hexdump`. |
| `/dev/mem` | read | RP2040 memory by address (offset = address): boot ROM `0x0`, flash `0x10000000`, RAM `0x20000000`. Anything else is refused (no crash). Read-only. Use `hexdump`. |
| `/dev/hours` | read | Power-on hours, `hours:minutes` (e.g. `1234:30`), or `corrupt`/`error`. See [Hour counter](#hour-counter-devhours). |
| `/dev/hours/status` | read | `ok`, `new` (started at 0 this boot), `corrupt` or `error`. |
| `/dev/hours/ctl` | write | `set H:MM`: deliberately set or reset the counter. |
| `/dev/cron` | read/write | `on`/`off`: whether `/fram/crontab` lines run. `on` at power-up. See [cron](#cron-framcrontab). |
| `/dev/cons` | write | The console as a file (Plan 9's name): `echo hello > /dev/cons` prints on the terminal. |

### Programs (`bin/...`)

| Program | Usage | What it does |
|---|---|---|
| `ls` | `ls` | List every device and program. |
| `cat` | `cat [path]` | Read a device, or pass piped input through. |
| `echo` | `echo <words...>` | Print its arguments. |
| `date` | `date` | `/dev/rtc`, human-readable: `Wed Nov 11 22:18:00 2026`. |
| `uptime` | `uptime` | `/dev/uptime`, human-readable: `up 0d 01:02:03`. |
| `wc` | `wc [-l]` | Count lines, words, bytes of piped input (`-l`: lines only). |
| `grep` | `grep [-v] <text>` | Keep piped lines containing `<text>` (`-v`: not containing). Plain substring, no regex. |
| `head` | `head [n]` | First n lines (default 10). |
| `tail` | `tail [n]` | Last n lines (default 10). |
| `tee` | `tee <path>` | Write piped input to a device and pass it on. |
| `label` | `label <word...> <path>` | Words followed by a device's value: `label temp = /dev/cputemp`. |
| `thresh` | `thresh <n or path>` | Piped number `>=` threshold → `on`, else `off`. |
| `hyst` | `hyst <n or path> <band>` | Like `thresh` with a hysteresis band, heating polarity (`on` when below). |
| `toggle` | `toggle <button> <target>` | If the button was pressed, flip the target `on`/`off`. Run with `&`. |
| `adjust` | `adjust <gate> <on-target> <off-target> <step>` | UP/DOWN steps whichever target the gate selects. Run with `&`. |
| `follow` | `follow <gate> <on-source> <off-source> <target>` | Copy whichever source the gate selects to the target. Run with `&`. |
| `select` | `select <state> <label>=<source> ...` | Output the source whose label matches the state device's value. |
| `ls` | `ls [-l] /fram` | List the files on a mount (`-l`: with sizes). |
| `rm` | `rm <path...>` | Remove files. |
| `cp` | `cp <from> <to>` | Copy a file, or a device's value into a file (at most 1 KB). `<to>` a mount (`/fram`): same name there. Refuses copying a file onto itself. |
| `fsck` | `fsck /fram` | Check the filesystem; frees blocks a power cut left behind, reports damaged files. Also runs by itself at every boot. See [fsck](#fsck-checking-the-filesystem). |
| `mv` | `mv <from> <to>` | Rename within `/fram`: only the name in the directory entry is rewritten (one write, no data copied). Replaces an existing `<to>`. Not for devices or between mounts (use `cp`, then `rm`). |
| `df` | `df` | Total, used and free space on every mount. |
| `mkfs` | `mkfs /fram` | Format: every file on it is gone. Never happens on its own. |
| `hexdump` | `hexdump [-s off] [-n len] [path]` | Hex + ASCII, `hexdump -C` layout, of a path or piped input. Repeated lines become `*`. See [hexdump](#hexdump). |
| `remember` | `remember <device> <file>` | Keep a file equal to a device: write the device's value to the file when they differ. Run with `&`. See [Settings that survive a power cycle](#settings-that-survive-a-power-cycle). |

Builtins (`help` lists them; they're not in `ls`, like `cd` isn't on
`$PATH`): `jobs`, `kill %<id>`, `sleep <ms>`, `run <name>`,
`loop <name>`, `scripts`, `watch <ms> <pipeline>`. Writing a file by
typing is `cat > <path>` / `cat >> <path>`, see [Writing a file:
`cat >`](#writing-a-file-cat-).

### Shell rules in one place

- `|`, `<`, `>`, `>>` and `&` don't need spaces around them: `ls|grep dev`
  works the same as `ls | grep dev`.
- `#` at the start of a word starts a comment, to the end of the line
  (handy in script files). Inside a word it's an ordinary character: `a#b`.
- **Quotes: `"..."` and `'...'`** (since 2026-11-16). Inside quotes
  everything is plain text, spaces and `|` `<` `>` `&` `#` included:
  `echo "a > b"` prints `a > b`, and `echo "|"` prints `|` (a quoted `|`
  is never a pipe). Both kinds mean the same (there are no variables to
  expand); use one kind to quote the other: `echo "it's"`,
  `echo 'say "hi"'`. Quoted and unquoted parts next to each other make
  one word, as in sh: `a"b c"d` is `ab cd`; `""` is an empty word. No
  backslash escapes. A missing closing quote is an error
  (`missing closing "`), not a guess.
- **Up to 15 arguments per program**, and a quoted word can be as long
  as the line (95 characters).
- **Internal commands (shell builtins) can't be piped or redirected.**
  `help`, `jobs`, `kill`, `sleep`, `run`, `loop`, `scripts`, `watch` print
  straight to the terminal, and the shell ignores whatever follows them on
  the line: `help | grep hours` prints all of `help`, unfiltered (found
  2026-11-16). Programs and files are fine: `ls /fram | grep lam`,
  `cat /fram/boot | grep butt`, `hexdump -n 256 /dev/fram | grep cron`.
  `help` lists them under "internal commands".
- One foreground command at a time. `sleep`, `run`, `loop` and `watch`
  hold the shell until they're done; anything typed or pasted meanwhile
  waits its turn.
- Background jobs (`&`) all run every 150 ms. Up to 20 at once.

### Errors, in words

Since 2026-11-23 a failure says what failed, where, and why, in the
style of Plan 9's `errstr`, instead of `cat: error`:

```
% cat /fram/nope
cat: /fram/nope: no such file
% echo hello > /fram/waytoolongname
/fram/waytoolongname: bad name (1-11 characters, no spaces or /)
% cat /dev/skintemp                      (babywarmer, ADS1115 unplugged)
cat: /dev/skintemp: ADS1115 (0x48) not answering
```

- The part that knows sets the reason (`werrstr()`, `kernel/errstr.h`):
  a device ("ADS1115 (0x48) not answering"), the FRAM filesystem
  ("directory full (32 files)", "no space left (df /fram)", "file full
  (files are at most 1 KB)", "not formatted (mkfs /fram) or FRAM not
  answering", "damaged file (run fsck /fram)"), or the kernel's file
  layer, which puts the path in front and fills in a general reason
  ("value not accepted", "can't be read (write-only)") when a device
  didn't say why.
- **A background job says it once.** A failing job prints its reason
  when it starts failing (and again only if the reason changes), and
  once when it works again; meanwhile `jobs` shows it:
  ```
  [7] cat: /dev/skintemp: ADS1115 (0x48) not answering
  % jobs
  [7] cat /dev/skintemp | pid /dev/state pid /dev/setpoint > /dev/pidout &
      failing: cat: /dev/skintemp: ADS1115 (0x48) not answering
  ...
  [7] ok again
  ```
  Before, a failing job retried every 150 ms and either printed its
  error every time (11,250 lines in 10 minutes in the babywarmer
  simulation) or nothing at all.
- A cron line that fails says so, `cron: <reason>`, at most once a minute.
- Why (Leon, 2026-11-23): no cryptic errors, and a console that stays
  usable exactly when something is wrong.

### Examples

**Look around**

```
ls                                   # every device, mount and program
ls | grep /dev | wc -l               # how many devices?
ls | grep bin/ | head 5              # the first five programs
cat /dev/rtc/vl                      # can the clock be trusted?
```

**Time**

```
date                                 # Thu Nov 12 18:04:13 2026
echo 2026-11-12 18:04:00 > /dev/rtc  # set the clock (ISO, what you read is what you write)
uptime                               # up 0d 00:01:25
label up = /dev/uptime               # up = 85
```

**Lamp, LED, buttons**

```
echo on > /dev/lamp
cat /dev/lamp                        # on
toggle /dev/buttons/lamp /dev/lamp & # LAMP button flips the lamp (the built-in boot does this)
toggle /dev/buttons/mute /dev/led &  # and MUTE flips the LED
cat /dev/buttons                     # which buttons were pressed since the last look
jobs                                 # [1] toggle /dev/buttons/lamp /dev/lamp &  ...
kill %2                              # stop the MUTE one
```

**Sensors and control, no control-loop code**

```
watch 1000 cat /dev/cputemp                  # Ctrl-C to stop
cat /dev/cputemp | thresh 30 > /dev/led &    # LED on while the chip is at 30 C or more
cat /dev/cputemp | hyst 30 2 > /dev/lamp &   # lamp on below 29, off above 31: a thermostat
cat /dev/cputemp | tee /fram/temp | thresh 30 > /dev/led &   # tee: keep the last reading in a file AND drive the LED
```

**Files**

```
mkfs /fram                           # once, on a new board
echo hello > /fram/a.txt             # create or replace
echo again >> /fram/a.txt            # append
cat /fram/a.txt
cat /fram/a.txt | grep again
ls -l /fram
date >> /fram/log                    # a log, one line per entry
label temp /dev/cputemp >> /fram/log
cat /fram/log | tail 4               # the last two entries
cat /fram/log | wc -l
cp /fram/boot /fram/boot.old         # a copy before changing boot
mv /fram/boot.old /fram/boot         # ...and back (replaces boot)
cp /dev/rtc /fram                    # a device's value into /fram/rtc
rm /fram/a.txt
df
```

**Looking at raw bytes**

```
hexdump /fram/boot                   # a file, as bytes
cat /dev/rtc | hexdump               # anything piped
hexdump /dev/fram                    # the whole FRAM, one screen (thanks to '*')
hexdump -s 0x40 -n 64 /dev/fram      # the first four directory entries
hexdump -s 0x10000000 -n 64 /dev/mem # start of the flash: boot2
```

**Maintenance**

```
echo bootsel > /dev/reboot           # USB bootloader: copy the next .uf2, no BOOTSEL button needed
echo now > /dev/reboot               # plain restart
```

### Files on the FRAM (`/fram`)

The 8 KB FRAM holds a small flat filesystem, mounted at `/fram`. Files
work exactly like devices: `cat`, `>`, `<`, pipes, `tee`. picoos never
had storage that survives a power cycle; this is it.

```
mkfs /fram                       format, once (like format c:)
echo hello > /fram/a.txt         create / replace
date >> /fram/log                append: incremental logfiles
cat /fram/log | wc -l
ls -l /fram
rm /fram/a.txt
df
```

- **Flat names, 1–11 characters**, no directories, no spaces or `/`.
- **At most 32 files, each at most 1 KB** (a simple, safe append path that
  rewrites the whole file; pipes themselves carry 4 KB).
  7104 bytes of space, in 64-byte blocks: a 3-byte file uses 64.
  (7360 until 2026-11-15, when the last 256 bytes became the
  [system area](#hour-counter-devhours) for the hour counters.)
- **Never formatted automatically.** A blank FRAM at boot just says
  `fram: no filesystem, run: mkfs /fram`, so a glitch can't wipe files.
- **Scripts are files.** Typed with `cat > /fram/<name>`;
  `run`/`loop <name>` take `/fram/<name>` first, else a built-in one.
- **`/fram/boot` replaces the built-in boot script** when it exists, so
  what the board does at power-up is a file you edit, not firmware.
  `rm /fram/boot` brings the built-in one back.

### Settings that survive a power cycle

A setting is a device value saved in a file: one value per file, plain
text, as the device speaks it. `remember <device> <file>`, run as a
background job, writes the device's value to the file whenever they
differ: every change is saved, whatever made it (a button through
`toggle` or `adjust`, an `echo ... >`, a script). At power-up, the boot
script restores the file into the device.

Two lines per setting in `/fram/boot`, **in this order**:

```
cat /fram/lamp > /dev/lamp           # restore what was saved
remember /dev/lamp /fram/lamp &      # from now on, keep saving changes
```

Restore **first**: started the other way round, `remember` would save the
device's power-up default over the stored value before it's restored.
The very first time, when `/fram/lamp` doesn't exist yet, the restore
line just prints `cat: error` and `remember` creates the file. (Why so
generic: plan2040 has no separate error output, no stderr. A message
from `cat` itself would go where its output goes, here through
`> /dev/lamp` into the device, so errors are reported by the shell as
`<program>: error` on the terminal instead.)

A whole boot script that remembers the lamp across power cycles:

```
cat > /fram/boot
> date
> cat /fram/lamp > /dev/lamp
> remember /dev/lamp /fram/lamp &
> toggle /dev/buttons/lamp /dev/lamp &
> .
```

Press LAMP, switch the board off and on: the lamp comes back as it was.
On babywarmer the same two lines will keep the setpoint, manual power,
PID tunings, auto/manual mode, ...:

```
cat /fram/setpoint > /dev/setpoint
remember /dev/setpoint /fram/setpoint &
```

- **Run once by hand** it saves (`saved /fram/lamp`) or says `unchanged`,
  and stops.
- **Stateless:** it compares with the file, not with a value it keeps, so
  any number of `remember` jobs never get in each other's way.
- **No wear, power-cut safe:** FRAM doesn't wear, so saving on every change
  is fine, and the filesystem's write order means a power cut leaves the
  old or the new value, never half of one.
- **Settings are files, so `mkfs /fram` erases them**: a factory reset.
  Unlike the hour counters, on purpose: settings may be reset,
  operating hours may not.
- **Not for devices whose read changes them.** `/dev/buttons` and
  `/dev/buttons/<name>` are read-and-clear: `remember` would read them
  every 150 ms and swallow the button presses. Values that change all the
  time (`/dev/rtc`, `/dev/uptime`) work, but rewrite the file every
  second: pointless.
- Each `remember` takes one of the 20 job slots and reads a few bytes from
  the FRAM every 150 ms.

### cron (`/fram/crontab`)

Run commands at set times, driven by the RTC. `/fram/crontab` holds one
job per line: the classic five time fields, then a command, a pipeline
as typed at the prompt. Type it with `cat > /fram/crontab` (and add a
line later with `cat >> /fram/crontab`):

```
cat > /fram/crontab
> # min   hour  day  month  weekday   command
> *       *     *    *      *         echo tick > /dev/cons
> */2     *     *    *      *         echo on > /dev/led
> 1-59/2  *     *    *      *         echo off > /dev/led
> 0       8     *    *      1-5       date >> /fram/log
> 0       22    *    *      *         echo off > /dev/lamp
> .
```

Every minute a `tick` on the terminal, the LED on in even minutes and
off in odd ones, a log line at 08:00 on weekdays, the lamp off at 22:00.

- **Fields:** minute 0–59, hour 0–23, day 1–31, month 1–12, weekday 0–7
  (0 and 7 are both Sunday). Each is `*`, a number, a range `1-5`, a step
  (`*/15`, `1-59/2`, `5/20` = 5, 25, 45), or a comma list (`0,30`).
- **Day and weekday:** if both are given (not `*`), a line runs when
  *either* matches, as in Unix cron: `0 12 15 * 3` runs at noon on the
  15th *and* on every Wednesday.
- **Output is discarded unless redirected**, as in Unix cron. To see
  something on the terminal, send it to `/dev/cons`.
- **A line ending in `&` starts a background job**: e.g.
  `0 8 * * * cat /dev/cputemp | hyst 30 2 > /dev/lamp &` starts a
  thermostat at 08:00. Give such lines a *fixed* time: on `* * * * *` it
  would start another job every minute, until all 20 slots are full.
- **Pipelines only, no builtins** (`sleep`, `run`, `loop`, `watch`, ...):
  a timer shouldn't take over the shell. Such a line is refused with a
  message when it comes up.
- **Edits work from the next minute**: the crontab is read again every
  minute, no restart. Bad lines are reported once
  (`cron: /fram/crontab line 3: bad, ignored`), when the crontab is first
  read or changed, and skipped.
- **`echo off > /dev/cron` stops it**, `echo on > /dev/cron` starts it
  again; `cat /dev/cron` tells which. On at power-up.
- **Only on a trusted clock:** nothing runs while `/dev/rtc/vl` is `on`
  (the RTC lost its time), or if the RTC can't be read.
- **On the minute, not caught up:** a job runs at the start of its
  minute. The minute in which the board starts is skipped, and minutes
  the clock jumps over (setting the time, a power cut) aren't run later.
- How it works: a kernel task (no job slot), once a second reads the RTC;
  on each new minute it reads `/fram/crontab` and runs the matching lines
  (`dev/cron.cpp`). Not a background job on purpose: a job is a program
  inside the pipeline machinery, and pipelines can't start pipelines.

### Writing a file: `cat >`

The oldest Unix way to write a small file without an editor: `cat` with
nothing to read, redirected into the file. Type the lines; a line with
just `.` (or Ctrl-D) ends it.

```
% cat > /fram/hello            # replace (or create) the file
typing into /fram/hello: end with '.' or Ctrl-D
> date
> sleep 1000
> echo one second later
> .
% cat >> /fram/hello           # add lines to the end
typing onto the end of /fram/hello: end with '.' or Ctrl-D
> echo and one more line
> .
```

- **Lines are kept exactly as typed**, `|`, `>`, `>>`, `&` and `#`
  included, without having to quote anything. (With quotes, `echo` can
  write such lines too: `echo "line with > in it" >> file`.)
- **To change or delete a line**, write the file again with `cat >`.
  (A real line editor, `ed`, is in `TODO.md`.)
- **Ends silently** when the file was written, as Unix `cat` does; if it
  couldn't be written (no filesystem, over 1 KB, a name over 11
  characters) it says so.
- **Only at the prompt.** A `cat > file` line inside a script or a
  crontab doesn't wait for typing (no one is there): it writes an empty
  file, as on Unix. And `cat x > y`, `cat < x > y`, `echo x | cat > y`
  are ordinary copies.
- Until 2026-11-16 this was the `script <name>` builtin (from picoos). It
  could only replace, never add, and most files written this way aren't
  scripts; `cat >` and `cat >>` replaced it.

### Scripts

A script is a file of shell lines, run one line per shell tick. `sleep`
inside a script really waits; `&` inside a script starts a background
job, exactly as if typed.

```
cat > /fram/hello
> date
> sleep 1000
> echo one second later
> .
run hello               # once (looks for /fram/hello)
loop hello              # forever, Ctrl-C to stop
cat /fram/hello         # it's just a file
rm /fram/hello
```

**Your own boot script.** At power-up plan2040 runs `/fram/boot` if it
exists, otherwise the built-in one (`date` + the LAMP toggle). To make
your own, type it with `cat >`:

```
cat > /fram/boot
> date                                    # say hello
> toggle /dev/buttons/lamp /dev/lamp &
> toggle /dev/buttons/mute /dev/led &
> cat /dev/cputemp | hyst 30 2 > /dev/lamp &
> .
```

and add to it later with `cat >> /fram/boot`. `rm /fram/boot` brings the
built-in boot script back.

**Built-in scripts are files too**, in the source tree: `scripts/boot`
(the generic one above), and an app's own in `apps/<app>/scripts/`
(babywarmer's boot wires the whole heater controller). The config
names which ones go in (`PLAN2040_SCRIPTS boot`; an app's file of the
same name wins), and CMake builds them into the firmware as read-only
text: comment lines and empty lines left out, at most 1023 bytes each,
and `boot` must be there. `scripts` lists them. (Until 2026-11-21 they
were C strings in `shell.cpp`, copied into a 4 KB RAM table at start-up;
now they stay in flash: 4 KB more free RAM.)

**`cat >>` or `echo "..." >>`.** One line with `|`, `<`, `>`, `&` or `#`
in it can be added with `echo`, **if it's quoted**:

```
echo "toggle /dev/buttons/lamp /dev/lamp &" >> /fram/boot
echo '*/2 * * * *  echo on > /dev/led' >> /fram/crontab
```

**Without quotes** it goes wrong: `echo toggle /dev/buttons/lamp
/dev/lamp & >> /fram/boot` doesn't write that line. The shell takes `&`
and `>>` for itself and starts a background job that appends
`toggle /dev/buttons/lamp /dev/lamp` to `/fram/boot` every 150 ms (until
`kill`, or until the file is full at 1 KB). For several lines, or when
in doubt, `cat >>` stores what you type, untouched.

**A logger.** `loop` keeps the shell busy until Ctrl-C (it's a
foreground command), but everything else, background jobs included,
keeps running:

```
cat > /fram/logger
> date >> /fram/log
> label temp /dev/cputemp >> /fram/log
> sleep 60000
> .
loop logger             # one entry a minute; Ctrl-C to stop
```

The log stops growing at 1 KB: from then on every pass prints
`/fram/log: write failed`. `rm /fram/log` starts a new one.

### How the filesystem works

The FRAM layout (decided 2026-11-15, meant to stay):

```
0x0000  block 0         superblock: "pfs1", version 2, block size, 124 blocks, 32 entries
0x0040  blocks 1-8      directory: 32 entries x 16 bytes (name, size, first block)
0x0240  blocks 9-12     chain table: 2 bytes per block
0x0340  blocks 13-123   file data: 111 blocks = 7104 bytes
0x1f00  blocks 124-127  system area: hour counters (fs/hours.h), NEVER touched by mkfs
0x2000  end
```

`fs/framfs.h` owns blocks 0–123: a superblock (`pfs1`), a 32-entry
directory (name, size, first block), a chain table saying for every
block which block comes next in its file, then 111 data blocks. A file is a chain of
blocks, not one contiguous run, so a file can grow without moving any
other file (the FAT idea). Writes go in an order that survives a power
cut: new data blocks, then their chain, then the directory entry, then
the old blocks are freed. A cut can leak a few blocks (`mkfs` reclaims
them) but never leaves a file pointing at garbage: tested on the PC by
cutting power at every single write of an overwrite. Chain entries are
16-bit, so a bigger FRAM later only needs `FRAM_SIZE` changed in
`board.h` and a `mkfs`: the filesystem grows, and the system area stays
the last 256 bytes of the chip.

The kernel side is a **mount** (`kernel/fs.h`): `fs_open("/fram/a.txt")`
finds the mount for `/fram` and hands `a.txt` to the filesystem. Programs
can't tell files from devices. Plan 9's idea of mounting a file server
into the namespace, still in one global namespace.

### hexdump

`hexdump [-s offset] [-n length] [path]` shows bytes in the classic
`hexdump -C` layout: offset, 16 bytes in hex, the same 16 as ASCII
(`.` for anything not printable). `-s` and `-n` take decimal or `0x` hex.

```
% hexdump /fram/a.txt
00000000  68 65 6c 6c 6f 20 77 6f  72 6c 64 0a              |hello world.|
0000000c
```

- **The last line is the offset where the dump ended**, as in Unix: here
  `0xc`, 12 bytes.
- **Identical lines in a row show as one `*`.** The empty parts of the
  FRAM collapse that way, so `hexdump /dev/fram` shows the whole chip.
- **A path or a pipe.** `hexdump /fram/a.txt` and `cat /fram/a.txt |
  hexdump` print the same. With a path, hexdump reads at offsets itself
  (`fs_pread()`, 256 bytes at a time), which is what makes the raw
  devices work: `/dev/fram` (the chip) and `/dev/mem` (memory by
  address, where the offset *is* the address).
- **One screen at most.** The output has to fit one pipe buffer (4 KB, ~50
  lines, ~800 bytes without `*` lines). When it doesn't, the dump stops
  at a line and the last line tells you how to go on:

  ```
  -- more: hexdump -s 0x1330 -n 1232 /dev/fram
  ```

  Copy, paste, next screen.

**/dev/mem: only safe regions.** Reading an unmapped address makes the
RP2040 crash (bus fault), and reading some peripheral registers changes
them (a FIFO loses its data). So `/dev/mem` only reads the boot ROM
(`0x00000000`, 16 KB), the flash (`0x10000000`, 2 MB) and RAM
(`0x20000000`, 264 KB); anything else is a `read error`, never a crash.
Both raw devices are read-only.

### Looking inside the filesystem with hexdump

A real dump from the board (2026-11-13), holding two files: `log`
(35 bytes) and `boot` (89 bytes). Earlier, `a.txt` and `t` had been
created and removed.

```
% hexdump /dev/fram
00000000  70 66 73 31 01 00 40 00  80 00 20 00 00 00 00 00  |pfs1..@... .....|
00000010  00 00 00 00 00 00 00 00  bf ff f7 ff bf ff f7 ff  |................|
*
00000040  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000050  6c 6f 67 00 00 00 00 00  00 00 00 00 23 00 0f 00  |log.........#...|
00000060  62 6f 6f 74 00 00 00 00  00 00 00 00 59 00 0d 00  |boot........Y...|
00000070  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000250  00 00 00 00 00 00 00 00  00 00 10 00 00 00 ff ff  |................|
00000260  ff ff 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000270  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000340  64 61 74 65 20 20 20 20  20 20 20 23 20 6d 79 20  |date       # my |
00000350  6f 77 6e 20 62 6f 6f 74  0a 74 6f 67 67 6c 65 20  |own boot.toggle |
00000360  2f 64 65 76 2f 62 75 74  74 6f 6e 73 2f 6c 61 6d  |/dev/buttons/lam|
00000370  70 20 2f 64 65 76 2f 6c  61 6d 70 20 26 0a 65 63  |p /dev/lamp &.ec|
00000380  68 69 0a 6c 6f 0a 61 67  61 69 6e 0a bf ff f7 ff  |hi.lo.again.....|
00000390  00 00 00 00 00 00 00 00  bf ff f7 ff bf ff f7 ff  |................|
*
000003c0  46 72 69 20 4e 6f 76 20  31 33 20 32 31 3a 35 37  |Fri Nov 13 21:57|
000003d0  3a 33 36 20 32 30 32 36  0a 74 65 6d 70 20 32 30  |:36 2026.temp 20|
000003e0  2e 32 0a 00 00 00 00 00  bf ff f7 ff bf ff f7 ff  |.2..............|
000003f0  00 00 00 00 00 00 00 00  bf ff f7 ff bf ff f7 ff  |................|
00000400  68 6f 20 68 65 6c 6c 6f  20 66 72 6f 6d 20 2f 66  |ho hello from /f|
00000410  72 61 6d 2f 62 6f 6f 74  0a ff f7 ff bf ff f7 ff  |ram/boot........|
00000420  00 00 00 00 00 00 00 00  bf ff f7 ff bf ff f7 ff  |................|
*
00002000
```

(Made 2026-11-13, with layout version 1. Since 2026-11-15 the superblock
reads `pfs1 02 00 40 00 7c 00 20`: version 2, 124 blocks, the last 4
being the system area. Directory, chain table and data work the same.)

Reading it (numbers are little-endian: `23 00` is 0x23 = 35):

- **`0x000` superblock (block 0):** `pfs1`, version `01`, block size
  `40 00` (64), `80 00` (128) blocks, `20` (32) directory entries.
- **`0x040` directory (blocks 1–8), 16 bytes per entry:** name, size,
  first block.
  - Entry 0 is all zeros: free. `a.txt`, and later `t`, lived there.
  - `log`: size `23 00` = 35 bytes, first block `0f 00` = 15.
  - `boot`: size `59 00` = 89 bytes, first block `0d 00` = 13.
- **`0x240` chain table (blocks 9–12):** 2 bytes per block; block *n*'s
  entry is at `0x240 + 2n`.
  - Block 13 (`0x25a`): `10 00` = next is block 16. `boot` is 89 bytes,
    more than one 64-byte block, so it's a chain of two blocks, and
    they aren't even neighbours: the FAT idea at work.
  - Block 14 (`0x25c`): `00 00` = free.
  - Blocks 15 and 16 (`0x25e`, `0x260`): `ff ff` = END.
- **Data, block *n* at *n* × 64:**
  - Block 13 (`0x340`): the first 64 bytes of `boot`.
  - Block 16 (`0x400`): its last 25 bytes, `ho hello from /fram/boot`.
  - Block 15 (`0x3c0`): `log`.
- **Deleted data stays until it's overwritten.** Free block 14 (`0x380`)
  reads `hi\nlo\nagain\n`: it held `a.txt` (`hello\nagain\n`), was
  reused for `t` (`hi\n`, overwriting only the first three bytes), and
  both were removed. `rm` frees blocks, it doesn't wipe them, as on a
  real disk. Worth knowing before storing anything sensitive.
- **`bf ff f7 ff` and runs of `00`:** whatever the chip held before. `mkfs`
  writes only the superblock, directory and chain table, never the data
  area, and a file only overwrites the bytes it needs.

### fsck: checking the filesystem

A power cut in the middle of a write can't damage a file (old or new,
never half), but it can leave **lost blocks**: blocks marked "in use"
that no file points to (the new copy was half written, or `rm` was cut
short). They don't hurt anything, they just take space. On a board that
gets unplugged at random moments while `remember` and cron write to the
FRAM, they'd slowly add up. `fsck` finds and frees them.

```
% fsck /fram
/fram: 4 files, 5 blocks in use, 106 free, no lost blocks
ok
```

- **At every boot, automatically.** Silent when all is well; it says
  `fram: freed 3 lost block(s), left by a power cut during a write`, or
  `fram: 1 damaged file(s), run: fsck /fram`. It checks the directory and
  chain table already cached in RAM, so it costs no FRAM reads.
- **What it checks:** every file has a valid name, a size of at most 1 KB,
  and a chain of exactly the right number of blocks, ending in END, with
  no block out of range, marked free, or used twice. No name appears
  twice. Every block in use belongs to a file.
- **What it fixes: only what's safe.** Lost blocks are freed, but only
  when every file checked out fine. A damaged file is reported, never
  "repaired": you decide (look with `cat`, then `rm`, then `fsck` again).
  With a damaged file around, nothing is freed at all: a "lost" block
  might be the rest of its chain.
- **Two files sharing a block** can't tell which one is wrong, so both are
  named: `damaged: b and c share block 14 (check both with cat, rm the
  wrong one)`. And `rm` of one of them never frees a block the other one
  still uses (2026-11-16: `rm` and overwrites skip any block another file
  reaches), so removing the wrong one doesn't take the good one with it.

### Hour counter (`/dev/hours`)

An operating-hours counter like a car's odometer: it only goes up, and
it survives power cuts at any moment, firmware updates, and `mkfs /fram`.

```
% cat /dev/hours
1234:30
% cat /dev/hours/status
ok
% cat /dev/hours | thresh 500 > /dev/led &     # LED on after 500 hours: a service interval
% echo set 0:00 > /dev/hours/ctl               # factory reset, deliberate
% echo set 1234:30 > /dev/hours/ctl            # a replacement board takes over the old count
```

- **Where it lives.** In the **system area**, the last 256 bytes of the
  FRAM (`0x1f00`–`0x1fff`), outside the filesystem. `mkfs` never touches
  it (decided 2026-11-15: formatting the storage must not reset the
  operating hours). Room for 7 counters; counter 0 is power-on hours,
  the others are reserved (e.g. heater hours on babywarmer).
- **How it counts.** With the RP2040's own timer, never the RTC: setting
  the clock or a flat RTC backup can't change it. A kernel task (not a
  job: `kill` can't stop it) saves once a minute. A power cut loses at
  most the minute in progress.
- **Why it can't go corrupt.**
  - Every counter is stored **twice** (copies A and B), each with a
    sequence number and a CRC32. Saves alternate A, B, A, ... At power-up
    the valid copy with the highest sequence number wins. A power cut
    can only damage the copy being written; the other one is intact and
    at most one minute older. Tested on the PC with *torn* writes: only
    1, 2, ... 15 of a record's 16 bytes reaching the chip. Every case
    came back as the old or the new value.
  - **Never backwards.** A save lower than the current value is refused.
    Only `/dev/hours/ctl` can lower it, on purpose.
  - **No wear.** FRAM takes ~10^14 writes; EEPROM or flash counters (often
    rated 100k–1M writes) wear out when saved every minute. That's the
    usual reason hour counters die.
  - **Both copies bad can't come from one power cut.** If it ever
    happens (a chip fault, a bug), the counter reads `corrupt`, stops
    counting, and **stays** corrupt across reboots until set by hand.
    It's never silently restarted at 0: that would hide what happened.
  - **A lost header doesn't reset anything.** The area only counts as new
    (all counters at 0) if neither the header nor any counter record is
    valid. A damaged header alone is rewritten; the counters are kept.
  - **An unreadable FRAM at boot is `error`, not "new".** Nothing is
    written that boot, so a bus glitch can never reset the counters.
  - **Even `bringup` can't take both copies.** Its FRAM test rewrites the
    chip in 32-byte chunks and restores each; a counter's copy A and B
    always fall in different chunks, so a power cut in the middle of the
    test damages at most one of them.
- **First boot with the counter**: `hours: new counter, starting at 0:00`.

Layout of the system area (`fs/hours.h`):

```
+0x00  header: "sys1", version 1, 7 counters, ..., crc32      16 bytes
+0x10  counter 0, copy A: seq, minutes, reserved, crc32         16 bytes
+0x20  counter 0, copy B                                        16 bytes
+0x30  counter 1, copy A ... up to counter 6, copy B at +0xe0
```

### What `rm` does, step by step

A PC simulation of the FRAM (same code as on the board), starting from a
fresh `mkfs` on an all-zero chip. (Made 2026-11-13, with the first
layout: `df` said 7360 total then; it's 7104 now. Everything else below
is unchanged.) A short file `a` and a two-block file
`fox`, built with four `>>` appends:

```
% echo hi > /fram/a
% echo the quick brown fox > /fram/fox
% echo jumps over the lazy >> /fram/fox
% echo dog, and back again, >> /fram/fox
% echo all day long. >> /fram/fox
% ls -l /fram
/fram/a                  3
/fram/fox                75
% df
/fram     7360 total   192 used  7168 free
```

**1. Before `rm`.** Directory, chain table, data:

```
% hexdump -s 0x40 -n 32 /dev/fram
00000040  61 00 00 00 00 00 00 00  00 00 00 00 03 00 0d 00  |a...............|
00000050  66 6f 78 00 00 00 00 00  00 00 00 00 4b 00 0f 00  |fox.........K...|
00000060
% hexdump -s 0x250 -n 16 /dev/fram
00000250  00 00 00 00 00 00 00 00  00 00 ff ff 00 00 10 00  |................|
00000260
% hexdump -s 0x340 -n 256 /dev/fram
00000340  68 69 0a 00 00 00 00 00  00 00 00 00 00 00 00 00  |hi..............|
00000350  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000380  74 68 65 20 71 75 69 63  6b 20 62 72 6f 77 6e 20  |the quick brown |
00000390  66 6f 78 0a 6a 75 6d 70  73 20 6f 76 65 72 20 74  |fox.jumps over t|
000003a0  68 65 20 6c 61 7a 79 0a  64 6f 67 2c 20 61 6e 64  |he lazy.dog, and|
000003b0  20 62 61 63 6b 20 61 67  61 69 6e 2c 0a 00 00 00  | back again,....|
000003c0  74 68 65 20 71 75 69 63  6b 20 62 72 6f 77 6e 20  |the quick brown |
000003d0  66 6f 78 0a 6a 75 6d 70  73 20 6f 76 65 72 20 74  |fox.jumps over t|
000003e0  68 65 20 6c 61 7a 79 0a  64 6f 67 2c 20 61 6e 64  |he lazy.dog, and|
000003f0  20 62 61 63 6b 20 61 67  61 69 6e 2c 0a 61 6c 6c  | back again,.all|
00000400  20 64 61 79 20 6c 6f 6e  67 2e 0a 00 00 00 00 00  | day long.......|
00000410  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000440
```

- `a`: 3 bytes, block 13 (`0x340`), chain `ff ff` = END.
- `fox`: `4b 00` = 75 bytes, first block `0f 00` = **15**; chain:
  block 15 (`0x25e`) → `10 00` = block 16; block 16 → END (at `0x260`,
  just past this window).
- Data: block 15 (`0x3c0`) and block 16 (`0x400`) hold all 75 bytes.
- **Block 14 (`0x380`) is free but not empty**: it holds `fox` *as it
  was before the last `>>`*. An append writes the whole file again into
  free blocks, and only then frees the old copy: that's what makes it
  power-cut safe (old or new, never half), and it leaves old versions
  behind in free blocks. Here the third version ended up in block 14,
  the fourth (current) in blocks 15–16.

**2. After `rm /fram/fox`.**

```
% rm /fram/fox
% ls -l /fram
/fram/a                  3
% df
/fram     7360 total    64 used  7296 free
% hexdump -s 0x40 -n 32 /dev/fram
00000040  61 00 00 00 00 00 00 00  00 00 00 00 03 00 0d 00  |a...............|
00000050  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000060
% hexdump -s 0x250 -n 16 /dev/fram
00000250  00 00 00 00 00 00 00 00  00 00 ff ff 00 00 00 00  |................|
00000260
```

`rm` changed exactly two things:

- **The directory entry** at `0x50` is all zeros: free.
- **The chain entries** of blocks 15 and 16 are `00 00`: free. `df`
  agrees: 192 → 64 bytes used.

And one thing it did **not** change: **the data**. A hexdump of
`0x340`–`0x440` after `rm` is byte for byte the same as before: the
whole text is still in blocks 14, 15 and 16. Nothing points at it any
more, so `ls`, `cat` and `df` can't see it, but the bytes are there
until a new file needs those blocks.

**3. A new file reuses the space.**

```
% echo new > /fram/b
% hexdump -s 0x40 -n 32 /dev/fram
00000040  61 00 00 00 00 00 00 00  00 00 00 00 03 00 0d 00  |a...............|
00000050  62 00 00 00 00 00 00 00  00 00 00 00 04 00 0e 00  |b...............|
00000060
% hexdump -s 0x250 -n 16 /dev/fram
00000250  00 00 00 00 00 00 00 00  00 00 ff ff ff ff 00 00  |................|
00000260
% hexdump -s 0x380 -n 16 /dev/fram
00000380  6e 65 77 0a 71 75 69 63  6b 20 62 72 6f 77 6e 20  |new.quick brown |
00000390
```

- `b` takes the first free directory entry (the one `fox` had) and the
  first free block, 14.
- Only its 4 bytes are written: `new\n` overwrites `the ` and the rest
  of the old text, `quick brown fox ...`, stays in the block behind
  `b`'s end. `cat /fram/b` shows `new`, because the size says 4.

That's the same thing seen in the real dump above, where block 14 read
`hi\nlo\nagain\n`. Like a real disk: deleting frees space, it doesn't
erase. **Overwriting doesn't wipe either**: `echo x > /fram/secret`
writes the new contents into *free* blocks first and only then frees
the old ones (that's the power-cut safety), so the old bytes stay where
they were. Really wiping a file needs a dedicated tool (an idea for
later, see `TODO.md`: `shred`, or an `rm` that zeroes blocks).

### Extending plan2040

The rule from picoos holds: **a device owns hardware, a program only
ever touches paths** (`fs_open`/`fs_read`/`fs_write`/`fs_close`). No
`ioctl()`: a device that needs another behavior gets another file, as in
Plan 9 (e.g. `/dev/rtc` and `/dev/rtc/vl`, not one file with modes).

**A device** is a `device_t` in `dev/<name>.cpp`: a path, optional
open/close, and `read`/`write` that speak plain text. Template, from
`dev/led.cpp`:

```c++
static int led_read(char *buf, int len)
{
    return snprintf(buf, len, gpio_get(PIN_ALARM_LED) ? "off\n" : "on\n");
}

static int led_write(const char *buf, int len)   // buf is NOT nul-terminated
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { gpio_put(PIN_ALARM_LED, 0); return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { gpio_put(PIN_ALARM_LED, 1); return len; }
    return -1;                                    // rejected
}

static const device_t dev_led = {"/dev/led", 0, 0, led_read, led_write};

void dev_led_register(void) { fs_register(&dev_led); }
```

**A program** is a `program_t` in `prog/<name>.cpp`: piped input + argv
in, text out. Template, from `prog/uptime.cpp`:

```c++
static int uptime_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    int fd = fs_open("/dev/uptime");
    if (fd < 0) return -1;
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);                                 // always close: files use open-file slots
    if (n < 0) return -1;
    buf[n] = '\0';
    unsigned long s = strtoul(buf, 0, 10);
    return snprintf(out, outlen, "up %lud %02lu:%02lu:%02lu\n", s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
}

static const program_t prog_uptime = {"uptime", uptime_run};

void prog_uptime_register(void) { prog_register(&prog_uptime); }
```

Then: add its name to the config (`PLAN2040_DEVS` or `PLAN2040_PROGS`
in `configs/<name>.cmake`). The register function must be called
`dev_<name>_register()` / `prog_<name>_register()`, after the file's name;
the build generates the call. `ls` shows it right away.

**A mount** is a `mount_t` (`kernel/fs.h`): a prefix and the functions
of a filesystem. `dev/fram.cpp` is the example: twelve lines that put
`fs/framfs.h` at `/fram`.

Capacities (2026-11-15): 64 devices (`FS_MAX_DEVICES`, 21 used), 48
programs (`PROG_MAX`, 25 used), 4 mounts (1 used), 4 files open at once,
20 background jobs, 32 tasks (23 used: the shell, the hour counter,
cron, the 20 job slots).
Every table says so loudly on the console when it's full, rather than
dropping an entry silently (a lesson picoos learned three times).

### Known limits

- **Pipes pass whole buffers, 4 KB each** (`STAGE_BUF`, was 1 KB until
  hexdump), not streams. Long output (a big `hexdump`) has to be paged
  with `-s`. Files are at most 1 KB, separately (`fs/framfs.h`).
- **At most 15 arguments per program** (`JOB_MAX_ARGS 16`, the program
  name included; was 5 until 2026-11-16), and a line is at most 95
  characters. More words fail with `too many args in one stage (max 15)`.
- **Quotes, but no variables, no `$(...)`, no backslash escapes** in the
  shell. `label` covers the most common need ("some text: a device's
  value").
- **One shared poll rate** for all background jobs (150 ms).
- **`hyst` keeps one state for everyone**: two `hyst` jobs at once would
  share it (inherited from picoos, documented in `prog/hyst.cpp`).
- **One core, cooperative**: a slow device read (I2C) holds everything up
  for its duration. Core 1 is still free.

### Test log

**2026-11-12, first picoos build on the board** (`picoos.uf2`):

| Test | Result |
|---|---|
| Boot | ok: clean boot, banner, `boot` script ran automatically (`date`, then `toggle ... &` started as job `[1]`) |
| `help`, `ls` | ok: 13 devices, 17 programs listed |
| `cat /dev/rtc`, `date`, `cat /dev/rtc/vl` | ok: agree with each other, VL `off`; the RTC kept time across the power-off (supercap) |
| `cat /dev/uptime`, `uptime` | ok: 70 s, then `up 0d 00:01:25` |
| `cat /dev/cputemp` | ok: 19.2 °C (cool room) |
| `ls \| grep cpu`, `ls \| grep dev` | ok: `/dev/cputemp`; all 13 `/dev` entries |
| `ls \| grep dev \| wc -l` | ok: `13` (three stages) |
| `ls\|grep dev` (no spaces) | **bug**: `ls\|grep: not found`. The shell, inherited from picoos, only split words on spaces. Fixed: `\|`, `<`, `>`, `&` are now words of their own (`tokenize()` in `kernel/shell.cpp`) |
| `ls\|grep dev`, `ls\|grep dev\|wc -l` retest | ok: same 14 lines as with spaces (now including `/dev/lamp`); `14` |
| LAMP button toggles `/dev/led` (GPIO1) | ok: GPIO1 toggles on each press (multimeter) |
| LED on GPIO6 | dimly lit, 1.5 V on the pin. Not the toggle racing: picoos didn't drive GPIO6 at all, so the pin floated on its internal pull-down (see Hardware notes). Added `/dev/lamp` on GPIO6 (as babywarmer), switched off at boot; `boot` now toggles `/dev/lamp` with the LAMP button |
| `/dev/lamp` retest | ok: LED off after boot, LAMP button toggles it, `cat /dev/lamp` matches what's lit (active-low, `PIN_LAMP_ON 0`, confirmed) |
| `watch 1000 cat /dev/cputemp`, Ctrl-C | ok: one reading per second (19.2–21.1 °C), Ctrl-C prints `(watch stopped)` and returns to the prompt. Readings jump in ~0.5 °C steps: that's one ADC count (3.3 V / 4096 / 1.721 mV/°C ≈ 0.47 °C), single samples, no averaging. Changed: `/dev/cputemp` now averages 16 samples per read, inside the driver (no knobs, no `ioctl()`) |
| `/dev/cputemp` retest (16-sample average) | ok: 20.4–20.6 °C over 14 s, moving by 0.1 at a time instead of jumping 0.5–1.4 °C |
| `echo bootsel > /dev/reboot` | ok: prints `rebooting into USB bootloader`, the USB drive appears, a new `.uf2` copied onto it boots. That boot reports `Rebooted by watchdog`: expected, the bootloader restarts the chip through the watchdog after a UF2 copy |
| `bringup.uf2` at 400 kHz, shared drivers | ok: scan finds `0x50` + `0x51`, full FRAM test passes in 1606 ms (was 6116 ms at 100 kHz), RTC ticks. `control_1`/`control_2` now read `0x00`: picoos's `rtc_init()` had set them, as intended (they powered up `0x08`/`0x80` on the new chip). So 4k7 pull-ups are fine at 400 kHz on this board |

**2026-11-12, FRAM filesystem** (`picoos.uf2`): tested on the PC so far
(filesystem against a simulated FRAM incl. power cuts; whole shell
session with `>>`, scripts and `/fram/boot`). On the board:

| Test | Result |
|---|---|
| Blank FRAM at boot: `fram: no filesystem` hint | ok (2026-11-13): `fram: no filesystem, run: mkfs /fram` before the banner; `ls` shows the mount point `/fram/` |
| `mkfs /fram` | ok: `/fram: formatted, 7360 bytes free` |
| Format survives power off/on | ok: after power-on no more `no filesystem` hint, so the superblock was found and `/fram` mounted |
| `df` | ok: `/fram     7360 total     0 used  7360 free` |
| `>`, `>>`, `cat`, `ls -l /fram`, `rm` | ok: `hello`/`again` in `a.txt` (12 bytes); `date` and `label temp` appended to `log` (35 bytes); `rm` removed `a.txt`. `rm` printed an empty line: the shell (from picoos) printed a newline even for no output. Fixed: no output, no line, as in Unix |
| `rm` retest | ok: `echo hi > /fram/t`, `ls -l`, `cat`, `rm /fram/t`: no empty line |
| Files survive a firmware update | ok: after `echo bootsel > /dev/reboot` and flashing a new `picoos.uf2`, `/fram/log` and `/fram/boot` were still there and `boot` ran. The FRAM is separate from the flash: new firmware never touches it |
| `script` → `/fram/<name>`, `run` | ok: `script boot` saved `/fram/boot` (89 bytes, comment included) |
| Power off/on: files and `/fram/boot` survive | ok: `running /fram/boot automatically...`, all three lines ran, the LAMP toggle job started; `log` and `boot` still listed |
| New: `#` comments (`date # hi` runs `date`) | ok: typed and inside `/fram/boot` |
| Stack fix (pipe buffers static): no visible change expected, everything above should behave the same | ok: nothing changed |
| Shell fix found on the PC: lines typed or pasted after `run`/`sleep`/`watch` ran immediately, mixed into the script (inherited from picoos). Now they wait until it's done. Paste e.g. `sleep 2000` + `echo after` in one go | ok: `after` came after the sleep |

**2026-11-24, what the nurse sees** (`build-babywarmer_v1/plan2040.uf2`,
after the hack): CHK and the TFT's SENSOR icon from `/dev/guard`, the big
display blinking (safe) or blank (none), the small display showing the
power in safe. On the PC: `bwsim` decodes both displays from the
74HC595 bits, every state as in the table above.

| Test | Result |
|---|---|
| Skin NTC off the baby, auto | pending: CHK on, SENSOR icon blinks, big display blinks the reading, small display shows the power (e.g. `39.0`), buzzer; MUTE: quiet for a minute |
| NTC back on | pending: 2 min later all back: steady skin temperature, setpoint on the small display, CHK off |
| Header SDA off | pending: big display blank, small display the manual %, MAN, CHK on |

**2026-11-24, the guard** (`build-babywarmer_v1/plan2040.uf2`, after
the hack): all heater safety in a kernel task, `/dev/guard`,
`/dev/heater/limit`, `/dev/alarm/sensor`, the bumpless handover. On the
PC (`bwsim`): the sensor out from the start → safe after 5 min; falls off
while warm → safe in 21 s, ok 2 min after it's back, the PID continuing
from 39 %; `kill` of the control job → safe in 11 s; the ADS1115 dead →
none, manual at 39 %, UP adjusts, back to auto by itself.

| Test | Result |
|---|---|
| `cat /dev/guard`, `cat /dev/heater/limit` in auto | pending: `ok`, `cap 100.0` |
| Skin NTC off the baby (in the air), auto | pending: within ~20 s `guard: safe: ...` once on the console, buzzer; `cat /dev/heater/limit` = `set <room table %>`; back on: `ok` 2 min later |
| `jobs`, then `kill` the `follow ... /dev/heater` job | pending: ~10 s later `guard: safe: control jobs not running ...`; start it again: `ok` |
| Header SDA off (ADS1115 gone) | pending: `guard: none: ...`, MAN, the manual % on the small display; SDA back: auto again |

**2026-11-23, the heater check against the SSR's real state**
(`build-babywarmer_v1/plan2040.uf2`, after the hack): `/dev/heater/ssr`,
a readable `/dev/relay`, `/dev/alarm/heater` with its reason,
`alarmcheck`'s 1 s current check. On the PC (`bwsim`): no false alarms
in boost any more; a broken element is reported within ~6 s at 39 %
power and clears a second after the repair; a stuck SSR within ~9 s,
relay held off until released.

| Test | Result |
|---|---|
| `cat /dev/heater/ssr` a few times at 25 % | pending: `on <ms>` ~3.8 s of every 15 s, `off <ms>` the rest |
| `cat /dev/relay` | pending: `on` while heating, `off` with the heater at 0 after boot |
| Boost (80 %) for a few minutes | pending: no heater alarm |
| Heater element disconnected, heating | pending: within seconds `cat /dev/alarm/heater` = `on: no current while switched on (element, fuse or SSR?)`, buzzer, FAIL; reconnected: `off` again |

**2026-11-23, our own ADS1115 driver, background sampling**
(`build-babywarmer_v1/bringup.uf2` and `plan2040.uf2`, after the hack):
`drivers/ads1115.cpp` from the datasheet, the "adc" task, the driver
contract (`drivers/README.md`). On the PC: builds without warnings;
`adctest` against a fake ADS1115 (numbers above). `bringup` got an `adc`
test.

| Test | Result |
|---|---|
| `bringup`: `adc` | pending: four single-ended inputs and the two NTC pairs, plausible volts; no "full scale" |
| `cat /dev/skintemp`, `cat /dev/ambient` | pending: the same values as before (same conversion) |
| `watch 100 cat /dev/skintemp`, a finger on the NTC | pending: follows within a fraction of a second |
| Header SDA off (optional) | pending: after ~1.5 s `[n] cat: /dev/skintemp: ADS1115 (0x48, header bus) not answering`, once; buttons and displays stay quick; SDA back: `[n] ok again` |

**2026-11-23, errors in words** (`plan2040.uf2` on cpu_rp2040 is
enough): `errstr` in the kernel, FRAM filesystem reasons, a failing job
reports once and "ok again". On the PC: both configs build without
warnings; host tests 18 ok (the changed messages checked line by line;
bwsim: 3 lines instead of 11,250 while the ADS1115 is dead, and "ok
again" when it's back). Cost: ~2.6 KB RAM (a reason per job slot), ~4 KB
flash.

| Test | Result |
|---|---|
| `cat /fram/nope` | pending: `cat: /fram/nope: no such file` |
| `echo x > /fram/waytoolongname` | pending: `/fram/waytoolongname: bad name (1-11 characters, no spaces or /)` |
| `echo abc > /dev/lamp` | pending: `/dev/lamp: value not accepted` |
| `cat /dev/nope &`, then `jobs` | pending: `[n] cat: /dev/nope: no such device or file` once; `jobs` shows `failing: ...` |

**2026-11-23, babywarmer TFT** (step 3f,
`build-babywarmer_v1/plan2040.uf2`, after the hack): the ST7735 screen
from the `st7735` repo, `/dev/tft/<name>`, `tftwire`, `tftmon`. On the
PC: builds without warnings; `bwsim` runs the display code too (fake
SPI: ~640 KB to the display in the first 2 minutes, then ~6 KB/s), and
`run tftmon` switches to text mode and back.

| Test | Result |
|---|---|
| Boot | pending: backlight on, the warmer screen: MODE icon manual, heater bar at 10 %, baby face ok |
| MANUAL | pending: MODE icon auto; heater bar follows `/dev/heater` (80 % in boost) |
| START | pending: the APGAR clock starts at 00:00, flashes at 1, 5 and 10 minutes |
| Warm the NTC above 40 °C | pending: hot face, ALARM + FAIL icons blink |
| `run tftmon`, then `echo graphical > /dev/tft/mode` | pending: ten `name = value` rows, then the warmer screen again |

**2026-11-22, babywarmer control** (step 3e,
`build-babywarmer_v1/plan2040.uf2`, after the hack): the control devices
and programs from picoos and the babywarmer boot script. On the PC:
builds without warnings; `bwsim` runs the whole controller for 110
simulated minutes (see the open points above). On the board, **the
safety relay engages as soon as the heater power is above 0**
(the guard, since 2026-11-24), and manual mode starts at 10 %: test with the heater
element safe to run, or disconnected.

| Test | Result |
|---|---|
| Boot | pending: the babywarmer boot runs, `jobs` shows 14 jobs (13 before the TFT); small display shows `10` (manual percent), big display the skin temperature |
| UP/DOWN in manual | pending: percent changes in steps of 0.5 (small display) |
| MANUAL | pending: auto on: small display shows the setpoint, `cat /dev/state` = `boost`, heater 80 %, AUT LED |
| `watch 1000 monitor` | pending: one line per second through boost, coast, pid |
| Warm the NTC by hand above 40 °C | pending: `/dev/alarm/temphigh` on, buzzer; MUTE silences it for a minute |
| Heater 50-99 % for a few minutes | pending: no `/dev/alarm/heater` (since 2026-11-23 the check judges only while the SSR is on; see the heater check rows above) |

**2026-11-21, babywarmer outputs** (step 3d,
`build-babywarmer_v1/plan2040.uf2`, after the hack): `/dev/heater` + the
`tpo` task, `/dev/relay`, `/dev/alarm`, `/dev/seg7big`/`small`, `/dev/leds`.
On the PC: `outtest` (SSR ticks per window, bargraph, relay and alarm
pins, the 56 bits per display write). On the board, **first with the
safety relay off** (`/dev/relay` stays off unless written), so the SSR
and the bargraph can be watched without heating:

| Test | Result |
|---|---|
| Boot | pending: 1 s lamp test (all digits, dots and LEDs), then blank; buzzer silent; heater off |
| `echo 36.5 > /dev/seg7big`, `echo 37 > /dev/seg7small` | pending: `36.5` and ` 37` |
| `echo on > /dev/leds/aut` ... each of the 7 | pending: the matching LED |
| `echo on > /dev/alarm`, `muted`, `off` | pending: buzzer + LED, LED only, neither |
| `echo 25 > /dev/heater` (relay off) | pending: SSR on ~3.8 s of every 15 s; bargraph at a quarter; `cat /dev/heater` = `25.0` |
| `echo on > /dev/relay`, then `off` | pending: the relay clicks |
| `ls` | pending: the generic devices + the 3c sensors + these, no `/dev/led` |

**2026-11-21, babywarmer sensors** (step 3c,
`build-babywarmer_v1/plan2040.uf2`, after the hack): `/dev/skintemp`,
`/dev/ambient`, `/dev/current` from picoos, on the ADS1115 driver with
timeouts. On the PC: builds without warnings; `ntctest` records the NTC
mapping.

| Test | Result |
|---|---|
| `cat /dev/skintemp`, `cat /dev/ambient` | pending: plausible °C, as picoos read them on this PCB |
| `watch 1000 cat /dev/skintemp` with a finger on the NTC | pending: rises |
| `cat /dev/current` | pending: a low count with the heater off (it stays off in 3c) |
| ADS1115 unplugged / header SDA pulled off (optional) | pending: `cat /dev/skintemp` says error, the shell keeps running, no watchdog reset |

**2026-11-21, built-in scripts from files, apps in CMake** (step 3b,
`plan2040.uf2` for cpu_rp2040): the built-in `boot` is `scripts/boot`
now, built into the firmware as read-only text (4160 bytes less RAM,
4 KB less flash). Behaviour should be the same as before. On the PC:
both configs build without warnings; host tests 15 ok, with the boot
script generated from the real `scripts/boot`.

| Test | Result |
|---|---|
| Boot without `/fram/boot` | pending: `running built-in 'boot' automatically...`, `date`, job 1 = the LAMP toggle |
| `scripts` | pending: `boot (built-in)`, `files: ls /fram` |
| `run boot` | pending: the same two lines again (a second LAMP toggle job) |

**2026-11-21, babywarmer_v1: the cpu_rp2040 board as the babywarmer
PCB's CPU** (`build-babywarmer_v1/bringup.uf2`, then `plan2040.uf2`):
step 3a, for after the hardware hack. Built on the PC, no hardware yet.
cpu_rp2040's firmwares are unchanged by it (same sizes, same bringup
output: the shared bringup prints the same as before there).

| Test | Result |
|---|---|
| `bringup.uf2`: banner and scan | pending: `babywarmer_v1 bring-up`; `local` bus `0x50 0x51`, `header` bus `0x48 0x52` |
| `bringup`: FRAM, RTC | pending: as on cpu_rp2040 (FRAM ~1.6 s at 400 kHz) |
| `bringup`: EEPROM | pending: `eeprom: ok`, no aliasing (it really is a 24C512) |
| `bringup`: buttons, alarm LED | pending: all six buttons reported; alarm LED blinks 1 Hz; buzzer, heater, relay, lamp stay off |
| `plan2040.uf2`: banner, `ls`, `date`, `df` | pending: `plan2040 on babywarmer_v1`; the same devices and programs as cpu_rp2040 |
| `echo on > /dev/lamp`, `echo on > /dev/led` | pending: the baby light and the alarm LED |

**2026-11-21, the restructure and the rename to plan2040**
(`plan2040.uf2`, `bringup.uf2`): one tree for all boards, config
`cpu_rp2040` (see "Source tree and configs"). Nothing should behave
differently except the banner. On the PC: both firmwares build without
warnings and come out exactly as big as before (`plan2040.elf` text
102988 / bss 27404 = old `picoos.elf`; `bringup.elf` 65076 / 3544 = old),
and the host tests give the same output as before the restructure
except the banner line (`make -C tests/host check`: 15 ok).

| Test | Result |
|---|---|
| Flash `plan2040.uf2` (`echo bootsel > /dev/reboot`): banner | ok: `Rebooted by watchdog` (expected after BOOTSEL), `plan2040 on cpu_rp2040`, then the built-in `boot` ran (`date`, `toggle /dev/buttons/lamp /dev/lamp &` as job 1): there's no `/fram/boot` on the board right now (`ls -l /fram`: only `crontab`, 325 bytes) |
| `ls` | ok: 21 `/dev` entries (`/dev/led` … `/dev/cons`), `/fram/`, 25 programs (`bin/cat` … `bin/ls`), in the config's order, the same as before: the generated `conf.cpp` works |
| `jobs` | ok: `[1] toggle /dev/buttons/lamp /dev/lamp &` (the built-in boot's one job) |
| `date`, `cat /dev/hours/status`, `df`, `fsck /fram` | `date`: ok, `Sat Nov 21 22:29:08 2026`. `fsck /fram`: ok, `1 files, 6 blocks in use, 105 free, no lost blocks`, so the FRAM contents survived. `cat /dev/hours/status`: ok. `df`: ok, `7104 total 384 used 6720 free`. Also `cat /fram/crontab` (intact), `cat /dev/cron` (`on`), `help` (unchanged) |
| LAMP button, cron LED on GPIO1 | pending: lamp toggles; LED follows `/fram/crontab` |
| `bringup.uf2` | ok: banner `cpu_rp2040 bring-up`, scan `0x50 0x51`, FRAM ok (1606 ms, as before), RTC `control_1=0x00 control_2=0x00`, ticking; `all` again the same. Button presses: not tried |

**2026-11-16, quotes, and the new pipeline storage** (`picoos.uf2`):
tested on the PC (`"..."` and `'...'` with spaces, `| < > & #` inside,
one kind inside the other, `a"b c"d`, `""`, a quoted `|` not being a
pipe, quoted paths, a quoted program name, a missing closing quote,
12 words OK and 16 refused, `jobs` showing quoted words quoted again;
and every earlier shell test again on the new parser: session, `cat >`,
`cp`/`mv`, cron minute by minute, `remember`, hexdump: all unchanged).
On the board:

| Test | Result |
|---|---|
| `echo "a > b \| c & d # e"`, `echo 'say "hi"'`, `echo "it's"` | ok: `a > b \| c & d # e`, `say "hi"`, `it's` |
| `echo "*/2 * * * *  echo on > /dev/led" >> /fram/crontab` (then check `cat /fram/crontab`, put the crontab back as it was) | ok: appended exactly; then the whole crontab rebuilt with `rm` and two quoted `echo ... >>` lines |
| `echo one two three four five six seven eight nine ten`; `echo "unterminated` → `missing closing "` | ok: all ten words; `missing closing "` |
| Everything else still as before: `boot` runs, `ls \| grep`, `cat >`, cron LED, `remember` | `boot`: ok, all four lines (`date`, the `> /dev/lamp` restore, both `&` jobs). `ls /fram \| grep cron`: ok. `remember`: ok, LAMP pressed → `/fram/lamp` followed (`off` → `on`), and after a power cycle the lamp came back `on`. Cron LED (GPIO1): ok, still alternating every minute from the crontab rebuilt with quoted `echo` lines |

**2026-11-16, `fsck`** (`picoos.uf2`): tested on the PC (lost blocks from a
power cut during an overwrite and during `rm`, freed and written back;
cross-linked files: both named, nothing freed, `rm` of one leaves the
other intact; a block marked free, a size over 1 KB, a block out of
range, an empty file with a block, a chain longer than its size, a name
twice, lost blocks together with a damaged file: nothing freed; the
boot message after a simulated power cut; the old filesystem and `mv`
power-cut tests still 20/20 and 16/16). On the board:

| Test | Result |
|---|---|
| `fsck /fram` on the board's filesystem: `ok`, no lost blocks | ok: `4 files, 6 blocks in use, 105 free, no lost blocks` (boot and crontab 2 blocks each, lamp and rtc 1 each), `ok`; the boot before it was silent |
| A real lost-block case (power cut while writing) | tried: `cat > /fram/junk`, four 96-character lines, power pulled right after `.`: the file was complete (384 bytes, 6 blocks), `fsck` ok, no lost blocks. A write takes milliseconds, too fast to cut by hand; the power-cut cases themselves are covered on the PC |
| `fsck`, `fsck /dev`: usage / refused | ok: `usage: fsck /fram`; `fsck: /dev: not a mount, or not formatted` |

**2026-11-16, `cp` and `mv`** (`picoos.uf2`): tested on the PC (`cp` file
→ file, device → file, to a mount point, onto itself refused, missing
source, too-long name; `mv` rename, replacing an existing file, refused
for devices / other mounts / same name / too-long name; a power cut at
every write of a rename, plain and replacing: 16/16 keep the source
data under exactly one name). On the board:

| Test | Result |
|---|---|
| `cp /fram/boot /fram/boot.old`, `ls -l /fram`, `cat /fram/boot.old` | ok: `boot.old` 101 bytes, same as `boot` |
| `cp /dev/rtc /fram`: a file `/fram/rtc` with the time | ok: `/fram/rtc`, 20 bytes, `2026-11-16 22:33:35` |
| `mv /fram/boot.old /fram/b2`, then `mv /fram/b2 /fram/boot` (replaces boot) | ok: renamed, then replaced `boot`; in `ls` the new `boot` sits where `b2` was (the old entry was removed, `b2`'s entry renamed, nothing copied), still 101 bytes |
| `cp /fram/boot /fram`, `mv /dev/rtc /fram/x`: refused with a message | ok: `cp: /fram/boot and /fram/boot are the same file`; `mv: /dev/rtc -> /fram/x: failed (...)`; nothing changed |
| `help`: "cp, mv, rm" under "often needed" | ok, also the "internal commands" heading and its "pipes ok" line |

**2026-11-16, `cat >` / `cat >>` replace `script`** (`picoos.uf2`): tested
on the PC (replace and append, lines with `>`, `&`, `#` kept, end with
`.` and with Ctrl-D incl. a half-typed last line, Ctrl-D on an empty
`cat >>` leaves the file alone, `cat < x > y` and `echo x | cat > y`
stay copies, `cat >` inside a running script doesn't wait for typing,
`script` gone, a too-long name reported). On the board:

| Test | Result |
|---|---|
| `cat > /fram/crontab` with the three example lines, `.`; `cat /fram/crontab` | ok (ended with Ctrl-D): the file replaced, `cat` shows the three lines; a cron `tick` arriving mid-typing only mixed the screen, the typed lines were intact |
| `cat >> /fram/crontab`, one more line, end with Ctrl-D; it's added at the end | ok: `bla bla bla` added as line 4 (a tick split the typed command on screen, `crtick`/`ontab`, the shell still had it whole) |
| `help` shows the "write a file" lines; `script` says `not found` | `help`: ok, the new layout as designed (the "write a file" lines, no `script`, `cat > /fram/crontab` under "often needed"). `script`: `scripts` lists only `boot (built-in)`; `script x` → `script: not found (try 'ls')` on the board too |

**2026-11-15, cron and `/dev/cons`** (`picoos.uf2`): tested on the PC
with a simulated clock (minute by minute: start-up minute skipped,
`*`, `*/2`, `1-59/2`, `1-5`, Sunday as 0 and 7, the day-or-weekday
rule, lists, `5/20` steps, `&` starting a job, a builtin refused, three
bad lines reported once, nothing on an untrusted clock / with
`/dev/cron` off / with an unreadable RTC). On the board:

| Test | Result |
|---|---|
| `echo hello > /dev/cons` | ok: `hello` |
| The example crontab: `tick` every minute, the LED on/off on even/odd minutes | `tick`: ok, every minute 00:02–00:06, from a crontab written with `script crontab` (191 bytes, comments included). A tick that arrives while the prompt is shown lands after the `%`, and the next typed line appears without a prompt (nothing lost, just confusing). LED (GPIO1, soldered 2026-11-16): ok, on for one minute, off for the next, repeating, from the `*/2` and `1-59/2` lines |
| `echo off > /dev/cron`: ticks stop; `on`: they come back | ok: off before 00:10:50, no ticks at 00:11 and 00:12 (not caught up later); on at ~00:12:10, next tick 00:13 |
| A bad line is reported once | ok: `61 * * * * echo x` → `cron: /fram/crontab line 1: bad, ignored` once, at the next minute (00:15), not again. Noted: `script crontab` replaces the whole file (as designed), so the tick lines were gone |
| Power off/on: cron runs again from the next minute, from the stored crontab | ok (with `echo now > /dev/reboot`): boot at 00:19:22, that minute skipped, first `tick` at 00:20 from `/fram/crontab`; `/dev/cron` `on` again |

**2026-11-15, `remember`** (`picoos.uf2`): tested on the PC (a simulated
lamp and button: first save creates the file, `unchanged` after that, a
button press through `toggle` and an `echo` both saved within a tick,
restore after a simulated reboot, bad arguments). On the board:

| Test | Result |
|---|---|
| `remember /dev/lamp /fram/lamp` by hand: `saved /fram/lamp`, again: `unchanged` | ok: with the job killed (`kill %1`), lamp switched on with LAMP: `saved /fram/lamp`, file `on`; run again: `unchanged` |
| The boot script from "Settings that survive a power cycle": press LAMP, power off/on, the lamp comes back as it was (both on and off) | **on**: ok, restored after two power cycles in a row. First boot without `/fram/lamp`: `cat: error` (harmless; the README said `not found`, corrected), then `remember` created it (`off`). **off**: ok, switched off with LAMP, power off/on: still off |
| `jobs` shows the `remember` job; `cat /fram/lamp` follows the lamp | ok: job `[1]`; `cat /fram/lamp` = `on` after LAMP was pressed |

**2026-11-15, hour counter and the new FRAM layout** (`picoos.uf2`):
tested on the PC (`fs/hours` against a simulated FRAM: counting,
persistence, never backwards, power cut before a save (200×), torn
records (1–15 of 16 bytes), lost header, both copies destroyed, set and
reset, a cut during set and during the very first init, an unreadable
FRAM at boot, `mkfs` and a full filesystem leaving the system area
byte-identical, a version-1 filesystem refusing to mount). On the board:

| Test | Result |
|---|---|
| First boot: `fram: no filesystem` (version 1 no longer mounts) and `hours: new counter, starting at 0:00` | ok: both messages on the first boot after flashing (22:55:30); `0:14` about 14 minutes later, after a reboot in between |
| `mkfs /fram`: 7104 bytes free; `/fram/boot` recreated with `script boot` | `mkfs` ok: `/fram: formatted, 7104 bytes free`; `ls /fram` empty. `/fram/boot` not recreated yet (the built-in boot ran) |
| `cat /dev/hours`, `/dev/hours/status` (`new`, after a reboot `ok`); the count goes up each minute | `cat /dev/hours` = `0:14`: counting since the first boot of this firmware, across `echo now > /dev/reboot`. `/dev/hours/status` = `ok` after that reboot (it was `new` on the first boot) |
| Power off for a while, on again: the count continues where it was (minus the minute in progress) | ok: `0:19` before power-off (23:16), `Clean boot` at 23:21, still `0:19`: the 5 minutes off don't count (power-on hours), nothing lost |
| `mkfs /fram` again: `/dev/hours` unchanged | ok: `mkfs` ran after the counter had started, and it still read `0:14` right after |
| `echo set 10:00 > /dev/hours/ctl`, then `set 0:00` | ok: `10:00`, then `0:00`, both at once; counting goes on from the set value (`0:05` five minutes later). `echo 5 > /dev/hours/ctl` refused (`write failed`), counter unchanged |
| `hexdump -s 0x1f00 /dev/fram`: `sys1` header, copies A/B alternating | ok at `0:19`: header `sys1 01 07`; counter 0 copy A seq 19 / 18 minutes, copy B seq 20 / 19 minutes (the newest, = `0:19`); counter 1 at seq 1 / 0. seq = minutes + 1: one save per minute since the init, none missed or doubled, across a reboot |

**2026-11-13, hexdump, `/dev/fram`, `/dev/mem`** (`picoos.uf2`): tested
on the PC (all of `hexdump` against a simulated FRAM, incl. `*`
collapsing, `-s`/`-n`, the `-- more:` hint, edge cases). On the board:

| Test | Result |
|---|---|
| `hexdump /fram/boot`, `cat /fram/boot \| hexdump` | ok: all three spellings (path, `\|` with and without spaces) identical, 0x59 = 89 bytes = what `ls -l` says; `0a` line ends and the `#` comment visible as stored |
| `hexdump /dev/fram`: superblock `pfs1`, directory with `log`/`boot` | ok: whole chip on one screen; superblock, directory (`log` 35 B at block 15, `boot` 89 B at block 13), chain 13 → 16 → END, data all where they should be. Annotated in [Looking inside the filesystem](#looking-inside-the-filesystem-with-hexdump) |
| `hexdump -s 0x10000000 -n 64 /dev/mem` (flash) | ok: boot2, the SDK's second-stage bootloader for the W25Q16JV (first 256 bytes of flash); starts `00 b5` = Thumb `push {lr}` |
| `hexdump -s 0x20000000 -n 64 /dev/mem` (RAM) | ok: the vector table, which the SDK copies to the start of RAM at startup (so drivers can install interrupt handlers): `00 20 04 20` = initial stack pointer 0x20042000 (`__StackTop` in the map file), `f7 01 00 10` = reset handler 0x100001f7 (Thumb bit set), then NMI, HardFault, and the SDK's catch-all handler `0x100001c1` |
| `hexdump -n 64 /dev/mem` (boot ROM) | ok: the ROM's vector table (stack `0x20041f00`, reset `0xeb`), then at `0x10` the magic `4d 75 01` (`'M' 'u' 0x01`) and at `0x13` the ROM version `03`: **this board's RP2040 is a B2** (B0 = 1, B1 = 2, B2 = 3) |
| `hexdump -s 0x30000000 /dev/mem`: refused with `read error`, no crash | ok: `hexdump: read error at 0x30000000`, prompt back, board keeps running |
| A dump too long for one screen ends with `-- more:` | ok: `hexdump -s 0x10000000 /dev/mem` stops after 58 lines (a `*` saved room) with `-- more: hexdump -s 0x100003a0 /dev/mem`. Along the way: boot2's CRC32 in its last 4 bytes (`0x100000fc`: `74 b2 4e 7a`), the vector table at `0x10000100` (same as the RAM copy), the SDK's binary-info marker `0x7188ebf2` at `0x100001d4` |
| STAGE_BUF 1 KB → 4 KB: `ls` and friends unchanged | ok: `ls` complete (16 devices, `/fram/`, 21 programs) |

## `bringup` — board test

Kept as a permanent test program: flash it whenever a board needs
checking. One program for every board (`boards/bringup.cpp`, since
2026-11-21): it tests what the board's `board.h` describes, and its
banner names the board (`cpu_rp2040 bring-up`, `babywarmer_v1 bring-up`).

`build/bringup.uf2` is a standalone test, no OS. Every output is
switched off first (`board_pins_init()`; on babywarmer_v1 that includes
the heater SSR and the safety relay, which bringup never switches on).
On boot (it waits up to 5 s for a USB serial terminal to attach) it runs:

- an I2C scan, expecting `0x50` (FRAM) and `0x51` (RTC); on a board
  with a second bus (babywarmer_v1) both buses, `local` and `header`,
  the header bus expecting `0x48` (ADS1115) and `0x52` (EEPROM)
- a non-destructive test of all 8 KB of FRAM: every byte is written
  with a pattern and its inverse, then restored, plus an address-line
  aliasing check
- an RTC check: control registers, sane date/time fields, seconds
  actually ticking, and the VL (voltage-low) flag
- on a board with an EEPROM (babywarmer_v1: 24C512): its last page
  written with a pattern and its inverse, then restored, plus the same
  address-line check as the FRAM's (a 24C256 fitted where `board.h`
  says 24C512 shows up there: `0x8000` lands on `0x0000`). An EEPROM
  wears (~1M writes per cell), so it isn't tested cell by cell: ~20 page
  writes per run

then prints a summary and drops into a `>` prompt:

| Command | |
|---|---|
| `all` | run every test again |
| `scan` / `fram` / `rtc` / `eeprom` | run one test (`eeprom` only on a board with one) |
| `time` | print the RTC date/time |
| `set YYYY-MM-DD HH:MM:SS` | set the RTC (clears VL) |

Meanwhile the alarm LED (GPIO1) blinks at 1 Hz and every push-button
press/release is printed (mute, up, down, manual, start, lamp).

## Resources and future hardware

### What plan2040 uses now (measured 2026-11-12, updated 2026-11-13 with hexdump)

| | Used | Available | |
|---|---|---|---|
| Flash (W25Q16JV) | 101 KB (`picoos.bin` 2026-11-16; `plan2040.bin` the same 102988 bytes of code after the restructure, 2026-11-21) | 2048 KB | 5 % |
| RAM | ~35 KB (2026-11-21, built-in scripts moved to flash; ~39 KB 2026-11-16, ~45 KB before the pipeline storage change) | 264 KB | 13 % |
| Stack, worst path | ~1 KB | 2 KB reserved | ok |
| FRAM | ~1 KB metadata | 8 KB | the real limit |
| GPIO | 11 (I2C 2, LED, lamp, 6 buttons, + ADC temp internal) | 30 | |

RAM breakdown: 5.2 KB is the 20 background-job slots (a parsed pipeline
each, ~270 bytes; 13 KB until 2026-11-16, when words stopped having
fixed 24-byte slots), ~12 KB shell and pipe buffers (2 × 4 KB pipe buffers,
1 KB script, 1 KB capture, parsed pipelines), 1.8 KB FRAM filesystem
(directory + chain cache, 1 KB append buffer), ~1.3 KB hexdump/`fs_pread`
buffers, ~7.7 KB code copied to RAM by the SDK, ~3 KB USB stack, plus
2 KB heap and 2 KB stack.

**If space ever gets tight: what to cut first** (measured 2026-11-13,
biggest win first):

| What | Flash | RAM | How |
|---|---|---|---|
| Job slots (`MAX_JOBS` 20, config) | 0 | 5.2 KB | Fewer slots: ~270 bytes each (was ~650 / 13 KB until 2026-11-16). `MAX_JOBS=` in the config. |
| Pipe buffers (`STAGE_BUF` 4 KB, config) | 0 | 6 KB | Back to 1024. Everything keeps working; `hexdump` then shows ~192 bytes per screen instead of ~800 (`-- more:` still pages on). |
| `hexdump` (`prog/hexdump.cpp`) | 1.5 KB | 0.3 KB | Remove `hexdump` from the config's `PLAN2040_PROGS`. Nothing depends on it. |
| `/dev/mem` (`dev/mem.cpp`) | 0.2 KB | 0 | Same: remove `mem` from `PLAN2040_DEVS`. |
| `/dev/fram` (raw, in `dev/fram.cpp`) | 0.1 KB | 0 | Its three small functions + the `fs_register(&dev_fram)` line. |
| `fs_pread()` (`kernel/fs.cpp`) | 0.2 KB | 1 KB | Better kept: tiny, and other features may want reads at an offset. |

Programs in general are a few hundred bytes of flash each, so leaving
out small tools saves little; the buffers and tables are where the RAM
is. The config's lists and sizes switch such things on and off (see
"Source tree and configs").

**A stack finding, fixed.** Measuring with `-fstack-usage` showed the
deepest call path (shell → `dispatch` → `pipeline_run_once` → a program
→ `printf`) needed ~4 KB of stack, twice the 2 KB the SDK reserves. It
only worked because the overflow ran into the memory set aside for core
1's stack, unused so far. That would have broken the day core 1 got
work (the TFT was always the candidate). Inherited from picoos.
The 2 KB of pipe buffers (`kernel/jobs.cpp`) and the parsed pipeline
(`kernel/shell.cpp`) are `static` now; the worst path is ~1 KB.

### With babywarmer and the ST7735 TFT back

The picoos build with everything (heater, NTC/ADS1115, 7-segment,
PID/phase/alarm, TFT with its bitmaps) was ~194 KB flash and ~20 KB RAM.
Most of that was warmer- and TFT-specific, so an estimate for all of it
on this board: **~210 KB flash (~10 %), ~45 KB RAM (~17 %)**. The TFT
driver draws directly to the display, no framebuffer (a full 128×160
16-bit framebuffer would be 40 KB, and would still fit).

GPIOs are the tightest resource. babywarmer + TFT on this board:

| GPIO | Use |
|---|---|
| 0, 1 | buzzer, alarm LED |
| 2, 7, 8, 9, 28, 29 | buttons |
| 3, 4, 5 | 7-segment shift register |
| 6 | lamp |
| 11, 16, 17 | SSR, bargraph PWM, safety relay |
| 12, 13 | ADS1115 I2C (SDA 12, SCL 13), exported on the headers |
| 14, 15 | RTC + FRAM I2C, local to this board, not on the headers |
| 20–25 | TFT (LED, RST, SCK, MOSI, DC, CS) |
| 26 | current-sense ADC |
| **10, 18, 19, 27** | **free** |

26 of 30 used. The pin assignments for the buttons, TFT, heater and
7-segment stay exactly as on babywarmer: cpu_rp2040 becomes the daughter
board of the next babywarmer PCB, which carries those parts. The
ADS1115 gets its own bus on GPIO12/13 (exported on the headers); the
RTC/FRAM bus on GPIO14/15 is local to this board and not exported.

### Recommendation

**Stay on RP2040 + 2 MB for babywarmer + TFT.** Flash and RAM won't be
the problem: ~10 % and ~17 %. CPU time won't either: babywarmer ran this
on one core with the second idle, and PID at 1 Hz in software floating
point is nothing.

**For the next PCB, in order of value:**

1. **A bigger FRAM in the same SOIC-8 footprint.** The cheapest real
   improvement. A 32 KB part with the same 2-byte addressing (e.g.
   Infineon/Cypress FM24V02A or FM24W256, check the datasheet pinout
   against the FM24CL64B) needs no driver change: set `FRAM_SIZE` to
   32768 in `boards/cpu_rp2040/board.h` and `mkfs /fram`. The filesystem format already
   scales (16-bit chain entries). Parts of 128 KB and up (FM24V10) use
   17-bit addresses, which needs a small driver change.
2. **A flash footprint that takes bigger W25Q parts.** All W25QxxJV share
   the same 8-pin pinout, but the 2×3 mm USON of the W25Q16JVUXIQ isn't
   offered in every size; a WSON-8 6×5 mm or SOIC-8 208-mil footprint
   takes 2–16 MB parts (check availability per size). RP2040 handles up
   to 16 MB; software only needs `PICO_FLASH_SIZE_BYTES`. Only worth it
   for big assets (images, fonts) or a flash filesystem.
3. **RP2350 only when you need what it adds**, not for babywarmer:
   - It is **not pin compatible** with RP2040: RP2040 is QFN-56; RP2350A
     is QFN-60 (30 GPIO) and RP2350B QFN-80 (48 GPIO). Power is different
     too: the RP2350 has an on-chip switching regulator that needs an
     external inductor. It's a new layout, not a chip swap. (The Pico 2
     module is pin compatible with the Pico module; the chips aren't.)
   - What it adds: 520 KB RAM, two Cortex-M33 at 150 MHz with a hardware
     FPU, secure boot, and on the RP2350B, 48 GPIO. RP2354A/B versions
     have 2 MB flash inside the package, so no external flash chip.
   - Early RP2350 silicon (A2 stepping) has erratum E9, about GPIO inputs
     and internal pull-downs; later steppings fix it. Buttons here use
     pull-ups, but check the stepping you buy.
   - Software moves easily: same pico-sdk (`PICO_PLATFORM=rp2350`), and
     plan2040 only uses plain SDK calls. Two things to check then: the ADC
     channel of the temperature sensor differs on RP2350B, and
     `bringup` gets a pin map for the new board.

   If a redesign happens anyway (more GPIO for a bigger project), RP2350B
   + a bigger FRAM in one go is the natural step.

## License

MIT — see [LICENSE](LICENSE).
