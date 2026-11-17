/* Moonlight HB adjustments to the default Mbed TLS 3.6 configuration (MBEDTLS_USER_CONFIG_FILE).
 * The app brings its own TLS because the PS4 system TLS (libSceSsl) of older firmware refuses modern
 * servers (0x8095F00C, seen on the console with 01.07 and 01.08). Certificate verification is always on. */
#undef MBEDTLS_NET_C              /* sockets come from the platform layer */
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_SSL_SRV_C          /* client only */
#undef MBEDTLS_SSL_PROTO_DTLS
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_DTLS_CONNECTION_ID
#undef MBEDTLS_SSL_COOKIE_C
#undef MBEDTLS_SSL_TICKET_C
#undef MBEDTLS_SSL_CACHE_C

/* Entropy: platform random (PS4 sceRandomGetRandomNumber, PC getrandom) via mbedtls_hardware_poll. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* Several app threads make requests at once (images, radio, UI): PSA keeps global state. */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT
