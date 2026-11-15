/*
 * The C library pieces Lua needs that libyaul doesn't have: number parsing
 * (strtod/strtof), the "C" locale, time, getenv, abort, and the output hook
 * of third_party/printf. (setjmp/longjmp are in setjmp.S, maths in musl.)
 */
#include <ctype.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- numbers ---- */

static const double _pow10[] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

static double _scale10(double v, int e) {
    if (v == 0.0) return v;
    if (e > 0) {
        while (e > 22) { v *= 1e22; e -= 22; if (v > 1.8e308) return v; }
        return v * _pow10[e];
    }
    while (e < -22) { v /= 1e22; e += 22; if (v == 0.0) return v; }
    return v / _pow10[-e];
}

static double _scale2(double v, int e) {
    while (e > 0) { v *= 2.0; e--; }
    while (e < 0) { v *= 0.5; e++; }
    return v;
}

static int _hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int _ieq(const char* s, const char* word) {
    for (; *word; s++, word++) if (tolower((unsigned char)*s) != *word) return 0;
    return 1;
}

double strtod(const char* restrict str, char** restrict end) {
    const char* s = str;
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') neg = *s++ == '-';

    if (_ieq(s, "inf")) {
        s += _ieq(s, "infinity") ? 8 : 3;
        if (end) *end = (char*)s;
        return neg ? -__builtin_inf() : __builtin_inf();
    }
    if (_ieq(s, "nan")) {
        if (end) *end = (char*)(s + 3);
        return __builtin_nan("");
    }

    double v = 0.0;
    int digits = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        /* Hex: 0x1.8p3 */
        const char* p = s + 2;
        int e = 0, h;
        while ((h = _hexval(*p)) >= 0) { v = v * 16.0 + h; p++; digits++; }
        if (*p == '.') {
            p++;
            while ((h = _hexval(*p)) >= 0) { v = v * 16.0 + h; e -= 4; p++; digits++; }
        }
        if (!digits) { if (end) *end = (char*)str; return 0.0; }
        if (*p == 'p' || *p == 'P') {
            const char* q = p + 1;
            int en = 0, x = 0, any = 0;
            if (*q == '+' || *q == '-') en = *q++ == '-';
            while (isdigit((unsigned char)*q)) { if (x < 100000) x = x * 10 + (*q - '0'); q++; any = 1; }
            if (any) { e += en ? -x : x; p = q; }
        }
        if (end) *end = (char*)p;
        v = _scale2(v, e);
        return neg ? -v : v;
    }

    /* Decimal: up to 19 significant digits exactly, the rest as exponent. */
    uint64_t m = 0;
    int sig = 0, e = 0;
    const char* p = s;
    while (isdigit((unsigned char)*p)) {
        if (sig < 19) { m = m * 10 + (uint64_t)(*p - '0'); if (m) sig++; }
        else e++;
        p++; digits++;
    }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            if (sig < 19) { m = m * 10 + (uint64_t)(*p - '0'); if (m) sig++; e--; }
            p++; digits++;
        }
    }
    if (!digits) { if (end) *end = (char*)str; return 0.0; }
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        int en = 0, x = 0, any = 0;
        if (*q == '+' || *q == '-') en = *q++ == '-';
        while (isdigit((unsigned char)*q)) { if (x < 100000) x = x * 10 + (*q - '0'); q++; any = 1; }
        if (any) { e += en ? -x : x; p = q; }
    }
    if (end) *end = (char*)p;
    v = _scale10((double)m, e);
    return neg ? -v : v;
}

float strtof(const char* restrict s, char** restrict end) { return (float)strtod(s, end); }

/* ---- the "C" locale ---- */

static struct lconv _c_locale = {".", "", ""};
struct lconv* localeconv(void) { return &_c_locale; }
char* setlocale(int category, const char* locale) {
    (void)category;
    return (!locale || !*locale || !strcmp(locale, "C")) ? "C" : NULL;
}
int strcoll(const char* a, const char* b) { return strcmp(a, b); }

/* ---- the rest ---- */

extern uint32_t sat_vblanks(void);
time_t time(time_t* t) {
    time_t v = (time_t)(sat_vblanks() / 60);
    if (t) *t = v;
    return v;
}
clock_t clock(void) { return (clock_t)sat_vblanks(); }

char* getenv(const char* name) { (void)name; return NULL; }

int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

/* third_party/printf's output for printf_(): unused (Lua formats to strings). */
void putchar_(char c) { (void)c; }
