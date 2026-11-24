/**
 * apps/babywarmer/dev/tft.cpp — /dev/tft/<name>: the ST7735 front-panel display
 *
 * Each device is a field of the screen's state; a write only stores it.
 * The "tftflush" kernel task redraws every 20 ms (blinking needs redraws
 * whether or not anything changed); a task, not a job, so `kill` can't
 * freeze the screen. tftwire (a job) fills the fields from the warmer's
 * devices.
 *
 *   aut, man       auto / manual mode
 *   chk            the guard not ok: SENSOR and warning icons blink
 *   low, high, fail  the alarm conditions: warning and ALARM icons blink;
 *                  fail also blinks the heater bar red/grey
 *   heater         the heater bar, 0-100
 *   apgar          on (re)starts the APGAR clock (the display edge-detects it)
 *   mode           graphical (the warmer screen) or text (a 21x16 console)
 *   seek           text mode: put the cursor on row 0-15
 *   text           text mode: a line at the cursor, which then moves down
 *                  (`echo Heater: 42 % > /dev/tft/text`)
 *   clear          text mode: clear the screen, cursor home
 *
 * The drawing is tft/ (a blackbox copy of the st7735 repo, see its README),
 * used only through display.h; this file and prog/tftwire are its only
 * users. dev_tft_register() starts the display (SPI, reset, backlight)
 * and checks at compile time that st7735.h's pins are the board's.
 */
#include "board.h"
#include "kernel/fs.h"
#include "kernel/task.h"
extern "C" {
#include "tft/st7735.h"  // only for the pin check below
#include "tft/display.h" // C, hence extern "C"
}
#include "pico/time.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// st7735.h hardcodes the display's pins (it's a blackbox copy, see
// tft/README.md); make sure they're the ones the board says.
static_assert(ST7735_SCK_PIN  == PIN_TFT_SCK  && ST7735_MOSI_PIN == PIN_TFT_MOSI &&
              ST7735_CS_PIN   == PIN_TFT_CS   && ST7735_DC_PIN   == PIN_TFT_DC   &&
              ST7735_RST_PIN  == PIN_TFT_RST  && ST7735_LED_PIN  == PIN_TFT_LED,
              "tft/st7735.h pins differ from board.h PIN_TFT_*");

static bool    aut = true, man = false, chk = false;
static bool    low = false, high = false, fail = false;
static uint8_t heater = 0;
static bool    apgar = false; // as tftwire wrote it; display.c edge-detects

// The text console's pending command: the fields of display.h's (anonymous)
// warmer_display_state_t.text, one for one.
static display_mode_t screen_mode = DISPLAY_MODE_GRAPHICAL;
static text_cmd_t     text_cmd    = TEXT_CMD_NONE;
static uint32_t        text_seq   = 0;
static uint8_t          text_row  = 0;
static char             text_line[TEXT_COLS + 1] = "";

#define FAIL_BLINK_MS 500
static bool            fail_blink_phase = false;
static absolute_time_t fail_blink_next;

// ---- the twelve devices --------------------------------------------------
static int bool_read(bool v, char *buf, int len)
{
    return snprintf(buf, len, v ? "on\n" : "off\n");
}

static int bool_write(bool *field, const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { *field = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { *field = false; return len; }
    return -1;
}

static int aut_read(char *buf, int len)         { return bool_read(aut, buf, len); }
static int aut_write(const char *buf, int len)  { return bool_write(&aut, buf, len); }
static int man_read(char *buf, int len)         { return bool_read(man, buf, len); }
static int man_write(const char *buf, int len)  { return bool_write(&man, buf, len); }
static int chk_read(char *buf, int len)         { return bool_read(chk, buf, len); }
static int chk_write(const char *buf, int len)  { return bool_write(&chk, buf, len); }
static int low_read(char *buf, int len)         { return bool_read(low, buf, len); }
static int low_write(const char *buf, int len)  { return bool_write(&low, buf, len); }
static int high_read(char *buf, int len)        { return bool_read(high, buf, len); }
static int high_write(const char *buf, int len) { return bool_write(&high, buf, len); }
static int fail_read(char *buf, int len)        { return bool_read(fail, buf, len); }
static int fail_write(const char *buf, int len) { return bool_write(&fail, buf, len); }
static int apgar_read(char *buf, int len)         { return bool_read(apgar, buf, len); }
static int apgar_write(const char *buf, int len)  { return bool_write(&apgar, buf, len); }

static int heater_read(char *buf, int len)
{
    return snprintf(buf, len, "%d\n", heater);
}

static int heater_write(const char *buf, int len)
{
    int v = atoi(buf);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    heater = (uint8_t)v;
    return len;
}

static int mode_read(char *buf, int len)
{
    return snprintf(buf, len, screen_mode == DISPLAY_MODE_TEXT ? "text\n" : "graphical\n");
}

static int mode_write(const char *buf, int len)
{
    if (len >= 4 && strncmp(buf, "text", 4) == 0)          { screen_mode = DISPLAY_MODE_TEXT;      return len; }
    if (len >= 9 && strncmp(buf, "graphical", 9) == 0)     { screen_mode = DISPLAY_MODE_GRAPHICAL; return len; }
    return -1;
}

// seek, text, clear: write-only, and refused unless mode is "text"
// (display.c would ignore them silently).
static int seek_write(const char *buf, int len)
{
    if (screen_mode != DISPLAY_MODE_TEXT) return -1;
    int v = atoi(buf);
    if (v < 0)              v = 0;
    if (v >= TEXT_ROWS)     v = TEXT_ROWS - 1;
    text_row = (uint8_t)v;
    text_cmd = TEXT_CMD_SEEK;
    text_seq++;
    return len;
}

// A line at the cursor; display.c moves the cursor down.
static int text_write(const char *buf, int len)
{
    if (screen_mode != DISPLAY_MODE_TEXT) return -1;
    int n = len;
    if (n > TEXT_COLS) n = TEXT_COLS;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--; // echo appends '\n'
    memcpy(text_line, buf, n);
    text_line[n] = '\0';
    text_cmd = TEXT_CMD_WRITE;
    text_seq++;
    return len;
}

static int text_read(char *buf, int len)
{
    return snprintf(buf, len, "%s\n", text_line);
}

// Any write clears the text screen and homes the cursor.
static int clear_write(const char *buf, int len)
{
    (void)buf;
    if (screen_mode != DISPLAY_MODE_TEXT) return -1;
    text_cmd = TEXT_CMD_CLEAR;
    text_seq++;
    return len;
}

static const device_t dev_tft_aut    = {"/dev/tft/aut",    0, 0, aut_read,    aut_write};
static const device_t dev_tft_man    = {"/dev/tft/man",    0, 0, man_read,    man_write};
static const device_t dev_tft_chk    = {"/dev/tft/chk",    0, 0, chk_read,    chk_write};
static const device_t dev_tft_low    = {"/dev/tft/low",    0, 0, low_read,    low_write};
static const device_t dev_tft_high   = {"/dev/tft/high",   0, 0, high_read,   high_write};
static const device_t dev_tft_fail   = {"/dev/tft/fail",   0, 0, fail_read,   fail_write};
static const device_t dev_tft_heater = {"/dev/tft/heater", 0, 0, heater_read, heater_write};
static const device_t dev_tft_apgar  = {"/dev/tft/apgar",  0, 0, apgar_read,  apgar_write};
static const device_t dev_tft_mode   = {"/dev/tft/mode",   0, 0, mode_read,   mode_write};
static const device_t dev_tft_seek   = {"/dev/tft/seek",   0, 0, 0,           seek_write};
static const device_t dev_tft_text   = {"/dev/tft/text",   0, 0, text_read,   text_write};
static const device_t dev_tft_clear  = {"/dev/tft/clear",  0, 0, 0,           clear_write};

static void tft_init(void);
static void tft_flush_task(void);

void dev_tft_register(void)
{
    tft_init();
    task_register("tftflush", tft_flush_task, 20);   // a task: `kill` can't freeze the screen
    fs_register(&dev_tft_aut);
    fs_register(&dev_tft_man);
    fs_register(&dev_tft_chk);
    fs_register(&dev_tft_low);
    fs_register(&dev_tft_high);
    fs_register(&dev_tft_fail);
    fs_register(&dev_tft_heater);
    fs_register(&dev_tft_apgar);
    fs_register(&dev_tft_mode);
    fs_register(&dev_tft_seek);
    fs_register(&dev_tft_text);
    fs_register(&dev_tft_clear);
}

// SPI, GPIOs, the panel's init sequence; ~0.5 s, at boot only.
static void tft_init(void)
{
    display_init();
    fail_blink_next = make_timeout_time_ms(FAIL_BLINK_MS);
}

// Every 20 ms: hand the fields to display.c, which redraws what changed.
static void tft_flush_task(void)
{
    if (absolute_time_diff_us(get_absolute_time(), fail_blink_next) < 0) {
        fail_blink_next  = make_timeout_time_ms(FAIL_BLINK_MS);
        fail_blink_phase = !fail_blink_phase;
    }

    warmer_display_state_t state;
    memset(&state, 0, sizeof(state));

    state.mode = aut ? MODE_AUTO : MODE_MANUAL;

    // chk: SENSOR and warning icons blink (display.c's blink timer).
    state.sensor_connected = !chk;
    state.warning          = chk;

    // low, high, fail: one alarm to the display (warning + ALARM icons).
    bool any_fault = low || high || fail;
    state.warning |= any_fault;
    state.alarm    = any_fault;

    if      (low)  state.baby = BABY_COLD;
    else if (high) state.baby = BABY_HOT;
    else           state.baby = BABY_OK;

    if (fail) {
        state.heater_percent = 0;
        state.heater_failed  = fail_blink_phase; // true=red phase, false=grey(0%) phase
    } else {
        state.heater_percent = heater;
        state.heater_failed  = false;
    }
    state.heater_on = state.heater_percent > 0;

    // display.c owns the APGAR clock; it starts it on a rising edge.
    state.apgar_start = apgar;

    // The text console's command, as staged by the writes above;
    // display.c acts on a new text.seq.
    state.screen_mode = screen_mode;
    state.text.cmd    = text_cmd;
    state.text.seq    = text_seq;
    state.text.row    = text_row;
    strncpy(state.text.line, text_line, TEXT_COLS);
    state.text.line[TEXT_COLS] = '\0';

    display_update(&state, to_ms_since_boot(get_absolute_time()));
}
