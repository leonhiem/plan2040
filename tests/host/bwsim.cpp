// bwsim: the whole babywarmer controller on the PC, 135 minutes of
// simulated time. The real kernel scheduler (task.cpp), shell, jobs, the
// real babywarmer boot script, every real device and program, and the
// guard (dev/guard.cpp, the kernel task that owns the heater safety);
// only the sensors are fakes, fed by a thermal model fitted to the first
// logged warm-up of plan2040 on the board (2026-11-26,
// plan2040-logs/warmup-2026-11-26.log: 50 minutes of boost, coast and PID
// at 37.0 C; rms error 0.18 C):
//
//   power -> 3 lags of 51 s each (heater, mattress) -> skin
//   skin' = 0.0775 C/s * lagged power (0..1) - (skin - room) / 347 s
//
// so the skin rises ~2 minutes after the heater goes on, and keeps rising
// through the coast after it goes off, as on the board. Its steady state,
// 3.72 % per C of (skin - room), is the feed-forward's FF_K. The heat only
// flows with the relay on and the element whole (or the SSR stuck on).
// (Until 2026-11-26 the model was fitted to the field warmer's log of 5
// April, warmerlog7: a heavier plant, ~4.1 %/C.)
//
// Story (each step with what the guard should do):
//   0:00  power-up in auto (as the field warmer); 0:02 MANUAL -> manual
//         10 %; the skin sensor is NOT on the baby (reads the room)
//   1:00  MANUAL -> auto: boost; scenario (2), sensor out during warm-up:
//         "no rise" after 5 min -> safe, heating from the room (33.5 %)
//  14:00  the sensor goes on the baby -> ok after 2 min plausible, warmer's pid
//         (feed-forward 30 % + the PID's correction, step 5)
//  20:00  UP 10 times -> setpoint 36.0 (+1.0: no re-boost)
//  29:00  `monitor`: the field warmer's telemetry line
//  30:00  scenario (1): the sensor falls off while warm -> safe; 33:00 back
//  40:00  `kill 5` (follow, the job that writes /dev/heater): control dead
//         -> safe after 10 s; 43:00 the job started again -> ok
//  50:00  START (APGAR), `run tftmon`; 52:00 back to the warmer screen
//  55:00  the heater element breaks; 57:00 works again
//  60:00  the SSR sticks on -> relay held off; 62:00 replaced; 63:00 released
//  66:00  UP 20 times, 1 s apart -> the setpoint stops at 37.5 (gap d:
//         30-37.5 C; above only by holding UP 3 s, tests/host/sptest);
//         the skin within 3 C: no re-boost, the PID climbs
//  75:00  the room NTC opens: /dev/alarm/room "warn" (CHK, no buzzer),
//         the PID runs on the last room seen (24 C after 10 min); 88:00 back
//  90:00  MANUAL -> manual at 10 % (step 5: always); 91:00 UP -> 15 %;
//         92:00 MANUAL -> auto
// 100:00  the ADS1115 stops answering -> none: manual at the room table's
//         power; 102:00 UP (+5 %); 104:00 `jobs`; 105:00 back -> auto by
//         itself, and with the feed-forward no undershoot (before step 5 the
//         PID started from picoos's 3 % alone: a false "safe" at 108:58)
// 110:00  MANUAL -> manual, UP to 30 %: the check timer (IEC gap b): at
//         12 min "check the baby", 3 min later unanswered -> heater off;
//         127:00 MUTE answers -> heater back; 130:00 DOWN to 20 %: no timer
// Gap c, the deviation from the setpoint: 22:00-26:00 a phototherapy lamp
// (HIGH warns at 1 C over, alarms at 2 C, the relay drops at 3 C);
// 44:00-49:00 a draught (LOW warns at 1 C under, alarms at 2 C).
// Real deviations the model shows: 44:00 the draught drops the skin to
// LOW "on" (the PID, Kp 3 %/C, Ti 200 s, can't follow it); afterwards back
// to 36.0 without overshoot (the integral only moves within 1 C of the
// setpoint, /dev/pid/band: before it, the skin overshot to 37.3 and to 38.6
// after the raise at 66:00). 58:05 LOW after the broken element, 63:22
// after the relay was held off for the stuck SSR; 93:36 LOW while the PID
// climbs after manual mode. Recovering from safe (16:01, 35:01) is a new
// warm-up: no LOW right after it.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include <unistd.h>
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include "kernel/errstr.h"
#include "drivers/buttons.h"
#include "board.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"

uint64_t fake_now = 0;
int fake_gpio[30];
void (*fake_gpio_hook)(uint pin, int value);
uint16_t fake_pwm_level[8][2];
i2c_inst_t *fake_i2c0, *fake_i2c1;
spi_inst_t *fake_spi0;
unsigned long fake_spi_bytes;
volatile bool button[BUTTON_COUNT];
volatile uint32_t button_presses[BUTTON_COUNT];
static void press(int b) { button[b] = true; button_presses[b]++; }   // as the driver does
// Typed input: type("...") queues a line for the shell.
static char typed[256]; static int typed_pos, typed_len;
static void type(const char *line) { typed_len = snprintf(typed, sizeof typed, "%s", line); typed_pos = 0; }
int getchar_timeout_us(uint32_t) { return typed_pos < typed_len ? (unsigned char)typed[typed_pos++] : -1; }

// ---- the world
static const float ROOM = 27.0f, LAG_S = 50.7f, GAIN = 0.0775f, LOSS_S = 347.0f;
static float skin = 27.0f;       // what's under the sensor
static float lag1, lag2, lag3;   // the heater's and the mattress's warmth, 0..1
static bool sensor_on_baby = false, adc_dead = false, room_dead = false;
static float draught = 1.0f, lamp = 0.0f;   // more heat loss; an extra heat source (C/s)
static bool element_broken = false, ssr_stuck = false;   // heater faults
static float heater_now(void)
{
    int fd = fs_open("/dev/heater"); char b[16]; int n = fs_read(fd, b, 15); fs_close(fd);
    b[n < 0 ? 0 : n] = 0; return strtof(b, 0);
}

// ---- fake sensor devices (in place of apps/babywarmer/dev/skintemp, ambient, current)
static int skin_read(char *buf, int len)
{
    if (adc_dead) { werrstr("ADS1115 (0x%02x) not answering", ADS1115_I2C_ADDR); return -1; }
    return snprintf(buf, len, "%.1f\n", sensor_on_baby ? skin : ROOM);
}
static int amb_read(char *buf, int len)
{
    if (room_dead) { werrstr("code 32767 is outside the calibrated range (open or shorted?)"); return -1; }
    if (adc_dead) { werrstr("ADS1115 (0x%02x) not answering", ADS1115_I2C_ADDR); return -1; }
    return snprintf(buf, len, "%.1f\n", ROOM);
}
static int cur_read(char *buf, int len)
{   // current flows while the SSR conducts (or is stuck on) and the relay is
    // energized, unless the element is broken
    bool flows = fake_gpio[PIN_HEATERSAFE] && (fake_gpio[PIN_SSR] || ssr_stuck) && !element_broken;
    return snprintf(buf, len, "%d\n", flows ? 800 : 5);
}
static const device_t d_skin = {"/dev/skintemp", 0, 0, skin_read, 0};
static const device_t d_amb  = {"/dev/ambient",  0, 0, amb_read,  0};
static const device_t d_cur  = {"/dev/current",  0, 0, cur_read,  0};
static int date_run(const char*, int, int, char**, char *out, int outlen) { return snprintf(out, outlen, "Sun Nov 22 10:00:00 2026\n"); }
static const program_t p_date = {"date", date_run};

void task_shell(void); void jobs_init(void);
#define D(n) void dev_##n##_register(void);
#define P(n) void prog_##n##_register(void);
D(lamp) D(buttons) D(heater) D(relay) D(alarm) D(seg7) D(setpoint) D(heaterauto) D(percent) D(warmer) D(guard) D(tft)
P(cat) P(echo) P(label) P(follow) P(toggle) P(adjust) P(select) P(warmer) P(alarm) P(ledwire) P(monitor) P(tftwire)

// The sim's own lines go to `out` (the real stdout); what the shell and
// the jobs print goes to stdout, which is sent to a scratch file while
// the ADS1115 is dead (two failing jobs print "cat: error" every 150 ms)
// and counted instead.
static FILE *out;
static FILE *quiet;
static int saved_stdout = -1;
static void sim_printf_begin(void) { fflush(stdout); }
#define SAY(...) do { sim_printf_begin(); fprintf(out, __VA_ARGS__); fflush(out); } while (0)

static void rd(const char *path, char *b, int len)
{
    int fd = fs_open(path); int n = fd < 0 ? -1 : fs_read(fd, b, len - 1); if (fd >= 0) fs_close(fd);
    if (n < 0) strcpy(b, "?"); else { b[n] = 0; char *nl = strchr(b, '\n'); if (nl) *nl = 0; }
}
// The 74HC595 chain: what the displays show, decoded back from the bits.
static unsigned seg_big;                 // last 3 digit bytes latched for the big display
static uint64_t seg_all;                 // all 56 bits of the last latch
static char big_lit[8] = "";             // the big display's last non-blank text (it may blink)
static uint64_t sh;
static void decode(uint64_t r, char *big, char *small)
{   // seg7.cpp's tables; bit 0 is the dot after the middle digit
    static const uint8_t L[10] = {0xFC,0x60,0xDA,0xF2,0x66,0xB6,0xBE,0xE4,0xFE,0xF6};
    static const uint8_t S[10] = {0xBE,0x18,0x76,0x7C,0xD8,0xEC,0xEE,0xB8,0xFE,0xFC};
    auto dig = [](uint8_t b, const uint8_t *t, bool *dot) -> char {
        for (int d = 0; d < 10; d++) if (t[d] == b) { *dot = false; return '0' + d; }
        for (int d = 0; d < 10; d++) if ((t[d] | 1) == b) { *dot = true; return '0' + d; }
        *dot = b & 1; return b > 1 ? '?' : ' ';
    };
    uint8_t by[7]; for (int i = 0; i < 7; i++) by[i] = (r >> (8 * (6 - i))) & 0xff; // l3 l2 l1 leds s3 s2 s1
    for (int k = 0; k < 2; k++) {
        const uint8_t *t = k ? S : L; int o = k ? 4 : 0; char *out = k ? small : big; bool d1, d2, d3;
        char c1 = dig(by[o + 2], t, &d1), c2 = dig(by[o + 1], t, &d2), c3 = dig(by[o], t, &d3);
        int n = 0; out[n++] = c1; out[n++] = c2; if (d2) out[n++] = '.'; out[n++] = c3; out[n] = 0;
        char *a = out; while (*a == ' ') a++; memmove(out, a, strlen(a) + 1);   // trim the left
    }
}
static void hook(uint pin, int v)
{ static int sck, lat; if (pin == PIN_SEG_SCK) { if (v && !sck) sh = (sh << 1) | fake_gpio[PIN_SEG_SER]; sck = v; }
  if (pin == PIN_SEG_LATCH) { if (v && !lat) { seg_big = (unsigned)(sh >> 32) & 0xffffff; seg_all = sh;
        char b[8], sm[8]; decode(sh, b, sm); if (b[0]) strcpy(big_lit, b); } lat = v; } }

static void status(int sec)
{
    char st[12], sp[12], ha[8], pc[12], po[12], ah[8], th[8], tl[8], gd[8];
    rd("/dev/state", st, sizeof st); rd("/dev/setpoint", sp, sizeof sp); rd("/dev/heaterauto", ha, sizeof ha);
    rd("/dev/percent", pc, sizeof pc); rd("/dev/autopower", po, sizeof po);
    char ff[12]; rd("/dev/pid/ff", ff, sizeof ff);
    rd("/dev/guard", gd, sizeof gd); { char *c = strchr(gd, ':'); if (c) *c = 0; }
    rd("/dev/alarm/heater", ah, sizeof ah); rd("/dev/alarm/temphigh", th, sizeof th); rd("/dev/alarm/templow", tl, sizeof tl);
    char *al[] = { ah, th, tl };
    for (char *a : al) { char *c = strchr(a, ':'); if (c) *c = 0; }   // the reason is logged separately
    char big[8], small[8], blink[8], chk[8];
    decode(seg_all, big, small);
    rd("/dev/seg7big/blink", blink, sizeof blink); rd("/dev/leds/chk", chk, sizeof chk);
    char face[8]; rd("/dev/tft/face", face, sizeof face);
    char bigs[16];
    if (!strcmp(blink, "on")) snprintf(bigs, sizeof bigs, "%s~", big_lit);   // ~ = blinking
    else snprintf(bigs, sizeof bigs, "%s", big[0] ? big : "-");               // - = blank
    SAY("%3d:%02d  auto %-3s %-5s guard %-4s set %-4s skin %5.2f%s heater %5.1f ff %-4s autopower %-6s relay %d buzzer %d  big %-5s small %-4s chk %-3s alarms heater %-3s high %-3s low %-3s face %s\n",
           sec / 60, sec % 60, ha, st, gd, sp, skin, sensor_on_baby ? " " : "*", heater_now(), ff, po,
           fake_gpio[PIN_HEATERSAFE], fake_gpio[PIN_ALARM], bigs, small[0] ? small : "-", chk, ah, th, tl, face);
}

// End of the quiet window: stdout back, and what the jobs printed meanwhile.
static void unquiet(void)
{
    fflush(stdout); dup2(saved_stdout, 1);
    rewind(quiet); char line[128]; int n = 0, errs = 0;
    while (fgets(line, sizeof line, quiet)) { n++; if (!strcmp(line, "cat: error\n")) errs++; }
    SAY("while the ADS1115 was dead: %d lines printed by the jobs, %d of them \"cat: error\"%s\n", n, errs, n <= 20 ? ":" : "");
    if (n <= 20) { rewind(quiet); while (fgets(line, sizeof line, quiet)) SAY("    | %s", line); }
    fclose(quiet); quiet = 0;
}

int main()
{
    fake_gpio_hook = hook;
    out = fdopen(dup(1), "w");
    fake_gpio[PIN_ALARM_LED] = 1;
    fs_register(&d_skin); fs_register(&d_amb); fs_register(&d_cur);
    dev_lamp_register(); dev_buttons_register(); dev_heater_register(); dev_relay_register(); dev_alarm_register();
    dev_seg7_register(); dev_setpoint_register(); dev_heaterauto_register(); dev_percent_register();
    dev_warmer_register(); dev_guard_register(); dev_tft_register();
    prog_register(&p_date);
    prog_cat_register(); prog_echo_register(); prog_follow_register(); prog_toggle_register(); prog_adjust_register();
    prog_select_register(); prog_warmer_register();
    prog_alarm_register(); prog_ledwire_register(); prog_monitor_register();
    prog_label_register(); prog_tftwire_register();
    jobs_init();
    task_register("shell", task_shell, 30);

    const uint64_t STEP = 10000;  // 10 ms of simulated time per loop
    uint64_t next_sec = 1000000;
    int sec = 0;
    while (sec < 135 * 60) {
        task_run();
        fake_now += STEP;
        if (fake_now < next_sec) continue;
        next_sec += 1000000; sec++;
        // the world, once a second
        {   // the heat that really flows: relay on, element whole (or the SSR stuck on)
            bool relay = fake_gpio[PIN_HEATERSAFE];
            float p = (!relay || element_broken) ? 0.0f : ssr_stuck ? 1.0f : heater_now() / 100.0f;
            lag1 += (p - lag1) / LAG_S; lag2 += (lag1 - lag2) / LAG_S; lag3 += (lag2 - lag3) / LAG_S;
            skin += GAIN * lag3 - (skin - ROOM) / LOSS_S * draught + lamp;
        }
        if (sec == 2)        { SAY("--- 0:02 press MANUAL (power-up is auto since 2026-11-26; -> manual 10 %%)\n"); button[BUTTON_MANUAL] = true; }
        if (sec == 60)       { SAY("--- 1:00 press MANUAL (-> auto); the skin sensor is not on the baby\n"); button[BUTTON_MANUAL] = true; }
        if (sec == 14 * 60)  { SAY("--- 14:00 the skin sensor goes on the baby\n"); sensor_on_baby = true; }
        if (sec == 20 * 60)  SAY("--- 20:00 press UP 10 times (0.1 C each: setpoint 36.0)\n");
        if (sec >= 20 * 60 && sec < 20 * 60 + 10) button[BUTTON_UP] = true;
        if (sec == 22 * 60)  { SAY("--- 22:00 a phototherapy lamp over the baby: more heat than the PID can take away\n"); lamp = 0.035f; }
        if (sec == 26 * 60)  { SAY("--- 26:00 the lamp off\n"); lamp = 0.0f; }
        if (sec == 29 * 60)  { SAY("--- 29:00 type `monitor` (the field warmer's telemetry line)\n"); type("monitor\n"); }
        if (sec == 30 * 60)  { SAY("--- 30:00 the skin sensor falls off the baby (reads the room)\n"); sensor_on_baby = false; }
        if (sec == 33 * 60)  { SAY("--- 33:00 the skin sensor back on the baby\n"); sensor_on_baby = true; }
        if (sec == 40 * 60)  { SAY("--- 40:00 type `kill 5` (follow ... /dev/heater: the job that sets the power)\n"); type("kill 5\n"); }
        if (sec == 43 * 60)  { SAY("--- 43:00 start it again\n"); type("follow /dev/heaterauto /dev/autopower /dev/percent /dev/heater &\n"); }
        if (sec == 44 * 60)  { SAY("--- 44:00 a draught: heat loss x1.8 (more than the PID can follow)\n"); draught = 1.8f; }
        if (sec == 49 * 60)  { SAY("--- 49:00 the draught stops\n"); draught = 1.0f; }
        if (sec == 50 * 60)  { SAY("--- 50:00 press START (APGAR clock); type `run tftmon`\n"); button[BUTTON_START] = true; type("run tftmon\n"); }
        if (sec == 52 * 60)  { SAY("--- 52:00 type `echo graphical > /dev/tft/mode`\n"); type("echo graphical > /dev/tft/mode\n"); }
        if (sec == 55 * 60)  { SAY("--- 55:00 the heater element breaks (no current)\n"); element_broken = true; }
        if (sec == 57 * 60)  { SAY("--- 57:00 the heater element works again\n"); element_broken = false; }
        if (sec == 60 * 60)  { SAY("--- 60:00 the SSR sticks on (conducts while switched off)\n"); ssr_stuck = true; }
        if (sec == 62 * 60)  { SAY("--- 62:00 the SSR is replaced\n"); ssr_stuck = false; }
        if (sec == 63 * 60)  { SAY("--- 63:00 type `echo off > /dev/alarm/heater` (release the latch)\n"); type("echo off > /dev/alarm/heater\n"); }
        if (sec == 66 * 60)  SAY("--- 66:00 press UP 20 times, 1 s apart (the setpoint stops at 37.5: above only on purpose)\n");
        if (sec >= 66 * 60 && sec < 66 * 60 + 20) button[BUTTON_UP] = true;
        if (sec == 75 * 60)  { SAY("--- 75:00 the room NTC opens (the skin sensor still works)\n"); room_dead = true; }
        if (sec == 88 * 60)  { SAY("--- 88:00 the room NTC works again\n"); room_dead = false; }
        if (sec == 90 * 60)  { SAY("--- 90:00 press MANUAL (-> manual: 10 %%)\n"); button[BUTTON_MANUAL] = true; }
        if (sec == 91 * 60)  { SAY("--- 91:00 press UP (manual: +5 %%)\n"); button[BUTTON_UP] = true; }
        if (sec == 92 * 60)  { SAY("--- 92:00 press MANUAL (-> auto)\n"); button[BUTTON_MANUAL] = true; }
        if (sec == 110 * 60) { SAY("--- 110:00 press MANUAL (-> manual 10 %%), then UP 4 times (30 %%)\n"); press(BUTTON_MANUAL); }
        if (sec >= 110 * 60 + 5 && sec < 110 * 60 + 9) press(BUTTON_UP);
        if (sec == 127 * 60) { SAY("--- 127:00 a nurse comes: press MUTE\n"); press(BUTTON_MUTE); }
        if (sec == 130 * 60) { SAY("--- 130:00 press DOWN twice (20 %%: no more check timer)\n"); press(BUTTON_DOWN); }
        if (sec == 130 * 60 + 1) press(BUTTON_DOWN);
        {   // log every change of the heater alarm and of the guard, to the second
            static char last[120] = "off", lastg[120] = "ok";
            char now[120]; rd("/dev/alarm/heater", now, sizeof now);
            if (strcmp(now, last)) { SAY("%7d:%02d  /dev/alarm/heater: %s (relay %d)\n", sec / 60, sec % 60, now, fake_gpio[PIN_HEATERSAFE]); strcpy(last, now); }
            static char lasth[120] = "off", lastl[120] = "off";
            // temphigh/templow: logged when the level changes (off, warn, on),
            // not every 0.1 C of the reason
            rd("/dev/alarm/temphigh", now, sizeof now);
            if (strncmp(now, lasth, 2)) { SAY("%7d:%02d  /dev/alarm/temphigh: %s\n", sec / 60, sec % 60, now); strcpy(lasth, now); }
            rd("/dev/alarm/templow", now, sizeof now);
            if (strncmp(now, lastl, 2)) { SAY("%7d:%02d  /dev/alarm/templow: %s\n", sec / 60, sec % 60, now); strcpy(lastl, now); }
            static char lastc[120] = "off";
            rd("/dev/alarm/check", now, sizeof now);
            if (strcmp(now, lastc)) { SAY("%7d:%02d  /dev/alarm/check: %s (heater %.1f)\n", sec / 60, sec % 60, now, heater_now()); strcpy(lastc, now); }
            static char lasts[120] = "off";   // the room sensor's own alarm
            rd("/dev/alarm/room", now, sizeof now);
            if (strcmp(now, lasts)) { SAY("%7d:%02d  /dev/alarm/room: %s\n", sec / 60, sec % 60, now); strcpy(lasts, now); }
            rd("/dev/guard", now, sizeof now);
            if (strcmp(now, lastg)) {
                char lim[24]; rd("/dev/heater/limit", lim, sizeof lim);
                SAY("%7d:%02d  /dev/guard: %s (heater %s)\n", sec / 60, sec % 60, now, lim); strcpy(lastg, now);
            }
        }
        if (sec == 100 * 60) { SAY("--- 100:00 the ADS1115 stops answering\n"); adc_dead = true;
                               fflush(stdout); saved_stdout = dup(1); quiet = tmpfile(); dup2(fileno(quiet), 1); }
        if (sec == 102 * 60) { SAY("--- 102:00 press UP\n"); button[BUTTON_UP] = true; }
        if (sec == 104 * 60) { SAY("--- 104:00 type `jobs`\n"); unquiet(); type("jobs\n"); }
        if (sec == 105 * 60) { SAY("--- 105:00 the ADS1115 answers again\n"); adc_dead = false; }
        if (sec % 60 == 0 || (sec < 120 && sec % 10 == 0)) status(sec);
        if (sec == 2 * 60 || sec == 51 * 60 || sec == 53 * 60) {
            char m[16], a[8]; rd("/dev/tft/mode", m, sizeof m); rd("/dev/tft/aut", a, sizeof a);
            SAY("        tft: mode %s, aut %s, %lu bytes sent to the display so far\n", m, a, fake_spi_bytes);
        }
    }
    char jb[8]; (void)jb;
    SAY("big display bytes at the end: %06x\n", seg_big);
    return 0;
}
