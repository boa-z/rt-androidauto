/* SPDX-License-Identifier: GPL-3.0-only */
#include "peer.h"
#include <stdio.h>
static void exchange(rtaa_tls *t, peer *s)
{
    uint8_t out[RTAA_FRAME_MAX];
    size_t n;
    assert(!rtaa_tls_output(t, out, sizeof(out), &n));
    if (n)
        peer_ingest(s, out, n);
    peer_step(s);
    if (s->output_size)
    {
        /* Arbitrary splits at the TLS byte-stream boundary. */
        for (size_t i = 0; i < s->output_size; ++i)
            assert(!rtaa_tls_ingest(t, s->output + i, 1));
        s->output_size = 0;
    }
}
static void run(bool wrong_pin)
{
    peer *s = calloc(1, sizeof(*s));
    assert(s);
    peer_init(s);
    rtaa_tls_config c = tls_config(s);
    if (wrong_pin)
        s->hash[0] ^= 1;
    rtaa_tls *t;
    assert(!rtaa_tls_create(&c, &t));
    int rc = RTAA_AGAIN;
    for (unsigned i = 0; i < 30 && rc == RTAA_AGAIN; ++i)
    {
        rc = rtaa_tls_handshake(t);
        if (rc >= 0)
            exchange(t, s);
    }
    if (wrong_pin)
    {
        assert(rc == RTAA_ESECURITY);
        assert(!rtaa_tls_ready(t));
    }
    else
    {
        assert(!rc && rtaa_tls_ready(t) && s->ready);
        assert(!strcmp(mbedtls_ssl_get_version(&s->ssl), "TLSv1.2"));
        const uint8_t text[] = "real TLS application data";
        assert(!rtaa_tls_encrypt(t, text, sizeof(text)));
        uint8_t out[RTAA_FRAME_MAX];
        size_t n;
        assert(!rtaa_tls_output(t, out, sizeof(out), &n));
        peer_ingest(s, out, n);
        int got = mbedtls_ssl_read(&s->ssl, out, sizeof(out));
        assert(got == sizeof(text) && !memcmp(out, text, sizeof(text)));
        assert(mbedtls_ssl_write(&s->ssl, text, sizeof(text)) == sizeof(text));
        assert(!rtaa_tls_ingest(t, s->output, s->output_size));
        s->output_size = 0;
        assert(!rtaa_tls_decrypt(t, out, sizeof(out), &n) && n == sizeof(text) &&
               !memcmp(out, text, n));
        assert(mbedtls_ssl_write(&s->ssl, text, sizeof(text)) == sizeof(text));
        s->output[s->output_size - 1] ^= 1;
        assert(!rtaa_tls_ingest(t, s->output, s->output_size));
        assert(rtaa_tls_decrypt(t, out, sizeof(out), &n) == RTAA_ESECURITY);
    }
    rtaa_tls_destroy(t);
    peer_free(s);
    free(s);
}
int main(void)
{
    run(false);
    run(true);
    rtaa_tls *t = (rtaa_tls *)(uintptr_t)1;
    assert(rtaa_tls_create(NULL, &t) == RTAA_EINVAL && !t);
    printf("TLS: PASS; wrapper storage=%zu bytes\n", rtaa_tls_storage_size());
    return 0;
}
