// apps/babywarmer/ntclut.cpp — see ntclut.h. Written 2026-11-24 for plan2040
// (step 3g), replacing picoos's ntc.cpp + ntc_lut.h.
#include "ntclut.h"
#include "kernel/errstr.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define MAX_POINTS   32
#define K0           273.15
#define SH_MARGIN_C  2.0    // a fitted curve counts 2 C beyond the coldest/hottest point

typedef struct {
    bool   bridge, ntc;
    double vs, rt_ntc, rt_ref, rb_ref;   // bridge: supply, R above NTC, R above/below ref point
    double fs;                           // ADC full scale (V for 32768 codes)
    double r25, t25, beta;               // ntc: datasheet curve
    double tmin, tmax;                   // range for the ntc curve
    int    n;
    double code[MAX_POINTS], temp[MAX_POINTS];
} calib_t;

// ---- parsing ------------------------------------------------------------

// Reads up to max numbers from s; returns how many, -1 if something else is there.
static int numbers(const char *s, double *v, int max)
{
    int n = 0;
    for (;;) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') return n;
        char *end;
        double x = strtod(s, &end);
        if (end == s || n == max) return -1;
        v[n++] = x;
        s = end;
    }
}

static bool parse(const char *text, int len, calib_t *c)
{
    memset(c, 0, sizeof(*c));
    c->fs = 1.024;
    c->tmin = 0.0;
    c->tmax = 60.0;
    int lineno = 0;
    for (int pos = 0; pos < len; ) {
        char line[128];
        int e = pos;
        while (e < len && text[e] != '\n') e++;
        int n = e - pos < (int)sizeof(line) - 1 ? e - pos : (int)sizeof(line) - 1;
        memcpy(line, text + pos, n);
        line[n] = '\0';
        pos = e + 1;
        lineno++;

        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        double v[4];
        int k;
        if (!strncmp(p, "bridge", 6)) {
            if ((k = numbers(p + 6, v, 4)) != 4 || v[0] <= 0 || v[1] <= 0 || v[2] <= 0 || v[3] <= 0) {
                werrstr("line %d: bridge <supply V> <R above NTC> <R above ref> <R below ref>", lineno);
                return false;
            }
            c->bridge = true; c->vs = v[0]; c->rt_ntc = v[1]; c->rt_ref = v[2]; c->rb_ref = v[3];
        } else if (!strncmp(p, "adc", 3)) {
            if ((k = numbers(p + 3, v, 1)) != 1 || v[0] <= 0) {
                werrstr("line %d: adc <full scale V>", lineno);
                return false;
            }
            c->fs = v[0];
        } else if (!strncmp(p, "ntc", 3)) {
            if ((k = numbers(p + 3, v, 3)) != 3 || v[0] <= 0 || v[2] <= 0) {
                werrstr("line %d: ntc <R25> <T25 C> <Beta>", lineno);
                return false;
            }
            c->ntc = true; c->r25 = v[0]; c->t25 = v[1]; c->beta = v[2];
        } else if (!strncmp(p, "range", 5)) {
            if ((k = numbers(p + 5, v, 2)) != 2 || v[0] >= v[1]) {
                werrstr("line %d: range <from C> <to C>", lineno);
                return false;
            }
            c->tmin = v[0]; c->tmax = v[1];
        } else {
            if ((k = numbers(p, v, 2)) != 2) {
                werrstr("line %d: expected '<ADC code> <temperature C>', or bridge/adc/ntc/range", lineno);
                return false;
            }
            if (v[0] < -32768 || v[0] > 32767) {
                werrstr("line %d: ADC code %.0f outside -32768..32767", lineno, v[0]);
                return false;
            }
            if (c->n == MAX_POINTS) {
                werrstr("line %d: at most %d points", lineno, MAX_POINTS);
                return false;
            }
            c->code[c->n] = v[0];
            c->temp[c->n] = v[1];
            c->n++;
        }
    }
    return true;
}

// ---- the bridge: ADC code -> NTC resistance -------------------------------

static bool code_to_r(const calib_t *c, double code, double *r)
{
    double vdiff = code * c->fs / 32768.0;                     // AIN0 - AIN1
    double vref  = c->vs * c->rb_ref / (c->rt_ref + c->rb_ref);
    double vntc  = vref + vdiff;
    if (vntc <= 0.0 || vntc >= c->vs) return false;
    *r = c->rt_ntc * vntc / (c->vs - vntc);
    return true;
}

// ---- Steinhart-Hart: 1/T = A + B ln R + C ln^3 R, least squares ---------

static bool sh_fit(const double *x, const double *y, int n, double abc[3])
{
    double m[3][4] = {{0}};
    for (int i = 0; i < n; i++) {
        double f[3] = { 1.0, x[i], x[i] * x[i] * x[i] };
        for (int r = 0; r < 3; r++) {
            for (int s = 0; s < 3; s++) m[r][s] += f[r] * f[s];
            m[r][3] += f[r] * y[i];
        }
    }
    for (int col = 0; col < 3; col++) {                        // Gauss, partial pivoting
        int piv = col;
        for (int r = col + 1; r < 3; r++) if (fabs(m[r][col]) > fabs(m[piv][col])) piv = r;
        if (fabs(m[piv][col]) < 1e-30) return false;
        if (piv != col) for (int s = 0; s < 4; s++) { double t = m[col][s]; m[col][s] = m[piv][s]; m[piv][s] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == col) continue;
            double f = m[r][col] / m[col][col];
            for (int s = col; s < 4; s++) m[r][s] -= f * m[col][s];
        }
    }
    for (int r = 0; r < 3; r++) abc[r] = m[r][3] / m[r][r];
    return true;
}

static double sh_temp(const double abc[3], double r)
{
    double x = log(r);
    return 1.0 / (abc[0] + abc[1] * x + abc[2] * x * x * x) - K0;
}

static double beta_temp(const calib_t *c, double r)
{
    return 1.0 / (1.0 / (c->t25 + K0) + log(r / c->r25) / c->beta) - K0;
}

// ---- monotonic interpolation through the points (Fritsch-Carlson) ------

static bool pchip_prepare(calib_t *c, double *m)
{
    int n = c->n;
    for (int i = 1; i < n; i++)                                // sort by code (insertion)
        for (int j = i; j > 0 && c->code[j - 1] > c->code[j]; j--) {
            double t = c->code[j]; c->code[j] = c->code[j - 1]; c->code[j - 1] = t;
            t = c->temp[j]; c->temp[j] = c->temp[j - 1]; c->temp[j - 1] = t;
        }
    double d[MAX_POINTS];
    for (int i = 0; i < n - 1; i++) {
        if (c->code[i + 1] == c->code[i]) { werrstr("two points with code %.0f", c->code[i]); return false; }
        d[i] = (c->temp[i + 1] - c->temp[i]) / (c->code[i + 1] - c->code[i]);
        if (i > 0 && d[i] * d[i - 1] <= 0) {
            werrstr("points not monotonic at code %.0f: the temperature must keep falling (or rising)", c->code[i]);
            return false;
        }
    }
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (int i = 1; i < n - 1; i++) m[i] = (d[i - 1] + d[i]) / 2.0;
    for (int i = 0; i < n - 1; i++) {
        double a = m[i] / d[i], b = m[i + 1] / d[i], h = a * a + b * b;
        if (h > 9.0) { double t = 3.0 / sqrt(h); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i]; }
    }
    return true;
}

static bool pchip_temp(const calib_t *c, const double *m, double code, double *t)
{
    int n = c->n;
    if (code < c->code[0] || code > c->code[n - 1]) return false;
    int i = 0;
    while (i < n - 2 && code > c->code[i + 1]) i++;
    double h = c->code[i + 1] - c->code[i], s = (code - c->code[i]) / h;
    double h00 = (1 + 2 * s) * (1 - s) * (1 - s), h10 = s * (1 - s) * (1 - s);
    double h01 = s * s * (3 - 2 * s),             h11 = s * s * (s - 1);
    *t = h00 * c->temp[i] + h10 * h * m[i] + h01 * c->temp[i + 1] + h11 * h * m[i + 1];
    return true;
}

// ---- the output text -----------------------------------------------------

typedef struct { char *buf; int len, max; bool full; } outbuf_t;

static void put(outbuf_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(outbuf_t *o, const char *fmt, ...)
{
    if (o->full) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->buf + o->len, o->max - o->len, fmt, ap);
    va_end(ap);
    if (n < 0 || o->len + n >= o->max) { o->full = true; return; }
    o->len += n;
}

// The input without its old `#=` lines, each line ending in '\n'.
static void copy_input(outbuf_t *o, const char *text, int len)
{
    for (int pos = 0; pos < len; ) {
        int e = pos;
        while (e < len && text[e] != '\n') e++;
        const char *p = text + pos;
        while (p < text + e && (*p == ' ' || *p == '\t')) p++;
        if (!(p + 1 < text + e && p[0] == '#' && p[1] == '=') && e > pos)
            put(o, "%.*s\n", e - pos, text + pos);
        pos = e + 1;
    }
}

// ---- making the table ----------------------------------------------------

bool ntclut_make(const char *text, int len, ntclut_t *lut, char *out, int outlen)
{
    static calib_t c;                              // static: ~600 bytes, not on the stack
    if (!parse(text, len, &c)) return false;

    enum { SH, BETA, PCHIP } method;
    double abc[3] = {0}, lo = 0, hi = 0;
    double m[MAX_POINTS];
    if (c.bridge && c.n >= 3) {
        method = SH;
        double x[MAX_POINTS], y[MAX_POINTS];
        for (int i = 0; i < c.n; i++) {
            double r;
            if (!code_to_r(&c, c.code[i], &r)) {
                werrstr("point %.0f %.2f: the code doesn't fit the bridge (open or shorted?)", c.code[i], c.temp[i]);
                return false;
            }
            x[i] = log(r);
            y[i] = 1.0 / (c.temp[i] + K0);
        }
        if (!sh_fit(x, y, c.n, abc)) { werrstr("the points don't give a curve (all at one temperature?)"); return false; }
        lo = hi = c.temp[0];
        for (int i = 1; i < c.n; i++) { if (c.temp[i] < lo) lo = c.temp[i]; if (c.temp[i] > hi) hi = c.temp[i]; }
        lo -= SH_MARGIN_C;
        hi += SH_MARGIN_C;
    } else if (c.bridge && c.ntc && c.n == 0) {
        method = BETA;
        lo = c.tmin;
        hi = c.tmax;
    } else if (!c.bridge && c.n >= 2) {
        method = PCHIP;
        if (!pchip_prepare(&c, m)) return false;
    } else if (c.bridge && c.n > 0) {
        werrstr("a curve through the bridge needs at least 3 points (%d given)", c.n);
        return false;
    } else if (c.bridge) {
        werrstr("a bridge needs points (at least 3), or an 'ntc <R25> <T25> <Beta>' line");
        return false;
    } else {
        werrstr("nothing to make a table from: points (at least 2), or a bridge with points or an ntc line");
        return false;
    }

    int valid = 0, first = -1, last = -1;
    for (int i = 0; i < NTCLUT_ENTRIES; i++) {
        double code = -32768.0 + (double)(i << NTCLUT_STEP_LOG2), t = 0.0, r = 0.0;
        bool ok;
        if (method == PCHIP) ok = pchip_temp(&c, m, code, &t);
        else {
            ok = code_to_r(&c, code, &r);
            if (ok) {
                t = method == SH ? sh_temp(abc, r) : beta_temp(&c, r);
                ok = t >= lo && t <= hi;
            }
        }
        if (ok && (t < -273.0 || t > 327.0)) ok = false;
        lut->t[i] = ok ? (int16_t)lround(t * 100.0) : NTCLUT_NONE;
        if (ok) { valid++; if (first < 0) first = i; last = i; }
    }
    if (!valid) { werrstr("the curve gives no temperatures in the ADC's range"); return false; }

    outbuf_t o = { out, 0, outlen, false };
    out[0] = '\0';
    copy_input(&o, text, len);
    int c0 = -32768 + (first << NTCLUT_STEP_LOG2), c1 = -32768 + (last << NTCLUT_STEP_LOG2);
    if (method == SH) {
        put(&o, "#= Steinhart-Hart through the bridge, %d points: A=%.6e B=%.6e C=%.6e\n", c.n, abc[0], abc[1], abc[2]);
        for (int i = 0; i < c.n; i++) {
            double r = 1.0; code_to_r(&c, c.code[i], &r);
            double t = sh_temp(abc, r);
            put(&o, "#= point %.0f %.2f: the curve says %.2f (%+.2f)\n", c.code[i], c.temp[i], t, t - c.temp[i]);
        }
        put(&o, "#= valid %.1f .. %.1f C (codes %d .. %d)\n", lo, hi, c0, c1);
    } else if (method == BETA) {
        put(&o, "#= Beta curve through the bridge: R25 %.0f, T25 %.1f C, B %.0f\n", c.r25, c.t25, c.beta);
        put(&o, "#= valid %.1f .. %.1f C (codes %d .. %d)\n", lo, hi, c0, c1);
    } else {
        put(&o, "#= monotonic interpolation through %d points (no bridge given)\n", c.n);
        put(&o, "#= valid codes %.0f .. %.0f (%.2f .. %.2f C)\n", c.code[0], c.code[c.n - 1], c.temp[0], c.temp[c.n - 1]);
    }
    if (o.full) { werrstr("the calibration text is too long (at most %d bytes with the generated lines)", outlen - 1); return false; }
    return true;
}

// ---- reading -------------------------------------------------------------

bool ntclut_lookup(const ntclut_t *lut, int16_t code, float *temp)
{
    int u = (int)code + 32768;
    int i = u >> NTCLUT_STEP_LOG2, f = u & ((1 << NTCLUT_STEP_LOG2) - 1);
    int16_t a = lut->t[i];
    int16_t b = i + 1 < NTCLUT_ENTRIES ? lut->t[i + 1] : a;
    if (a == NTCLUT_NONE || (f && b == NTCLUT_NONE)) {
        werrstr("code %d is outside the calibrated range (open or shorted?)", code);
        return false;
    }
    *temp = (a + (b - a) * f / (float)(1 << NTCLUT_STEP_LOG2)) / 100.0f;
    return true;
}
