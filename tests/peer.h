/* SPDX-License-Identifier: GPL-3.0-only
 * Test-only mbedTLS server and public test keys; never linked into rtaa. */
#ifndef RTAA_TEST_PEER_H
#define RTAA_TEST_PEER_H
#include "rtaa/tls.h"
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/certs.h>
#include <mbedtls/sha256.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    uint8_t input[65536], output[65536], hash[32];
    size_t input_size, output_size;
    bool ready;
} peer;
static int peer_send(void *ctx, const unsigned char *p, size_t n)
{
    peer *s = ctx;
    assert(n <= sizeof(s->output) - s->output_size);
    memcpy(s->output + s->output_size, p, n);
    s->output_size += n;
    return (int)n;
}
static int peer_recv(void *ctx, unsigned char *p, size_t n)
{
    peer *s = ctx;
    if (!s->input_size)
        return MBEDTLS_ERR_SSL_WANT_READ;
    if (n > s->input_size)
        n = s->input_size;
    memcpy(p, s->input, n);
    s->input_size -= n;
    memmove(s->input, s->input + n, s->input_size);
    return (int)n;
}
static void peer_ingest(peer *s, const uint8_t *p, size_t n)
{
    assert(n <= sizeof(s->input) - s->input_size);
    memcpy(s->input + s->input_size, p, n);
    s->input_size += n;
}
static void peer_init(peer *s)
{
    memset(s, 0, sizeof(*s));
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->cert);
    mbedtls_pk_init(&s->key);
    mbedtls_entropy_init(&s->entropy);
    mbedtls_ctr_drbg_init(&s->rng);
    assert(!mbedtls_ctr_drbg_seed(&s->rng, mbedtls_entropy_func, &s->entropy,
                                  (const uint8_t *)"rtaa-test", 9));
    assert(!mbedtls_x509_crt_parse(&s->cert, (const uint8_t *)mbedtls_test_srv_crt_rsa_sha256_pem,
                                   mbedtls_test_srv_crt_rsa_sha256_pem_len));
    assert(!mbedtls_pk_parse_key(&s->key, (const uint8_t *)mbedtls_test_srv_key_rsa_pem,
                                 mbedtls_test_srv_key_rsa_pem_len, NULL, 0));
    assert(!mbedtls_sha256_ret(s->cert.raw.p, s->cert.raw.len, s->hash, 0));
    assert(!mbedtls_ssl_config_defaults(&s->conf, MBEDTLS_SSL_IS_SERVER,
                                        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT));
    mbedtls_ssl_conf_min_version(&s->conf, 3, 3);
    mbedtls_ssl_conf_max_version(&s->conf, 3, 3);
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s->rng);
    assert(!mbedtls_ssl_conf_own_cert(&s->conf, &s->cert, &s->key));
    assert(!mbedtls_ssl_setup(&s->ssl, &s->conf));
    mbedtls_ssl_set_bio(&s->ssl, s, peer_send, peer_recv, NULL);
}
static void peer_free(peer *s)
{
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_x509_crt_free(&s->cert);
    mbedtls_pk_free(&s->key);
    mbedtls_ctr_drbg_free(&s->rng);
    mbedtls_entropy_free(&s->entropy);
}
static int peer_step(peer *s)
{
    int rc = mbedtls_ssl_handshake(&s->ssl);
    if (!rc)
        s->ready = true;
    assert(!rc || rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
    return rc;
}
static void *allocate(void *ctx, size_t size)
{
    (void)ctx;
    return calloc(1, size);
}
static void release(void *ctx, void *p)
{
    (void)ctx;
    free(p);
}
static int pin(void *ctx, const uint8_t hash[32])
{
    return memcmp(((peer *)ctx)->hash, hash, 32);
}
static rtaa_tls_config tls_config(peer *s)
{
    rtaa_tls_config c = {0};
    c.memory = (rtaa_allocator){NULL, allocate, release};
    c.certificate = (const uint8_t *)mbedtls_test_cli_crt_rsa_pem;
    c.certificate_size = mbedtls_test_cli_crt_rsa_pem_len;
    c.private_key = (const uint8_t *)mbedtls_test_cli_key_rsa_pem;
    c.private_key_size = mbedtls_test_cli_key_rsa_pem_len;
    c.random = mbedtls_ctr_drbg_random;
    c.random_ctx = &s->rng;
    c.peer_fingerprint = pin;
    c.peer_ctx = s;
    return c;
}
#endif
