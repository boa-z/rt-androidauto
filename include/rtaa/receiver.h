/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_RECEIVER_H
#define RTAA_RECEIVER_H
#include "rtaa/tls.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        RTAA_VIDEO,
        RTAA_MEDIA,
        RTAA_SYSTEM,
        RTAA_SPEECH,
        RTAA_MIC,
        RTAA_INPUT,
        RTAA_SENSOR,
        RTAA_ROLE_COUNT
    } rtaa_role;
    typedef enum
    {
        RTAA_IDLE,
        RTAA_VERSION,
        RTAA_TLS,
        RTAA_DISCOVERY,
        RTAA_CONNECTED,
        RTAA_CLOSING,
        RTAA_CLOSED,
        RTAA_FAILED
    } rtaa_state;
    typedef struct rtaa_receiver rtaa_receiver;
    typedef struct
    {
        bool has_level, has_time_remaining, has_critical;
        uint32_t level, time_remaining_s;
        bool critical;
    } rtaa_battery_status;
    typedef struct
    {
        uint8_t id; /* 1..255, unique. 0 disables this service. Never fixed by role. */
        size_t message_capacity;
    } rtaa_channel_config;
    typedef struct
    {
        void *ctx;
        /* Sink start/data/stop must enqueue or consume real PCM/H264 data. Returning
         * an error closes the session; it is never ACKed as successful playback. */
        int (*start)(void *, rtaa_role, uint32_t rate, uint8_t channels, uint8_t bits);
        int (*data)(void *, rtaa_role, const uint8_t *, size_t, uint64_t timestamp_us);
        void (*stop)(void *, rtaa_role);
        /* Returns negotiated AudioFocusState (1..7), or a negative error. */
        int (*audio_focus)(void *, unsigned request);
        int (*video_focus)(void *, bool focused);
        int (*microphone)(void *, bool open, uint32_t rate, bool ec, bool anc);
        void (*state)(void *, rtaa_state, int error);
        /* Return actual navigation owner: native=1, projected=2. Without a
         * callback the receiver reports native, rather than claiming focus. */
        int (*navigation_focus)(void *, unsigned requested);
        void (*voice_session)(void *, bool started);
        void (*battery_status)(void *, const rtaa_battery_status *);
        /* Optional scheduler seam for Night(10)/DrivingStatus(13). Start/update
         * receives the peer's exact nonnegative interval; stop retires it on
         * channel/session teardown. Supply both callbacks, or neither. */
        int (*sensor_start)(void *, uint32_t type, int64_t refresh_interval);
        void (*sensor_stop)(void *, uint32_t type);
    } rtaa_sink;
    typedef struct
    {
        rtaa_allocator memory;
        rtaa_transport transport;
        rtaa_tls_config tls;
        rtaa_sink sink;
        rtaa_geometry geometry;
        rtaa_channel_config channels[RTAA_ROLE_COUNT];
        const char *name, *manufacturer, *model;
        uint32_t io_timeout_ms, session_timeout_ms, heartbeat_ms;
        /* Includes receiver buffers and TLS wrapper, excludes mbedTLS internal
         * allocations; the port must also enforce its own allocator budget. */
        size_t memory_limit;
        /* 0 keeps legacy 1.1. Nonzero requests AAP 1.minor (1..7), accepting
         * a peer's lower supported minor. This does not enable extra services. */
        uint16_t protocol_minor;
        /* Optional real hardware key set (including 65536 for rotary), up to
         * 32 distinct positive values. Zero count preserves the legacy set. */
        uint32_t input_keycodes[32];
        uint8_t input_keycode_count;
    } rtaa_receiver_config;
    typedef struct
    {
        uint64_t messages, video_bytes, audio_bytes, microphone_bytes, acks, errors;
    } rtaa_statistics;
    int rtaa_receiver_create(const rtaa_receiver_config *, rtaa_receiver **);
    void rtaa_receiver_destroy(rtaa_receiver *);
    int rtaa_receiver_start(rtaa_receiver *);
    int rtaa_receiver_feed(rtaa_receiver *, const uint8_t *, size_t);
    int rtaa_receiver_poll(rtaa_receiver *);
    /* Single pointer: PRESS=0, RELEASE=1, DRAG=2. A press must begin inside
     * the viewport. Drags outside return EBOUNDS without forwarding coordinates;
     * an outside release ends the captured gesture at its last valid position.
     * ID must match the captured press. Calls must be serialized. */
    int rtaa_receiver_touch(rtaa_receiver *, uint8_t action, uint16_t id, uint16_t x, uint16_t y,
                            uint64_t us);
    /* AAP has no verified cancel action in this profile: emit RELEASE at the
     * last accepted position. No active gesture is an idempotent success. */
    int rtaa_receiver_touch_cancel(rtaa_receiver *, uint64_t us);
    /* Only keys acknowledged by InputBinding may be sent. Each press requires
     * a matching release; repeats are not implemented by this profile. */
    int rtaa_receiver_key(rtaa_receiver *, uint32_t keycode, bool pressed, uint64_t us);
    int rtaa_receiver_rotary(rtaa_receiver *, int32_t delta, uint64_t us);
    int rtaa_receiver_night(rtaa_receiver *, bool night);
    int rtaa_receiver_driving(rtaa_receiver *, uint32_t restrictions);
    int rtaa_receiver_microphone(rtaa_receiver *, const uint8_t *, size_t, uint64_t us);
    int rtaa_receiver_shutdown(rtaa_receiver *);
    /* The negotiated minor is available after VERSION, zero beforehand. */
    uint16_t rtaa_receiver_protocol_minor(const rtaa_receiver *);
    /* Explicitly notify actual HFP/call availability. Never sent automatically;
     * the application chooses whether to use this optional peer capability. */
    int rtaa_receiver_call_available(rtaa_receiver *, bool available);
    /* Close only this service, preserving the control/TLS session. The peer
     * may reopen and renegotiate it later. Does not fabricate release events. */
    int rtaa_receiver_close_channel(rtaa_receiver *, rtaa_role);
    rtaa_state rtaa_receiver_state(const rtaa_receiver *);
    int rtaa_receiver_error(const rtaa_receiver *);
    void rtaa_receiver_statistics(const rtaa_receiver *, rtaa_statistics *);
    /* Geometry/capabilities are immutable during a session. To switch FULLSCREEN
     * and FUSION, close and destroy, then create with the requested geometry. */
    size_t rtaa_receiver_storage_size(void);
#ifdef __cplusplus
}
#endif
#endif
