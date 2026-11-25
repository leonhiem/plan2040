/**
 * prog/serve.cpp — serve [-r | off]: answer 9P from the other board
 *
 *   serve        this board's files, read and write, over /dev/eia0
 *   serve -r     read only (a babywarmer: decided 2026-11-24)
 *   serve off    stop answering ("<name> doesn't serve")
 *
 * Nothing is served until a serve line says so: in the boot script, or
 * typed. Plan 9's exportfs (with -R for read-only). See 9p/p9.h.
 */
#include "kernel/prog.h"
#include "kernel/errstr.h"
#include "9p/p9.h"
#include <cstdio>
#include <cstring>

static int serve_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)in; (void)inlen;
    int how = SRV_RW;
    if (argc == 1 && !strcmp(argv[0], "-r")) how = SRV_RO;
    else if (argc == 1 && !strcmp(argv[0], "off")) how = SRV_OFF;
    else if (argc != 0) { werrstr("usage: serve [-r | off]"); return -1; }
    srv_serve(how);
    char name[16];
    sysname_get(name, sizeof(name));
    if (how == SRV_OFF) return snprintf(out, outlen, "%s: not serving\n", name);
    return snprintf(out, outlen, "%s: serving over /dev/eia0%s\n", name, how == SRV_RO ? ", read-only" : "");
}

static const program_t prog_serve = {"serve", serve_run};

void prog_serve_register(void)
{
    prog_register(&prog_serve);
}
