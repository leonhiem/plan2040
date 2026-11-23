// tools/ntclut: check an NTC calibration text on the PC, before it goes to
// the board (cp ... /dev/ntc/lut). It runs the same generator as the board
// (apps/babywarmer/ntclut.cpp), so what it prints is what the board makes.
//
//   ntclut [file]            the text as the board will store it (#= lines:
//                            method, coefficients, the error at each point,
//                            the valid range), then a table: per whole
//                            degree, the ADC code where it's crossed
//   ntclut -c code... [file] the temperature for these ADC codes instead
//
// No file: the text from stdin. Refused: the reason, as the board says it,
// and exit status 1.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "ntclut.h"
#include "kernel/errstr.h"

static ntclut_t lut;
static char text[NTCLUT_TEXT_MAX * 2 + 1], out[NTCLUT_TEXT_MAX * 2 + 1];

static void usage(void)
{
    fprintf(stderr, "usage: ntclut [-c code ...] [file]\n");
    exit(2);
}

// The code where the temperature crosses each whole degree (the table may
// run either way: the bridge makes warmer = lower code).
static void degrees(void)
{
    float prev = 0, t;
    bool have = false;
    int first = 0, last = 0, n = 0;
    printf("# per whole degree: the ADC code where it's crossed\n");
    printf("#    C    code\n");
    for (int code = -32768; code <= 32767; code++) {
        if (!ntclut_lookup(&lut, (int16_t)code, &t)) { have = false; continue; }
        if (n++ == 0) first = code;
        last = code;
        if (have && floorf(prev) != floorf(t)) {
            float d = (t > prev) ? floorf(t) : floorf(prev);
            printf("# %5.0f  %6d\n", d, code);
        }
        prev = t;
        have = true;
    }
    if (n == 0) { printf("# (nothing calibrated)\n"); return; }
    float t1, t2;
    ntclut_lookup(&lut, (int16_t)first, &t1);
    ntclut_lookup(&lut, (int16_t)last, &t2);
    printf("# calibrated: codes %d .. %d (%.2f .. %.2f C)\n", first, last, t1, t2);
}

int main(int argc, char **argv)
{
    int codes[64], ncodes = 0, i = 1;
    bool lookup = false;
    if (i < argc && strcmp(argv[i], "-c") == 0) {
        lookup = true;
        for (i++; i < argc; i++) {
            char *end;
            long v = strtol(argv[i], &end, 10);
            if (*end) break;                      // not a number: the file
            if (v < -32768 || v > 32767 || ncodes == 64) usage();
            codes[ncodes++] = (int)v;
        }
        if (ncodes == 0) usage();
    }
    if (argc - i > 1) usage();

    FILE *f = stdin;
    const char *name = "stdin";
    if (i < argc) {
        name = argv[i];
        if (!(f = fopen(name, "r"))) { perror(name); return 1; }
    }
    int len = (int)fread(text, 1, sizeof text - 1, f);
    if (!feof(f)) { fprintf(stderr, "ntclut: %s: longer than %d bytes\n", name, (int)sizeof text - 1); return 1; }
    text[len] = 0;

    if (!ntclut_make(text, len, &lut, out, sizeof out)) {
        fprintf(stderr, "ntclut: %s: %s\n", name, errstr());
        return 1;
    }
    if (lookup) {
        for (int k = 0; k < ncodes; k++) {
            float t;
            if (ntclut_lookup(&lut, (int16_t)codes[k], &t)) printf("%6d  %6.2f C\n", codes[k], t);
            else printf("%6d  %s\n", codes[k], errstr());
        }
        return 0;
    }
    fputs(out, stdout);
    degrees();
    return 0;
}
