// luttest: apps/babywarmer/ntclut.cpp, the NTC lookup table generator.
// The built-in table (bridge + MF58 B3950 datasheet curve) against the
// physics computed here independently; a Steinhart-Hart calibration from
// synthetic "measured" points (the exact MF58 curve, then with reading
// errors); the agnostic interpolation; and the errors a person can make
// in a calibration text.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include "ntclut.h"
#include "kernel/errstr.h"

static ntclut_t lut;
static char out[2048];

// The physics: the bridge and the Beta curve, independently of ntclut.cpp.
static double code_for(double tc, double beta)
{
    double r = 10000.0 * exp(beta * (1.0 / (tc + 273.15) - 1.0 / 298.15));
    double vntc = 3.3 * r / (47000.0 + r), vref = 3.3 * 10000.0 / 57000.0;
    return (vntc - vref) * 32768.0 / 1.024;
}

static void show(const char *title, const char *text)
{
    printf("--- %s\n", title);
    if (!ntclut_make(text, strlen(text), &lut, out, sizeof out)) { printf("  refused: %s\n", errstr()); return; }
    printf("%s", out);
}

static void check(double tc, double beta)
{
    int code = (int)lround(code_for(tc, beta));
    float t;
    if (ntclut_lookup(&lut, (int16_t)code, &t)) printf("  %5.1f C = code %6d -> %6.2f C (%+.3f)\n", tc, code, t, t - tc);
    else printf("  %5.1f C = code %6d -> %s\n", tc, code, errstr());
}

int main()
{
    show("the built-in table", NTCLUT_BUILTIN);
    int valid = 0; for (int i = 0; i < NTCLUT_ENTRIES; i++) if (lut.t[i] != NTCLUT_NONE) valid++;
    printf("  %d of %d entries calibrated\n", valid, NTCLUT_ENTRIES);
    for (double tc : { -5.0, 0.0, 10.0, 25.0, 30.0, 35.0, 36.5, 37.0, 40.0, 50.0, 59.0, 62.0 }) check(tc, 3950);
    float t; printf("  code 32767 (open NTC): %s\n", ntclut_lookup(&lut, 32767, &t) ? "a temperature!" : errstr());
    printf("  code -32768 (shorted): %s\n", ntclut_lookup(&lut, -32768, &t) ? "a temperature!" : errstr());

    // A calibration: 5 points measured on a sensor whose real Beta is 3900
    // (not the datasheet's 3950), exact readings.
    char text[1024]; int n = snprintf(text, sizeof text, "# 5 points, exact, real Beta 3900\nbridge 3.3 47000 47000 10000\n");
    for (double tc : { 0.0, 25.0, 35.0, 37.0, 45.0 }) n += snprintf(text + n, sizeof text - n, "%.0f %.1f\n", code_for(tc, 3900), tc);
    show("calibration, Steinhart-Hart through the bridge (a sensor with B 3900)", text);
    for (double tc : { -3.0, 10.0, 30.0, 36.0, 37.0, 38.5, 47.0, 48.0 }) check(tc, 3900);

    // The same, with reading errors of up to +-0.1 C on the reference thermometer.
    const double err[5] = { +0.1, -0.08, +0.05, -0.1, +0.07 };
    n = snprintf(text, sizeof text, "bridge 3.3 47000 47000 10000\n");
    int k = 0; for (double tc : { 0.0, 25.0, 35.0, 37.0, 45.0 }) n += snprintf(text + n, sizeof text - n, "%.0f %.2f\n", code_for(tc, 3900), tc + err[k++]);
    show("the same, reference readings off by up to 0.1 C", text);
    for (double tc : { 10.0, 30.0, 36.0, 37.0, 38.5 }) check(tc, 3900);

    // Agnostic: no bridge, just 7 points (any circuit, any sensor).
    n = snprintf(text, sizeof text, "# no bridge: interpolation only\n");
    for (double tc : { 20.0, 30.0, 34.0, 36.0, 38.0, 42.0, 50.0 }) n += snprintf(text + n, sizeof text - n, "%.0f %.1f\n", code_for(tc, 3900), tc);
    show("agnostic, 7 points, no bridge", text);
    for (double tc : { 19.0, 25.0, 32.0, 35.0, 37.0, 40.0, 46.0, 51.0 }) check(tc, 3900);

    // Round trip: the stored text, written again, gives the same table.
    static ntclut_t a; memcpy(&a, &lut, sizeof a); char again[2048]; strcpy(again, out);
    ntclut_make(again, strlen(again), &lut, out, sizeof out);
    printf("--- round trip (the stored text written again): %s\n", memcmp(&a, &lut, sizeof a) || strcmp(again, out) ? "DIFFERENT" : "same table, same text");

    show("error: a bridge with too few points", "bridge 3.3 47000 47000 10000\n-6600 37\n1000 22\n");
    show("error: a typo", "bridge 3.3 47000 47000 10000\n-6600 37\n1000 22 x\n");
    show("error: not monotonic", "100 30\n200 31\n300 29\n");
    show("error: a code outside the ADC", "bridge 3.3 47000 47000 10000\n-66000 37\n0 25\n100 24\n");
    show("error: empty", "# nothing\n");
    return 0;
}
