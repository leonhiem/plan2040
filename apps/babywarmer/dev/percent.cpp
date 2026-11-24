/**
 * apps/babywarmer/dev/percent.cpp — /dev/percent: the manual heater power
 *
 *   echo 25 > /dev/percent    set it, within 0-80 % (the field warmer's maximum)
 *
 * 10 % at boot and on every switch to manual (dev/heaterauto.cpp). UP/DOWN
 * step it by 5 % in manual (`adjust` in the boot script); `follow` sends it
 * to the heater while /dev/heaterauto is off.
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>

#define PERCENT_DEF 10.0f // matches the original warmer's manual-mode default
#define PERCENT_MAX 80.0f // the field warmer's manual maximum

static float percent = PERCENT_DEF;

static void clamp(void)
{
    if (percent < 0.0f)   percent = 0.0f;
    if (percent > PERCENT_MAX) percent = PERCENT_MAX;
}

static int percent_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", percent);
}

static int percent_write(const char *buf, int len)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number

    percent = v;
    clamp();
    return len;
}

static const device_t dev_percent = {
    "/dev/percent",
    0,
    0,
    percent_read,
    percent_write,
};

void dev_percent_register(void)
{
    fs_register(&dev_percent);
}
