/**
 * dev/sysname.cpp — /dev/sysname: this board's name (Plan 9's name for it)
 *
 *   echo alpha > /fram/sysname      name the board (kept across power cycles)
 *   cat /dev/sysname                alpha
 *
 * Other boards will find this one as /n/<name> over 9P (plan2040_ideas/
 * IDEAS.md, section 1). The name is the first word of /fram/sysname:
 * letters, digits and '-', at most SYSNAME_MAX characters. Without that
 * file, or with a name that isn't valid, the board is "rp-" and the last
 * 4 hex digits of its flash chip's unique ID (the same ID as the USB
 * serial number, so `ls /dev/serial/by-id` on a PC tells which is which):
 * two fresh boards never get the same name.
 */
#include "kernel/fs.h"
#include "pico/unique_id.h"
#include <cstdio>

#define SYSNAME_MAX 15

static bool valid(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-';
}

// This board's name, nul-terminated, in buf (at least SYSNAME_MAX + 1).
int sysname_get(char *buf, int len)
{
    char file[64];
    int n = -1;
    int fd = fs_open("/fram/sysname");
    if (fd >= 0) {
        n = fs_read(fd, file, sizeof(file) - 1);
        fs_close(fd);
    }
    int i = 0, k = 0;
    if (n > 0) {
        file[n] = '\0';
        while (i < n && (file[i] == ' ' || file[i] == '\t')) i++;
        while (i < n && valid(file[i]) && k < SYSNAME_MAX && k < len - 1)
            buf[k++] = file[i++];
        // A name must end at a space or the end of the line, not in the
        // middle of something invalid or too long.
        if (i < n && file[i] != '\n' && file[i] != ' ' && file[i] != '\t' && file[i] != '\r')
            k = 0;
    }
    if (k == 0) {
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        return snprintf(buf, len, "rp-%02x%02x",
                        id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2],
                        id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1]);
    }
    buf[k] = '\0';
    return k;
}

static int sysname_read(char *buf, int len)
{
    char name[SYSNAME_MAX + 1];
    sysname_get(name, sizeof(name));
    return snprintf(buf, len, "%s\n", name);
}

static const device_t dev_sysname = {"/dev/sysname", 0, 0, sysname_read, 0};

void dev_sysname_register(void)
{
    fs_register(&dev_sysname);
}
