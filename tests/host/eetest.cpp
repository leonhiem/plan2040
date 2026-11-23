// eetest: apps/babywarmer/dev/ntc.cpp (/dev/ntc/*) with the real
// drivers/at24c.cpp and kernel scheduler, against a fake 24C512 that
// behaves like the chip: 128-byte pages that wrap inside the page, and
// ~5 ms of not answering after each write. Every "boot" is a fresh
// process (fresh RAM), reading the same EEPROM image from a file:
//   eetest                  the story (boots itself again with --boot)
//   eetest --boot <image>   one boot: which table, a few readings
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <stdint.h>
#include <string>
#include "kernel/fs.h"
#include "kernel/task.h"
#include "kernel/errstr.h"
#include "board.h"
#include "ntc.h"
#include "ntcadc.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"

uint64_t fake_now = 0;
i2c_inst_t *fake_i2c0, *fake_i2c1;
int getchar_timeout_us(uint32_t) { return -1; }
void dev_ntc_register(void);
bool ntcadc_get(int ch, int16_t *code) { *code = ch == NTC_SKIN ? -6590 : 3658; return true; }  // 37 C and 20 C

// ---- the fake 24C512 ----------------------------------------------------
static uint8_t rom[EEPROM_SIZE];
static uint16_t ptr;
static uint64_t busy_until;
static long page_writes = -1;        // >= 0: power cut after this many page writes
static long writes_done;
int i2c_write_timeout_us(i2c_inst_t *, uint8_t addr, const uint8_t *src, size_t len, bool, unsigned)
{
    fake_now += 50 + len * 45;
    if (addr != EEPROM_I2C_ADDR || fake_now < busy_until) return PICO_ERROR_GENERIC;
    ptr = (uint16_t)(src[0] << 8 | src[1]);
    if (len > 2) {
        if (page_writes >= 0 && writes_done >= page_writes) return (int)len;   // power gone: nothing lands
        uint16_t base = ptr & ~(EEPROM_PAGE - 1);
        for (size_t i = 2; i < len; i++) { rom[ptr] = src[i]; ptr = base | ((ptr + 1) & (EEPROM_PAGE - 1)); }
        busy_until = fake_now + 5000;
        writes_done++;
    }
    return (int)len;
}
int i2c_read_timeout_us(i2c_inst_t *, uint8_t addr, uint8_t *dst, size_t len, bool, unsigned)
{
    fake_now += 50 + len * 45;
    if (addr != EEPROM_I2C_ADDR || fake_now < busy_until) return PICO_ERROR_GENERIC;
    for (size_t i = 0; i < len; i++) dst[i] = rom[ptr++];
    return (int)len;
}

// ---- helpers --------------------------------------------------------------
static std::string rd(const char *path)
{
    static char b[4096];
    int fd = fs_open(path); if (fd < 0) return std::string("error: ") + errstr();
    int n = fs_read(fd, b, sizeof b - 1); fs_close(fd);
    if (n < 0) return std::string("error: ") + errstr();
    b[n] = 0; return b;
}
static int wr(const char *path, const char *text)
{
    int fd = fs_open(path); int n = fs_write(fd, text, strlen(text)); fs_close(fd);
    if (n < 0) printf("  write refused: %s\n", errstr());
    return n;
}
static void run_ms(int ms) { uint64_t end = fake_now + ms * 1000ull; while (fake_now < end) { task_run(); fake_now += 1000; } }
static void save(const char *f) { FILE *o = fopen(f, "wb"); fwrite(rom, 1, sizeof rom, o); fclose(o); }
static void load(const char *f) { FILE *i = fopen(f, "rb"); if (!i || fread(rom, 1, sizeof rom, i) != sizeof rom) exit(1); fclose(i); }
static void boot(const char *title, const char *img)
{
    printf("--- boot: %s\n", title); fflush(stdout);
    std::string cmd = std::string("./eetest --boot ") + img;
    if (system(cmd.c_str()) != 0) printf("  (boot failed)\n");
}
static void temps(void)
{
    float t;
    for (int code : { -6590, 0, 3658, -13566 })
        if (ntc_temp((int16_t)code, &t)) printf("  code %6d -> %.2f C\n", code, t);
        else printf("  code %6d -> %s\n", code, errstr());
}

// 5 points of a sensor with B 3900 (not the datasheet's 3950)
static const char *CAL =
    "# skin + room NTC, measured 2026-11-24 in water baths\n"
    "bridge 3.3 47000 47000 10000\n"
    "25118 0.0\n0 25.0\n-5625 35.0\n-6521 37.0\n-9497 45.0\n";

int main(int argc, char **argv)
{
    if (argc == 3) {                       // one boot
        load(argv[2]);
        dev_ntc_register();
        printf("  %s", rd("/dev/ntc/status").c_str());
        temps();
        printf("  cat /dev/ntc/skin: %s", rd("/dev/ntc/skin").c_str());
        return 0;
    }
    memset(rom, 0xff, sizeof rom);         // a new chip
    save("eetest.img");
    boot("a new, empty EEPROM", "eetest.img");

    printf("--- write a calibration (5 points, B 3900): cp /fram/ntc.pts /dev/ntc/lut\n");
    dev_ntc_register();
    uint64_t t0 = fake_now;
    wr("/dev/ntc/lut", CAL);
    run_ms(100);
    printf("  after 0.1 s: %s", rd("/dev/ntc/status").c_str());
    printf("  a second write meanwhile:\n"); wr("/dev/ntc/lut", CAL);
    while (rd("/dev/ntc/status").find("writing") != std::string::npos) run_ms(10);
    printf("  done after %.2f s, %ld page writes: %s", (fake_now - t0) / 1e6, writes_done, rd("/dev/ntc/status").c_str());
    temps();
    printf("  cat /dev/ntc/lut:\n%s", rd("/dev/ntc/lut").c_str());
    save("eetest.img");
    boot("with the calibration", "eetest.img");

    printf("--- a power cut halfway through writing a new calibration\n");
    page_writes = writes_done + 20;        // the 20th page write is the last to land
    wr("/dev/ntc/lut", "bridge 3.3 47000 47000 10000\n0 25\n-6590 37\n-11036 50\n");
    run_ms(2000);
    save("eetest-cut.img");
    page_writes = -1;
    boot("after the power cut", "eetest-cut.img");

    printf("--- one damaged byte in the table\n");
    load("eetest.img"); rom[3000] ^= 0x10; save("eetest-bad.img");
    boot("a damaged EEPROM", "eetest-bad.img");

    printf("--- texts that are refused\n");
    load("eetest.img");
    wr("/dev/ntc/lut", "bridge 3.3 47000 47000 10000\n-6590 37\n");
    wr("/dev/ntc/lut", "-6590 37 x\n");
    printf("  still: %s", rd("/dev/ntc/status").c_str());
    remove("eetest.img"); remove("eetest-cut.img"); remove("eetest-bad.img");
    return 0;
}
