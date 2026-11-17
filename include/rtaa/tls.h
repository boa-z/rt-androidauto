/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_TLS_H
#define RTAA_TLS_H
#include "rtaa/core.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct rtaa_tls rtaa_tls;
    typedef struct
    {
        void *ctx;
        void *(*allocate)(void *, size_t);
        void (*release)(void *, void *);
    } rtaa_allocator;
    typedef struct
    {
        rtaa_allocator memory;
        const uint8_t *certificate, *private_key, *ca;
        size_t certificate_size, private_key_size, ca_size;
        int (*random)(void *, uint8_t *, size_t);
        void *random_ctx;
        /* Explicit trust policy for an Android peer certificate. Return 0 to
         * accept the SHA256 fingerprint. NULL uses CA + hostname verification.
         * This hook may implement provisioned pinning; never accept blindly. */
        int (*peer_fingerprint)(void *, const uint8_t sha256[32]);
        void *peer_ctx;
        const char *hostname;
    } rtaa_tls_config;
    /* No fallback credentials and no compiled-in private keys. PEM sizes include
     * the terminating NUL; DER sizes do not. mbedTLS 2.28 backend, TLS 1.2 only. */
    int rtaa_tls_create(const rtaa_tls_config *, rtaa_tls **);
    void rtaa_tls_destroy(rtaa_tls *);
    int rtaa_tls_ingest(rtaa_tls *, const uint8_t *, size_t);
    int rtaa_tls_output(rtaa_tls *, uint8_t *, size_t, size_t *);
    int rtaa_tls_handshake(rtaa_tls *);
    int rtaa_tls_encrypt(rtaa_tls *, const uint8_t *, size_t);
    int rtaa_tls_decrypt(rtaa_tls *, uint8_t *, size_t, size_t *);
    bool rtaa_tls_ready(const rtaa_tls *);
    size_t rtaa_tls_pending(const rtaa_tls *);
    size_t rtaa_tls_storage_size(void);
#ifdef __cplusplus
}
#endif
#endif
