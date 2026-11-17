/* Mutex type for MBEDTLS_THREADING_ALT: a small spinlock (functions in src/net/tls.cpp).
 * Contention is rare (a few HTTP threads), and it needs nothing from the platform. */
#ifndef ML_MBEDTLS_THREADING_ALT_H
#define ML_MBEDTLS_THREADING_ALT_H
typedef struct mbedtls_threading_mutex_t {
    volatile int lock;
    int valid;
} mbedtls_threading_mutex_t;
#endif
