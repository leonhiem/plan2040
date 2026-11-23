// adctest: drivers/ads1115.cpp + apps/babywarmer/ntcadc.cpp (the "adc"
// background task) + /dev/skintemp, /dev/ambient, against a fake ADS1115
// on a fake I2C bus, with the real kernel scheduler. A conversion takes
// 7.8 ms (128 SPS), as on the chip. Measures what matters (Leon's rules,
// 2026-11-23): how fast a reading follows a change, how soon a failure
// shows and with what text, how much time a broken bus costs per second,
// and how fast it all recovers.
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/fs.h"
#include "kernel/task.h"
#include "kernel/errstr.h"
#include "board.h"
#include "ntcadc.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"

uint64_t fake_now = 0;
i2c_inst_t *fake_i2c0, *fake_i2c1;
int getchar_timeout_us(uint32_t) { return -1; }
void dev_skintemp_register(void); void dev_ambient_register(void);

// ---- the fake ADS1115
enum { OK, NACK, STUCK, NEVER_READY } mode = OK;
static int nack_next = 0;                    // NACK just this many transfers (a glitch)
static int16_t input[8];                     // code per mux setting
static uint16_t cfg = 0x8583, conv;
static uint8_t ptr;
static uint64_t conv_done_at;
static long transfers; static uint64_t busy_us;  // I2C load

static int bus(unsigned timeout_us, size_t len)
{
    transfers++;
    if (mode == STUCK) { fake_now += timeout_us; busy_us += timeout_us; return PICO_ERROR_TIMEOUT; }
    if (mode == NACK || nack_next > 0) { if (nack_next > 0) nack_next--; fake_now += 50; busy_us += 50; return PICO_ERROR_GENERIC; }
    uint64_t t = 50 + len * 45;              // 200 kHz: ~45 us a byte
    fake_now += t; busy_us += t;
    return (int)len;
}
int i2c_write_timeout_us(i2c_inst_t *, uint8_t addr, const uint8_t *src, size_t len, bool, unsigned timeout_us)
{
    if (addr != ADS1115_I2C_ADDR) return PICO_ERROR_GENERIC;
    int r = bus(timeout_us, len); if (r < 0) return r;
    ptr = src[0];
    if (len == 3 && ptr == 0x01) {
        cfg = (uint16_t)(src[1] << 8 | src[2]);
        if (cfg & 0x8000) { conv_done_at = fake_now + 7813; cfg &= 0x7fff; }
    }
    return r;
}
int i2c_read_timeout_us(i2c_inst_t *, uint8_t addr, uint8_t *dst, size_t len, bool, unsigned timeout_us)
{
    if (addr != ADS1115_I2C_ADDR) return PICO_ERROR_GENERIC;
    int r = bus(timeout_us, len); if (r < 0) return r;
    bool done = mode != NEVER_READY && fake_now >= conv_done_at;
    if (done && !(cfg & 0x8000)) { conv = (uint16_t)input[(cfg >> 12) & 7]; cfg |= 0x8000; }
    uint16_t v = ptr == 0x01 ? cfg : conv;
    dst[0] = v >> 8; dst[1] = v & 0xff;
    return r;
}

// ---- helpers
static bool rd(const char *path, char *out, int len)
{
    int fd = fs_open(path); if (fd < 0) { snprintf(out, len, "%s", errstr()); return false; }
    int n = fs_read(fd, out, len - 1); fs_close(fd);
    if (n < 0) { snprintf(out, len, "%s", errstr()); return false; }
    out[n] = 0; char *nl = strchr(out, '\n'); if (nl) *nl = 0; return true;
}
static void run_ms(uint64_t ms) { uint64_t end = fake_now + ms * 1000; while (fake_now < end) { task_run(); fake_now += 1000; } }
static double t_s(void) { return fake_now / 1e6; }
// run until /dev/skintemp's ok-ness becomes `want`; ms it took (or -1)
static long until(bool want, long limit_ms)
{
    uint64_t t0 = fake_now; char b[120];
    while ((long)((fake_now - t0) / 1000) < limit_ms) { if (rd("/dev/skintemp", b, sizeof b) == want) return (long)((fake_now - t0) / 1000); run_ms(5); }
    return -1;
}
static void load(const char *what, int seconds)
{
    long t0 = transfers; uint64_t b0 = busy_us;
    run_ms(seconds * 1000);
    printf("  %-34s %5.1f I2C transfers/s, %6.2f ms/s of bus time (%.2f %% of the CPU)\n", what,
           (transfers - t0) / (double)seconds, (busy_us - b0) / 1000.0 / seconds, (busy_us - b0) / 10000.0 / seconds);
}

int main()
{
    char a[120], b[120];
    input[0] = 0;       // diff 0-1, the skin NTC: code 0 (25.0 C, the bridge in balance)
    input[3] = 5000;    // diff 2-3, the room NTC (18.4 C with the MF58 table)
    dev_skintemp_register(); dev_ambient_register();
    printf("boot: first readings took %.1f ms (blocking, at boot only)\n", t_s() * 1000);
    rd("/dev/skintemp", a, sizeof a); rd("/dev/ambient", b, sizeof b);
    printf("  skintemp %s, ambient %s\n", a, b);

    printf("normal running:\n");
    load("2 channels, each every ~100 ms:", 5);

    printf("a change on the skin NTC (code 0 -> 5000, the same as the room channel):\n");
    input[0] = 5000; uint64_t t0 = fake_now;
    char want[16]; rd("/dev/ambient", want, sizeof want);       // the room channel reads code 5000 already
    for (;;) {
        rd("/dev/skintemp", a, sizeof a);
        if (!strcmp(a, want)) break;
        if (fake_now - t0 > 5000000) { printf("  NEVER (after 5 s: %s, want %s)\n", a, want); return 1; }
        run_ms(1);
    }
    printf("  /dev/skintemp shows it after %llu ms\n", (unsigned long long)((fake_now - t0) / 1000));

    printf("one glitch (a single NACK):\n");
    nack_next = 1; long f = until(false, 3000);
    printf("  /dev/skintemp %s\n", f < 0 ? "never failed (as it should)" : "FAILED (it shouldn't)");

    const struct { int m; const char *name; } faults[] = {
        { NACK, "ADS1115 not answering (NACK)" }, { STUCK, "stuck bus (every transfer times out)" },
        { NEVER_READY, "conversion never finishes" } };
    for (auto &fl : faults) {
        printf("%s:\n", fl.name);
        mode = (decltype(mode))fl.m;
        long ms = until(false, 5000);
        rd("/dev/skintemp", a, sizeof a); rd("/dev/ambient", b, sizeof b);
        printf("  /dev/skintemp fails after %ld ms: %s\n", ms, a);
        printf("  /dev/ambient:  %s\n", b);
        uint64_t r0 = fake_now; int fd = fs_open("/dev/skintemp"); fs_read(fd, a, sizeof a); fs_close(fd);
        printf("  a read meanwhile takes %llu us\n", (unsigned long long)(fake_now - r0));
        load("while failing:", 5);
        mode = OK;
        ms = until(true, 5000);
        rd("/dev/skintemp", a, sizeof a);
        printf("  fixed: /dev/skintemp ok again after %ld ms: %s\n", ms, a);
    }
    return 0;
}
