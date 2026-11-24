/* ps4_missing_syms.c — stub implementations for libc/libm symbols missing
 * from the OpenOrbis musl stubs. These are compiled into the PS4 binary
 * directly so the linker doesn't need them from libc.a / libm.a.
 * All functions are defined as weak so a future real libc can override them.
 */
#include <stddef.h>
#include <stdarg.h>
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
/* locale_t already defined in musl headers — include it */
#include <locale.h>

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

/* ---- time ---- */
/* mbedtls uses time() for certificate expiry checks (x509.c).
 * On PS4 the kernel provides real time via sceRtcGetCurrentTick / sceKernelGetSystemtime,
 * but mbedtls only needs a rough epoch for certificate window checks.
 * Return 0 (epoch) so mbedtls accepts any certificate whose window includes 1970-01-01.
 * If real clock is ever needed, replace with a proper PS4 syscall here.
 */
__attribute__((weak)) time_t time(time_t *t) {
    if (t) *t = 0;
    return 0;
}

/* ---- printf / vfprintf (mbedtls + libc++ abort stubs) ---- */
/* mbedtls calls printf() for debug trace. libc++ abort_message/verbose_abort
 * call vfprintf(stderr,...). No console on PS4 — swallow silently. */
__attribute__((weak)) int printf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}
__attribute__((weak)) int vfprintf(void *stream, const char *fmt, __builtin_va_list ap) {
    (void)stream; (void)fmt; (void)ap;
    return 0;
}

/* ---- stderr (libc++ abort message needs FILE* stderr) ---- */
/* libc++ only passes it to vfprintf which we stub out above.
 * Provide a non-NULL pointer so the pointer-dereference in abort_message
 * doesn't fault (libc++ checks if stream != NULL before calling vfprintf). */
static int _stderr_dummy = 0;
/* FILE* is void* at ABI level; expose it as a weak symbol. */
__attribute__((weak)) void *stderr = (void*)&_stderr_dummy;

/* ---- string conversions (libc++ std::stoll / stoull / stof / stold) ---- */
#include <errno.h>
/* strtoll / strtoull: simple sign+digit scanner, no locale */
__attribute__((weak)) long long strtoll(const char *s, char **end, int base) {
    int neg = 0;
    unsigned long long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1]=='x'||s[1]=='X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s += 2;
    const char *start = s;
    for (;;) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
        s++;
    }
    if (s == start) { if (end) *end = (char*)s; return 0; }
    if (end) *end = (char*)s;
    return neg ? -(long long)v : (long long)v;
}
__attribute__((weak)) unsigned long long strtoull(const char *s, char **end, int base) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;
    if (base == 0) {
        if (s[0]=='0' && (s[1]=='x'||s[1]=='X')) { base=16; s+=2; }
        else if (s[0]=='0') { base=8; s++; }
        else base=10;
    } else if (base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s+=2;
    unsigned long long v = 0;
    const char *start = s;
    for (;;) {
        int d;
        if (*s>='0'&&*s<='9') d=*s-'0';
        else if (*s>='a'&&*s<='f') d=*s-'a'+10;
        else if (*s>='A'&&*s<='F') d=*s-'A'+10;
        else break;
        if (d>=base) break;
        v=v*(unsigned)base+(unsigned)d; s++;
    }
    (void)start;
    if (end) *end=(char*)s;
    return v;
}
/* strtof / strtold: delegate to strtod, cast */
__attribute__((weak)) float strtof(const char *s, char **end) {
    /* minimal: parse optional sign + integer + fraction + exponent */
    while (*s==' '||*s=='\t') s++;
    int neg=0;
    if (*s=='-'){neg=1;s++;}else if(*s=='+')s++;
    double v=0;
    while (*s>='0'&&*s<='9') v=v*10+(*s++-'0');
    if (*s=='.') {
        s++; double f=0.1;
        while (*s>='0'&&*s<='9'){v+=(*s++-'0')*f;f*=0.1;}
    }
    if (*s=='e'||*s=='E') {
        s++; int eneg=0;
        if (*s=='-'){eneg=1;s++;}else if(*s=='+')s++;
        int e=0;
        while (*s>='0'&&*s<='9') e=e*10+(*s++-'0');
        double m=1; for(int i=0;i<e;i++) m*=10.0;
        if(eneg) v/=m; else v*=m;
    }
    if (end) *end=(char*)s;
    return (float)(neg?-v:v);
}
__attribute__((weak)) long double strtold(const char *s, char **end) {
    return (long double)strtof(s, end);
}

/* ---- wcslen (mbedtls ssl_handshake_init uses MBEDTLS_SSL_VERIFY_NONE label) ---- */
#include <stddef.h>
__attribute__((weak)) size_t wcslen(const unsigned int *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ---- syscall (libc++ __cxa_guard_acquire for static-local mutex) ---- */
/* On PS4 we disable static-local constructors via build rules, so
 * __cxa_guard_acquire should never actually run. If it does,
 * return 1 (lock acquired) so initialisation proceeds rather than hanging. */
__attribute__((weak)) long syscall(long number, ...) {
    (void)number;
    return 1;
}
