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
 *            ff     = FF_K * (setpoint - room)  (the steady load, feedforward.h)
 *            part   = Kp*error + integral - Kp*Td*d(skin)/dt, within
 *                     /dev/pid/min..max (-35..+50 %); the integral only
 *                     moves while part is within them (anti-windup) and
 *                     the skin within /dev/pid/band (1 C) of the setpoint
 *            power  = ff + part, within 0../dev/pid/outmax (100 %)
 *          The derivative is on the skin, not the error: no kick when the
 *          setpoint changes. A setpoint raised (any amount) with the skin
 *          more than 3 C below it boosts again (/dev/setpoint/raised);
 *          within 3 C the PID climbs itself.
 *   safe   while the guard doesn't trust the skin reading (it heats by
 *          itself then): asks what the guard heats with. When the guard
 *          says ok again: a new warm-up (Leon, 2026-11-26): boost if the
 *          skin is more than 3 C below, else the PID fresh from the
 *          feed-forward (integral 0). It used to continue from the safe
 *          power (bumpless), which climbed far too slowly: the integral
 *          started at -12 % (41 -> 46 % in 5 minutes on the board).
 *
 * The room sensor down: the last room seen for 10 minutes (a room changes
 * slowly), then 24 C; the guard warns (/dev/alarm/room).
 * The skin unreadable: the power is held; the guard goes to safe.
 *
 * The tuning is read every second from the /dev/pid/ files (dev/warmer.cpp); the
 * integral and the feed-forward in use are published there.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include "feedforward.h"
#include "warmup.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

#define COAST_SEED        3.0f      // the integral after coast, on top of ff
#define DT                1.0f      // seconds per step
#define ROOM_FRESH_S      600       // the room sensor down: the last room seen this long ...
#define ROOM_FALLBACK     24.0f     // ... then this

enum { IDLE, BOOST, COAST, PID, SAFE };
static const char *NAMES[] = { "idle", "boost", "coast", "pid", "safe" };

static int   state = IDLE;
static bool  started;
static absolute_time_t next_step;
static int   coast_s;
static float integral, prev_skin, power;
static float ff;
static float room = ROOM_FALLBACK;
static int   room_age_s = ROOM_FRESH_S;   // no room reading yet
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

static float pid_step(float sp, float skin, float lo, float hi, float outmax)
{
    float kp = get("/dev/pid/kp", 3.0f);
    float ti = get("/dev/pid/ti", 200.0f);
    float td = get("/dev/pid/td", 5.0f);
    float band = get("/dev/pid/band", 1.0f);
    float error = sp - skin;
    float i_new = integral + (kp / ti) * error * DT;
    float part  = kp * error + i_new - kp * td * (skin - prev_skin) / DT;
    prev_skin = skin;
    // The integral only moves near the setpoint (within band) and while part
    // is within its limits: far off, it would build a reserve that overshoots
    // later (the board: +0.9 C after a recovery; a draught: +2.2 C after).
    if (part >= lo && part <= hi && fabsf(error) < band) integral = i_new;
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
    float r;
    if (read_float("/dev/ambient", &r))   { room = r; room_age_s = 0; }
    else if (++room_age_s >= ROOM_FRESH_S) { room = ROOM_FALLBACK; room_age_s = ROOM_FRESH_S; }
    ff = feedforward_power(sp, room);
    float lo = get("/dev/pid/min", -35.0f), hi = get("/dev/pid/max", 50.0f);
    float outmax = get("/dev/pid/outmax", 100.0f);
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
    case SAFE:                                           // the guard says ok again: a new warm-up
        if (!skin_ok) break;
        if (skin < sp - WARMUP_EXIT_C) { set_state(BOOST); power = WARMUP_POWER; break; }
        integral = 0.0f;                                 // the PID fresh from the feed-forward
        prev_skin = skin;
        set_state(PID);
        power = pid_step(sp, skin, lo, hi, outmax);
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
