/**
 * prog/unmount.cpp — unmount /n/<name>: undo an import
 *
 *   unmount /n/beta      /n/beta is gone; `import /dev/eia0` can run again
 *
 * Plan 9's unmount (for what import mounted). See 9p/p9.h.
 */
#include "kernel/prog.h"
#include "kernel/errstr.h"
#include "9p/p9.h"
#include <cstdio>

static int unmount_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    if (argc != 1) { werrstr("usage: unmount /n/<name>"); return -1; }
    char msg[ERRMAX];
    if (mnt_unmount(argv[0], msg, sizeof(msg)) < 0) { werrstr("%s", msg); return -1; }
    return snprintf(out, outlen, "%s", msg);
}

static const program_t prog_unmount = {"unmount", unmount_run};

void prog_unmount_register(void)
{
    prog_register(&prog_unmount);
}
