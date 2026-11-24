/**
 * apps/babywarmer/dev/guard.cpp — the guard: all of the babywarmer's heater
 * safety in one kernel task, and /dev/guard
 *
 * Decided with Leon 2026-11-24 (TODO.md idea 3, step 4). The control (the
 * warm-up phases, the PID, which power goes to the heater) stays in jobs:
 * visible, adjustable, replaceable from /fram/boot. The safety doesn't: it
 * is a kernel task, once a second, registered by this device itself, so
 * `kill` can't stop it and no boot script can leave it out.
 *
 * It trusts only hardware facts and the user's intent: /dev/skintemp,
 * /dev/ambient, /dev/current, /dev/heater/ssr, /dev/relay, /dev/setpoint,
 * /dev/heaterauto, /dev/percent. Never what a job computed (/dev/state,
 * /dev/pidout): those are what it guards.
 *
 * How much the skin reading can be trusted, in /dev/guard with the reason:
 *
 *   ok                  the skin reading is used: the jobs' PID power, as asked
 *   safe: <why>         the skin reading isn't usable; the guard heats from the
 *                       room temperature itself (safelut.h), whatever the jobs
 *                       ask
 *   none: <why>         no temperature at all (skin and room both unreadable):
 *                       manual mode at a conservative % (the room table for the
 *                       last room temperature seen, else 10 %), UP/DOWN adjust
 *                       it; back to auto by itself when the sensors answer
 *
 * Why "safe" (auto mode only), Leon's two scenarios plus the hardware:
 *   - the sensor falls out while the baby is warm: the skin falls faster than
 *     a baby can (> 0.08 C/s for 3 s, more than 2 C below the setpoint), or
 *     stays more than 3 C below the setpoint for 20 s;
 *   - the sensor is out from the start: during warm-up the skin rises less
 *     than 0.3 C in 5 minutes (the field warmer's code meant 5, but a tick
 *     bug made it 50; Leon 2026-11-24, from the April field logs: at 100 %
 *     boost the 0.3 C came after ~2.5-3 min and a whole warm-up took
 *     3.5-5.5 min, so a longer window means more open-loop heat with the
 *     sensor out of the bed); a warm-up is auto switching on, or the
 *     setpoint raised by 2 C or more (/dev/setpoint/raised) with the skin
 *     more than 3 C below it (as phase boosts again);
 *   - the skin reading fails (ADS1115 or NTC);
 *   - the control jobs are gone: in auto mode nothing has written /dev/heater
 *     for 10 s.
 * The first three are picoos's prog/phase.cpp watchdogs, the field
 * warmer's numbers (no-rise: see above).
 * Deliberately no cleverer "plausibility" checks (Leon: too many false
 * alarms, e.g. a ceiling fan makes the heater work harder). Known limit: a
 * sensor lying on the mattress under the heater warms up and isn't caught.
 *
 * Back to ok by itself: after a sensor cause, when the skin has been within
 * 2 C of the setpoint for 2 minutes; after a hardware or control cause, when
 * the reading or the control has been back for 5 s.
 *
 * The guard's decision acts where the hardware is: heater_guard() (heater.h),
 * applied by tpo_apply() within 100 ms. In ok it's a cap: AUTO_CAP (80 %,
 * the most auto ever asks: boost; 2026-11-24) in auto, none in manual;
 * in safe and none the guard's own power. Manual mode (the nurse's choice):
 * hardware safety only, no power of the guard's own.
 *
 * Also here, from picoos's prog/alarmcheck.cpp (that job is gone): the
 * safety relay (off on overtemp, on when heating), the temperature alarms,
 * the heater-current check against the SSR's real state (2026-11-23), the
 * stuck SSR (relay held off until `echo off > /dev/alarm/heater` or a
 * reboot: Leon's exception), and the alarm files /dev/alarm/{heater,
 * temphigh,templow,sensor}, which the jobs alarm, ledwire, tftwire show.
 * The temperature alarms are the field warmer's (2026-11-24): high when
 * the skin is more than 2 C above the setpoint, low when it is more than
 * 2 C below the room. /dev/alarm/sensor also says when the room sensor is
 * down in ok (the PID then runs on the last feed-forward, then 30 %).
 * Now every second (alarmcheck: temperatures every 15 s).
 *
 * Each change of level is said once on the console:
 *   guard: safe: skin fell 2.4 C in 3 s (sensor off the baby?), heating 39 % from the room (24.0 C)
 *   guard: ok: skin 35.2 C, plausible for 2 minutes
 */
#include "board.h"
#include "kernel/fs.h"
#include "kernel/task.h"
#include "kernel/errstr.h"
#include "heater.h"
#include "safelut.h"
#include "pico/stdlib.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GUARD_MS              1000

// The watchdogs: picoos prog/phase.cpp's numbers.
#define WARMUP_EXIT_MARGIN    3.0f   // warm-up ends when skin >= setpoint - this (phase's boost exit)
#define NO_RISE_S             300    // 5 minutes of warm-up ...
#define NO_RISE_MIN_DELTA     0.3f   // ... must raise the skin at least this
#define DROP_RATE             0.08f  // C per second
#define DROP_DELTA            2.0f   // ... and this far below the setpoint
#define DROP_S                3
#define LOW_MARGIN            3.0f   // more than this below the setpoint ...
#define LOW_S                 20     // ... for this long
#define RECOVER_MARGIN        2.0f   // back: within this of the setpoint ...
#define RECOVER_S             120    // ... for 2 minutes (Leon, 2026-11-24)
#define HW_RECOVER_S          5      // a reading or the control back this long
#define CONTROL_DEAD_S        10     // auto mode, nobody writes /dev/heater
#define NONE_DEFAULT_PERCENT  10.0f  // no room temperature ever seen
#define AUTO_CAP              80.0f  // the backstop in auto: boost's 80 %, the most auto asks

// picoos prog/alarmcheck.cpp's numbers.
#define OVERTEMP_MARGIN       3.0f   // skin > setpoint + this -> relay off
#define TEMPHIGH_MARGIN       2.0f   // skin > setpoint + this: alarm (warmer.cpp)
#define TEMPLOW_MARGIN        2.0f   // skin < room - this: alarm (warmer.cpp)
#define CURRENT_FAIL_THRESHOLD 50    // raw ADC counts
#define SETTLE_MS             500    // SSR state settled this long before judging
#define CONFIRM_S             3      // seconds in a row before a current verdict

enum { G_OK, G_SAFE, G_NONE };
enum { C_SENSOR, C_HARDWARE, C_CONTROL };  // why safe

static int   level = G_OK, cause;
static char  why[96];
static bool  forced_manual;                // none: the guard switched to manual
static float last_room;
static bool  have_room;

static bool  warming;                      // the warm-up since auto went on
static int   warm_s;
static float warm_start_skin;
static float prev_skin;
static bool  have_prev;
static int   drop_s, low_s, recover_s, back_s;
static bool  last_auto;
static int   raised_seen = -1;             // /dev/setpoint/raised

static int   nocurrent_s, stuck_s;
static bool  nocurrent, stuck;

// ---- files ------------------------------------------------------------

static bool read_float(const char *path, float *v)
{
    char b[24];
    int fd = fs_open(path);
    if (fd < 0) return false;
    int n = fs_read(fd, b, sizeof(b) - 1);
    fs_close(fd);
    if (n <= 0) return false;
    b[n] = '\0';
    char *end;
    *v = strtof(b, &end);
    return end != b;
}

static bool read_on(const char *path)
{
    char b[8];
    int fd = fs_open(path);
    if (fd < 0) return false;
    int n = fs_read(fd, b, sizeof(b) - 1);
    fs_close(fd);
    return n >= 2 && strncmp(b, "on", 2) == 0;
}

static void write_str(const char *path, const char *v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

// Writes an alarm file only when its text changes (cheap, and no churn).
static void alarm_set(const char *path, char *last, int lastlen, const char *text)
{
    if (strcmp(last, text) == 0) return;
    snprintf(last, lastlen, "%s", text);
    write_str(path, text);
}

// ---- the level --------------------------------------------------------

static void say_level(void)
{
    float v;
    heater_guard_get(&v);
    if (level == G_OK)        printf("guard: ok: %s\n", why);
    else if (level == G_SAFE) {
        if (have_room) printf("guard: safe: %s, heating %.0f %% from the room (%.1f C)\n", why, v, last_room);
        else           printf("guard: safe: %s, heating %.0f %%\n", why, v);
    }
    else                      printf("guard: none: %s, manual at %.0f %%\n", why, v);
}

static void start_warmup(float skin)
{
    warming = true;
    warm_s = 0;
    warm_start_skin = skin;
    drop_s = low_s = 0;
}

static void power(void);

static void set_level(int lv, int c, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void set_level(int lv, int c, const char *fmt, ...)
{
    char text[sizeof(why)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    bool changed = lv != level || c != cause;
    level = lv;
    cause = c;
    snprintf(why, sizeof(why), "%s", text);
    recover_s = back_s = 0;
    if (changed) { power(); say_level(); }
}

// ---- the power: the guard's say over the heater -----------------------

static void power(void)
{
    float pct;
    if (level == G_NONE && forced_manual) {
        if (!read_float("/dev/percent", &pct)) pct = NONE_DEFAULT_PERCENT;
        heater_guard(HEATER_GUARD_SET, pct);
    } else if (level == G_SAFE && last_auto) {
        heater_guard(HEATER_GUARD_SET, have_room ? safelut_power(last_room) : NONE_DEFAULT_PERCENT);
    } else {
        heater_guard(HEATER_GUARD_CAP, last_auto ? AUTO_CAP : 100.0f);
    }
}

// ---- the heater current (from alarmcheck, 2026-11-23) -----------------

static void current_check(bool overtemp)
{
    if (stuck) {
        // Latched; `echo off > /dev/alarm/heater` releases it (to try again).
        if (!read_on("/dev/alarm/heater")) { stuck = false; stuck_s = 0; }
        else { write_str("/dev/relay", "off"); return; }
    }
    char ssr[16] = "";
    int fd = fs_open("/dev/heater/ssr");
    if (fd >= 0) { int n = fs_read(fd, ssr, sizeof(ssr) - 1); fs_close(fd); ssr[n < 0 ? 0 : n] = '\0'; }
    bool on = strncmp(ssr, "on ", 3) == 0;
    long ms = strtol(ssr + (on ? 3 : 4), 0, 10);
    bool relay = read_on("/dev/relay");
    float curr = 0.0f;
    bool have_curr = read_float("/dev/current", &curr);

    if (!ssr[0] || !have_curr || !relay || overtemp || ms < SETTLE_MS) {
        // nothing to judge right now
    } else if (on) {
        if (curr < CURRENT_FAIL_THRESHOLD) { if (++nocurrent_s >= CONFIRM_S) nocurrent = true; }
        else { nocurrent_s = 0; nocurrent = false; }
        stuck_s = 0;
    } else {
        if (curr > CURRENT_FAIL_THRESHOLD) {
            if (++stuck_s >= CONFIRM_S) { stuck = true; write_str("/dev/relay", "off"); }
        } else stuck_s = 0;
    }
}

// ---- once a second ----------------------------------------------------

static void guard_task(void)
{
    float skin = 0.0f, room = 0.0f, sp = 35.0f;
    char skin_err[ERRMAX] = "";
    bool skin_ok = read_float("/dev/skintemp", &skin);
    if (!skin_ok) snprintf(skin_err, sizeof(skin_err), "%s", errstr()[0] ? errstr() : "/dev/skintemp: no reading");
    char room_err[ERRMAX] = "";
    bool room_ok = read_float("/dev/ambient", &room);
    if (room_ok) { last_room = room; have_room = true; }
    else snprintf(room_err, sizeof(room_err), "%s", errstr()[0] ? errstr() : "/dev/ambient: no reading");
    read_float("/dev/setpoint", &sp);
    bool autom = read_on("/dev/heaterauto");
    float r;
    int raised = read_float("/dev/setpoint/raised", &r) ? (int)r : 0;
    bool reboost = raised_seen >= 0 && raised != raised_seen;
    raised_seen = raised;

    if (autom && !last_auto && !forced_manual) {        // auto just switched on
        last_auto = true;
        start_warmup(skin);
        if (level != G_OK && skin_ok) set_level(G_OK, C_SENSOR, "auto mode on");
    }
    last_auto = autom;

    // -- how far the skin reading can be trusted --
    if (forced_manual) {
        if (skin_ok && room_ok) {
            if (++back_s >= HW_RECOVER_S) {
                forced_manual = false;
                write_str("/dev/heaterauto", "on");
                last_auto = true;
                start_warmup(skin);
                set_level(G_OK, C_SENSOR, "sensors answer again, back to auto");
            }
        } else back_s = 0;
    } else if (autom) {
        if (!skin_ok && !room_ok) {
            char pct[16];
            snprintf(pct, sizeof(pct), "%.0f", have_room ? safelut_power(last_room) : NONE_DEFAULT_PERCENT);
            write_str("/dev/heaterauto", "off");     // first: switching to manual resets /dev/percent to 10
            write_str("/dev/percent", pct);
            forced_manual = true;
            set_level(G_NONE, C_HARDWARE, "no temperature sensors (%s)", skin_err);
        } else if (!skin_ok) {
            if (level != G_SAFE || cause != C_HARDWARE) set_level(G_SAFE, C_HARDWARE, "%s", skin_err);
        } else if (level == G_SAFE && cause == C_HARDWARE) {
            if (++back_s >= HW_RECOVER_S) { start_warmup(skin); set_level(G_OK, C_SENSOR, "skin sensor answers again"); }
        } else if (heater_ms_since_asked() > CONTROL_DEAD_S * 1000) {
            if (level != G_SAFE || cause != C_CONTROL)
                set_level(G_SAFE, C_CONTROL, "control jobs not running (nothing writes /dev/heater)");
        } else if (level == G_SAFE && cause == C_CONTROL) {
            if (++back_s >= HW_RECOVER_S) set_level(G_OK, C_SENSOR, "control jobs running again");
        } else if (level == G_OK) {
            if (reboost && !warming && skin < sp - WARMUP_EXIT_MARGIN) start_warmup(skin);   // phase boosts again
            if (warming) {
                warm_s++;
                if (skin >= sp - WARMUP_EXIT_MARGIN) warming = false;
                else if (warm_s >= NO_RISE_S) {
                    if (skin - warm_start_skin < NO_RISE_MIN_DELTA)
                        set_level(G_SAFE, C_SENSOR, "skin rose only %.1f C in 5 min of warm-up (sensor off the baby?)",
                                  skin - warm_start_skin);
                    else { warm_start_skin = skin; warm_s = 0; }
                }
            } else {
                if (have_prev && prev_skin - skin > DROP_RATE && skin < sp - DROP_DELTA) {
                    if (++drop_s >= DROP_S)
                        set_level(G_SAFE, C_SENSOR, "skin fell to %.1f C in %d s (sensor off the baby?)", skin, DROP_S);
                } else drop_s = 0;
                if (level == G_OK && skin < sp - LOW_MARGIN) {
                    if (++low_s >= LOW_S)
                        set_level(G_SAFE, C_SENSOR, "skin %.1f C, more than 3 C below the setpoint for 20 s (sensor off the baby?)", skin);
                } else low_s = 0;
            }
        } else if (level == G_SAFE && cause == C_SENSOR) {
            if (skin >= sp - RECOVER_MARGIN) {
                if (++recover_s >= RECOVER_S) { warming = false; set_level(G_OK, C_SENSOR, "skin %.1f C, plausible for 2 minutes", skin); }
            } else recover_s = 0;
        }
    } else {
        // Manual, the nurse's choice: no power of the guard's own; the level
        // says what's readable, for the alarms and the display.
        if (!skin_ok && !room_ok) { if (level != G_NONE) set_level(G_NONE, C_HARDWARE, "no temperature sensors (%s)", skin_err); }
        else if (!skin_ok)        { if (level != G_SAFE) set_level(G_SAFE, C_HARDWARE, "%s", skin_err); }
        else if (level != G_OK)   set_level(G_OK, C_SENSOR, "manual mode");
    }
    prev_skin = skin;
    have_prev = skin_ok;
    power();                                              // e.g. /dev/percent changed in none

    // -- hardware safety (alarmcheck's) --
    bool overtemp = skin_ok && skin > sp + OVERTEMP_MARGIN;
    if (overtemp) write_str("/dev/relay", "off");
    else if (heater_get_power() > 0.0f && !stuck) write_str("/dev/relay", "on");
    current_check(overtemp);

    static char a_heater[100] = "off", a_high[8] = "off", a_low[8] = "off", a_sensor[128] = "off";
    alarm_set("/dev/alarm/heater", a_heater, sizeof(a_heater),
              stuck ? "on: current while switched off (SSR stuck on?), relay held off"
              : nocurrent ? "on: no current while switched on (element, fuse or SSR?)" : "off");
    alarm_set("/dev/alarm/temphigh", a_high, sizeof(a_high), skin_ok && skin > sp + TEMPHIGH_MARGIN ? "on" : "off");
    alarm_set("/dev/alarm/templow",  a_low,  sizeof(a_low),  skin_ok && room_ok && skin < room - TEMPLOW_MARGIN ? "on" : "off");
    char s[sizeof(a_sensor)];
    if (level != G_OK) snprintf(s, sizeof(s), "on: %s", why);
    else if (!room_ok && autom) snprintf(s, sizeof(s), "on: %.90s", room_err);
    else snprintf(s, sizeof(s), "off");
    alarm_set("/dev/alarm/sensor", a_sensor, sizeof(a_sensor), s);
}

// ---- /dev/guard ---------------------------------------------------------

static int guard_read(char *buf, int len)
{
    if (level == G_OK) return snprintf(buf, len, "ok\n");
    return snprintf(buf, len, "%s: %s\n", level == G_SAFE ? "safe" : "none", why);
}

static const device_t dev_guard = { "/dev/guard", 0, 0, guard_read, 0 };

void dev_guard_register(void)
{
    snprintf(why, sizeof(why), "starting");
    fs_register(&dev_guard);
    task_register("guard", guard_task, GUARD_MS);
}
