/**
 * apps/babywarmer/dev/guard.cpp — the guard: all of the heater's safety in
 * one kernel task, and /dev/guard
 *
 * The control (phases, PID, which power goes to the heater) is jobs:
 * visible, adjustable, replaceable from /fram/boot. The safety is this
 * kernel task, once a second, registered by the device itself: `kill`
 * can't stop it and no boot script can leave it out. It trusts only
 * hardware facts and the user's intent (/dev/skintemp, ambient, current,
 * heater/ssr, relay, setpoint, heaterauto, percent), never what a job
 * computed (/dev/state, /dev/autopower): that is what it guards.
 *
 * /dev/guard, how far the skin reading can be trusted, with the reason:
 *
 *   ok           the jobs' power, at most AUTO_CAP (80 %, boost's) in auto
 *   safe: <why>  auto mode, the skin reading not usable: the guard heats
 *                from the room temperature itself (safelut.h)
 *   none: <why>  skin and room unreadable: manual at the room table's power
 *                for the last room seen (else 10 %), UP/DOWN adjust it;
 *                back to auto by itself when the sensors answer
 *
 * Why safe (the field warmer's watchdogs and numbers, plus the hardware):
 *   - the sensor falls off a warm baby: the skin falls > 0.08 C/s for 3 s
 *     while more than 2 C below the setpoint, or stays more than 3 C below
 *     it for 20 s (not during the boost and the coast after it, 0 % on
 *     purpose, WARMUP_COAST_S);
 *   - the sensor is out from the start: less than 0.3 C rise in 5 minutes
 *     of warm-up (auto switched on, or the setpoint raised 2 C or more with
 *     the skin more than 3 C below it). The April 2026 field logs: the
 *     0.3 C came after 2.5-3 min at 100 %; a longer window means more heat
 *     with nothing watching the baby;
 *   - the skin reading fails (ADS1115 or NTC);
 *   - the control jobs are gone: nothing wrote /dev/heater for 10 s.
 * No cleverer plausibility checks: too many false alarms (a ceiling fan
 * makes the heater work harder). Known limit: a sensor on the mattress
 * under the heater warms up and isn't caught.
 * Back to ok by itself: 2 minutes with the skin within 2 C of the setpoint
 * (sensor causes), 5 s after the reading or the control is back (others).
 *
 * The decision acts at the heater: heater_guard() (heater.h), applied by
 * tpo_apply() within 100 ms. Manual mode is the nurse's choice: hardware
 * safety, and the check timer: nothing watches the baby's temperature in
 * manual, so above CHECK_POWER (25 %) for CHECK_S (12 min) the alarm
 * /dev/alarm/check asks the nurse to check the baby; unanswered for
 * CHECK_OFF_S (3 more minutes), the heater goes off until answered. MUTE
 * answers (and restarts the timer); so does lowering the power to 25 % or
 * less, or auto mode. As the GE Panda warmer (IEC 60601-2-21).
 *
 * The hardware safety, in any mode: the relay (off when the skin is more
 * than 3 C above the setpoint, on when heating); the heater current against
 * the SSR's real state; a stuck SSR (relay held off until `echo off >
 * /dev/alarm/heater` or a reboot). And the alarm files the jobs alarm,
 * ledwire, tftwire show: heater; sensor ("on" with the level's reason, or "warn" for the room
 * sensor down: the PID then runs on the last feed-forward, then 30 %);
 * check (manual's timer); and the deviation from the setpoint (IEC
 * 60601-2-21, as the GE Panda warmer), in auto with a trusted reading:
 * temphigh / templow "warn" at 1 C off (LED), "on" at 2 C off (buzzer).
 * templow is quiet during a warm-up (until the skin first comes within
 * 1 C of the setpoint) and for 10 minutes after the setpoint was changed,
 * while the skin climbs. In manual, where the setpoint means
 * nothing: temphigh "on" above 39 C only.
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
#include "warmup.h"
#include "drivers/buttons.h"
#include "pico/stdlib.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GUARD_MS              1000

// The watchdogs (the field warmer's numbers).
#define NO_RISE_S             300    // 5 minutes of warm-up ...
#define NO_RISE_MIN_DELTA     0.3f   // ... must raise the skin at least this
#define DROP_RATE             0.08f  // C per second
#define DROP_DELTA            2.0f   // ... and this far below the setpoint
#define DROP_S                3
#define LOW_MARGIN            3.0f   // more than this below the setpoint ...
#define LOW_S                 20     // ... for this long
#define RECOVER_MARGIN        2.0f   // back: within this of the setpoint ...
#define RECOVER_S             120    // ... for 2 minutes
#define HW_RECOVER_S          5      // a reading or the control back this long
#define CONTROL_DEAD_S        10     // auto mode, nobody writes /dev/heater
#define NONE_DEFAULT_PERCENT  10.0f  // no room temperature ever seen
#define AUTO_CAP              WARMUP_POWER  // the backstop in auto: the most auto asks

// Manual mode's check timer.
#define CHECK_POWER           25.0f  // manual above this ...
#define CHECK_S               720    // ... for 12 minutes: "check the baby"
#define CHECK_OFF_S           180    // unanswered 3 more minutes: heater off

// The hardware safety.
#define OVERTEMP_MARGIN       3.0f   // skin > setpoint + this -> relay off
#define DEV_WARN_C            1.0f   // skin this far off the setpoint: LOW/HIGH LED
#define DEV_ALARM_C           2.0f   // ... this far: the buzzer too
#define LOW_QUIET_S           600    // no LOW for 10 min after a setpoint change
#define MANUAL_HIGH_C         39.0f  // manual: HIGH above this
#define DEV_HYST_C            0.2f   // a level clears this much inside its threshold
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
static int   coast_left;                   // after the boost: the coast, no drop checks
static bool  last_auto;
static int   raised_seen = -1;             // /dev/setpoint/raised
static bool  reached;                      // the skin came within DEV_WARN_C since the warm-up began
static float last_sp;
static int   high_lv, low_lv;              // 0 off, 1 warn, 2 on (with hysteresis)
static int   sp_same_s;                    // seconds since the setpoint changed

static int   check_s;                      // manual above CHECK_POWER this long
static bool  check_off;                    // unanswered: the heater off
static float check_power;
static uint32_t mute_seen;

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
    reached = false;
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
    if (check_off) {
        heater_guard(HEATER_GUARD_SET, 0.0f);
    } else if (level == G_NONE && forced_manual) {
        if (!read_float("/dev/percent", &pct)) pct = NONE_DEFAULT_PERCENT;
        heater_guard(HEATER_GUARD_SET, pct);
    } else if (level == G_SAFE && last_auto) {
        heater_guard(HEATER_GUARD_SET, have_room ? safelut_power(last_room) : NONE_DEFAULT_PERCENT);
    } else {
        heater_guard(HEATER_GUARD_CAP, last_auto ? AUTO_CAP : 100.0f);
    }
}

// ---- manual mode's check timer -----------------------------------------

static void manual_check(bool manual)
{
    uint32_t mutes = button_presses[BUTTON_MUTE];
    bool answered = mutes != mute_seen;
    mute_seen = mutes;
    check_power = heater_asked();
    if (!manual || check_power <= CHECK_POWER || answered) {
        if (check_off) printf("guard: check answered, heater back on\n");
        check_s = 0;
        check_off = false;
        return;
    }
    if (++check_s == CHECK_S)
        printf("guard: manual %.0f %% for 12 min: check the baby, then press MUTE\n", check_power);
    if (check_s >= CHECK_S + CHECK_OFF_S && !check_off) {
        check_off = true;
        printf("guard: check not answered for 3 min: heater off until MUTE\n");
    }
}

// ---- the deviation from the setpoint ------------------------------------

// The level for being `over` beyond the setpoint, from the level before.
static int dev_level(float over, int was)
{
    if (over > DEV_ALARM_C || (was == 2 && over > DEV_ALARM_C - DEV_HYST_C)) return 2;
    if (over > DEV_WARN_C  || (was >= 1 && over > DEV_WARN_C - DEV_HYST_C))  return 1;
    return 0;
}

static void deviation(bool autom, bool skin_ok, float skin, float sp, char *s, int slen,
                      char *a_high, int hlen, char *a_low, int llen)
{
    if (sp != last_sp) { last_sp = sp; sp_same_s = 0; }
    else if (sp_same_s < LOW_QUIET_S) sp_same_s++;
    float d = skin - sp;
    if (skin_ok && d >= -DEV_WARN_C) reached = true;

    // HIGH: in auto with a trusted reading, 1 C over warns, 2 C alarms; in
    // manual (any reading) above MANUAL_HIGH_C. LOW: auto, a trusted
    // reading, the skin once within 1 C since the warm-up began (the boost
    // ends 3 C below; the last degrees take minutes), not in the coast
    // (0 % on purpose), the setpoint steady for 10 minutes. A level clears DEV_HYST_C inside its threshold, so
    // a reading on the border doesn't flicker.
    bool trusted = autom && level == G_OK && skin_ok;
    high_lv = trusted ? dev_level(d, high_lv) : 0;
    bool quiet = !trusted || !reached || coast_left > 0 || sp_same_s < LOW_QUIET_S;
    low_lv = quiet ? 0 : dev_level(-d, low_lv);

    if (high_lv)                                  snprintf(s, slen, "%s: skin %.1f C, %.1f C above the setpoint", high_lv == 2 ? "on" : "warn", skin, d);
    else if (!autom && skin_ok && skin > MANUAL_HIGH_C) snprintf(s, slen, "on: skin %.1f C, above %.0f C (manual)", skin, MANUAL_HIGH_C);
    else                                          snprintf(s, slen, "off");
    alarm_set("/dev/alarm/temphigh", a_high, hlen, s);

    if (low_lv) snprintf(s, slen, "%s: skin %.1f C, %.1f C below the setpoint", low_lv == 2 ? "on" : "warn", skin, -d);
    else        snprintf(s, slen, "off");
    alarm_set("/dev/alarm/templow", a_low, llen, s);
}

// ---- the heater current ------------------------------------------------

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
            if (reboost && !warming && skin < sp - WARMUP_EXIT_C) start_warmup(skin);   // warmer boosts again
            if (warming) {
                warm_s++;
                if (skin >= sp - WARMUP_EXIT_C) { warming = false; coast_left = WARMUP_COAST_S; }
                else if (warm_s >= NO_RISE_S) {
                    if (skin - warm_start_skin < NO_RISE_MIN_DELTA)
                        set_level(G_SAFE, C_SENSOR, "skin rose only %.1f C in 5 min of warm-up (sensor off the baby?)",
                                  skin - warm_start_skin);
                    else { warm_start_skin = skin; warm_s = 0; }
                }
            } else if (coast_left > 0) {
                coast_left--;                                // 0 % on purpose: the skin may fall
                drop_s = low_s = 0;
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
    manual_check(!autom);
    power();                                              // e.g. /dev/percent changed in none

    // -- hardware safety --
    bool overtemp = skin_ok && skin > sp + OVERTEMP_MARGIN;
    if (overtemp) write_str("/dev/relay", "off");
    else if (heater_get_power() > 0.0f && !stuck) write_str("/dev/relay", "on");
    current_check(overtemp);

    static char a_heater[100] = "off", a_high[100] = "off", a_low[100] = "off", a_sensor[128] = "off";
    alarm_set("/dev/alarm/heater", a_heater, sizeof(a_heater),
              stuck ? "on: current while switched off (SSR stuck on?), relay held off"
              : nocurrent ? "on: no current while switched on (element, fuse or SSR?)" : "off");
    char s[sizeof(a_sensor)];
    deviation(autom && !forced_manual, skin_ok, skin, sp, s, sizeof(s), a_high, sizeof(a_high), a_low, sizeof(a_low));
    if (level != G_OK) snprintf(s, sizeof(s), "on: %s", why);
    else if (!room_ok && autom) snprintf(s, sizeof(s), "warn: room sensor: %.80s", room_err);   // CHK, no buzzer: the skin control goes on
    else snprintf(s, sizeof(s), "off");
    alarm_set("/dev/alarm/sensor", a_sensor, sizeof(a_sensor), s);
    static char a_check[100] = "off";
    if (check_off)              snprintf(s, sizeof(s), "on: manual %.0f %%, not answered: heater off, check the baby, press MUTE", check_power);
    else if (check_s >= CHECK_S) snprintf(s, sizeof(s), "on: manual %.0f %% for 12 min: check the baby, press MUTE", check_power);
    else                        snprintf(s, sizeof(s), "off");
    alarm_set("/dev/alarm/check", a_check, sizeof(a_check), s);
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
