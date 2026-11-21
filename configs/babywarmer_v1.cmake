# configs/babywarmer_v1.cmake — plan2040 on the old babywarmer PCB, with
# the cpu_rp2040 board wired in as its CPU (TODO.md idea 3, "The
# babywarmer_v1 hardware hack"). Build it in a directory of its own:
#
#   mkdir build-babywarmer_v1 && cd build-babywarmer_v1
#   cmake -DPLAN2040_CONFIG=babywarmer_v1 .. && make -j$(nproc)
#
# Built up step by step (TODO.md idea 3): 3a the generic devices, as on
# cpu_rp2040, to test the hack; 3c the babywarmer's sensors. Every
# babywarmer output not driven by a device here yet (heater SSR, safety
# relay, buzzer, 7-segment, TFT backlight) stays off.

set(PLAN2040_BOARD   babywarmer_v1)
set(PLAN2040_APP     babywarmer)     # apps/babywarmer/: from picoos @ 89c16e5

set(PLAN2040_DEVS
    led          # /dev/led: the alarm LED (GPIO1); /dev/alarm takes it over in 3d
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
    skintemp     # /dev/skintemp: skin NTC, ADS1115 channels 0-1
    ambient      # /dev/ambient: room NTC, ADS1115 channels 2-3
    current      # /dev/current: heater current sense, ADC0 (raw counts)
)

set(PLAN2040_PROGS
    cat echo label thresh hyst toggle adjust follow select date uptime
    wc grep headtail tee hexdump remember cpmv rm fsck mkfs df ls
)

# Built-in scripts: scripts/<name> (or the app's), "boot" runs at power-up
# unless /fram/boot exists.
set(PLAN2040_SCRIPTS boot)

set(PLAN2040_FS      framfs hours)
set(PLAN2040_DRIVERS pcf8563 fm24cl64 buttons at24c ads1115)
set(PLAN2040_APP_SRC ntc.cpp)                    # apps/babywarmer/: the NTC table
set(PLAN2040_LIBS    hardware_i2c hardware_adc hardware_gpio)

set(PLAN2040_SIZES
    MAX_JOBS=20
    STAGE_BUF=4096
    PROG_MAX=48
    FS_MAX_DEVICES=64    # the babywarmer app brings ~45 more: 96 then
    TASK_MAX=32
)
