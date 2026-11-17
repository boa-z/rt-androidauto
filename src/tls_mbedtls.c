/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/tls.h"
#include <string.h>
#include <limits.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/version.h>

#if MBEDTLS_VERSION_NUMBER < 0x021c0000 || MBEDTLS_VERSION_NUMBER >= 0x03000000
#error "rt-androidauto currently requires mbedTLS 2.28.x headers and matching libraries"
#endif

#define QUEUE_SIZE (RTAA_FRAME_MAX * 2u)
struct rtaa_tls
{
    rtaa_tls_config config;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt certificate, ca;
    mbedtls_pk_context key;
    uint8_t input[QUEUE_SIZE], output[QUEUE_SIZE];
    size_t input_size, output_size;
    bool ready, failed;
};
size_t rtaa_tls_storage_size(void)
{
    return sizeof(rtaa_tls);
}
static int send_bytes(void *ctx, const unsigned char *p, size_t size)
{
    rtaa_tls *t = ctx;
    if (size > sizeof(t->output) - t->output_size)
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    memcpy(t->output + t->output_size, p, size);
    t->output_size += size;
    return (int)size;
}
static int receive_bytes(void *ctx, unsigned char *p, size_t size)
{
    rtaa_tls *t = ctx;
    if (!t->input_size)
        return MBEDTLS_ERR_SSL_WANT_READ;
    if (size > t->input_size)
        size = t->input_size;
    memcpy(p, t->input, size);
    t->input_size -= size;
    memmove(t->input, t->input + size, t->input_size);
    return (int)size;
}
static int verify_peer(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    rtaa_tls *t = ctx;
    if (depth != 0)
    {
        *flags = 0;
        return 0;
    }
    uint8_t hash[32];
    if (mbedtls_sha256_ret(crt->raw.p, crt->raw.len, hash, 0) ||
        t->config.peer_fingerprint(t->config.peer_ctx, hash))
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    *flags = 0;
    return 0;
}
int rtaa_tls_create(const rtaa_tls_config *c, rtaa_tls **out)
{
    if (!out)
        return RTAA_EINVAL;
    *out = NULL;
    if (!c || !c->memory.allocate || !c->memory.release || !c->random || !c->certificate ||
        !c->certificate_size || !c->private_key || !c->private_key_size ||
        (!c->peer_fingerprint && (!c->ca || !c->ca_size || !c->hostname)))
        return RTAA_EINVAL;
    rtaa_tls *t = c->memory.allocate(c->memory.ctx, sizeof(*t));
    if (!t)
        return RTAA_EBOUNDS;
    memset(t, 0, sizeof(*t));
    t->config = *c;
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_x509_crt_init(&t->certificate);
    mbedtls_x509_crt_init(&t->ca);
    mbedtls_pk_init(&t->key);
    int rc = mbedtls_x509_crt_parse(&t->certificate, c->certificate, c->certificate_size);
    if (!rc)
        rc = mbedtls_pk_parse_key(&t->key, c->private_key, c->private_key_size, NULL, 0);
    if (!rc)
        rc = mbedtls_pk_check_pair(&t->certificate.pk, &t->key);
    if (!rc)
        rc = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (!rc)
    {
        mbedtls_ssl_conf_min_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_max_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_rng(&t->conf, c->random, c->random_ctx);
#if defined(MBEDTLS_SSL_RENEGOTIATION)
        mbedtls_ssl_conf_renegotiation(&t->conf, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
        if (c->peer_fingerprint)
        {
            /* A nonempty trust chain is required by mbedTLS even when the
             * application uses explicit fingerprint verification. */
            mbedtls_ssl_conf_ca_chain(&t->conf, &t->certificate, NULL);
            mbedtls_ssl_conf_verify(&t->conf, verify_peer, t);
        }
        else
        {
            rc = mbedtls_x509_crt_parse(&t->ca, c->ca, c->ca_size);
            mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
        }
        if (!rc)
            rc = mbedtls_ssl_conf_own_cert(&t->conf, &t->certificate, &t->key);
        if (!rc)
            rc = mbedtls_ssl_setup(&t->ssl, &t->conf);
        if (!rc && c->hostname)
            rc = mbedtls_ssl_set_hostname(&t->ssl, c->hostname);
        mbedtls_ssl_set_bio(&t->ssl, t, send_bytes, receive_bytes, NULL);
    }
    if (rc)
    {
        rtaa_tls_destroy(t);
        return RTAA_ESECURITY;
    }
    *out = t;
    return RTAA_OK;
}
void rtaa_tls_destroy(rtaa_tls *t)
{
    if (!t)
        return;
    rtaa_allocator memory = t->config.memory;
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->certificate);
    mbedtls_x509_crt_free(&t->ca);
    mbedtls_pk_free(&t->key);
    mbedtls_platform_zeroize(t, sizeof(*t));
    memory.release(memory.ctx, t);
}
int rtaa_tls_ingest(rtaa_tls *t, const uint8_t *p, size_t size)
{
    if (!t || (!p && size))
        return RTAA_EINVAL;
    if (t->failed)
        return RTAA_ESTATE;
    if (size > sizeof(t->input) - t->input_size)
    {
        t->failed = true;
        return RTAA_EBOUNDS;
    }
    if (size)
        memcpy(t->input + t->input_size, p, size);
    t->input_size += size;
    return RTAA_OK;
}
int rtaa_tls_output(rtaa_tls *t, uint8_t *p, size_t capacity, size_t *size)
{
    if (!t || !p || !size)
        return RTAA_EINVAL;
    if (capacity < t->output_size)
        return RTAA_EBOUNDS;
    *size = t->output_size;
    memcpy(p, t->output, *size);
    t->output_size = 0;
    return RTAA_OK;
}
int rtaa_tls_handshake(rtaa_tls *t)
{
    if (!t || t->failed)
        return RTAA_ESTATE;
    int rc = mbedtls_ssl_handshake(&t->ssl);
    if (!rc)
    {
        t->ready = true;
        return RTAA_OK;
    }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return RTAA_AGAIN;
    t->failed = true;
    return RTAA_ESECURITY;
}
int rtaa_tls_encrypt(rtaa_tls *t, const uint8_t *p, size_t size)
{
    if (!t || !p || !size || size > RTAA_PLAIN_MAX)
        return RTAA_EINVAL;
    if (!t->ready || t->failed)
        return RTAA_ESTATE;
    int rc = mbedtls_ssl_write(&t->ssl, p, size);
    if (rc != (int)size)
    {
        t->failed = true;
        return RTAA_ESECURITY;
    }
    return RTAA_OK;
}
int rtaa_tls_decrypt(rtaa_tls *t, uint8_t *p, size_t capacity, size_t *size)
{
    if (!t || !p || !size || !capacity || capacity > INT_MAX)
        return RTAA_EINVAL;
    *size = 0;
    if (!t->ready || t->failed)
        return RTAA_ESTATE;
    int rc = mbedtls_ssl_read(&t->ssl, p, capacity);
    if (rc > 0)
    {
        *size = (size_t)rc;
        return RTAA_OK;
    }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return RTAA_AGAIN;
    t->failed = true;
    return RTAA_ESECURITY;
}
bool rtaa_tls_ready(const rtaa_tls *t)
{
    return t && t->ready && !t->failed;
}
size_t rtaa_tls_pending(const rtaa_tls *t)
{
    return t ? t->input_size + mbedtls_ssl_get_bytes_avail(&t->ssl) : 0;
}
