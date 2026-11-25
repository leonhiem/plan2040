/**
 * dev/eia.cpp — /dev/eia0 and /dev/eia0ctl: the serial link to another
 * board (Plan 9's names for serial ports: eia0, eia0ctl)
 *
 *   echo hello > /dev/eia0          send "hello\n" to the other board
 *   cat /dev/eia0                   what has arrived since the last read
 *                                   (nothing: an empty line; never waits)
 *   cat /dev/eia0 > /dev/cons &     show whatever arrives, as it arrives
 *   cat /dev/eia0ctl                speed, flow control, CTS, counters
 *   echo b115200 > /dev/eia0ctl     another speed (both boards the same)
 *   echo m0 > /dev/eia0ctl          flow control off (m1: on, the default)
 *
 * The raw link, for testing the wires; 9P will use the driver
 * (drivers/eia.h) underneath, with its own framing.
 */
#include "drivers/eia.h"
#include "kernel/fs.h"
#include "kernel/errstr.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int eia0_read(char *buf, int len)  { return eia_read(buf, len); }
static int eia0_write(const char *buf, int len) { return eia_write(buf, len); }

static int ctl_read(char *buf, int len) { return eia_status(buf, len); }

static int ctl_write(const char *buf, int len)
{
    char tmp[24];
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' ')) len--;
    if (len <= 0 || len >= (int)sizeof(tmp)) goto bad;
    memcpy(tmp, buf, len);
    tmp[len] = '\0';
    if (tmp[0] == 'b' && tmp[1] >= '0' && tmp[1] <= '9') {
        char *end;
        unsigned long b = strtoul(tmp + 1, &end, 10);
        if (*end) goto bad;
        return eia_set_baud(b) ? len : -1;
    }
    if (!strcmp(tmp, "m0")) { eia_set_flow(false); return len; }
    if (!strcmp(tmp, "m1")) { eia_set_flow(true);  return len; }
bad:
    werrstr("want b<speed> (e.g. b115200), m1 (flow control on) or m0 (off)");
    return -1;
}

static const device_t dev_eia0    = {"/dev/eia0",    0, 0, eia0_read, eia0_write};
static const device_t dev_eia0ctl = {"/dev/eia0ctl", 0, 0, ctl_read,  ctl_write};

void dev_eia_register(void)
{
    if (!eia_init())
        printf("eia0: %s\n", errstr());
    fs_register(&dev_eia0);
    fs_register(&dev_eia0ctl);
}
