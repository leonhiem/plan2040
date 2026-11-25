# configs/cpu_rp2040.cmake — plan2040 on the cpu_rp2040 board
#
# One file per firmware, Plan 9 kernel-config style: which board, which
# devices and programs go in, and the sizes. Build it with
#
#   cmake -DPLAN2040_CONFIG=cpu_rp2040 ..     (cpu_rp2040 is the default)
#
# The lists are names, not files:
#   PLAN2040_DEVS   dev/<name>.cpp,  registered by dev_<name>_register()
#   PLAN2040_PROGS  prog/<name>.cpp, registered by prog_<name>_register()
# in exactly this order (the order `ls` shows them, and the order they
# start in: fram must come before hours, which reads the FRAM's system
# area). Adding a program = writing prog/<name>.cpp + one word here.

set(PLAN2040_BOARD   cpu_rp2040)   # boards/<board>/: board.h, board.cpp, bringup.cpp

set(PLAN2040_DEVS
    led          # /dev/led, GPIO1
    lamp         # /dev/lamp, GPIO6
    buttons      # /dev/buttons + /dev/buttons/<name>
    rtc          # /dev/rtc, /dev/rtc/vl (PCF8563)
    uptime
    cputemp
    reboot       # /dev/reboot: now | bootsel
    fram         # mounts /fram, and /dev/fram (raw)
    mem
    hours        # power-on hours, from the FRAM's system area
    cron         # runs /fram/crontab lines, on the RTC's minutes
    cons         # /dev/cons: write to the terminal
    sysname      # /dev/sysname: this board's name, from /fram/sysname
    eia          # /dev/eia0, /dev/eia0ctl: UART0, the link to another board
    p9           # /dev/9p: 9P between boards (serve, import); after eia
)

set(PLAN2040_PROGS
    cat echo label thresh hyst toggle adjust follow select date uptime
    wc grep headtail tee hexdump remember cpmv rm fsck mkfs df ls
    serve import
)

# Built-in scripts: scripts/<name> (or the app's), "boot" runs at power-up
# unless /fram/boot exists.
set(PLAN2040_SCRIPTS boot)

# Other sources the devices above need.
set(PLAN2040_FS      framfs hours)               # fs/<name>.cpp
set(PLAN2040_9P      fcall frame link srv mnt)   # 9p/<name>.cpp
set(PLAN2040_DRIVERS pcf8563 fm24cl64 buttons eia)   # drivers/<name>.cpp
set(PLAN2040_LIBS    hardware_i2c hardware_adc hardware_gpio hardware_uart pico_unique_id)

# Sizes (defaults in the headers; listed here so a smaller board can
# shrink them). RAM: job slots ~260 bytes each, pipe buffers 2 x STAGE_BUF.
set(PLAN2040_SIZES
    MAX_JOBS=20          # background jobs at once (kernel/jobs.h), 1..32
    STAGE_BUF=4096       # pipe buffer (kernel/jobs.h)
    PROG_MAX=48          # programs in bin/ (kernel/prog.h)
    FS_MAX_DEVICES=64    # entries in /dev (kernel/fs.h)
    TASK_MAX=32          # tasks (kernel/task.cpp): MAX_JOBS + shell, hours, cron = 23
)
