/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_WIRELESS_H
#define RTAA_WIRELESS_H
#include "rtaa/core.h"
#ifdef __cplusplus
extern "C"
{
#endif
#define RTAA_WIRELESS_UUID "4de17a00-52cb-11e6-bdf4-0800200c9a66"
#define RTAA_WIRELESS_PAYLOAD_MAX 1024u
    typedef enum
    {
        RTAA_WIRELESS_IDLE,
        RTAA_WIRELESS_WAIT_INFO,
        RTAA_WIRELESS_WAIT_TCP,
        RTAA_WIRELESS_FAILED,
        RTAA_WIRELESS_CLOSED
    } rtaa_wireless_state;
    typedef struct
    {
        const char *ssid, *key, *bssid, *address;
        uint16_t port;
        bool dynamic_ap;
        uint32_t timeout_ms;
    } rtaa_wireless_config;
    /* Raw peer status: no success interpretation of undocumented status codes.
     * Only the separately authenticated AAP/TLS session establishes projection.
     * status_text is borrowed for the duration of this serialized callback. */
    typedef void (*rtaa_wireless_status_fn)(void *, uint16_t message, bool has_status,
                                            int32_t status, const char *status_text);
    typedef struct
    {
        rtaa_transport io;
        rtaa_wireless_state state;
        rtaa_wireless_status_fn status;
        void *status_ctx;
        uint8_t rx[RTAA_WIRELESS_PAYLOAD_MAX + 4], tx[RTAA_WIRELESS_PAYLOAD_MAX + 4];
        size_t rx_used, tx_size, tx_sent;
        uint64_t started;
        uint32_t timeout_ms;
        char ssid[33], key[65], bssid[18];
        bool dynamic_ap;
        uint16_t sending;
        int error;
    } rtaa_wireless;
    /* Dongle bootstrap dialect: 1=start, 2=info request, 3=credentials,
     * 6=connect status, 7=start response, 8/9=ping, 10=rejection.
     * Platform must expose the UUID via SDP and provide an authenticated,
     * encrypted RFCOMM stream, plus a live WPA2 AP and TCP listener first.
     * The object takes stream ownership ONLY on successful begin. No heap use.
     * Version negotiation/setup-info (4/5/11) is explicitly unsupported. */
    int rtaa_wireless_begin(rtaa_wireless *, const rtaa_transport *, const rtaa_wireless_config *,
                            rtaa_wireless_status_fn, void *);
    /* Bounded one-read or one-write operation. Zero I/O progress is AGAIN;
     * the session deadline still expires. WAIT_TCP is not a completed session. */
    int rtaa_wireless_poll(rtaa_wireless *, uint32_t io_timeout_ms);
    /* Serialized with poll; cancels and closes exactly once, erases AP key. */
    void rtaa_wireless_close(rtaa_wireless *);
#ifdef __cplusplus
}
#endif
#endif
