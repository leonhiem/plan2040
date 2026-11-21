// outtest: babywarmer's outputs (step 3d) through their devices, on fake
// GPIO/PWM: /dev/heater's time-proportional SSR over one 15 s window and
// the bargraph level, /dev/relay, /dev/alarm, and the exact 56 bits
// /dev/seg7big, /dev/seg7small and /dev/leds/* shift into the 74HC595s.
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include "kernel/fs.h"
#include "kernel/task.h"
#include "board.h"
#include "heater.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

uint64_t fake_now = 0;
int fake_gpio[30];
void (*fake_gpio_hook)(uint pin, int value);
uint16_t fake_pwm_level[8][2];
i2c_inst_t *fake_i2c0, *fake_i2c1;
int getchar_timeout_us(uint32_t) { return -1; }
static task_fn_t tpo; static const char *tpo_name; static uint32_t tpo_ms;
void task_register(const char *name, task_fn_t f, uint32_t ms) { tpo_name = name; tpo = f; tpo_ms = ms; }

void dev_heater_register(void); void dev_relay_register(void);
void dev_alarm_register(void);  void dev_seg7_register(void);

// 74HC595 chain: collect SER on each SCK rising edge, show on LATCH.
static uint64_t shift; static int nbits; static int latches;
static void hook(uint pin, int v)
{
    static int sck = 0, latch = 0;
    if (pin == PIN_SEG_SCK) { if (v && !sck) { shift = (shift << 1) | fake_gpio[PIN_SEG_SER]; nbits++; } sck = v; }
    if (pin == PIN_SEG_LATCH) {
        if (v && !latch) {
            latches++;
            printf("    595s latched %d bits:", nbits);
            for (int b = 6; b >= 0; b--) printf(" %02x", (unsigned)((shift >> (8 * b)) & 0xff));
            printf("   (big l3 l2 l1, leds, small s3 s2 s1)\n");
            nbits = 0; shift = 0;
        }
        latch = v;
    }
}

static int wr(const char *path, const char *text)
{
    int fd = fs_open(path, FS_RDWR);
    if (fd < 0) { printf("  %-18s open failed\n", path); return -1; }
    int r = fs_write(fd, text, strlen(text));
    fs_close(fd);
    return r;
}
static void rd(const char *path)
{
    char b[32]; int fd = fs_open(path, FS_RDWR); int n = fs_read(fd, b, sizeof b - 1); fs_close(fd);
    b[n < 0 ? 0 : n] = 0; printf("  cat %-14s -> %s", path, n < 0 ? "error\n" : b);
}

static void window(const char *power)
{
    int r = wr("/dev/heater", power);
    int on = 0, first_off = -1;
    for (int t = 0; t < TPO_TICKS; t++) {
        tpo();
        if (fake_gpio[PIN_SSR]) on++; else if (first_off < 0) first_off = t;
    }
    printf("  echo %-5s > /dev/heater: write %s, SSR on %3d of %d ticks (%4.1f s of 15), bargraph %5u of %d\n",
           power, r < 0 ? "REFUSED" : "ok", on, TPO_TICKS, on * TICK_MS / 1000.0, fake_pwm_level[PWM_SLICE_NUM][PWM_CHAN_A], PWM_WRAP_VAL);
}

int main()
{
    fake_gpio_hook = hook;
    printf("register (seg7's lamp test: all on, then blank):\n");
    dev_heater_register(); dev_relay_register(); dev_alarm_register(); dev_seg7_register();
    printf("  task \"%s\" every %u ms; lamp test took %llu ms\n", tpo_name, tpo_ms, (unsigned long long)(fake_now / 1000));

    printf("heater (TPO, %d ms window):\n", TPO_PERIOD_MS);
    window("0"); window("25"); window("50.5"); window("100"); window("150"); window("-3");
    window("on"); window("off"); window("abc");
    rd("/dev/heater");

    printf("relay (GPIO%d):\n", PIN_HEATERSAFE);
    wr("/dev/relay", "on");  printf("  on  -> pin %d\n", fake_gpio[PIN_HEATERSAFE]);
    wr("/dev/relay", "off"); printf("  off -> pin %d\n", fake_gpio[PIN_HEATERSAFE]);
    printf("  bad -> write %d\n", wr("/dev/relay", "x"));

    printf("alarm (buzzer GPIO%d 1 = sounding, LED GPIO%d 0 = lit):\n", PIN_ALARM, PIN_ALARM_LED);
    const char *a[] = { "on", "muted", "off" };
    for (const char *s : a) { wr("/dev/alarm", s); printf("  %-5s -> buzzer %d led %d\n", s, fake_gpio[PIN_ALARM], fake_gpio[PIN_ALARM_LED]); }

    printf("seg7:\n");
    printf("  echo 36.5 > /dev/seg7big\n");    wr("/dev/seg7big", "36.5");
    printf("  echo 37 > /dev/seg7small\n");    wr("/dev/seg7small", "37");
    printf("  echo on > /dev/leds/aut\n");     wr("/dev/leds/aut", "on");
    printf("  echo 'man on' > /dev/leds\n");   wr("/dev/leds", "man on");
    rd("/dev/leds/aut"); rd("/dev/leds/warm");
    printf("  echo 1234 > /dev/seg7big (only 3 digits)\n"); wr("/dev/seg7big", "1234");
    printf("  latches: %d\n", latches);
    return 0;
}
