/**
 * apps/babywarmer/prog/pid.cpp — pid <gate> <gate-value> <setpoint> [<bias>]
 *
 *   cat /dev/skintemp | pid /dev/state pid /dev/setpoint /dev/ffpower > /dev/pidout &
 *
 * A PID on the piped measurement, while <gate> reads <gate-value>. Once
 * every /dev/pid/dt seconds (the job runs every 150 ms; in between it
 * repeats /dev/pidout):
 *
 *   error  = setpoint - measurement
 *   part   = Kp*error + integral + Kp*Td*(-d measurement/dt)   derivative on
 *            the measurement: no kick when the setpoint changes
 *   output = bias + part
 *
 * With a <bias> (the feed-forward for the room, /dev/ffpower) the formula
 * is the field-tested warmer.cpp's: part within /dev/pid/min..max (-35..
 * +25 %), output within 0../dev/pid/outmax (75 %): the feed-forward
 * carries the steady load, the PID corrects. Without one, part is the
 * output, within 0..100. Anti-windup: the integral only moves while part
 * is within its limits.
 *
 * Kp, Ti, Td, dt, the integral and the last measurement are devices
 * (dev/pid.cpp): tunable while running, visible with cat, and seeded by
 * prog/phase.cpp when it starts the PID (after coast, or back from safe).
 * <setpoint> and <bias> are device paths or numbers.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool was_active = false;
static absolute_time_t next_step;

static float read_value(const char *arg, float fallback)
{
    int fd = fs_open(arg);
    if (fd >= 0) {
        char buf[16];
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n >= 0) { buf[n] = '\0'; return strtof(buf, 0); }
    }
    char *end;
    float v = strtof(arg, &end);
    return (end != arg) ? v : fallback;
}

static void write_dev(const char *path, float v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%.3f", v);
    fs_write(fd, buf, n);
    fs_close(fd);
}

static int echo_pidout(char *out, int outlen)
{
    int fd = fs_open("/dev/pidout");
    if (fd < 0) return -1;
    int n = fs_read(fd, out, outlen);
    fs_close(fd);
    return n < 0 ? 0 : n;
}

static int pid_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    if (argc < 3) return -1;
    const char *gate         = argv[0];
    const char *gate_value   = argv[1];
    const char *setpoint_arg = argv[2];
    bool  with_bias = argc >= 4;
    float bias = with_bias ? read_value(argv[3], 0.0f) : 0.0f;
    write_dev("/dev/pid/bias", bias);   // always: phase's bumpless seed needs it before pid runs

    int gfd = fs_open(gate);
    if (gfd < 0) return -1;
    char gbuf[16];
    int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
    fs_close(gfd);
    if (gn < 0) return -1;
    gbuf[gn] = '\0';
    size_t vlen = strlen(gate_value);
    bool gate_match = ((size_t)gn >= vlen && strncmp(gbuf, gate_value, vlen) == 0);

    if (!gate_match) {
        was_active = false; // next activation re-arms the timer fresh
        return echo_pidout(out, outlen);
    }

    float dt = read_value("/dev/pid/dt", 1.0f);

    if (!was_active) {
        was_active = true;
        next_step = make_timeout_time_ms((int)(dt * 1000.0f));
        return echo_pidout(out, outlen);
    }

    if (absolute_time_diff_us(get_absolute_time(), next_step) > 0) {
        return echo_pidout(out, outlen); // not due yet
    }

    char mbuf[16];
    int mn = inlen < (int)sizeof(mbuf) - 1 ? inlen : (int)sizeof(mbuf) - 1;
    for (int i = 0; i < mn; i++) mbuf[i] = in[i];
    mbuf[mn] = '\0';
    float measurement = strtof(mbuf, 0);

    float kp        = read_value("/dev/pid/kp", 3.0f);
    float ti        = read_value("/dev/pid/ti", 200.0f);
    float td        = read_value("/dev/pid/td", 5.0f);
    float setpoint  = read_value(setpoint_arg, 35.0f);
    float integral  = read_value("/dev/pid/integral", 0.0f);
    float prev_meas = read_value("/dev/pid/prevmeas", measurement);

    float lo = 0.0f, hi = 100.0f, outmax = 100.0f;
    if (with_bias) {
        lo     = read_value("/dev/pid/min", -35.0f);
        hi     = read_value("/dev/pid/max", 25.0f);
        outmax = read_value("/dev/pid/outmax", 75.0f);
    }

    float error  = setpoint - measurement;
    float p_term = kp * error;
    float d_term = -kp * td * (measurement - prev_meas) / dt;
    float i_new  = integral + (kp / ti) * error * dt;
    float output = p_term + i_new + d_term;

    float part = output;
    if (part > hi) part = hi;
    if (part < lo) part = lo;

    if (output >= lo && output <= hi) integral = i_new; // else: frozen, stays at previous value

    float out_clamped = bias + part;
    if (out_clamped > outmax) out_clamped = outmax;
    if (out_clamped < 0.0f)   out_clamped = 0.0f;

    write_dev("/dev/pid/integral", integral);
    write_dev("/dev/pid/prevmeas", measurement);
    write_dev("/dev/pidout", out_clamped);

    next_step = make_timeout_time_ms((int)(dt * 1000.0f));

    return snprintf(out, outlen, "%.1f\n", out_clamped);
}

static const program_t prog_pid = {"pid", pid_run};

void prog_pid_register(void)
{
    prog_register(&prog_pid);
}
