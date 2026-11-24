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
/* fmod: remainder of x/y */
__attribute__((weak)) double fmod(double x, double y) {
    if (y == 0.0) return x;
    long long n = (long long)(x / y);
    return x - n * y;
}
__attribute__((weak)) float fmodf(float x, float y) {
    if (y == 0.0f) return x;
    long long n = (long long)(x / y);
    return x - (float)n * y;
}

/* ceil: round up to integer */
__attribute__((weak)) double ceil(double x) {
    long long i = (long long)x;
    return (double)(x > (double)i ? i + 1 : i);
}
__attribute__((weak)) float ceilf(float x) {
    long long i = (long long)x;
    return (float)(x > (float)i ? i + 1 : i);
}

/* floor */
__attribute__((weak)) double floor(double x) {
    long long i = (long long)x;
    return (double)(x < (double)i ? i - 1 : i);
}
__attribute__((weak)) float floorf(float x) {
    long long i = (long long)x;
    return (float)(x < (float)i ? i - 1 : i);
}

/* round */
__attribute__((weak)) double round(double x) {
    return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5);
}
__attribute__((weak)) float roundf(float x) {
    return x >= 0.0f ? floorf(x + 0.5f) : ceilf(x - 0.5f);
}

/* ---- strftime: minimal implementation ---- */
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
