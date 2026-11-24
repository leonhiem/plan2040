/**
 * apps/babywarmer/prog/fftable.cpp — fftable: the room temperature in, the
 * feed-forward power out (fftable.h)
 *
 *   cat /dev/ambient | fftable > /dev/ffpower &
 *
 * The twin of safelut, stateless and ungated the same way. When the room
 * sensor fails, `cat` fails and nothing is written: /dev/ffpower then
 * keeps the last value for 10 minutes, and gives 30 % after that
 * (dev/phase.cpp). Added 2026-11-24 (TODO.md idea 3, step 5).
 */
#include "kernel/prog.h"
#include "kernel/errstr.h"
#include <cstdio>
#include <cstdlib>

#include "fftable.h"

static int fftable_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)argc;
    (void)argv;
    char buf[16];
    int n = inlen < (int)sizeof(buf) - 1 ? inlen : (int)sizeof(buf) - 1;
    for (int i = 0; i < n; i++) buf[i] = in[i];
    buf[n] = '\0';
    char *end;
    float t_room = strtof(buf, &end);
    if (end == buf) { werrstr("fftable: not a room temperature: '%.15s'", buf); return -1; }
    return snprintf(out, outlen, "%.1f\n", fftable_power(t_room));
}

static const program_t prog_fftable = {"fftable", fftable_run};

void prog_fftable_register(void)
{
    prog_register(&prog_fftable);
}
