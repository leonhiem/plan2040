/**
 * prog/import.cpp — import /dev/eia0 [/n/<name>]: the other board's files here
 *
 *   import /dev/eia0           mounts it as /n/<its /dev/sysname>, e.g. /n/beta
 *   import /dev/eia0 /n/bench  under a name of your own; also when it
 *                              doesn't answer yet: then it keeps trying
 *
 * Then `cat /n/beta/dev/rtc`, `echo on > /n/beta/dev/lamp`. The other
 * board must `serve`. Plan 9's import(4). See 9p/p9.h.
 */
#include "kernel/prog.h"
#include "kernel/errstr.h"
#include "9p/p9.h"
#include <cstdio>
#include <cstring>

static int import_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc < 1 || argc > 2) { werrstr("usage: import /dev/eia0 [/n/<name>]"); return -1; }
    if (strcmp(argv[0], "/dev/eia0") != 0) { werrstr("%s: only /dev/eia0 for now", argv[0]); return -1; }
    char msg[ERRMAX + 40];
    if (mnt_import(argc == 2 ? argv[1] : 0, msg, sizeof(msg)) < 0) {
        werrstr("%s: %s", argv[0], msg);
        return -1;
    }
    return snprintf(out, outlen, "%s", msg);
}

static const program_t prog_import = {"import", import_run};

void prog_import_register(void)
{
    prog_register(&prog_import);
}
