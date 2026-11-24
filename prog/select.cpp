/**
 * prog/select.cpp — select <state> <label>=<source> [<label>=<source> ...]
 *
 *   select /dev/state idle=/dev/percent pid=/dev/setpoint safe=/dev/heater > /dev/seg7small &
 *
 * follow's N-way sibling: outputs the <source> whose <label> matches
 * <state>'s value (a device path or a number); no match gives 0. Run as a
 * job, feeding a target with `>`.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static float read_value(const char *arg)
{
    int fd = fs_open(arg);
    if (fd >= 0) {
        char buf[16];
        int n = fs_read(fd, buf, sizeof(buf) - 1);
        fs_close(fd);
        if (n >= 0) { buf[n] = '\0'; return strtof(buf, 0); }
    }
    return strtof(arg, 0);
}

static int select_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 1) return -1;

    int sfd = fs_open(argv[0]);
    if (sfd < 0) return -1;
    char sbuf[16];
    int sn = fs_read(sfd, sbuf, sizeof(sbuf) - 1);
    fs_close(sfd);
    if (sn < 0) return -1;
    sbuf[sn] = '\0';

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) continue; // malformed pair, skip rather than error the whole job
        size_t label_len = eq - argv[i];
        if ((size_t)sn >= label_len && strncmp(sbuf, argv[i], label_len) == 0) {
            float v = read_value(eq + 1);
            return snprintf(out, outlen, "%.1f\n", v);
        }
    }

    return snprintf(out, outlen, "0\n"); // no matching label -- safe default
}

static const program_t prog_select = {"select", select_run};

void prog_select_register(void)
{
    prog_register(&prog_select);
}
