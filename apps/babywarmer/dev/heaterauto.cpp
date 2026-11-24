/**
 * apps/babywarmer/dev/heaterauto.cpp — /dev/heaterauto: on = auto, off = manual
 *
 *   toggle /dev/buttons/manual /dev/heaterauto &    the MANUAL button
 *
 * Starts off (manual). Every switch to manual sets /dev/percent to 10 %,
 * no memory of an earlier value (the field warmer's customer request);
 * whoever wants another manual power (the guard in none) writes
 * /dev/percent after switching.
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static bool auto_mode = false; // starts in manual

static int heaterauto_read(char *buf, int len)
{
    return snprintf(buf, len, auto_mode ? "on\n" : "off\n");
}

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
