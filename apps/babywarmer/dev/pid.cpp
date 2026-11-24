/**
 * apps/babywarmer/dev/pid.cpp — the PID's devices (prog/pid.cpp)
 *
 *   /dev/pid/kp, ti, td, dt    tuning: 3.0 %/C, 200 s, 5 s, 1 s (warmer.cpp's,
 *                              tuned in the field); ti and dt are kept > 0
 *   /dev/pid/min, max, outmax  with a feed-forward: the PID's part within
 *                              -35..+25 %, the total within 0..75 %
 *   /dev/pid/integral          the integral term: watch it with cat;
 *                              `echo 0 > /dev/pid/integral` resets windup
 *   /dev/pid/prevmeas          the last measurement (for the derivative)
 *   /dev/pid/bias              the feed-forward pid used last
 *   /dev/pidout                pid's output, 0-100
 *
 * integral, prevmeas and bias are devices so prog/phase.cpp can seed the
 * PID when it starts it.
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

#define KP_DEF   3.0f
#define TI_DEF 200.0f
#define TD_DEF   5.0f
#define DT_DEF   1.0f

#define TI_MIN   1.0f   // Kp/Ti divides by it
#define DT_MIN   0.1f   // .../dt divides by it

static float kp = KP_DEF;
static float ti = TI_DEF;
static float td = TD_DEF;
static float dt = DT_DEF;
static float integral = 0.0f;
static float prevmeas = 0.0f;
static float pidout = 0.0f;
static float pmin = -35.0f, pmax = 25.0f, outmax = 75.0f, bias = 0.0f;

static int write_float(const char *buf, int len, float *dst, float lo, float hi)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number

    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *dst = v;
    return len;
}

static int kp_read(char *buf, int len)        { return snprintf(buf, len, "%.2f\n", kp); }
static int kp_write(const char *buf, int len) { return write_float(buf, len, &kp, 0.0f, 100.0f); }

static int ti_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", ti); }
static int ti_write(const char *buf, int len) { return write_float(buf, len, &ti, TI_MIN, 10000.0f); }

static int td_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", td); }
static int td_write(const char *buf, int len) { return write_float(buf, len, &td, 0.0f, 1000.0f); }

static int dt_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", dt); }
static int dt_write(const char *buf, int len) { return write_float(buf, len, &dt, DT_MIN, 60.0f); }

static int integral_read(char *buf, int len)        { return snprintf(buf, len, "%.3f\n", integral); }
static int integral_write(const char *buf, int len) { return write_float(buf, len, &integral, -1000.0f, 1000.0f); }

static int prevmeas_read(char *buf, int len)        { return snprintf(buf, len, "%.2f\n", prevmeas); }
static int prevmeas_write(const char *buf, int len) { return write_float(buf, len, &prevmeas, -1000.0f, 1000.0f); }

static int pidout_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", pidout); }
static int pidout_write(const char *buf, int len) { return write_float(buf, len, &pidout, 0.0f, 100.0f); }

static int min_read(char *buf, int len)           { return snprintf(buf, len, "%.1f\n", pmin); }
static int min_write(const char *buf, int len)     { return write_float(buf, len, &pmin, -100.0f, 0.0f); }
static int max_read(char *buf, int len)           { return snprintf(buf, len, "%.1f\n", pmax); }
static int max_write(const char *buf, int len)     { return write_float(buf, len, &pmax, 0.0f, 100.0f); }
static int outmax_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", outmax); }
static int outmax_write(const char *buf, int len)  { return write_float(buf, len, &outmax, 0.0f, 100.0f); }
static int bias_read(char *buf, int len)          { return snprintf(buf, len, "%.1f\n", bias); }
static int bias_write(const char *buf, int len)    { return write_float(buf, len, &bias, 0.0f, 100.0f); }

static const device_t dev_kp       = { "/dev/pid/kp",       0, 0, kp_read,       kp_write };
static const device_t dev_ti       = { "/dev/pid/ti",       0, 0, ti_read,       ti_write };
static const device_t dev_td       = { "/dev/pid/td",       0, 0, td_read,       td_write };
static const device_t dev_dt       = { "/dev/pid/dt",       0, 0, dt_read,       dt_write };
static const device_t dev_integral = { "/dev/pid/integral", 0, 0, integral_read, integral_write };
static const device_t dev_prevmeas = { "/dev/pid/prevmeas", 0, 0, prevmeas_read, prevmeas_write };
static const device_t dev_pidout   = { "/dev/pidout",       0, 0, pidout_read,   pidout_write };
static const device_t dev_min      = { "/dev/pid/min",      0, 0, min_read,      min_write };
static const device_t dev_max      = { "/dev/pid/max",      0, 0, max_read,      max_write };
static const device_t dev_outmax   = { "/dev/pid/outmax",   0, 0, outmax_read,   outmax_write };
static const device_t dev_bias     = { "/dev/pid/bias",     0, 0, bias_read,     bias_write };

void dev_pid_register(void)
{
    fs_register(&dev_kp);
    fs_register(&dev_ti);
    fs_register(&dev_td);
    fs_register(&dev_dt);
    fs_register(&dev_integral);
    fs_register(&dev_prevmeas);
    fs_register(&dev_pidout);
    fs_register(&dev_min);
    fs_register(&dev_max);
    fs_register(&dev_outmax);
    fs_register(&dev_bias);
}
