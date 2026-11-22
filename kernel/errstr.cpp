#include "kernel/errstr.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

static char err[ERRMAX];

void werrstr(const char *fmt, ...)
{
    char tmp[ERRMAX];          // formatted aside first: an argument may be err itself
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    memcpy(err, tmp, sizeof(err));
}

const char *errstr(void)
{
    return err;
}

void errstr_clear(void)
{
    err[0] = '\0';
}
