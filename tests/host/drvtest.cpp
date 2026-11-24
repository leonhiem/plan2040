// drvtest: the FRAM and RTC drivers' reasons (drivers/README.md rule 3,
// 2026-11-24). The real drivers/fm24cl64.cpp and drivers/pcf8563.cpp,
// dev/rtc.cpp, dev/fram.cpp, fs/framfs.cpp and kernel/fs.cpp, on a fake
// I2C bus with an FM24CL64B (0x50) and a PCF8563 (0x51) that can stop
// answering. What a user reads: `cat: /dev/rtc: RTC (0x51, local bus) not
// answering`, not "read failed".
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include "kernel/fs.h"
#include "kernel/errstr.h"
#include "fs/framfs.h"
#include "board.h"
#include "hardware/i2c.h"

uint64_t fake_now = 0;
i2c_inst_t *fake_i2c0, *fake_i2c1;

// ---- the fake chips ------------------------------------------------------
static uint8_t fram[FRAM_SIZE];
static uint16_t fram_ptr;
static uint8_t rtc[16] = { 0, 0, 0x00, 0x30, 0x14, 0x24, 2, 0x11, 0x26 };  // 2026-11-24 14:30:00
static uint8_t rtc_ptr;
static bool fram_dead, rtc_dead;

int i2c_write_timeout_us(i2c_inst_t *, uint8_t addr, const uint8_t *src, size_t len, bool, unsigned)
{
    if (addr == FRAM_I2C_ADDR && !fram_dead) {
        fram_ptr = (uint16_t)(src[0] << 8 | src[1]);
        for (size_t i = 2; i < len; i++) fram[fram_ptr++ % FRAM_SIZE] = src[i];
        return (int)len;
    }
    if (addr == RTC_I2C_ADDR && !rtc_dead) {
        rtc_ptr = src[0];
        for (size_t i = 1; i < len; i++) rtc[rtc_ptr++ & 15] = src[i];
        return (int)len;
    }
    return PICO_ERROR_GENERIC;                 // nobody answers
}
int i2c_read_timeout_us(i2c_inst_t *, uint8_t addr, uint8_t *dst, size_t len, bool, unsigned)
{
    if (addr == FRAM_I2C_ADDR && !fram_dead) { for (size_t i = 0; i < len; i++) dst[i] = fram[fram_ptr++ % FRAM_SIZE]; return (int)len; }
    if (addr == RTC_I2C_ADDR && !rtc_dead)   { for (size_t i = 0; i < len; i++) dst[i] = rtc[rtc_ptr++ & 15]; return (int)len; }
    return PICO_ERROR_GENERIC;
}

// ---- like `cat` and `echo ... >` -------------------------------------------
static void cat(const char *path)
{
    char b[128];
    int fd = fs_open(path);
    int n = fd < 0 ? -1 : fs_read(fd, b, sizeof(b) - 1);
    if (fd >= 0) fs_close(fd);
    if (n < 0) { printf("%% cat %s\ncat: %s\n", path, errstr()); return; }
    b[n] = 0;
    printf("%% cat %s\n%s%s", path, b, n > 0 && b[n - 1] == '\n' ? "" : "\n");
}
static void echo(const char *text, const char *path)
{
    int fd = fs_open(path, FS_CREATE);            // as `>` does
    int n = fd < 0 ? -1 : fs_write(fd, text, strlen(text));
    if (fd >= 0) fs_close(fd);
    printf("%% echo %s > %s\n", text, path);
    if (n < 0) printf("echo: %s\n", errstr());
}
static void mkfs(void)
{
    printf("%% mkfs /fram\n");
    if (fs_format("/fram") < 0) printf("mkfs: %s\n", errstr());
}

void dev_rtc_register(void);
void dev_fram_register(void);

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--silent") == 0) {
        // A boot of its own (a fresh process): both chips silent.
        framfs_mkfs();                               // the FRAM as the first boot left it
        framfs_write("setpoint", "36.5", 4, false);
        printf("--- a boot with both chips silent\n");
        fram_dead = rtc_dead = true;
        dev_rtc_register();
        dev_fram_register();
        cat("/fram/setpoint");
        printf("--- they answer again\n");
        fram_dead = rtc_dead = false;
        cat("/dev/rtc");
        cat("/fram/setpoint");                       // within the second: not tried yet
        fake_now += 1000000;
        cat("/fram/setpoint");                       // mounted again by itself
        return 0;
    }
    printf("--- boot, both chips answer, the FRAM blank\n");
    dev_rtc_register();
    dev_fram_register();
    cat("/dev/rtc");
    cat("/fram/setpoint");
    mkfs();
    echo("36.5", "/fram/setpoint");
    cat("/fram/setpoint");

    printf("--- wrong dates\n");
    echo("tomorrow", "/dev/rtc");
    echo("2026-13-01 10:00:00", "/dev/rtc");
    echo("2026-11-24 25:00:00", "/dev/rtc");
    echo("2026-11-24 15:00:00", "/dev/rtc");
    cat("/dev/rtc");

    printf("--- the RTC stops answering\n");
    rtc_dead = true;
    cat("/dev/rtc");
    cat("/dev/rtc/vl");
    echo("2026-11-24 15:00:00", "/dev/rtc");
    rtc_dead = false;

    printf("--- the FRAM stops answering\n");
    fram_dead = true;
    echo("37.0", "/fram/setpoint");
    cat("/dev/fram");
    mkfs();
    fram_dead = false;
    cat("/fram/setpoint");                           // within the second
    fake_now += 1000000;
    cat("/fram/setpoint");                           // mounted again: the failed mkfs changed nothing on the chip

    fflush(stdout);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "%s --silent", argv[0]);
    return system(cmd) == 0 ? 0 : 1;
}
