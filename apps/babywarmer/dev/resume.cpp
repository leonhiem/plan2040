/**
 * apps/babywarmer/dev/resume.cpp — after a short power cut, carry on as before
 *
 *   cat /fram/resume     auto 36.5 10 2026-11-24 10:31:05
 *                        (mode, setpoint, manual %, the RTC time of the save)
 *   cat /dev/resume      what this boot did, and why:
 *                        restored: auto, 36.5 C, manual 10 %; power was off 3 min
 *                        defaults: power was off 25 min (more than 10)
 *
 * During a power cut the RP2040 is dead and the hardware's own buzzer
 * sounds, from a supercapacitor. Power back is a cold boot: without this
 * the warmer would start with the defaults (auto, setpoint 35.0), and a
 * baby's setpoint or manual mode would silently be lost.
 *
 * The "resume" kernel task, once a second, saves the nurse's settings in
 * /fram/resume when they change, and the RTC time at least once a minute.
 * At its first run after boot it reads them back, and restores them when
 * the power was off less than RESUME_MAX_S (10 minutes): after a longer
 * outage the baby under the warmer may be another one. Anything it can't
 * be sure of (no file, an unreadable one, the RTC without a valid time,
 * the clock gone back) means the defaults, with the reason in /dev/resume.
 *
 * A setpoint above 37.5 C comes back as 37.5 (/dev/setpoint clamps it:
 * higher must be on purpose again). Auto mode restarts with a warm-up,
 * watched by the guard as any switch to auto. While the guard says
 * "none" it has switched to manual itself, not the nurse: the mode and
 * the manual % saved before stay, the time is still saved.
 *
 * framfs replaces a file safely: a cut during a save leaves the old one.
 */
#include "kernel/fs.h"
#include "kernel/task.h"
#include "kernel/errstr.h"
#include "fs/framfs.h"
#include "drivers/pcf8563.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define RESUME_MAX_S  600     // restore only after an outage shorter than this
#define SAVE_EVERY_S  60      // the RTC time saved at least this often
#define FILE_NAME     "resume"

struct settings { bool autom; float setpoint, percent; };

static char why[120] = "not yet (the resume task runs 1 s after boot)";
static bool restored_once;
static settings saved;          // what /fram/resume holds now
static int  since_save_s;

// ---- small helpers ----------------------------------------------------------

static int read_dev(const char *path, char *b, int len)
{
    int fd = fs_open(path);
    if (fd < 0) return -1;
    int n = fs_read(fd, b, len - 1);
    fs_close(fd);
    b[n < 0 ? 0 : n] = '\0';
    return n;
}

static void write_dev(const char *path, const char *v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

// Seconds since an arbitrary day: only differences are used.
static long seconds(const rtc_time &t)
{
    long y = t.year, m = t.month;
    if (m <= 2) { y--; m += 12; }
    long days = 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + t.day;
    return ((days * 24 + t.hour) * 60 + t.min) * 60 + t.sec;
}

// The RTC's time, or false with the reason in *err.
static bool now(rtc_time *t, const char **err)
{
    if (!rtc_get(t))  { *err = errstr(); return false; }
    if (t->vl)        { *err = "the RTC lost its time (VL; set it: echo \"YYYY-MM-DD HH:MM:SS\" > /dev/rtc)"; return false; }
    if (!rtc_valid(t)) { *err = "the RTC's time is out of range"; return false; }
    return true;
}

static settings current(void)
{
    char b[24];
    settings s;
    s.autom    = read_dev("/dev/heaterauto", b, sizeof(b)) > 0 && strncmp(b, "on", 2) == 0;
    s.setpoint = read_dev("/dev/setpoint", b, sizeof(b)) > 0 ? strtof(b, 0) : 35.0f;
    s.percent  = read_dev("/dev/percent", b, sizeof(b)) > 0 ? strtof(b, 0) : 10.0f;
    return s;
}

// ---- restore, once at boot -------------------------------------------------------

static void restore(void)
{
    char line[64];
    int n = framfs_read(FILE_NAME, line, sizeof(line) - 1);
    if (n < 0) { snprintf(why, sizeof(why), "defaults: /fram/resume: %s", errstr()); return; }
    line[n] = '\0';

    char mode[8], date[24];
    settings s;
    rtc_time then;
    if (sscanf(line, "%7s %f %f %23[0-9: -]", mode, &s.setpoint, &s.percent, date) != 4
        || (strcmp(mode, "auto") != 0 && strcmp(mode, "manual") != 0)
        || !rtc_parse(date, &then)) {
        snprintf(why, sizeof(why), "defaults: /fram/resume not understood");
        return;
    }
    s.autom = strcmp(mode, "auto") == 0;

    rtc_time t;
    const char *err;
    if (!now(&t, &err)) { snprintf(why, sizeof(why), "defaults: %s", err); return; }
    long off = seconds(t) - seconds(then);
    if (off < 0) { snprintf(why, sizeof(why), "defaults: the clock is earlier than the last save"); return; }
    if (off >= RESUME_MAX_S) {
        snprintf(why, sizeof(why), "defaults: power was off %ld min (more than %d)", off / 60, RESUME_MAX_S / 60);
        return;
    }

    // The setpoint first (its range clamps), then the mode (a switch to
    // manual resets the %), then the %.
    char v[16];
    snprintf(v, sizeof(v), "%.1f", s.setpoint);
    write_dev("/dev/setpoint", v);
    write_dev("/dev/heaterauto", s.autom ? "on" : "off");
    snprintf(v, sizeof(v), "%.0f", s.percent);
    write_dev("/dev/percent", v);

    settings now_s = current();
    snprintf(why, sizeof(why), "restored: %s, %.1f C, manual %.0f %%; power was off %ld min",
             now_s.autom ? "auto" : "manual", now_s.setpoint, now_s.percent, (off + 30) / 60);
}

// ---- save --------------------------------------------------------------------

static void save(const settings &s)
{
    rtc_time t;
    const char *err;
    if (!now(&t, &err)) return;        // no time: an old save, so a later boot takes the defaults
    char date[24], line[64];
    rtc_format(date, sizeof(date), &t);
    int n = snprintf(line, sizeof(line), "%s %.1f %.0f %s\n", s.autom ? "auto" : "manual", s.setpoint, s.percent, date);
    if (framfs_write(FILE_NAME, line, n, false) == n) { saved = s; since_save_s = 0; }
}

static void resume_task(void)
{
    if (!restored_once) {
        restored_once = true;
        restore();
        printf("resume: %s\n", why);
        save(current());
        return;
    }
    settings s = current();
    char g[8];
    if (read_dev("/dev/guard", g, sizeof(g)) > 0 && strncmp(g, "none", 4) == 0) {
        s.autom = saved.autom;         // the guard's manual, not the nurse's
        s.percent = saved.percent;
    }
    bool changed = s.autom != saved.autom || s.setpoint != saved.setpoint || s.percent != saved.percent;
    if (changed || ++since_save_s >= SAVE_EVERY_S) save(s);
}

// ---- /dev/resume -------------------------------------------------------------

static int resume_read(char *buf, int len) { return snprintf(buf, len, "%s\n", why); }

static const device_t dev_resume = { "/dev/resume", 0, 0, resume_read, 0 };

void dev_resume_register(void)
{
    fs_register(&dev_resume);
    task_register("resume", resume_task, 1000);
}
