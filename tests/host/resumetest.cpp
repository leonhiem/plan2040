// resumetest: after a power cut shorter than 10 minutes the babywarmer
// carries on with the nurse's settings (apps/babywarmer/dev/resume.cpp);
// after a longer one, or anything it can't be sure of, the defaults (manual
// 10 %, 35.0 C) with the reason in /dev/resume.
//
// Each boot is a fork()ed child: the devices start from scratch, as after
// a cold boot. The FRAM and the RTC (a fake I2C bus, the real drivers)
// live in shared memory, so they survive; the RTC keeps running while the
// power is off. The real resume, setpoint, heaterauto and percent devices,
// framfs and the task scheduler; /dev/guard is a stand-in.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "kernel/fs.h"
#include "kernel/task.h"
#include "kernel/errstr.h"
#include "board.h"
#include "hardware/i2c.h"

uint64_t fake_now = 0;
int fake_gpio[30];
void (*fake_gpio_hook)(unsigned, int);
i2c_inst_t *fake_i2c0, *fake_i2c1;

// ---- the FRAM and the RTC, shared between the boots ---------------------------
struct chips {
    uint8_t fram[FRAM_SIZE];
    long    clock_s;     // the RTC: seconds since 2026-11-24 00:00, while the power is off too
    bool    vl, rtc_dead;
};
static chips *hw;
static uint16_t fram_ptr;
static uint8_t  rtc_ptr;

static uint8_t bcd(int v) { return (uint8_t)((v / 10) << 4 | v % 10); }
static void rtc_regs(uint8_t r[16])
{
    long s = hw->clock_s + (long)(fake_now / 1000000);
    memset(r, 0, 16);
    r[2] = bcd(s % 60) | (hw->vl ? 0x80 : 0);
    r[3] = bcd(s / 60 % 60);
    r[4] = bcd(s / 3600 % 24);
    r[5] = 0x24; r[6] = 2; r[7] = 0x11; r[8] = 0x26;      // 2026-11-24, Tuesday
}

int i2c_write_timeout_us(i2c_inst_t *, uint8_t addr, const uint8_t *src, size_t len, bool, unsigned)
{
    if (addr == FRAM_I2C_ADDR) {
        fram_ptr = (uint16_t)(src[0] << 8 | src[1]);
        for (size_t i = 2; i < len; i++) hw->fram[fram_ptr++ % FRAM_SIZE] = src[i];
        return (int)len;
    }
    if (addr == RTC_I2C_ADDR && !hw->rtc_dead) { rtc_ptr = src[0]; return (int)len; }
    return PICO_ERROR_GENERIC;
}
int i2c_read_timeout_us(i2c_inst_t *, uint8_t addr, uint8_t *dst, size_t len, bool, unsigned)
{
    if (addr == FRAM_I2C_ADDR) { for (size_t i = 0; i < len; i++) dst[i] = hw->fram[fram_ptr++ % FRAM_SIZE]; return (int)len; }
    if (addr == RTC_I2C_ADDR && !hw->rtc_dead) {
        uint8_t r[16];
        rtc_regs(r);
        for (size_t i = 0; i < len; i++) dst[i] = r[rtc_ptr++ & 15];
        return (int)len;
    }
    return PICO_ERROR_GENERIC;
}

// ---- /dev/guard, a stand-in --------------------------------------------------
static char guard[32] = "ok";
static int guard_read(char *b, int len) { return snprintf(b, len, "%s\n", guard); }
static const device_t dev_guard = { "/dev/guard", 0, 0, guard_read, 0 };

// ---- the shell's view --------------------------------------------------------
static void cat(const char *path)
{
    char b[128];
    int fd = fs_open(path);
    int n = fd < 0 ? -1 : fs_read(fd, b, sizeof(b) - 1);
    if (fd >= 0) fs_close(fd);
    if (n < 0) { printf("  %% cat %s\n  cat: %s\n", path, errstr()); return; }
    b[n] = 0;
    printf("  %% cat %s\n  %s%s", path, b, n > 0 && b[n - 1] == '\n' ? "" : "\n");
}
static void echo(const char *text, const char *path)
{
    int fd = fs_open(path, FS_CREATE);
    int n = fd < 0 ? -1 : fs_write(fd, text, strlen(text));
    if (fd >= 0) fs_close(fd);
    printf("  %% echo %s > %s\n", text, path);
    if (n < 0) printf("  echo: %s\n", errstr());
}
static void run(int seconds)
{
    for (int i = 0; i < seconds * 10; i++) { fake_now += 100000; task_run(); }
}
static void settings(void)
{
    printf("  ->"); fflush(stdout);
    char b[16]; const char *p[] = { "/dev/heaterauto", "/dev/setpoint", "/dev/percent" };
    for (const char *path : p) { int fd = fs_open(path); int n = fs_read(fd, b, 15); fs_close(fd); b[n < 0 ? 0 : n - 1] = 0; printf(" %s %s", path + 5, b); }
    printf("\n");
}

void dev_fram_register(void);
void dev_setpoint_register(void);
void dev_heaterauto_register(void);
void dev_percent_register(void);
void dev_resume_register(void);

// One boot: the devices registered from scratch, `what` done in it, then
// the power cut (the RTC runs on).
static void boot(const char *title, void (*what)(void))
{
    printf("\n== %s\n", title);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        dev_fram_register();
        dev_setpoint_register();
        dev_heaterauto_register();
        dev_percent_register();
        fs_register(&dev_guard);
        dev_resume_register();
        run(2);                                     // the resume task's first run
        cat("/dev/resume");
        settings();
        what();
        fflush(stdout);
        hw->clock_s += (long)(fake_now / 1000000);
        _exit(0);
    }
    waitpid(pid, 0, 0);
}
static void power_off(int minutes) { printf("-- power off %d min\n", minutes); hw->clock_s += minutes * 60L; }

// ---- the scenarios -----------------------------------------------------------
static void first(void)
{
    echo("36.5", "/dev/setpoint");
    echo("on", "/dev/heaterauto");
    run(70);
    cat("/fram/resume");
}
static void to_manual(void)
{
    echo("off", "/dev/heaterauto");
    echo("40", "/dev/percent");
    run(2);
    cat("/fram/resume");
}
static void high(void)
{
    echo("on", "/dev/setpoint/high");
    echo("38.5", "/dev/setpoint");
    echo("on", "/dev/heaterauto");
    run(2);
    cat("/fram/resume");
}
static void guard_none(void)
{
    printf("  (both sensors gone: the guard switches to manual itself)\n");
    snprintf(guard, sizeof(guard), "none: no temperature sensors");
    echo("off", "/dev/heaterauto");
    echo("30", "/dev/percent");
    run(70);
    cat("/fram/resume");
}
static void nothing(void) { run(70); }
static void garble(void) { echo("hello", "/fram/resume"); }

int main()
{
    hw = (chips *)mmap(0, sizeof(chips), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(hw, 0xff, sizeof(hw->fram));
    hw->clock_s = 8 * 3600;                         // 08:00

    printf("== mkfs /fram\n");
    if (fork() == 0) { dev_fram_register(); fs_format("/fram"); _exit(0); }
    wait(0);

    boot("first boot: no /fram/resume yet; the nurse sets 36.5 C, auto", first);
    power_off(3);
    boot("3 min later: auto 36.5 again; to manual, 40 %", to_manual);
    power_off(9);
    boot("9 min later: manual 40 %; then 38.5 C on purpose, auto", high);
    power_off(1);
    boot("1 min later: auto, the setpoint back to 37.5", nothing);
    power_off(25);
    boot("25 min later: the defaults (maybe another baby)", first);
    power_off(2);
    boot("2 min later: auto 36.5; the sensors fail", guard_none);
    power_off(2);
    boot("2 min later: still auto (the guard's manual isn't the nurse's)", nothing);
    hw->vl = true;
    power_off(2);
    boot("2 min later, the RTC lost its time (VL)", nothing);
    hw->vl = false;
    hw->clock_s -= 3600;
    power_off(1);
    boot("the clock set back an hour", garble);
    power_off(1);
    boot("1 min later, /fram/resume garbled", nothing);
    hw->rtc_dead = true;
    power_off(1);
    boot("1 min later, the RTC not answering", nothing);
    return 0;
}
