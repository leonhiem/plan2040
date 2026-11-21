/**
 * boards/bringup.cpp — first-power-up test for a board
 *
 * Written for the cpu_rp2040 PCB (2026-11-10), shared by every board
 * since 2026-11-21: what it tests comes from the board's board.h. A
 * board with a second I2C bus (HEADER_I2C) gets that bus scanned too, a
 * board with an EEPROM (EEPROM_I2C_ADDR) gets the EEPROM test.
 *
 * Standalone, no plan2040 kernel: proves the board itself before the OS
 * moves onto it. On boot it runs every test once and prints a summary,
 * then stays in a small command loop over USB serial:
 *
 *   all                          run every test again
 *   scan                         I2C bus scan (expect 0x50 FRAM, 0x51 RTC;
 *                                header bus: 0x48 ADS1115, 0x52 EEPROM)
 *   fram                         non-destructive FRAM test (all 8 KB)
 *   rtc                          RTC test: registers sane, clock ticking
 *   eeprom                       EEPROM test: one page written and restored,
 *                                address lines (only with an EEPROM)
 *   time                         print RTC date/time once
 *   set YYYY-MM-DD HH:MM:SS      set the RTC
 *   help
 *
 * Meanwhile the alarm LED blinks a 1 Hz heartbeat and every button
 * press/release is printed. Every other output stays off
 * (board_pins_init()): bringup never switches a heater.
 */

#include <pico/stdlib.h>
#include <cstdio>
#include <cstring>
#include "hardware/i2c.h"
#include "board.h"
#include "drivers/fm24cl64.h"
#include "drivers/pcf8563.h"
#ifdef EEPROM_I2C_ADDR
#include "drivers/at24c.h"
#endif

#ifndef PLAN2040_BOARD          // set by CMakeLists.txt from the config
#define PLAN2040_BOARD "?"
#endif

#define I2C_TIMEOUT_US 10000

// ---------------------------------------------------------------- I2C

static bool i2c_reserved(uint8_t addr)
{
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

struct chip { i2c_inst_t *i2c; uint8_t addr; const char *name; };

static const chip chips[] = {
    { FRAM_I2C,    FRAM_I2C_ADDR,    "FRAM"    },
    { RTC_I2C,     RTC_I2C_ADDR,     "RTC"     },
#ifdef ADS1115_I2C_ADDR
    { ADS1115_I2C, ADS1115_I2C_ADDR, "ADS1115" },
#endif
#ifdef EEPROM_I2C_ADDR
    { EEPROM_I2C,  EEPROM_I2C_ADDR,  "EEPROM"  },
#endif
};
#define NCHIPS (int)(sizeof(chips) / sizeof(chips[0]))

// Scan one bus; true if every chip board.h puts on it answered.
static bool scan_bus(i2c_inst_t *i2c, const char *name, int sda, int scl)
{
    bool seen[0x80] = { false };
    int found = 0;
    printf("i2c scan%s%s (SDA=GPIO%d SCL=GPIO%d):", name[0] ? " " : "", name, sda, scl);
    for (int addr = 0; addr < 0x80; addr++) {
        if (i2c_reserved(addr))
            continue;
        uint8_t b;
        if (i2c_read_timeout_us(i2c, addr, &b, 1, false, I2C_TIMEOUT_US) >= 0) {
            printf(" 0x%02x", addr);
            found++;
            seen[addr] = true;
        }
    }
    printf("%s\n", found ? "" : " nothing found");
    bool ok = true;
    for (int i = 0; i < NCHIPS; i++) {
        if (chips[i].i2c == i2c && !seen[chips[i].addr]) {
            printf("  %s not found at 0x%02x\n", chips[i].name, chips[i].addr);
            ok = false;
        }
    }
    return ok;
}

static bool test_scan(void)
{
#ifdef HEADER_I2C
    bool local  = scan_bus(BOARD_I2C, "local", BOARD_I2C_SDA, BOARD_I2C_SCL);
    bool header = scan_bus(HEADER_I2C, "header", HEADER_I2C_SDA, HEADER_I2C_SCL);
    return local && header;
#else
    return scan_bus(BOARD_I2C, "", BOARD_I2C_SDA, BOARD_I2C_SCL);
#endif
}

// ---------------------------------------------------------------- FRAM
// drivers/fm24cl64.h does the I2C.

static uint8_t fram_pattern(uint16_t addr)
{
    return (uint8_t)(addr * 7 + 0x5a) ^ (uint8_t)(addr >> 8);
}

// Every cell: save, write pattern, verify, write inverse, verify, restore.
static bool fram_test_cells(void)
{
    const int CHUNK = 32;
    uint8_t orig[CHUNK], pat[CHUNK], got[CHUNK];
    int errors = 0;

    for (int base = 0; base < FRAM_SIZE; base += CHUNK) {
        if (!fram_read(base, orig, CHUNK)) {
            printf("  i2c error reading 0x%04x\n", base);
            return false;
        }
        for (int inv = 0; inv < 2; inv++) {
            for (int i = 0; i < CHUNK; i++)
                pat[i] = fram_pattern(base + i) ^ (inv ? 0xff : 0x00);
            if (!fram_write(base, pat, CHUNK) || !fram_read(base, got, CHUNK)) {
                printf("  i2c error at 0x%04x\n", base);
                fram_write(base, orig, CHUNK);
                return false;
            }
            for (int i = 0; i < CHUNK; i++) {
                if (got[i] != pat[i] && errors++ < 8)
                    printf("  0x%04x: wrote 0x%02x read 0x%02x\n", base + i, pat[i], got[i]);
            }
        }
        if (!fram_write(base, orig, CHUNK) || !fram_read(base, got, CHUNK)
            || memcmp(orig, got, CHUNK) != 0) {
            printf("  could not restore 0x%04x\n", base);
            return false;
        }
    }
    if (errors)
        printf("  %d bad bytes\n", errors);
    return errors == 0;
}

// Address lines: a unique marker at 0 and at every power of two. If an
// address bit is stuck or the chip is smaller than expected, markers
// overwrite each other. The cell test above can't see that, since it
// restores each chunk before moving on.
static bool fram_test_aliasing(void)
{
    uint16_t addrs[16];
    uint8_t orig[16];
    int n = 0;
    bool ok = true;

    addrs[n++] = 0;
    for (uint16_t a = 1; a < FRAM_SIZE; a <<= 1)
        addrs[n++] = a;

    for (int i = 0; i < n; i++)
        if (!fram_read(addrs[i], &orig[i], 1))
            return false;
    for (int i = 0; i < n; i++) {
        uint8_t m = 0xa0 + i;
        if (!fram_write(addrs[i], &m, 1))
            ok = false;
    }
    for (int i = 0; i < n; i++) {
        uint8_t m;
        if (!fram_read(addrs[i], &m, 1) || m != 0xa0 + i) {
            printf("  address 0x%04x aliased (read 0x%02x)\n", addrs[i], m);
            ok = false;
        }
    }
    for (int i = 0; i < n; i++)
        fram_write(addrs[i], &orig[i], 1);
    return ok;
}

static bool test_fram(void)
{
    printf("fram: testing %d bytes (non-destructive)...\n", FRAM_SIZE);
    absolute_time_t t0 = get_absolute_time();
    bool ok = fram_test_cells() && fram_test_aliasing();
    printf("fram: %s (%lld ms)\n", ok ? "ok" : "FAILED",
           absolute_time_diff_us(t0, get_absolute_time()) / 1000);
    return ok;
}

// ---------------------------------------------------------------- RTC
//
// drivers/pcf8563.h does the I2C. No rtc_init() here on purpose: the
// test wants to see the chip's raw power-on state.

static void print_time(const rtc_time *t)
{
    static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "???" };
    printf("%04d-%02d-%02d %02d:%02d:%02d %s%s\n", t->year, t->month, t->day,
           t->hour, t->min, t->sec, wd[t->weekday], t->vl ? "  (VL: time not trusted, use set)" : "");
}

static bool test_rtc(void)
{
    uint8_t ctl[2];
    rtc_time a, b;

    if (!rtc_read_regs(0x00, ctl, 2) || !rtc_get(&a)) {
        printf("rtc: i2c error\nrtc: FAILED\n");
        return false;
    }
    printf("rtc: control_1=0x%02x control_2=0x%02x\n", ctl[0], ctl[1]);
    printf("rtc: ");
    print_time(&a);
    if (ctl[0] & 0x20) {
        printf("rtc: STOP bit set, clock halted (set clears it)\nrtc: FAILED\n");
        return false;
    }
    if (!rtc_valid(&a)) {
        printf("rtc: registers out of range\nrtc: FAILED\n");
        return false;
    }

    // Must tick within ~1s; allow 2.5s.
    absolute_time_t deadline = make_timeout_time_ms(2500);
    do {
        sleep_ms(50);
        if (!rtc_get(&b)) {
            printf("rtc: i2c error\nrtc: FAILED\n");
            return false;
        }
    } while (b.sec == a.sec && !time_reached(deadline));

    if (b.sec == a.sec) {
        printf("rtc: seconds not advancing (crystal?)\nrtc: FAILED\n");
        return false;
    }
    printf("rtc: ticking (%02d -> %02d)\n", a.sec, b.sec);
    printf("rtc: %s\n", a.vl ? "ok, but VL set: time needs setting" : "ok");
    return true;
}

// ---------------------------------------------------------------- EEPROM
#ifdef EEPROM_I2C_ADDR
// drivers/at24c.h does the I2C. Unlike the FRAM, an EEPROM wears
// (~1M writes per cell), so this doesn't touch every cell: one page
// written twice and restored, plus one marker byte per address line.
// That's ~20 page writes per run, nothing against a million.

static bool eeprom_test_page(void)
{
    const uint32_t base = EEPROM_SIZE - EEPROM_PAGE;    // the last page
    uint8_t orig[EEPROM_PAGE], pat[EEPROM_PAGE], got[EEPROM_PAGE];
    int errors = 0;

    if (!eeprom_read(base, orig, EEPROM_PAGE)) {
        printf("  i2c error reading 0x%05lx\n", (unsigned long)base);
        return false;
    }
    for (int inv = 0; inv < 2; inv++) {
        for (int i = 0; i < EEPROM_PAGE; i++)
            pat[i] = (uint8_t)(i * 7 + 0x5a) ^ (inv ? 0xff : 0x00);
        if (!eeprom_write(base, pat, EEPROM_PAGE) || !eeprom_read(base, got, EEPROM_PAGE)) {
            printf("  i2c error at 0x%05lx\n", (unsigned long)base);
            eeprom_write(base, orig, EEPROM_PAGE);
            return false;
        }
        for (int i = 0; i < EEPROM_PAGE; i++)
            if (got[i] != pat[i] && errors++ < 8)
                printf("  0x%05lx: wrote 0x%02x read 0x%02x\n", (unsigned long)(base + i), pat[i], got[i]);
    }
    if (!eeprom_write(base, orig, EEPROM_PAGE) || !eeprom_read(base, got, EEPROM_PAGE)
        || memcmp(orig, got, EEPROM_PAGE) != 0) {
        printf("  could not restore 0x%05lx\n", (unsigned long)base);
        return false;
    }
    if (errors)
        printf("  %d bad bytes\n", errors);
    return errors == 0;
}

// Same idea as fram_test_aliasing(): markers at 0 and every power of two.
// A 24C256 fitted where board.h says 24C512 shows up here: 0x8000 then
// lands on 0x0000.
static bool eeprom_test_aliasing(void)
{
    uint32_t addrs[17];
    uint8_t orig[17];
    int n = 0;
    bool ok = true;

    addrs[n++] = 0;
    for (uint32_t a = 1; a < EEPROM_SIZE; a <<= 1)
        addrs[n++] = a;

    for (int i = 0; i < n; i++)
        if (!eeprom_read(addrs[i], &orig[i], 1))
            return false;
    for (int i = 0; i < n; i++) {
        uint8_t m = 0xa0 + i;
        if (!eeprom_write(addrs[i], &m, 1))
            ok = false;
    }
    for (int i = 0; i < n; i++) {
        uint8_t m;
        if (!eeprom_read(addrs[i], &m, 1) || m != 0xa0 + i) {
            printf("  address 0x%05lx aliased (read 0x%02x)\n", (unsigned long)addrs[i], m);
            ok = false;
        }
    }
    for (int i = 0; i < n; i++)
        eeprom_write(addrs[i], &orig[i], 1);
    return ok;
}

static bool test_eeprom(void)
{
    printf("eeprom: testing %d bytes, %d-byte pages (non-destructive)...\n", EEPROM_SIZE, EEPROM_PAGE);
    absolute_time_t t0 = get_absolute_time();
    bool ok = eeprom_test_page() && eeprom_test_aliasing();
    printf("eeprom: %s (%lld ms)\n", ok ? "ok" : "FAILED",
           absolute_time_diff_us(t0, get_absolute_time()) / 1000);
    return ok;
}
#endif

// ---------------------------------------------------------------- buttons

static const struct { int pin; const char *name; } buttons[] = {
    { BUTTON_PIN_MUTE,   "mute"   },
    { BUTTON_PIN_UP,     "up"     },
    { BUTTON_PIN_DOWN,   "down"   },
    { BUTTON_PIN_MANUAL, "manual" },
    { BUTTON_PIN_START,  "start"  },
    { BUTTON_PIN_LAMP,   "lamp"   },
};
#define NBUTTONS (sizeof(buttons) / sizeof(buttons[0]))

static bool btn_stable[NBUTTONS], btn_prev[NBUTTONS];

static void buttons_setup(void)
{
    for (unsigned i = 0; i < NBUTTONS; i++) {
        gpio_init(buttons[i].pin);
        gpio_pull_up(buttons[i].pin);
    }
}

// Called every 20ms: two equal samples in a row = debounced.
static void buttons_poll(void)
{
    for (unsigned i = 0; i < NBUTTONS; i++) {
        bool pressed = !gpio_get(buttons[i].pin);
        if (pressed == btn_prev[i] && pressed != btn_stable[i]) {
            btn_stable[i] = pressed;
            printf("button %s %s\n", buttons[i].name, pressed ? "pressed" : "released");
        }
        btn_prev[i] = pressed;
    }
}

// ---------------------------------------------------------------- main

static void run_all(void)
{
    bool scan = test_scan();
    bool fram = test_fram();
    bool rtc = test_rtc();
#ifdef EEPROM_I2C_ADDR
    bool eeprom = test_eeprom();
    printf("\nsummary: i2c %s, fram %s, rtc %s, eeprom %s\n\n",
           scan ? "ok" : "FAIL", fram ? "ok" : "FAIL", rtc ? "ok" : "FAIL", eeprom ? "ok" : "FAIL");
#else
    printf("\nsummary: i2c %s, fram %s, rtc %s\n\n",
           scan ? "ok" : "FAIL", fram ? "ok" : "FAIL", rtc ? "ok" : "FAIL");
#endif
}

static void help(void)
{
#ifdef EEPROM_I2C_ADDR
    const char *more = " eeprom";
#else
    const char *more = "";
#endif
    printf("commands: all scan fram rtc%s time help\n"
           "          set YYYY-MM-DD HH:MM:SS\n"
           "press any button to see it reported\n", more);
}

static void dispatch(char *line)
{
    rtc_time t;
    if (!strcmp(line, "all"))       run_all();
    else if (!strcmp(line, "scan")) test_scan();
    else if (!strcmp(line, "fram")) test_fram();
    else if (!strcmp(line, "rtc"))  test_rtc();
#ifdef EEPROM_I2C_ADDR
    else if (!strcmp(line, "eeprom")) test_eeprom();
#endif
    else if (!strcmp(line, "time")) {
        if (rtc_get(&t)) print_time(&t);
        else printf("rtc: i2c error\n");
    } else if (!strncmp(line, "set ", 4)) {
        if (!rtc_parse(line + 4, &t))
            printf("usage: set YYYY-MM-DD HH:MM:SS\n");
        else if (!rtc_set(&t))
            printf("rtc: i2c error\n");
        else if (rtc_get(&t))
            print_time(&t);
    } else if (!strcmp(line, "help")) help();
    else if (line[0]) printf("?%s\n", line);
}

int main(void)
{
    stdio_init_all();

    board_pins_init(); // every output off (LED, lamp; on babywarmer_v1 the heater too)

    buttons_setup();
    board_i2c_init();

    // Give a terminal a moment to attach so the boot report isn't lost.
    absolute_time_t wait = make_timeout_time_ms(5000);
    while (!stdio_usb_connected() && !time_reached(wait))
        sleep_ms(50);
    sleep_ms(200);

    printf("\n%s bring-up\n\n", PLAN2040_BOARD);
    run_all();
    help();

    char line[64];
    int len = 0;
    absolute_time_t next_blink = get_absolute_time();
    absolute_time_t next_poll = get_absolute_time();
    bool led = false;

    printf("> ");
    for (;;) {
        if (time_reached(next_blink)) {
            led = !led;
            gpio_put(PIN_ALARM_LED, !led);
            next_blink = make_timeout_time_ms(500);
        }
        if (time_reached(next_poll)) {
            buttons_poll();
            next_poll = make_timeout_time_ms(20);
        }

        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT)
            continue;
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[len] = 0;
            dispatch(line);
            len = 0;
            printf("> ");
        } else if ((c == 0x08 || c == 0x7f) && len > 0) {
            len--;
            printf("\b \b");
        } else if (c >= 0x20 && c < 0x7f && len < (int)sizeof(line) - 1) {
            line[len++] = c;
            putchar(c);
        }
    }
}
