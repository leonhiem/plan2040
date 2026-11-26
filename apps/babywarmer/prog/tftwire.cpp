/**
 * apps/babywarmer/prog/tftwire.cpp — tftwire: the TFT's fields from the
 * warmer's devices, in one job (as ledwire does for the LEDs)
 *
 *   aut, man   /dev/heaterauto              chk   /dev/alarm/sensor or room (warn or on)
 *   low, high  /dev/alarm/templow, temphigh fail  /dev/alarm/heater
 *   heater     /dev/heater (0-100)
 *   face       the skin vs the setpoint (Leon, 2026-11-26: how warm the
 *              baby is, not whether an alarm is on): cold more than 1 C
 *              below, hot more than 1 C above, 0.2 C hysteresis; also during
 *              a warm-up. While the guard doesn't trust the reading (safe,
 *              none) it stays as it was: the display has no "no face".
 *   apgar      the START button's one-shot ("1" for one tick): the display
 *              starts its APGAR clock on the rising edge
 *
 * (No warm: the heater bar's colour shows it.) The redrawing is the
 * "tftflush" kernel task's (dev/tft.cpp), not this job's.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static bool dev_is(const char *path, const char *want)
{
    int fd = fs_open(path);
    if (fd < 0) return false;
    char buf[8];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) return false;
    size_t wlen = strlen(want);
    return (size_t)n >= wlen && strncmp(buf, want, wlen) == 0;
}

#define FACE_C      1.0f   // the face: cold/hot this far off the setpoint ...
#define FACE_HYST_C 0.2f   // ... and back this far inside, so it doesn't flicker

static bool read_num(const char *path, float *v)
{
    char buf[16];
    int fd = fs_open(path);
    if (fd < 0) return false;
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    char *end;
    *v = strtof(buf, &end);
    return end != buf;
}

static void set_word(const char *path, const char *v)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static void set_tft(const char *path, bool on)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    const char *v = on ? "on" : "off";
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int tftwire_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    bool auto_mode = dev_is("/dev/heaterauto", "on");
    set_tft("/dev/tft/aut", auto_mode);
    set_tft("/dev/tft/man", !auto_mode);

    set_tft("/dev/tft/chk", !dev_is("/dev/alarm/sensor", "off") || !dev_is("/dev/alarm/room", "off"));

    set_tft("/dev/tft/low",  dev_is("/dev/alarm/templow",  "on") || dev_is("/dev/alarm/templow",  "warn"));
    set_tft("/dev/tft/high", dev_is("/dev/alarm/temphigh", "on") || dev_is("/dev/alarm/temphigh", "warn"));
    set_tft("/dev/tft/fail", dev_is("/dev/alarm/heater",   "on"));

    static int face;                                 // -1 cold, 0 ok, 1 hot
    float skin, sp;
    if (dev_is("/dev/guard", "ok") && read_num("/dev/skintemp", &skin) && read_num("/dev/setpoint", &sp)) {
        float d = skin - sp;
        if (d < -FACE_C || (face < 0 && d < -(FACE_C - FACE_HYST_C)))     face = -1;
        else if (d > FACE_C || (face > 0 && d > FACE_C - FACE_HYST_C))   face = 1;
        else                                                             face = 0;
        set_word("/dev/tft/face", face < 0 ? "cold" : face > 0 ? "hot" : "ok");
    }

    int fd = fs_open("/dev/heater");
    if (fd >= 0) {
        char buf[8];
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n > 0) {
            buf[n] = '\0';
            int fd2 = fs_open("/dev/tft/heater");
            if (fd2 >= 0) {
                char num[8];
                int nlen = snprintf(num, sizeof(num), "%d", atoi(buf));
                fs_write(fd2, num, nlen);
                fs_close(fd2);
            }
        }
    }

    set_tft("/dev/tft/apgar", dev_is("/dev/buttons/start", "1"));

    return snprintf(out, outlen, "ok\n");
}

static const program_t prog_tftwire = {"tftwire", tftwire_run};

void prog_tftwire_register(void)
{
    prog_register(&prog_tftwire);
}
