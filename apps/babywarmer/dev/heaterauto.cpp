/**
 * apps/babywarmer/dev/heaterauto.cpp — /dev/heaterauto: read/write, auto (PID) vs manual
 *
 * "on" = automatic/PID control active, "off" = manual (open-loop).
 * Named so the on/off value reads unambiguously on its own -- "is auto
 * control on?" -- rather than needing you to remember which of two
 * unrelated modes "on" happens to mean. Same on/off vocabulary as
 * /dev/lamp/alarm/relay -- specifically so the existing toggle program
 * works against it completely unchanged:
 *
 *   toggle /dev/buttons/manual /dev/heaterauto &
 *
 * makes the physical MANUAL button flip it, same shape as the lamp's
 * button toggle. Not auto-started from main() -- like the lamp demo,
 * it's a job you start yourself when you want it running; ask if
 * you'd rather it start automatically at boot.
 *
 * Scope, deliberately: this device only stores and reports the mode.
 * It does not yet change how heaterpower gets computed -- /dev/heater
 * still applies whatever's written to it directly, in either mode.
 * Wiring auto mode to an actual control loop is its own, later step.
 *
 * From picoos @ 89c16e5 (dev/heaterauto.cpp), unchanged apart from the includes
 * and the register function's name (2026-11-22).
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static bool auto_mode = false; // starts in manual

static int heaterauto_read(char *buf, int len)
{
    return snprintf(buf, len, auto_mode ? "on\n" : "off\n");
}

// Every switch to manual starts at 10 %, no memory of an earlier value:
// the field warmer's customer request (warmer.cpp, SETPOINT_PCT_DEF;
// plan2040 2026-11-24, step 5). Whoever sets another manual power (the
// guard in none) writes /dev/percent after switching.
static void manual_default(void)
{
    int fd = fs_open("/dev/percent");
    if (fd < 0) return;
    fs_write(fd, "10", 2);
    fs_close(fd);
}

static int heaterauto_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { auto_mode = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        if (auto_mode) manual_default();
        auto_mode = false;
        return len;
    }
    return -1;
}

static const device_t dev_heaterauto = {
    "/dev/heaterauto",
    0,
    0,
    heaterauto_read,
    heaterauto_write,
};

void dev_heaterauto_register(void)
{
    fs_register(&dev_heaterauto);
}
