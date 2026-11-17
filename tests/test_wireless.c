/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/wireless.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct
{
    uint8_t in[4096], out[4096];
    size_t in_size, pos, out_size, chunk;
    uint64_t now;
    unsigned cancels, closes, events;
    uint16_t id;
    bool has_status, zero, overshoot;
    int32_t status;
} mock;
static int read_bytes(void *ctx, uint8_t *p, size_t n, uint32_t timeout)
{
    mock *m = ctx;
    assert(timeout);
    if (m->overshoot)
        return (int)n + 1;
    if (n > m->chunk)
        n = m->chunk;
    if (n > m->in_size - m->pos)
        n = m->in_size - m->pos;
    memcpy(p, m->in + m->pos, n);
    m->pos += n;
    return (int)n;
}
static int write_bytes(void *ctx, const uint8_t *p, size_t n, uint32_t timeout)
{
    mock *m = ctx;
    assert(timeout);
    if (m->overshoot)
        return (int)n + 1;
    if (m->zero)
        return 0;
    if (n > m->chunk)
        n = m->chunk;
    assert(m->out_size + n <= sizeof(m->out));
    memcpy(m->out + m->out_size, p, n);
    m->out_size += n;
    return (int)n;
}
static uint64_t clock_ms(void *ctx)
{
    return ((mock *)ctx)->now;
}
static void cancel(void *ctx)
{
    ++((mock *)ctx)->cancels;
}
static void close_io(void *ctx)
{
    ++((mock *)ctx)->closes;
}
static void status(void *ctx, uint16_t id, bool has, int32_t value, const char *text)
{
    mock *m = ctx;
    ++m->events;
    m->id = id;
    m->has_status = has;
    m->status = value;
    if (id == 6)
        assert(text && !strcmp(text, "test"));
}
static rtaa_wireless_config config(void)
{
    return (rtaa_wireless_config){"AA-test", "12345678", "02:11:22:33:44:55", "192.168.43.1", 5288,
                                  true,      30000};
}
static void begin(rtaa_wireless *w, mock *m)
{
    rtaa_transport t = {m, read_bytes, write_bytes, clock_ms, cancel, close_io};
    rtaa_wireless_config c = config();
    assert(rtaa_wireless_begin(w, &t, &c, status, m) == 0);
}
static void pump(rtaa_wireless *w, unsigned n)
{
    while (n--)
        assert(rtaa_wireless_poll(w, 10) == RTAA_AGAIN);
}
static void incoming(mock *m, uint16_t id, const uint8_t *p, size_t n)
{
    assert(m->in_size + 4 + n <= sizeof(m->in));
    uint8_t *out = m->in + m->in_size;
    out[0] = (uint8_t)(n >> 8);
    out[1] = (uint8_t)n;
    out[2] = (uint8_t)(id >> 8);
    out[3] = (uint8_t)id;
    if (n)
        memcpy(out + 4, p, n);
    m->in_size += n + 4;
}
int main(void)
{
    /* Literal independently specified protobuf wire vectors, no production
     * encoder used to generate expected credentials or peer messages. */
    static const uint8_t start[] = {0,   17,  0,   1,   10,  12,  '1', '9', '2',  '.', '1',
                                    '6', '8', '.', '4', '3', '.', '1', 16,  0xa8, 0x29};
    /* length is 17: address field 14 + port field 3. */
    for (size_t chunk = 1; chunk <= 64; ++chunk)
    {
        mock m = {0};
        m.chunk = chunk;
        rtaa_wireless w;
        begin(&w, &m);
        pump(&w, 32);
        assert(m.out_size == sizeof(start));
        uint8_t expected[sizeof(start)];
        memcpy(expected, start, sizeof(start));
        expected[1] = 17;
        assert(!memcmp(m.out, expected, sizeof(start)));
        incoming(&m, 2, NULL, 0);
        pump(&w, 128);
        assert(w.state == RTAA_WIRELESS_WAIT_TCP);
        static const uint8_t creds[] = {0,   42,  0,   3,   10,  7,   'A', 'A', '-', 't', 'e', 's',
                                        't', 18,  8,   '1', '2', '3', '4', '5', '6', '7', '8', 26,
                                        17,  '0', '2', ':', '1', '1', ':', '2', '2', ':', '3', '3',
                                        ':', '4', '4', ':', '5', '5', 32,  8,   40,  1};
        assert(m.out_size == sizeof(start) + sizeof(creds));
        assert(!memcmp(m.out + sizeof(start), creds, sizeof(creds)));
        /* Coalesced duplicate requests, status, start acknowledgement and ping. */
        incoming(&m, 2, NULL, 0);
        static const uint8_t connect[] = {8, 0, 18, 4, 't', 'e', 's', 't'};
        incoming(&m, 6, connect, sizeof(connect));
        static const uint8_t ack[] = {24, 1};
        incoming(&m, 7, ack, sizeof(ack));
        static const uint8_t ping[] = {8, 0x96, 1};
        incoming(&m, 8, ping, sizeof(ping));
        pump(&w, 256);
        assert(m.events == 2 && m.id == 7 && m.has_status && m.status == 1);
        assert(w.state == RTAA_WIRELESS_WAIT_TCP);
        static const uint8_t pong[] = {0, 3, 0, 9, 8, 0x96, 1};
        assert(!memcmp(m.out + m.out_size - sizeof(pong), pong, sizeof(pong)));
        m.now = 30000;
        assert(rtaa_wireless_poll(&w, 10) == RTAA_ETIMEOUT);
        rtaa_wireless_close(&w);
        rtaa_wireless_close(&w);
        assert(m.cancels == 1 && m.closes == 1);
        for (size_t i = 0; i < sizeof(w.key); ++i)
            assert(w.key[i] == 0);
    }
    mock m = {.chunk = 4096};
    rtaa_wireless w;
    begin(&w, &m);
    pump(&w, 1);
    incoming(&m, 4, NULL, 0);
    assert(rtaa_wireless_poll(&w, 10) == RTAA_ENOTSUP);
    rtaa_wireless_close(&w);
    m = (mock){.chunk = 4096};
    begin(&w, &m);
    pump(&w, 1);
    incoming(&m, 10, NULL, 0);
    assert(rtaa_wireless_poll(&w, 10) == RTAA_ESECURITY);
    assert(m.events == 1 && !m.has_status);
    rtaa_wireless_close(&w);
    m = (mock){.chunk = 4096};
    begin(&w, &m);
    pump(&w, 1);
    const uint8_t bad[] = {0x80};
    incoming(&m, 2, bad, sizeof(bad));
    assert(rtaa_wireless_poll(&w, 10) == RTAA_AGAIN);
    assert(rtaa_wireless_poll(&w, 10) == RTAA_EPROTO);
    rtaa_wireless_close(&w);
    m = (mock){.chunk = 4096};
    begin(&w, &m);
    pump(&w, 1);
    m.in[0] = 0xff;
    m.in[1] = 0xff;
    m.in_size = 4;
    assert(rtaa_wireless_poll(&w, 10) == RTAA_EBOUNDS);
    rtaa_wireless_close(&w);
    m = (mock){.chunk = 4096, .zero = true};
    begin(&w, &m);
    pump(&w, 2);
    m.now = 30000;
    assert(rtaa_wireless_poll(&w, 10) == RTAA_ETIMEOUT);
    rtaa_wireless_close(&w);
    m = (mock){.chunk = 4096, .overshoot = true};
    begin(&w, &m);
    assert(rtaa_wireless_poll(&w, 10) == RTAA_EIO);
    rtaa_wireless_close(&w);
    rtaa_transport t = {&m, read_bytes, write_bytes, clock_ms, cancel, close_io};
    const char *invalid[] = {"",          "0.0.0.0", "127.0.0.1", "224.0.0.1",
                             "256.1.2.3", "1.2.3",   "1.2.3.4.5", "192.168.43.255"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        rtaa_wireless_config c = config();
        c.address = invalid[i];
        assert(rtaa_wireless_begin(&w, &t, &c, NULL, NULL) == RTAA_EINVAL);
    }
    rtaa_wireless_config c = config();
    c.bssid = "01:11:22:33:44:55";
    assert(rtaa_wireless_begin(&w, &t, &c, NULL, NULL) == RTAA_EINVAL);
    c = config();
    c.key = "short";
    assert(rtaa_wireless_begin(&w, &t, &c, NULL, NULL) == RTAA_EINVAL);
    puts("wireless bootstrap: PASS");
    return 0;
}
