/**
 * apps/babywarmer/prog/warmer.cpp — warmer: the warmer's skin control
 *
 *   warmer > /dev/autopower &
 *
 * The field-tested warmer.cpp's control (LH20260427) as one program: once
 * a second it sets the phase (/dev/state) and the power auto mode asks for
 * (its output). `follow` sends that or the manual power to the heater; the
 * guard (dev/guard.cpp, a kernel task) keeps the last word on safety.
 *
 *   idle   manual mode (/dev/heaterauto off): asks 0. Auto on: boost.
 *   boost  80 % until the skin is within 3 C of the setpoint: coast.
 *   coast  0 % for 45 s, so the heat stored in the mattress shows: pid,
 *          the integral starting at 3 %.
 *   pid    feed-forward + PID:
 *            ff     = fftable_power(room)       (the steady load, fftable.h)
 *            part   = Kp*error + integral - Kp*Td*d(skin)/dt, within
 *                     /dev/pid/min..max (-35..+25 %); the integral only
 *                     moves while part is within them (anti-windup)
 *            power  = ff + part, within 0../dev/pid/outmax (75 %)
 *          The derivative is on the skin, not the error: no kick when the
 *          setpoint changes. A setpoint raised by 2 C or more with the skin
 *          more than 3 C below it boosts again (/dev/setpoint/raised); a
 *          smaller step the PID climbs itself.
 *   safe   while the guard doesn't trust the skin reading (it heats by
 *          itself then): asks what the guard heats with, so that when the
 *          guard says ok the PID takes over from that power (bumpless).
 *
 * The room sensor down: the last feed-forward for 10 minutes (a room
 * changes slowly), then 30 % (the table at 24 C); the guard alarms.
 * The skin unreadable: the power is held; the guard goes to safe.
 *
 * The tuning is read every second from the /dev/pid/ files (dev/warmer.cpp); the
 * integral and the feed-forward in use are published there.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "fftable.h"
#include "warmup.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define COAST_SEED        3.0f      // the integral after coast, on top of ff
#define DT                1.0f      // seconds per step
#define FF_FRESH_S        600       // keep the last feed-forward this long
#define FF_FALLBACK       30.0f

enum { IDLE, BOOST, COAST, PID, SAFE };
static const char *NAMES[] = { "idle", "boost", "coast", "pid", "safe" };

static int   state = IDLE;
static bool  started;
static absolute_time_t next_step;
static int   coast_s;
static float integral, prev_skin, power;
static float ff = FF_FALLBACK;
static int   ff_age_s = FF_FRESH_S;   // no room reading yet
static int   raised_seen = -1;

// ---- devices ------------------------------------------------------------

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

static float get(const char *path, float fallback)
{
    float v;
    return read_float(path, &v) ? v : fallback;
}

static bool starts_with(const char *path, const char *word)
{
    char b[8];
    int fd = fs_open(path);
    if (fd < 0) return false;
    int n = fs_read(fd, b, sizeof(b) - 1);
    fs_close(fd);
    return n >= (int)strlen(word) && strncmp(b, word, strlen(word)) == 0;
}

static void put(const char *path, const char *fmt, float v)
{
    char b[16];
    int n = snprintf(b, sizeof(b), fmt, v);
    int fd = fs_open(path);
    if (fd < 0) return;
    fs_write(fd, b, n);
    fs_close(fd);
}

static void set_state(int s)
{
    state = s;
    int fd = fs_open("/dev/state");
    if (fd < 0) return;
    fs_write(fd, NAMES[s], strlen(NAMES[s]));
    fs_close(fd);
}

// ---- the PID --------------------------------------------------------------

// Starts the PID so its first output is `from` (bumpless): power = ff +
// Kp*(setpoint - skin) + integral, the derivative 0 with prev_skin = skin.
static void pid_start_from(float from, float sp, float skin, float lo, float hi)
{
    float kp = get("/dev/pid/kp", 3.0f);
    integral = from - ff - kp * (sp - skin);
    integral = integral < lo ? lo : integral > hi ? hi : integral;
    prev_skin = skin;
}

static float pid_step(float sp, float skin, float lo, float hi, float outmax)
{
    float kp = get("/dev/pid/kp", 3.0f);
    float ti = get("/dev/pid/ti", 200.0f);
    float td = get("/dev/pid/td", 5.0f);
    float error = sp - skin;
    float i_new = integral + (kp / ti) * error * DT;
    float part  = kp * error + i_new - kp * td * (skin - prev_skin) / DT;
    prev_skin = skin;
    if (part >= lo && part <= hi) integral = i_new;      // else frozen: anti-windup
    part = part < lo ? lo : part > hi ? hi : part;
    float p = ff + part;
    return p < 0.0f ? 0.0f : p > outmax ? outmax : p;
}

// ---- once a second ----------------------------------------------------------

static void step(void)
{
    bool  autom   = starts_with("/dev/heaterauto", "on");
    bool  trusted = starts_with("/dev/guard", "ok");
    float sp      = get("/dev/setpoint", 35.0f);
    float skin;
    bool  skin_ok = read_float("/dev/skintemp", &skin);
    float room;
    if (read_float("/dev/ambient", &room)) { ff = fftable_power(room); ff_age_s = 0; }
    else if (++ff_age_s >= FF_FRESH_S)     { ff = FF_FALLBACK; ff_age_s = FF_FRESH_S; }
    float lo = get("/dev/pid/min", -35.0f), hi = get("/dev/pid/max", 25.0f);
    float outmax = get("/dev/pid/outmax", 75.0f);
    int   raised = (int)get("/dev/setpoint/raised", 0.0f);
    bool  reboost = raised_seen >= 0 && raised != raised_seen;
    raised_seen = raised;

    if (!autom) {
        set_state(IDLE);
        power = 0.0f;
    } else if (!trusted) {
        set_state(SAFE);
        power = get("/dev/heater", power);               // what the guard heats with
    } else switch (state) {
    case IDLE:
        set_state(BOOST);
        power = WARMUP_POWER;
        break;
    case BOOST:
        if (skin_ok && skin >= sp - WARMUP_EXIT_C) { set_state(COAST); coast_s = 0; power = 0.0f; }
        else power = WARMUP_POWER;
        break;
    case COAST:
        power = 0.0f;
        if (++coast_s >= WARMUP_COAST_S && skin_ok) {
            integral = COAST_SEED;
            prev_skin = skin;
            set_state(PID);
        }
        break;
    case SAFE:                                           // the guard says ok again
        if (skin_ok) { pid_start_from(power, sp, skin, lo, hi); set_state(PID); }
        break;
    case PID:
        if (reboost && skin_ok && skin < sp - WARMUP_EXIT_C) { set_state(BOOST); power = WARMUP_POWER; }
        else if (skin_ok) power = pid_step(sp, skin, lo, hi, outmax);
        break;                                           // skin unreadable: power held
    }
    put("/dev/pid/integral", "%.3f", integral);
    put("/dev/pid/ff", "%.1f", ff);
}

static int warmer_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen; (void)argc; (void)argv;
    if (!started || absolute_time_diff_us(get_absolute_time(), next_step) <= 0) {
        next_step = make_timeout_time_ms(1000);
        started = true;
        step();
    }
    return snprintf(out, outlen, "%.1f\n", power);
}

static const program_t prog_warmer = {"warmer", warmer_run};

void prog_warmer_register(void)
{
    prog_register(&prog_warmer);
}
