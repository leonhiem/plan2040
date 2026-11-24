/**
 * apps/babywarmer/dev/heater.cpp — /dev/heater: the heater power, 0-100 %
 *
 *   echo 25 > /dev/heater      ask for 25 % (on = 100, off = 0)
 *   cat /dev/heater            the power applied: what was asked, within
 *                              the guard's say (/dev/heater/limit)
 *   cat /dev/heater/ssr        the SSR now and for how long: "on 1300"
 *   cat /dev/heater/limit      "cap 80.0" (the jobs' power, at most that)
 *                              or "set 39.0" (the guard's own power)
 *
 * The "tpo" kernel task (heater.cpp, every 100 ms) switches the SSR over a
 * 15 s window and drives the bargraph. The heater only heats with the
 * safety relay on too (/dev/relay, the guard's).
 */
#include "board.h"
#include "heater.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int heater_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", heater_get_power());
}

static int heater_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { heater_set_power(100.0f); return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { heater_set_power(0.0f);   return len; }

    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float pct = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number, and not on/off either

    heater_set_power(pct);
    return len;
}

// /dev/heater/ssr: the guard's current check judges only a settled state.
static int ssr_read(char *buf, int len)
{
    uint32_t ms;
    bool on = heater_ssr(&ms);
    return snprintf(buf, len, "%s %lu\n", on ? "on" : "off", (unsigned long)ms);
}
static const device_t dev_heater_ssr = { "/dev/heater/ssr", 0, 0, ssr_read, 0 };

// /dev/heater/limit: the guard's say.
static int limit_read(char *buf, int len)
{
    float v;
    bool set = heater_guard_get(&v);
    return snprintf(buf, len, "%s %.1f\n", set ? "set" : "cap", v);
}
static const device_t dev_heater_limit = { "/dev/heater/limit", 0, 0, limit_read, 0 };

static const device_t dev_heater = {
    "/dev/heater",
    0,
    0,
    heater_read,
    heater_write,
};

void dev_heater_register(void)
{
    heater_set_power(0.0f);
    heater_pwm_init();
    task_register("tpo", tpo_apply, TPO_INTERVAL_TIME); // applies /dev/heater's value
    fs_register(&dev_heater);
    fs_register(&dev_heater_ssr);
    fs_register(&dev_heater_limit);
}
