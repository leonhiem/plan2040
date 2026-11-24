/**
 * apps/babywarmer/dev/relay.cpp — /dev/relay: the mechanical safety relay
 *
 *   echo on > /dev/relay      energized: the heater can heat
 *   echo off > /dev/relay     released (also at boot, board_pins_init())
 *
 * The guard (dev/guard.cpp) switches it: off on overtemperature and on a
 * stuck SSR, on when heating. Readable, because "no current" only means
 * something with the relay on.
 */
#include "board.h"
#include "kernel/fs.h"
#include "hardware/gpio.h"
#include <cstdio>
#include <cstring>

static int relay_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0) {
        gpio_put(PIN_HEATERSAFE, 1);
        return len;
    }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        gpio_put(PIN_HEATERSAFE, 0);
        return len;
    }
    return -1;
}

static int relay_read(char *buf, int len)
{
    return snprintf(buf, len, gpio_get(PIN_HEATERSAFE) ? "on\n" : "off\n");
}

static const device_t dev_relay = {
    "/dev/relay",
    0,
    0,
    relay_read,
    relay_write,
};

void dev_relay_register(void)
{
    fs_register(&dev_relay);
}
