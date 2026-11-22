/**
 * apps/babywarmer/dev/relay.cpp — /dev/relay: read/write, the mechanical safety relay
 *
 * write /dev/relay on   -> PIN_HEATERSAFE energized
 * write /dev/relay off  -> PIN_HEATERSAFE released
 *
 * PIN_HEATERSAFE was the safety cutoff in babywarmer's original
 * heater_check_task -- recovered as prog/alarmcheck.cpp, which writes
 * here through this same write() like any other consumer, not by
 * reaching around it. Still a bare manual toggle as far as this device
 * itself is concerned -- it doesn't know or care who's writing it,
 * same deliberately-raw spirit as /dev/heater.
 *
 * From picoos @ 89c16e5 (dev/relay.cpp), unchanged apart from the
 * includes and the register name, and readable (2026-11-23). Released at
 * boot (board_pins_init()).
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

// Readable since 2026-11-23 (plan2040): the heater-current check has to
// know the relay is energized before "no current" means anything.
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
