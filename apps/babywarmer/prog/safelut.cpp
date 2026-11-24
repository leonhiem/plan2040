/**
 * apps/babywarmer/prog/safelut.cpp — safelut: the room temperature in, the
 * safe mode's power out (safelut.h)
 *
 *   cat /dev/ambient | safelut
 *
 * What the guard heats with when the skin reading can't be trusted, for a
 * look from the shell. The warmer doesn't run it as a job: the guard uses
 * the table itself.
 */
#include "kernel/prog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "safelut.h"

static int safelut_run(const char *in, int inlen, int argc, char **argv, char *out, int outlen)
{
    (void)argc;
    (void)argv;

    char buf[16];
    int n = inlen < (int)sizeof(buf) - 1 ? inlen : (int)sizeof(buf) - 1;
    for (int i = 0; i < n; i++) buf[i] = in[i];
    buf[n] = '\0';
    float t_room = strtof(buf, 0);

    float pwr = safelut_power(t_room);

    return snprintf(out, outlen, "%.1f\n", pwr);
}

static const program_t prog_safelut = {"safelut", safelut_run};

void prog_safelut_register(void)
{
    prog_register(&prog_safelut);
}
