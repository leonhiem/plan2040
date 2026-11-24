/**
 * apps/babywarmer/prog/monitor.cpp — monitor: the warmer's status in one line
 *
 *   watch 1000 monitor
 *
 * The field-tested warmer.cpp's telemetry line, column for column (state
 * setp skin amb heat low high curr fail warn pid_i pid_pm), with ff (the
 * feed-forward) added at the end, so logs compare side by side with the
 * April 2026 field logs. warn is the guard not ok. The header line
 * repeats every 10 lines, as warmer.cpp's did.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int line_idx = 0;

static float read_dev(const char *path)
{
    int fd = fs_open(path);
    if (fd < 0) return 0.0f;
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return 0.0f;
    buf[n] = '\0';
    return strtof(buf, 0);
}

// For short text devices (/dev/heaterauto, /dev/state) -- copies at
// most outlen-1 bytes into out, trimming any trailing newline, and
// always nul-terminates. Falls back to "?" if the device is missing.
static void read_dev_str(const char *path, char *out, int outlen)
{
    int fd = fs_open(path);
    if (fd < 0) { snprintf(out, outlen, "?"); return; }
    char buf[16];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) { snprintf(out, outlen, "?"); return; }
    if (n > 0 && buf[n - 1] == '\n') n--;
    int m = n < outlen - 1 ? n : outlen - 1;
    for (int i = 0; i < m; i++) out[i] = buf[i];
    out[m] = '\0';
}

static bool is_on(const char *path)
{
    char b[8];
    read_dev_str(path, b, sizeof(b));
    return strncmp(b, "on", 2) == 0;
}

// The field warmer's state names (warmer.cpp's state_str).
static const char *field_state(bool auto_mode, const char *phase)
{
    if (!auto_mode) return "man";
    if (strncmp(phase, "idle", 4) == 0)  return "idl";
    if (strncmp(phase, "boost", 5) == 0) return "p1A";
    if (strncmp(phase, "coast", 5) == 0) return "coa";
    if (strncmp(phase, "pid", 3) == 0)   return "pid";
    if (strncmp(phase, "safe", 4) == 0)  return "saf";
    return "?";
}

static int monitor_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    char mode[8];
    read_dev_str("/dev/heaterauto", mode, sizeof(mode));
    bool auto_mode = strncmp(mode, "on", 2) == 0;

    char phase[8];
    read_dev_str("/dev/state", phase, sizeof(phase));

    float setp     = read_dev("/dev/setpoint");
    float skin     = read_dev("/dev/skintemp");
    float amb      = read_dev("/dev/ambient");
    float heat     = read_dev("/dev/heater");
    float curr     = read_dev("/dev/current");
    float integral = read_dev("/dev/pid/integral");
    float prevmeas = read_dev("/dev/pid/prevmeas");
    float ff       = read_dev("/dev/pid/bias");
    char guard[4];
    read_dev_str("/dev/guard", guard, sizeof(guard));

    int pos = 0;
    if (line_idx % 10 == 0) {
        int avail = outlen - pos;
        int n = snprintf(out + pos, avail,
            "state setp skin amb  heat  low  high  curr fail warn pid_i  pid_pm  ff\n");
        if (n >= 0 && n < avail) pos += n;
    }
    line_idx++;

    int avail = outlen - pos;
    int n = snprintf(out + pos, avail,
        "[%s] %.1f %.1f %.1f %.1f   %d     %d   %d   %d   %d   %+.2f  %.2f  %.1f\n",
        field_state(auto_mode, phase), setp, skin, amb, heat,
        is_on("/dev/alarm/templow"), is_on("/dev/alarm/temphigh"), (int)curr,
        is_on("/dev/alarm/heater"), strncmp(guard, "ok", 2) != 0,
        integral, prevmeas, ff);
    if (n >= 0 && n < avail) pos += n;

    return pos;
}

static const program_t prog_monitor = {"monitor", monitor_run};

void prog_monitor_register(void)
{
    prog_register(&prog_monitor);
}
