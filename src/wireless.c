/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/wireless.h"
#include "aap.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h>

static int fail(rtaa_wireless *w, int error)
{
    w->state = RTAA_WIRELESS_FAILED;
    w->error = error;
    return error;
}
static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static bool valid(const rtaa_wireless_config *c)
{
    if (!c || !c->ssid || !c->key || !c->bssid || !c->address || !c->port || !c->timeout_ms ||
        !strlen(c->ssid) || strlen(c->ssid) > 32 || strlen(c->key) < 8 || strlen(c->key) > 64 ||
        strlen(c->bssid) != 17)
        return false;
    for (size_t i = 0; i < strlen(c->key); ++i)
        if (strlen(c->key) == 64 ? hex(c->key[i]) < 0
                                 : (unsigned char)c->key[i] < 32 || (unsigned char)c->key[i] > 126)
            return false;
    unsigned any = 0, first = 0;
    for (unsigned i = 0; i < 6; ++i)
    {
        int hi = hex(c->bssid[i * 3]), lo = hex(c->bssid[i * 3 + 1]);
        if (hi < 0 || lo < 0 || (i < 5 && c->bssid[i * 3 + 2] != ':'))
            return false;
        unsigned value = (unsigned)(hi * 16 + lo);
        any |= value;
        if (!i)
            first = value;
    }
    if (!any || (first & 1))
        return false;
    /* This profile advertises a concrete IPv4 AP endpoint, never a hostname. */
    const char *p = c->address;
    unsigned parts[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        unsigned n = 0, count = 0;
        while (*p >= '0' && *p <= '9')
        {
            n = n * 10 + (unsigned)(*p++ - '0');
            if (++count > 3 || n > 255)
                return false;
        }
        if (!count || (i < 3 ? *p++ != '.' : *p != 0))
            return false;
        parts[i] = n;
    }
    return parts[0] && parts[0] < 224 && parts[0] != 127 && parts[3] != 255;
}
static int queue(rtaa_wireless *w, uint16_t id, const pb_msgdesc_t *fields, const void *msg)
{
    pb_ostream_t out = pb_ostream_from_buffer(w->tx + 4, sizeof(w->tx) - 4);
    if (!pb_encode(&out, fields, msg))
        return fail(w, RTAA_EBOUNDS);
    w->tx[0] = (uint8_t)(out.bytes_written >> 8);
    w->tx[1] = (uint8_t)out.bytes_written;
    w->tx[2] = (uint8_t)(id >> 8);
    w->tx[3] = (uint8_t)id;
    w->tx_size = out.bytes_written + 4;
    w->tx_sent = 0;
    w->sending = id;
    return RTAA_OK;
}
int rtaa_wireless_begin(rtaa_wireless *w, const rtaa_transport *io, const rtaa_wireless_config *c,
                        rtaa_wireless_status_fn status, void *ctx)
{
    if (!w || !io || !io->read || !io->write || !io->now_ms || !io->cancel || !io->close ||
        !valid(c))
        return RTAA_EINVAL;
    memset(w, 0, sizeof(*w));
    w->io = *io;
    w->state = RTAA_WIRELESS_WAIT_INFO;
    w->status = status;
    w->status_ctx = ctx;
    w->timeout_ms = c->timeout_ms;
    w->started = io->now_ms(io->ctx);
    strcpy(w->ssid, c->ssid);
    strcpy(w->key, c->key);
    strcpy(w->bssid, c->bssid);
    w->dynamic_ap = c->dynamic_ap;
    aap_WifiStart start = aap_WifiStart_init_zero;
    strcpy(start.address, c->address);
    start.port = c->port;
    return queue(w, 1, aap_WifiStart_fields, &start);
}
static bool decode(const pb_msgdesc_t *fields, void *msg, const uint8_t *p, size_t n)
{
    pb_istream_t in = pb_istream_from_buffer(p, n);
    return pb_decode(&in, fields, msg);
}
static int dispatch(rtaa_wireless *w, uint16_t id, const uint8_t *p, size_t n)
{
    switch (id)
    {
    case 2:
    {
        aap_WifiInfoRequest request = aap_WifiInfoRequest_init_zero;
        if (!decode(aap_WifiInfoRequest_fields, &request, p, n))
            break;
        aap_WifiCredentials c = aap_WifiCredentials_init_zero;
        strcpy(c.ssid, w->ssid);
        strcpy(c.key, w->key);
        strcpy(c.bssid, w->bssid);
        c.security = 8; /* WPA2 personal */
        c.access_point_type = w->dynamic_ap ? 1 : 0;
        int rc = queue(w, 3, aap_WifiCredentials_fields, &c);
        /* Do not retain a second copy of the key on the stack. */
        volatile char *key = c.key;
        for (size_t i = 0; i < sizeof(c.key); ++i)
            key[i] = 0;
        return rc;
    }
    case 6:
    {
        aap_WifiConnectStatus s = aap_WifiConnectStatus_init_zero;
        if (!decode(aap_WifiConnectStatus_fields, &s, p, n))
            break;
        if (w->status)
            w->status(w->status_ctx, id, s.has_state, s.state, s.has_text ? s.text : NULL);
        return RTAA_OK;
    }
    case 7:
    {
        aap_WifiStartResponse s = aap_WifiStartResponse_init_zero;
        if (!decode(aap_WifiStartResponse_fields, &s, p, n))
            break;
        if (w->status)
            w->status(w->status_ctx, id, s.has_status, s.status, NULL);
        /* Never redirect the listener based on an unauthenticated endpoint. */
        return RTAA_OK;
    }
    case 8:
    case 9:
    {
        aap_WifiPing ping = aap_WifiPing_init_zero;
        if (!decode(aap_WifiPing_fields, &ping, p, n))
            break;
        return id == 8 ? queue(w, 9, aap_WifiPing_fields, &ping) : RTAA_OK;
    }
    case 10:
    {
        aap_WifiRejection s = aap_WifiRejection_init_zero;
        if (!decode(aap_WifiRejection_fields, &s, p, n))
            break;
        if (w->status)
            w->status(w->status_ctx, id, s.has_reason, s.reason, NULL);
        return fail(w, RTAA_ESECURITY);
    }
    default:
        return fail(w, RTAA_ENOTSUP);
    }
    return fail(w, RTAA_EPROTO);
}
int rtaa_wireless_poll(rtaa_wireless *w, uint32_t timeout)
{
    if (!w || !timeout)
        return RTAA_EINVAL;
    if (w->state == RTAA_WIRELESS_FAILED)
        return w->error;
    if (w->state != RTAA_WIRELESS_WAIT_INFO && w->state != RTAA_WIRELESS_WAIT_TCP)
        return RTAA_ESTATE;
    uint64_t elapsed = w->io.now_ms(w->io.ctx) - w->started;
    if (elapsed >= w->timeout_ms)
        return fail(w, RTAA_ETIMEOUT);
    if (timeout > w->timeout_ms - elapsed)
        timeout = w->timeout_ms - (uint32_t)elapsed;
    if (w->tx_sent < w->tx_size)
    {
        size_t left = w->tx_size - w->tx_sent;
        int n = w->io.write(w->io.ctx, w->tx + w->tx_sent, left, timeout);
        if (n < 0)
            return fail(w, n);
        if ((size_t)n > left)
            return fail(w, RTAA_EIO);
        w->tx_sent += (size_t)n;
        if (w->tx_sent == w->tx_size)
        {
            if (w->sending == 3)
                w->state = RTAA_WIRELESS_WAIT_TCP;
            memset(w->tx, 0, sizeof(w->tx));
            w->tx_size = w->tx_sent = 0;
        }
        return RTAA_AGAIN;
    }
    size_t need = w->rx_used < 4 ? 4 : 4u + ((size_t)w->rx[0] << 8) + w->rx[1];
    if (need > sizeof(w->rx))
        return fail(w, RTAA_EBOUNDS);
    if (w->rx_used < need)
    {
        size_t left = need - w->rx_used;
        int n = w->io.read(w->io.ctx, w->rx + w->rx_used, left, timeout);
        if (n < 0)
            return fail(w, n);
        if ((size_t)n > left)
            return fail(w, RTAA_EIO);
        w->rx_used += (size_t)n;
        if (w->rx_used < 4)
            return RTAA_AGAIN;
        need = 4u + ((size_t)w->rx[0] << 8) + w->rx[1];
        if (need > sizeof(w->rx))
            return fail(w, RTAA_EBOUNDS);
        if (w->rx_used < need)
            return RTAA_AGAIN;
    }
    uint16_t id = (uint16_t)((unsigned)w->rx[2] << 8 | w->rx[3]);
    int rc = dispatch(w, id, w->rx + 4, need - 4);
    memset(w->rx, 0, sizeof(w->rx));
    w->rx_used = 0;
    return rc < 0 ? rc : RTAA_AGAIN;
}
void rtaa_wireless_close(rtaa_wireless *w)
{
    if (!w || w->state == RTAA_WIRELESS_IDLE || w->state == RTAA_WIRELESS_CLOSED)
        return;
    w->io.cancel(w->io.ctx);
    w->io.close(w->io.ctx);
    volatile uint8_t *p = (volatile uint8_t *)w;
    for (size_t i = 0; i < sizeof(*w); ++i)
        p[i] = 0;
    w->state = RTAA_WIRELESS_CLOSED;
}
