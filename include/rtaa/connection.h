/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_CONNECTION_H
#define RTAA_CONNECTION_H
#include "rtaa/receiver.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct rtaa_connection rtaa_connection;
    typedef enum
    {
        RTAA_CONNECTION_STOPPED,
        RTAA_CONNECTION_OPENING,
        RTAA_CONNECTION_SESSION,
        RTAA_CONNECTION_WAITING,
        RTAA_CONNECTION_EXHAUSTED
    } rtaa_connection_state;
    typedef struct
    {
        void *ctx;
        /* One bounded step of opening the same physical/logical peer. Return
         * AGAIN while pending, OK with a fresh owned transport, or a negative
         * error. No unbounded I/O: honor the remaining timeout, including USB
         * enumeration/Wi-Fi association. All callbacks are serialized.
         * On OK, ownership transfers to the receiver; close must release ALL
         * transport resources synchronously. Returned callbacks/context remain
         * valid until close. The platform enforces peer/USB route identity. */
        int (*open)(void *, uint32_t remaining_ms, rtaa_transport *);
        /* Cancel and release ALL pending-open resources synchronously. Called
         * exactly once per unsuccessful/cancelled opening attempt, even if its
         * first open step failed. Never called after ownership transfer. */
        void (*abandon)(void *);
        uint64_t (*now_ms)(void *);
    } rtaa_connector;
    typedef struct
    {
        /* Transport is supplied by connector. memory_limit includes controller
         * storage in addition to the receiver's documented buffer budget. */
        rtaa_receiver_config receiver;
        rtaa_connector connector;
        uint32_t open_timeout_ms, retry_delay_ms, retry_delay_max_ms;
        /* Hard limit for one start/stop cycle, INCLUDING its first attempt.
         * Successful connects do not reset it (bounds repeated peer flapping).
         * Only transport EIO/ETIMEOUT retry. Trust/protocol/configuration errors,
         * peer shutdown and explicit cancellation are terminal. */
        uint32_t max_attempts;
    } rtaa_connection_config;
    typedef struct
    {
        rtaa_connection_state state;
        uint32_t attempts;
        int last_error;
    } rtaa_connection_status;
    int rtaa_connection_create(const rtaa_connection_config *, rtaa_connection **);
    void rtaa_connection_destroy(rtaa_connection *);
    int rtaa_connection_start(rtaa_connection *);
    int rtaa_connection_poll(rtaa_connection *);
    int rtaa_connection_feed(rtaa_connection *, const uint8_t *, size_t);
    /* Immediate stop, synchronously releasing the session or pending open.
     * For graceful shutdown call receiver_shutdown and continue polling first.
     * Idempotent. A later explicit start begins a new bounded attempt cycle. */
    void rtaa_connection_stop(rtaa_connection *);
    void rtaa_connection_get_status(const rtaa_connection *, rtaa_connection_status *);
    /* Borrowed only until the next connection call. Use for serialized input,
     * stats and graceful shutdown, never destroy or retain this pointer.
     * A failure from a receiver API is retired on the next connection poll.
     * Do not poll/feed the receiver directly; use the connection entry points. */
    rtaa_receiver *rtaa_connection_receiver(rtaa_connection *);
#ifdef __cplusplus
}
#endif
#endif
