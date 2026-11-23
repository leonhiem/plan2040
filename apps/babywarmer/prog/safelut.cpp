/**
 * apps/babywarmer/prog/safelut.cpp — safelut: ambient-temp lookup table, safe-mode's
 * open-loop power source
 *
 * Reads ambient temp from piped input (e.g. `cat /dev/ambient |
 * safelut`), linearly interpolates babywarmer's own safe_ff_table
 * (recovered verbatim from heater.cpp's safe_feedforward()/
 * feedforward_lookup()), clamps to DUTY_SAFE_CAP (55%), outputs the
 * result. Meant to feed /dev/safepower:
 *
 *   cat /dev/ambient | safelut > /dev/safepower &
 *
 * Stateless and ungated on purpose, unlike prog/pid.cpp -- a table
 * lookup has no windup/derivative concerns, so there's nothing to
 * freeze while safe mode isn't active. Always computing is harmless,
 * same reasoning as thresh/hyst always evaluating regardless of
 * whether their output is currently the one prog/select.cpp forwards.
 *
 * The table and its cap are hardcoded here, same as hyst's heating-
 * only polarity is hardcoded -- this is specifically this warmer's
 * safe-mode curve, not a generic interpolator. Worth a tunable device
 * later if commissioning ever needs to adjust it live; not built now.
 *
 * From picoos @ 89c16e5 (prog/safelut.cpp), unchanged apart from the includes
 * and the register function's name (2026-11-22); the table moved to
 * safelut.cpp (2026-11-24), shared with the guard.
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
