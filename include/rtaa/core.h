/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_CORE_H
#define RTAA_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

    /* Public APIs are serialized by their caller. Callbacks must not reenter the
     * same object. Buffers borrowed by callbacks are valid only during the call. */
    typedef enum
    {
        RTAA_OK = 0,
        RTAA_AGAIN = 1,
        RTAA_EINVAL = -1,
        RTAA_EBOUNDS = -2,
        RTAA_EPROTO = -3,
        RTAA_ESTATE = -4,
        RTAA_EIO = -5,
        RTAA_ETIMEOUT = -6,
        RTAA_ECANCEL = -7,
        RTAA_ESECURITY = -8,
        RTAA_ENOTSUP = -9
    } rtaa_result;

    enum
    {
        RTAA_FIRST = 1,
        RTAA_LAST = 2,
        RTAA_CONTROL = 4,
        RTAA_ENCRYPTED = 8
    };
#define RTAA_FRAME_MAX 18432u
#define RTAA_PLAIN_MAX 16384u
#define RTAA_CHANNEL_MAX 16u

    typedef struct
    {
        uint8_t channel, flags;
        uint32_t total;
        const uint8_t *data;
        size_t size;
    } rtaa_frame;
    typedef int (*rtaa_frame_fn)(void *, const rtaa_frame *);
    typedef struct
    {
        uint8_t bytes[RTAA_FRAME_MAX + 8];
        size_t used, needed;
        bool failed;
    } rtaa_parser;
    void rtaa_parser_reset(rtaa_parser *);
    int rtaa_parser_feed(rtaa_parser *, const uint8_t *, size_t, rtaa_frame_fn, void *);
    int rtaa_frame_encode(const rtaa_frame *, uint8_t *, size_t, size_t *);

    typedef struct
    {
        uint8_t channel, attributes;
        uint8_t *data;
        size_t capacity, used, total;
        bool active;
    } rtaa_assembly;
    /* frame data must be plaintext, decrypted separately for each frame. */
    int rtaa_assembly_push(rtaa_assembly *, const rtaa_frame *, bool *complete);
    void rtaa_assembly_reset(rtaa_assembly *);

    typedef struct
    {
        void *ctx;
        /* Return bytes transferred, RTAA_AGAIN (via zero), or negative error.
         * read/write must honor timeout_ms and cancel. No infinite blocking. */
        int (*read)(void *, uint8_t *, size_t, uint32_t timeout_ms);
        int (*write)(void *, const uint8_t *, size_t, uint32_t timeout_ms);
        uint64_t (*now_ms)(void *);
        void (*cancel)(void *);
        void (*close)(void *);
    } rtaa_transport;
    int rtaa_write_all(const rtaa_transport *, const uint8_t *, size_t, uint32_t);

    typedef struct
    {
        uint16_t frame_width, frame_height;
        uint16_t margin_width, margin_height; /* total symmetric margins */
        uint16_t viewport_x, viewport_y, viewport_width, viewport_height;
    } rtaa_geometry;
    int rtaa_geometry_validate(const rtaa_geometry *);
    /* Rejects touches outside the video viewport, including the LVGL strip. */
    int rtaa_touch_map(const rtaa_geometry *, uint16_t, uint16_t, uint16_t *, uint16_t *);

    typedef enum
    {
        RTAA_AOA_IDLE,
        RTAA_AOA_REENUMERATING,
        RTAA_AOA_READY,
        RTAA_AOA_FAILED,
        RTAA_AOA_DETACHED
    } rtaa_aoa_state;
    typedef struct
    {
        void *ctx;
        int (*control)(void *, uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                       uint8_t *data, uint16_t length, uint32_t timeout_ms);
        void (*cancel)(void *);
    } rtaa_usb_control;
    typedef struct
    {
        rtaa_aoa_state state;
        rtaa_usb_control usb;
        uint64_t deadline;
        uint16_t protocol;
        uint8_t interface_number, endpoint_in, endpoint_out;
    } rtaa_aoa;
    /* Strings are manufacturer/model/description/version/URI/serial, all required.
     * Accessory re-enumeration must be matched by the port to the SAME physical
     * USB route. AOA IDs alone are not sufficient device identity. */
    int rtaa_aoa_begin(rtaa_aoa *, const rtaa_usb_control *, const char *const[6], uint64_t);
    bool rtaa_aoa_accessory(uint16_t vid, uint16_t pid);
    int rtaa_aoa_bind(rtaa_aoa *, uint16_t vid, uint16_t pid,
                      const uint8_t *configuration_descriptor, size_t, uint64_t);
    int rtaa_aoa_poll(rtaa_aoa *, uint64_t);
    void rtaa_aoa_detach(rtaa_aoa *);

#ifdef __cplusplus
}
#endif
#endif
