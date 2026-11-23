# configs/babywarmer_v1.cmake — plan2040 on the old babywarmer PCB, with
# the cpu_rp2040 board wired in as its CPU (TODO.md idea 3, "The
# babywarmer_v1 hardware hack"). Build it in a directory of its own:
#
#   mkdir build-babywarmer_v1 && cd build-babywarmer_v1
#   cmake -DPLAN2040_CONFIG=babywarmer_v1 .. && make -j$(nproc)
#
# Built up step by step (TODO.md idea 3): 3a the generic devices, as on
# cpu_rp2040, to test the hack; 3c the babywarmer's sensors; 3d its
# outputs (heater, safety relay, buzzer + alarm LED, 7-segment + LEDs),
# each off at boot and only switched by a write to its device; 3e the
# control (setpoint, auto/manual, PID, phases, alarms) and the boot
# script that wires it all (apps/babywarmer/scripts/boot); 3f the
# ST7735 TFT.

set(PLAN2040_BOARD   babywarmer_v1)
set(PLAN2040_APP     babywarmer)     # apps/babywarmer/: from picoos @ 89c16e5

set(PLAN2040_DEVS
    lamp         # /dev/lamp: the baby light (GPIO6, active-high here)
    buttons      # /dev/buttons + /dev/buttons/<name>
    rtc          # /dev/rtc, /dev/rtc/vl (PCF8563, local bus)
    uptime
    cputemp
    reboot       # /dev/reboot: now | bootsel
    fram         # mounts /fram, and /dev/fram (raw)
    mem
    hours        # power-on hours, from the FRAM's system area
    cron         # runs /fram/crontab lines, on the RTC's minutes
    cons         # /dev/cons: write to the terminal
    # babywarmer (apps/babywarmer/dev/)
    skintemp     # /dev/skintemp: skin NTC, ADS1115 channels 0-1 (the "adc" task samples)
    ambient      # /dev/ambient: room NTC, ADS1115 channels 2-3
    current      # /dev/current: heater current sense, ADC0 (raw counts)
    heater       # /dev/heater: 0-100 %, time-proportional SSR + bargraph; the "tpo" task
    relay        # /dev/relay: the mechanical safety relay, on/off
    alarm        # /dev/alarm: buzzer + alarm LED (GPIO0, GPIO1): on | muted | off
    seg7         # /dev/seg7big, /dev/seg7small, /dev/leds + /dev/leds/<name>; lamp test at boot
    setpoint     # /dev/setpoint: 30.0-39.0 C, default 35.0
    heaterauto   # /dev/heaterauto: on = auto (PID), off = manual
    percent      # /dev/percent: manual heater power, default 10
    pid          # /dev/pid/{kp,ti,td,dt,integral,prevmeas}, /dev/pidout
    phase        # /dev/state, /dev/autopower, /dev/safepower
    alarmcheck   # /dev/alarm/{heater,temphigh,templow,sensor}: the alarm conditions
    guard        # /dev/guard: all heater safety, an unkillable kernel task (after alarmcheck)
    tft          # /dev/tft/<name>: the ST7735 screen, 12 files; the "tftflush" task
)

set(PLAN2040_PROGS
    cat echo label thresh hyst toggle adjust follow select date uptime
    wc grep headtail tee hexdump remember cpmv rm fsck mkfs df ls
    # babywarmer (apps/babywarmer/prog/)
    pid phase safelut alarm ledwire monitor tftwire
)

# Built-in scripts: scripts/<name> (or the app's), "boot" runs at power-up
# unless /fram/boot exists.
set(PLAN2040_SCRIPTS boot tftmon)

set(PLAN2040_FS      framfs hours)
set(PLAN2040_DRIVERS pcf8563 fm24cl64 buttons at24c ads1115)
set(PLAN2040_APP_SRC ntc.cpp ntcadc.cpp heater.cpp seg7.cpp safelut.cpp  # apps/babywarmer/: NTC table + sampling,
    # TPO, 74HC595, the room table,
    tft/st7735.c tft/gfx.c tft/icons.c tft/baby.c    # and the TFT (a blackbox copy of the
    tft/heat_indicator.c tft/apgar_timer.c          # st7735 repo, see tft/README.md)
    tft/text_console.c tft/cue_icons.c tft/display.c)
set(PLAN2040_LIBS    hardware_i2c hardware_adc hardware_gpio hardware_pwm hardware_spi)

set(PLAN2040_SIZES
    MAX_JOBS=20
    STAGE_BUF=4096
    PROG_MAX=48
    FS_MAX_DEVICES=96    # 64 used with the TFT
    TASK_MAX=32          # MAX_JOBS + shell, hours, cron, tpo, tftflush, adc, guard, seg7 = 28
)
