/**
 * dev/p9.cpp — /dev/9p: how 9P between boards is doing
 *
 *   % cat /dev/9p
 *   serve: read and write, 2 fids in use
 *   import: /n/beta up
 *   link: /dev/eia0, frames 41 bad_crc 0 bad_cobs 1 too_long 0
 *
 * Also starts the "9p" task (9p/link.cpp), which does nothing until
 * `serve` or `import` takes /dev/eia0 for 9P. See 9p/p9.h.
 */
#include "9p/p9.h"
#include "kernel/fs.h"
#include <cstdio>

static int p9_read(char *buf, int len)
{
    static const char *how[] = { "off (no serve line)", "read and write", "read-only" };
    int n = snprintf(buf, len, "serve: %s, %d fids in use\n", how[srv_serving()], srv_fids());
    if (n < len) n += mnt_status(buf + n, len - n);
    if (n < len) {
        const Framer *f = link_framer();
        if (link_started())
            n += snprintf(buf + n, len - n, "link: /dev/eia0, frames %lu bad_crc %lu bad_cobs %lu too_long %lu\n",
                          (unsigned long)f->frames, (unsigned long)f->bad_crc,
                          (unsigned long)f->bad_cobs, (unsigned long)f->too_long);
        else
            n += snprintf(buf + n, len - n, "link: /dev/eia0 not used for 9P yet\n");
    }
    return n < len ? n : len;
}

static const device_t dev_p9 = {"/dev/9p", 0, 0, p9_read, 0};

void dev_p9_register(void)
{
    link_init();
    fs_register(&dev_p9);
}
