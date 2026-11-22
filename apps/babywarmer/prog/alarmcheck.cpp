/**
 * apps/babywarmer/prog/alarmcheck.cpp — alarmcheck: safety relay control + the three
 * alarm conditions, recovered from babywarmer's heater_check_task()
 * (renamed from heatercheck once temphigh/templow joined it)
 *
 * Runs as a background job (`alarmcheck &`) polled at the usual
 * JOB_POLL_MS like every other job, but self-paces to babywarmer's own
 * 15-second CHECK_INTERVAL_TIME internally -- same resumable-timer
 * pattern prog/pid.cpp and prog/phase.cpp already established, chosen
 * specifically so this doesn't need jobs.h to grow a per-job custom
 * interval (a real, documented, still-open limitation -- "all jobs
 * share one fixed poll interval"). Every other tick this is just a
 * cheap timestamp comparison, not real work.
 *
 * Per real check, reading /dev/skintemp, /dev/setpoint, /dev/heater
 * (read/write), and /dev/current:
 *
 *   1. skin > setpoint + OVERTEMP_MARGIN -> force /dev/relay off, skip
 *      the current-sense heater-fail check this tick (matches the
 *      original's own early return -- an overtemp trip isn't what
 *      "heater fail" means here, and current-sense is meaningless once
 *      the relay's already been forced off regardless of what it says).
 *      temphigh/templow are NOT skipped though (2026-08-30 fix) --
 *      unlike current-sense they don't depend on relay state, and
 *      skipping them let an overtemp condition (typically also above
 *      TEMPHIGH_C) permanently starve /dev/alarm/temphigh, silencing
 *      the TFT's BABY_HOT face and prog/alarm.cpp's buzzer for exactly
 *      the case that most needs them.
 *   2. heaterpower > 0 -> /dev/relay on.
 *   3./4. the heater current: since 2026-11-23 its own check every
 *      second, against the SSR's real state, see current_check() below
 *      (picoos: here, every 15 s, against the commanded power:
 *      "> HIGH_POWER_THRESHOLD but no current", "0 but current").
 *   5. skin > TEMPHIGH_C or skin < TEMPLOW_C -> the matching
 *      /dev/alarm/temphigh or /dev/alarm/templow.
 *
 * TEMPHIGH_C/TEMPLOW_C are Leon's own fixed testing values (40°C,
 * 23°C) -- deliberately not babywarmer's original logic, which instead
 * checked skin against setpoint+2 and ambient-2 (relative margins,
 * computed every 1s inside task_sensor, not this 15s check). Simpler
 * fixed thresholds, on purpose, for now -- easy to trigger on a bench
 * without needing a real setpoint/ambient delta. Worth revisiting
 * (relative margins, and/or a faster cadence than 15s, and/or tunable
 * devices like /dev/pid/kp) if temperature-limit responsiveness ever
 * needs to be better than this task's cadence -- not done now, Leon
 * asked for these two folded into this same task specifically.
 *
 * /dev/current already does the 8-sample averaging babywarmer's own
 * curr_sense_table rolling buffer did (see dev/current.cpp) -- nothing
 * to reimplement here, just read it live like everything else.
 *
 * Adapted from the original, not a literal copy: babywarmer's version
 * called gpio_put(PIN_HEATERSAFE, ...) directly inline. This one goes
 * through /dev/relay's existing write() instead -- one writer per GPIO
 * via the device layer, same discipline as everywhere else in this
 * namespace, rather than reaching around it.
 *
 * (picoos's first_check, which suppressed the heater-fail bit on the very
 * first check after boot, is gone with the 2026-11-23 current check: it
 * needs CONFIRM_S settled seconds in a row anyway.)
 *
 * Ungated on purpose, unlike pid/phase -- this is a hardware safety
 * check, not part of the auto/manual control split; the original ran
 * it unconditionally in every heater mode, so this does too.
 *
 * Reacting to these three conditions (buzzer, LED, mute) is a
 * separate, later concern -- see prog/alarm.cpp, which runs at a much
 * faster cadence than this 15s check needs, on purpose.
 *
 * From picoos @ 89c16e5 (prog/alarmcheck.cpp): unchanged apart from the
 * includes and the register function's name (2026-11-22), and the
 * heater-current check (2026-11-23, current_check() below).
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK_INTERVAL_MS      15000
#define OVERTEMP_MARGIN         3.0f  // skin > setpoint + this -> force relay off
#define CURRENT_FAIL_THRESHOLD 50     // raw ADC counts, matches babywarmer's own literal "50"
#define TEMPHIGH_C             40.0f  // Leon's fixed testing threshold, "for now"
#define TEMPLOW_C              23.0f  // Leon's fixed testing threshold, "for now" -- just
                                       // above the recalibrated ntc_lut.h's floor (22.9C,
                                       // see that file's own header comment), so it's
                                       // actually reachable with the temporary NTC in place

static bool have_next_step = false;
static absolute_time_t next_step;
static bool have_last_mode = false;
static bool last_mode = false;

static float read_dev(const char *path, float fallback)
{
    int fd = fs_open(path);
    if (fd < 0) return fallback;
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return fallback;
    buf[n] = '\0';
    return strtof(buf, 0);
}

static bool read_dev_bool(const char *path, bool fallback)
{
    int fd = fs_open(path);
    if (fd < 0) return fallback;
    char buf[8];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return fallback;
    buf[n] = '\0';
    return strncmp(buf, "on", 2) == 0;
}

static void write_dev(const char *path, const char *v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

// ---- the heater current, every second (plan2040, 2026-11-23) -------------
// picoos judged it in the 15 s check, from one instant /dev/current reading
// against the commanded power. Two problems (TODO idea 3, step 3e): the
// TPO switches the SSR on and off within its own 15 s window, so a check
// landing in an "off" part saw "heating hard, no current" (a false alarm
// between 50 and 99 %); and with a 15 s check against a 15 s window it
// landed at nearly the same point of the cycle every time, so at low
// power a dead heater could go unnoticed for minutes. Now: judge only
// what must be true at that moment, every second, from the SSR's real
// state (/dev/heater/ssr: "on 1300", ms in that state):
//   SSR on >= SETTLE_MS, relay on, no current  -> no current while on
//   SSR off >= SETTLE_MS, relay on, current    -> current while off: the
//                                                 SSR conducts, stuck on
// each only after CONFIRM_S such seconds in a row (one noisy sample
// never alarms). A stuck SSR means the heater can't be switched off any
// more: the relay is opened and held open (latched) until a reboot or
// `echo off > /dev/alarm/heater`.
#define CURRENT_CHECK_MS 1000
#define SETTLE_MS        500
#define CONFIRM_S        3

static absolute_time_t next_current_check;
static int  nocurrent_s, stuck_s;
static bool nocurrent, stuck;
static bool overtemp_now;          // the last 15 s check found overtemp: relay forced off

static void heater_alarm(void)
{
    static int last = -1;          // write /dev/alarm/heater only when it changes
    int now = stuck ? 2 : nocurrent ? 1 : 0;
    if (now == last) return;
    last = now;
    if (now == 2)      write_dev("/dev/alarm/heater", "on: current while switched off (SSR stuck on?), relay held off");
    else if (now == 1) write_dev("/dev/alarm/heater", "on: no current while switched on (element, fuse or SSR?)");
    else               write_dev("/dev/alarm/heater", "off");
}

static void current_check(void)
{
    if (stuck) {
        // Latched; `echo off > /dev/alarm/heater` releases it (to try again).
        if (!read_dev_bool("/dev/alarm/heater", true)) { stuck = false; stuck_s = 0; }
        else { write_dev("/dev/relay", "off"); return; }
    }
    char ssr[16] = "";
    int fd = fs_open("/dev/heater/ssr");
    if (fd >= 0) { int n = fs_read(fd, ssr, sizeof(ssr) - 1); fs_close(fd); ssr[n < 0 ? 0 : n] = '\0'; }
    bool on = strncmp(ssr, "on ", 3) == 0;
    long ms = strtol(ssr + (on ? 3 : 4), 0, 10);
    bool relay = read_dev_bool("/dev/relay", false);
    float curr = read_dev("/dev/current", 0.0f);

    if (!ssr[0] || !relay || overtemp_now || ms < SETTLE_MS) {
        // nothing to judge right now
    } else if (on) {
        if (curr < CURRENT_FAIL_THRESHOLD) { if (++nocurrent_s >= CONFIRM_S) nocurrent = true; }
        else { nocurrent_s = 0; nocurrent = false; }
        stuck_s = 0;
    } else {
        if (curr > CURRENT_FAIL_THRESHOLD) {
            if (++stuck_s >= CONFIRM_S) { stuck = true; write_dev("/dev/relay", "off"); }
        } else stuck_s = 0;
    }
    heater_alarm();
}

static int alarmcheck_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;

    if (time_reached(next_current_check)) {
        next_current_check = make_timeout_time_ms(CURRENT_CHECK_MS);
        current_check();
    }

    // Cheap every-tick check (like the timestamp compare below): a
    // buzzer reading it in prog/alarm.cpp) can sit on a stale verdict for
    // up to 15s after a mode switch. Detecting the flip and forcing the
    // real check to run right now, same tick, fixes that -- have_last_mode
    // suppresses a false "changed" on the very first tick.
    bool mode = read_dev_bool("/dev/heaterauto", last_mode);
    bool mode_changed = have_last_mode && (mode != last_mode);
    last_mode = mode;
    have_last_mode = true;

    if (!mode_changed && have_next_step && absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
        return snprintf(out, outlen, "-\n"); // not due yet
    }
    next_step = make_timeout_time_ms(CHECK_INTERVAL_MS);
    have_next_step = true;

    float skin        = read_dev("/dev/skintemp", 0.0f);
    float setpoint     = read_dev("/dev/setpoint", 35.0f);
    float heaterpower  = read_dev("/dev/heater", 0.0f);

    bool overtemp = skin > (setpoint + OVERTEMP_MARGIN);
    overtemp_now = overtemp;

    if (overtemp) {
        // Relay off regardless of anything else (the original's early
        // return for the heater check; the current check above skips
        // while this holds). temphigh/templow are still evaluated below:
        // skipping them here meant an overtemp condition (which by
        // construction is *also* usually above TEMPHIGH_C) could
        // permanently starve /dev/alarm/temphigh of ever going "on",
        // silencing both the TFT's BABY_HOT face and prog/alarm.cpp's
        // buzzer for exactly the most dangerous case.
        write_dev("/dev/relay", "off");
    } else if (heaterpower > 0.0f && !stuck) {
        write_dev("/dev/relay", "on");
    }

    write_dev("/dev/alarm/temphigh", skin > TEMPHIGH_C ? "on" : "off");
    write_dev("/dev/alarm/templow",  skin < TEMPLOW_C  ? "on" : "off");

    if (overtemp) return snprintf(out, outlen, "overtemp: relay off\n");
    return snprintf(out, outlen, (stuck || nocurrent) ? "fail\n" : "ok\n");
}

static const program_t prog_alarmcheck = {"alarmcheck", alarmcheck_run};

void prog_alarmcheck_register(void)
{
    prog_register(&prog_alarmcheck);
}
