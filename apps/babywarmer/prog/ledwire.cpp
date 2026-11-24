/**
 * apps/babywarmer/prog/ledwire.cpp — ledwire: the seven front-panel LEDs and
 * the big display's blink, in one job
 *
 *   aut   auto mode (/dev/heaterauto on)     man   manual mode
 *   warm  boost (/dev/state)                 chk   the guard not ok
 *   low   /dev/alarm/templow (warn or on)    high  /dev/alarm/temphigh (warn or on)
 *   fail  /dev/alarm/heater
 *
 * and the big display: blinking while the guard says safe (the reading is
 * shown but not trusted), blank in none (no reading); the small display
 * blinks while the setpoint is above 37.5 C (/dev/setpoint/high). One job, not seven:
 * this is simply what the front panel means, nothing to rewire.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

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

static void set_led(const char *path, bool on)
{
    int fd = fs_open(path);
    if (fd < 0) return;
    const char *v = on ? "on" : "off";
    fs_write(fd, v, strlen(v));
    fs_close(fd);
}

static int ledwire_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    (void)argc;
    (void)argv;

    bool auto_mode = dev_is("/dev/heaterauto", "on");
    set_led("/dev/leds/aut", auto_mode);
    set_led("/dev/leds/man", !auto_mode);

    set_led("/dev/leds/warm", dev_is("/dev/state", "boost"));
    // CHK whenever the guard doesn't trust the skin reading, in auto and
    // manual; the big display blinks in safe (the reading may be the room)
    // and is blank in none (there is no reading).
    bool guard_ok   = dev_is("/dev/guard", "ok");
    bool guard_none = dev_is("/dev/guard", "none");
    set_led("/dev/leds/chk", !guard_ok);
    set_led("/dev/seg7big/blink", !guard_ok && !guard_none);
    // The small display blinks the setpoint while it is above 37.5 C.
    set_led("/dev/seg7small/blink", auto_mode && dev_is("/dev/setpoint/high", "on"));
    if (guard_none) set_led("/dev/seg7big", false);      // "off": no digits = blank

    set_led("/dev/leds/low",  dev_is("/dev/alarm/templow",  "on") || dev_is("/dev/alarm/templow",  "warn"));
    set_led("/dev/leds/high", dev_is("/dev/alarm/temphigh", "on") || dev_is("/dev/alarm/temphigh", "warn"));
    set_led("/dev/leds/fail", dev_is("/dev/alarm/heater",   "on"));

    return snprintf(out, outlen, "ok\n");
}

static const program_t prog_ledwire = {"ledwire", ledwire_run};

void prog_ledwire_register(void)
{
    prog_register(&prog_ledwire);
}
