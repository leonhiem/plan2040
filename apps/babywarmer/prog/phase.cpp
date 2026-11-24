/**
 * apps/babywarmer/prog/phase.cpp — phase: the warm-up sequence
 *
 * Recovered in picoos from babywarmer's task_pidctrl() (the ControlState
 * switch statement), onto /dev/state instead of a C enum global. Once a
 * second (self-paced, like prog/pid.cpp):
 *
 *   idle   manual mode (/dev/heaterauto off); the moment auto is on: boost
 *   boost  80 % (the number lives in the boot script's `select`), until the
 *          skin reaches setpoint - BOOST_EXIT_MARGIN
 *   coast  0 % for COAST_TICKS seconds (lets the current settle after the
 *          big power step), then pid, seeding /dev/pid/integral at
 *          COAST_INTEGRAL_SEED (babywarmer's documented head-start)
 *   pid    prog/pid.cpp controls the skin temperature (feed-forward + PID);
 *          back to boost when the setpoint is raised by 2 C or more
 *          (/dev/setpoint/raised; warmer.cpp 2026-04-27: "large upward
 *          step -- skin needs active warmup")
 *   safe   shown while the guard (dev/guard.cpp) says the skin reading
 *          can't be trusted (safe or none); the guard heats by itself then.
 *          When it says ok again: pid, bumpless (the integral seeded so the
 *          PID starts from the power in use; 2026-11-24, see below).
 *
 * Changed 2026-11-24 (plan2040, TODO.md idea 3 step 4): the watchdogs that
 * decided "safe" (no rise in boost, a fast or sustained drop in pid, the
 * recovery margin) moved into the guard, a kernel task that can't be
 * killed; picoos had them here, in a job. This program now only sequences
 * the warm-up. From picoos @ 89c16e5 (prog/phase.cpp); the includes and
 * the register function's name changed 2026-11-22.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BOOST_EXIT_MARGIN     3.0f  // skin >= setpoint - this -> coast
#define COAST_TICKS           45
#define COAST_INTEGRAL_SEED   3.0f

static bool have_next_step = false;
static absolute_time_t next_step;

static int   coast_ticks = 0;
static int   raised_seen = -1;     // /dev/setpoint/raised last seen

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

static bool gate_on(const char *path)
{
    int fd = fs_open(path);
    if (fd < 0) return false;
    char buf[4];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    return n >= 2 && strncmp(buf, "on", 2) == 0;
}

static bool guard_ok(void)
{
    int fd = fs_open("/dev/guard");
    if (fd < 0) return true;           // no guard in this firmware
    char buf[4];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    return n >= 2 && strncmp(buf, "ok", 2) == 0;
}

static bool state_is(const char *want)
{
    int fd = fs_open("/dev/state");
    if (fd < 0) return false;
    char buf[8];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    return n >= 0 && (size_t)n >= strlen(want) && strncmp(buf, want, strlen(want)) == 0;
}

static void set_state(const char *s)
{
    int fd = fs_open("/dev/state");
    if (fd < 0) return;
    fs_write(fd, s, strlen(s));
    fs_close(fd);
}

static void seed_pid(float integral_seed, float prevmeas_seed)
{
    int fd; char buf[16]; int n;

    fd = fs_open("/dev/pid/integral");
    if (fd >= 0) { n = snprintf(buf, sizeof(buf), "%.3f", integral_seed); fs_write(fd, buf, n); fs_close(fd); }

    fd = fs_open("/dev/pid/prevmeas");
    if (fd >= 0) { n = snprintf(buf, sizeof(buf), "%.3f", prevmeas_seed); fs_write(fd, buf, n); fs_close(fd); }
}

static int phase_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;

    if (have_next_step && absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
        return snprintf(out, outlen, "-\n"); // not due yet
    }
    next_step = make_timeout_time_ms(1000);
    have_next_step = true;

    bool auto_mode = gate_on("/dev/heaterauto");
    float skin      = read_dev("/dev/skintemp", 0.0f);
    float setpoint  = read_dev("/dev/setpoint", 35.0f);
    // Read every second, whatever the state: only a raise that settles
    // while in pid re-boosts (as warmer.cpp).
    int raised = (int)read_dev("/dev/setpoint/raised", 0.0f);
    bool reboost = raised_seen >= 0 && raised != raised_seen;
    raised_seen = raised;

    if (!auto_mode) {
        set_state("idle");
        coast_ticks = 0;
        return snprintf(out, outlen, "idle\n");
    }

    // The guard (dev/guard.cpp) decides whether the skin reading can be
    // trusted; while it says safe or none, /dev/state shows "safe" (the
    // guard heats by itself meanwhile, whatever select/follow send).
    if (!guard_ok()) {
        set_state("safe");
        return snprintf(out, outlen, "safe (guard)\n");
    }

    if (state_is("idle")) {
        set_state("boost");
        return snprintf(out, outlen, "-> boost\n");
    }

    if (state_is("boost")) {
        if (skin >= (setpoint - BOOST_EXIT_MARGIN)) {
            coast_ticks = 0;
            set_state("coast");
            return snprintf(out, outlen, "-> coast\n");
        }
        return snprintf(out, outlen, "boost\n");
    }

    if (state_is("coast")) {
        coast_ticks++;
        if (coast_ticks >= COAST_TICKS) {
            seed_pid(COAST_INTEGRAL_SEED, skin);
            set_state("pid");
            return snprintf(out, outlen, "-> pid\n");
        }
        return snprintf(out, outlen, "coast\n");
    }

    if (state_is("pid")) {
        if (reboost) {
            set_state("boost");
            return snprintf(out, outlen, "-> boost (setpoint raised to %.1f)\n", setpoint);
        }
        return snprintf(out, outlen, "pid\n");
    }

    if (state_is("safe")) {
        // The guard said ok again (checked above): back to the PID,
        // bumpless (Leon, 2026-11-24): the integral is seeded so the PID's
        // first output is the power heating the baby right now (the room
        // table's, in safe), not 0. picoos seeded 0.0 ("a recovery, not a
        // cold start"); bwsim showed the cost: the power dropped from 39 %
        // to ~3 %, the baby cooled, and after a few minutes the guard
        // called it "sensor off the baby" again, every ~10 minutes.
        // output = bias + Kp * (setpoint - skin) + integral (+ D, 0 with
        // prevmeas = skin), so integral = power - bias - Kp * (setpoint -
        // skin), within the PID part's limits. The bias is the
        // feed-forward pid used (/dev/pid/bias, 0 without one).
        float power = read_dev("/dev/heater", 0.0f);
        float kp    = read_dev("/dev/pid/kp", 3.0f);
        float bias  = read_dev("/dev/pid/bias", 0.0f);
        float lo    = bias > 0.0f ? read_dev("/dev/pid/min", -35.0f) : 0.0f;
        float hi    = bias > 0.0f ? read_dev("/dev/pid/max", 25.0f) : 100.0f;
        float integral = power - bias - kp * (setpoint - skin);
        if (integral < lo) integral = lo;
        if (integral > hi) integral = hi;
        seed_pid(integral, skin);
        set_state("pid");
        return snprintf(out, outlen, "-> pid (recovered, from %.0f %%)\n", power);
    }

    // Unrecognized /dev/state content -- shouldn't happen (its own
    // write() rejects anything outside the vocabulary), but land
    // somewhere safe rather than getting stuck doing nothing.
    set_state("idle");
    return snprintf(out, outlen, "-> idle (unknown state)\n");
}

static const program_t prog_phase = {"phase", phase_run};

void prog_phase_register(void)
{
    prog_register(&prog_phase);
}
