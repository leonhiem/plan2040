/**
 * prog/follow.cpp — follow <gate> <on-source> <off-source> <target>
 *
 *   follow /dev/heaterauto /dev/autopower /dev/percent /dev/heater &
 *
 * Copies <on-source> to <target> while <gate> reads "on", else
 * <off-source>: one job, the only writer of the target, so two sources
 * never race over it (two jobs each writing the same target did: the
 * idle one's 0 stomped the other's value). Sources are device paths or
 * numbers.
 */
#include "kernel/prog.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstring>

static int follow_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in;
    (void)inlen;
    if (argc < 4) return -1;

    const char *gate       = argv[0];
    const char *source_on  = argv[1];
    const char *source_off = argv[2];
    const char *target     = argv[3];

    int gfd = fs_open(gate);
    if (gfd < 0) return -1;
    char gbuf[4];
    int gn = fs_read(gfd, gbuf, sizeof(gbuf) - 1);
    fs_close(gfd);
    bool gate_on = (gn >= 2 && strncmp(gbuf, "on", 2) == 0);

    const char *source = gate_on ? source_on : source_off;

    int sfd = fs_open(source);
    if (sfd < 0) return -1;
    char sbuf[16];
    int sn = fs_read(sfd, sbuf, sizeof(sbuf) - 1);
    fs_close(sfd);
    if (sn < 0) return -1;

    int tfd = fs_open(target);
    if (tfd < 0) return -1;
    fs_write(tfd, sbuf, sn);
    fs_close(tfd);

    return snprintf(out, outlen, "%s -> %s\n", source, target);
}

static const program_t prog_follow = {"follow", follow_run};

void prog_follow_register(void)
{
    prog_register(&prog_follow);
}
