/* ps4_missing_syms.c — stub implementations for libc/libm symbols missing
 * from the OpenOrbis musl stubs. These are compiled into the PS4 binary
 * directly so the linker doesn't need them from libc.a / libm.a.
 * All functions are defined as weak so a future real libc can override them.
 */
#include <stddef.h>
#include <time.h>

/* ---- ctype ---- */
__attribute__((weak)) int isdigit(int c) { return c >= '0' && c <= '9'; }
__attribute__((weak)) int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
__attribute__((weak)) int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
__attribute__((weak)) int isalnum(int c) { return isdigit(c) || isalpha(c); }
__attribute__((weak)) int isupper(int c) { return c >= 'A' && c <= 'Z'; }
__attribute__((weak)) int islower(int c) { return c >= 'a' && c <= 'z'; }
__attribute__((weak)) int tolower(int c) { return isupper(c) ? c + 32 : c; }
__attribute__((weak)) int toupper(int c) { return islower(c) ? c - 32 : c; }

/* ---- math ---- */
/* Use hardware x87/SSE instructions directly where possible */

__attribute__((weak)) double fabs(double x)  { return __builtin_fabs(x); }
__attribute__((weak)) float  fabsf(float x)  { return __builtin_fabsf(x); }

__attribute__((weak)) double fmod(double x, double y) {
    if (y == 0.0) return x;
    long long n = (long long)(x / y);
    return x - (double)n * y;
}
__attribute__((weak)) float fmodf(float x, float y) {
    if (y == 0.0f) return x;
    long long n = (long long)(x / y);
    return x - (float)n * y;
}

__attribute__((weak)) double ceil(double x)  { return __builtin_ceil(x); }
__attribute__((weak)) float  ceilf(float x)  { return __builtin_ceilf(x); }
__attribute__((weak)) double floor(double x) { return __builtin_floor(x); }
__attribute__((weak)) float  floorf(float x) { return __builtin_floorf(x); }
__attribute__((weak)) double round(double x) { return __builtin_round(x); }
__attribute__((weak)) float  roundf(float x) { return __builtin_roundf(x); }
__attribute__((weak)) double trunc(double x) { return __builtin_trunc(x); }
__attribute__((weak)) float  truncf(float x) { return __builtin_truncf(x); }
__attribute__((weak)) double sqrt(double x)  { return __builtin_sqrt(x); }
__attribute__((weak)) float  sqrtf(float x)  { return __builtin_sqrtf(x); }

__attribute__((weak)) double pow(double x, double y) {
    /* integer exponent fast path */
    if (y == 0.0) return 1.0;
    if (y == 1.0) return x;
    if (y == 2.0) return x * x;
    if (y == 0.5) return sqrt(x);
    /* general: e^(y*ln(x)) via compiler builtins */
    return __builtin_exp(__builtin_log(x) * y);
}
__attribute__((weak)) float powf(float x, float y) {
    if (y == 0.0f) return 1.0f;
    if (y == 1.0f) return x;
    if (y == 2.0f) return x * x;
    if (y == 0.5f) return sqrtf(x);
    return (float)__builtin_exp((double)__builtin_log((double)x) * (double)y);
}

__attribute__((weak)) double log(double x)   { return __builtin_log(x); }
__attribute__((weak)) float  logf(float x)   { return __builtin_logf(x); }
__attribute__((weak)) double exp(double x)   { return __builtin_exp(x); }
__attribute__((weak)) float  expf(float x)   { return __builtin_expf(x); }
__attribute__((weak)) double sin(double x)   { return __builtin_sin(x); }
__attribute__((weak)) float  sinf(float x)   { return __builtin_sinf(x); }
__attribute__((weak)) double cos(double x)   { return __builtin_cos(x); }
__attribute__((weak)) float  cosf(float x)   { return __builtin_cosf(x); }
__attribute__((weak)) double atan2(double y, double x) { return __builtin_atan2(y, x); }
__attribute__((weak)) float  atan2f(float y, float x)  { return __builtin_atan2f(y, x); }

/* ---- strftime: minimal implementation ---- */
/* locale_t is opaque on PS4/musl — just an integer handle */
typedef void* locale_t;

/* Only the formats actually used in Moonlight: %H %M %S %Y %m %d */
__attribute__((weak)) size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    size_t pos = 0;
#define PUT(c) do { if (pos + 1 >= max) goto done; s[pos++] = (c); } while(0)
#define PUT2(v) do { \
    if (pos + 2 >= max) goto done; \
    s[pos++] = '0' + (v) / 10; \
    s[pos++] = '0' + (v) % 10; \
} while(0)
#define PUT4(v) do { \
    if (pos + 4 >= max) goto done; \
    s[pos++] = '0' + (v) / 1000; \
    s[pos++] = '0' + ((v) / 100) % 10; \
    s[pos++] = '0' + ((v) / 10) % 10; \
    s[pos++] = '0' + (v) % 10; \
} while(0)
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUT(*p); continue; }
        p++;
        switch (*p) {
            case 'H': PUT2(tm->tm_hour); break;
            case 'M': PUT2(tm->tm_min);  break;
            case 'S': PUT2(tm->tm_sec);  break;
            case 'Y': PUT4(tm->tm_year + 1900); break;
            case 'm': PUT2(tm->tm_mon + 1); break;
            case 'd': PUT2(tm->tm_mday);    break;
            case '%': PUT('%'); break;
            default:  PUT('%'); PUT(*p); break;
        }
    }
done:
    s[pos] = '\0';
    return pos;
#undef PUT
#undef PUT2
#undef PUT4
}

/* locale-aware variant — just forward to strftime, locale ignored on PS4 */
__attribute__((weak)) size_t strftime_l(char *s, size_t max, const char *fmt, const struct tm *tm, locale_t loc) {
    (void)loc;
    return strftime(s, max, fmt, tm);
}
