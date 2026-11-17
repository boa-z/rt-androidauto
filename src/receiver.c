/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/receiver.h"
#include "status.h"
#include "aap.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>
#include <string.h>
#include <limits.h>

typedef struct
{
    rtaa_assembly assembly;
    bool open, setup, streaming, bound;
    int32_t session;
    uint32_t window, outstanding;
} channel;
struct rtaa_receiver
{
    rtaa_receiver_config c;
    rtaa_state state;
    int error;
    rtaa_tls *tls;
    rtaa_parser parser;
    channel channels[RTAA_ROLE_COUNT];
    rtaa_assembly control;
    uint8_t control_data[65536], plain[RTAA_PLAIN_MAX], wire[RTAA_FRAME_MAX + 8];
    uint8_t message[RTAA_PLAIN_MAX], cipher[RTAA_FRAME_MAX];
    uint64_t stage_time, last_rx, ping_time, ping_token;
    bool ping_pending, mic_open, night_subscribed, driving_subscribed, closed, navigation_enabled;
    bool touching;
    uint16_t touch_id, touch_x, touch_y;
    uint32_t bound_keys, pressed_keys;
    uint16_t protocol_minor;
    rtaa_statistics stats;
};
static const uint32_t input_keys[] = {3, 4, 5, 6, 19, 20, 21, 23};
static uint32_t key_mask(const rtaa_receiver *r, uint32_t key)
{
    for (unsigned i = 0; i < r->c.input_keycode_count; ++i)
        if (r->c.input_keycodes[i] == key)
            return UINT32_C(1) << i;
    return 0;
}
size_t rtaa_receiver_storage_size(void)
{
    return sizeof(rtaa_receiver);
}
static uint64_t now(rtaa_receiver *r)
{
    return r->c.transport.now_ms(r->c.transport.ctx);
}
static int decode(const pb_msgdesc_t *fields, void *out, const uint8_t *data, size_t size)
{
    pb_istream_t in = pb_istream_from_buffer(data, size);
    return pb_decode(&in, fields, out) ? RTAA_OK : RTAA_EPROTO;
}
static void transition(rtaa_receiver *r, rtaa_state state, int error)
{
    r->state = state;
    r->stage_time = now(r);
    if (r->c.sink.state)
        r->c.sink.state(r->c.sink.ctx, state, error);
}
static void retire_channel(rtaa_receiver *r, rtaa_role role)
{
    channel *c = &r->channels[role];
    if (role == RTAA_MEDIA_INFO && c->open)
        r->c.sink.media_info_reset(r->c.sink.ctx);
    if (role == RTAA_PHONE && c->open)
        r->c.sink.phone_reset(r->c.sink.ctx);
    if (role == RTAA_NAVIGATION)
    {
        if (c->open)
            r->c.sink.navigation_reset(r->c.sink.ctx);
        r->navigation_enabled = false;
    }
    if (c->streaming && role != RTAA_MIC && r->c.sink.stop)
        r->c.sink.stop(r->c.sink.ctx, role);
    if (role == RTAA_MIC && r->mic_open)
    {
        r->c.sink.microphone(r->c.sink.ctx, false, 16000, false, false);
        r->mic_open = false;
    }
    if (role == RTAA_INPUT)
    {
        r->touching = false;
        r->bound_keys = r->pressed_keys = 0;
    }
    if (role == RTAA_SENSOR)
    {
        if (r->c.sink.sensor_stop)
        {
            if (r->night_subscribed)
                r->c.sink.sensor_stop(r->c.sink.ctx, 10);
            if (r->driving_subscribed)
                r->c.sink.sensor_stop(r->c.sink.ctx, 13);
        }
        r->night_subscribed = r->driving_subscribed = false;
    }
    c->open = c->setup = c->streaming = c->bound = false;
    c->window = c->outstanding = 0;
    /* Keep session monotonically increasing across microphone reopens so a
     * delayed ACK for the previous stream cannot free the new stream window. */
    rtaa_assembly_reset(&c->assembly);
}
static void retire(rtaa_receiver *r)
{
    for (unsigned i = 0; i < RTAA_ROLE_COUNT; ++i)
        retire_channel(r, (rtaa_role)i);
    r->touching = false;
    r->bound_keys = r->pressed_keys = 0;
    if (!r->closed)
    {
        if (r->c.transport.cancel)
            r->c.transport.cancel(r->c.transport.ctx);
        if (r->c.transport.close)
            r->c.transport.close(r->c.transport.ctx);
        r->closed = true;
    }
}
static int fail(rtaa_receiver *r, int rc)
{
    if (r->state != RTAA_FAILED && r->state != RTAA_CLOSED)
    {
        ++r->stats.errors;
        r->error = rc;
        retire(r);
        transition(r, RTAA_FAILED, rc);
    }
    return rc;
}
static int raw_fragment(rtaa_receiver *r, uint8_t id, uint8_t flags, uint32_t total,
                        const uint8_t *data, size_t size)
{
    rtaa_frame f = {id, flags, total, data, size};
    size_t written;
    int rc = rtaa_frame_encode(&f, r->wire, sizeof(r->wire), &written);
    if (!rc)
        rc = rtaa_write_all(&r->c.transport, r->wire, written, r->c.io_timeout_ms);
    return rc;
}
static int send_parts(rtaa_receiver *r, uint8_t id, uint16_t message, const uint8_t *prefix,
                      size_t prefix_size, const uint8_t *body, size_t size, bool encrypted,
                      bool control)
{
    /* Bounded scatter/gather avoids a message-sized copy or a large RTOS
     * stack buffer. Total is the plaintext size, including the message ID. */
    if (prefix_size > 65526 || size > 65526 - prefix_size)
        return RTAA_EBOUNDS;
    uint8_t header[2] = {(uint8_t)(message >> 8), (uint8_t)message};
    const uint8_t *parts[] = {header, prefix, body};
    const size_t lengths[] = {2, prefix_size, size};
    size_t total = 2 + prefix_size + size;
    size_t offset = 0, part = 0, used = 0;
    while (offset < total)
    {
        size_t chunk = total - offset;
        if (chunk > sizeof(r->message))
            chunk = sizeof(r->message);
        for (size_t copied = 0; copied < chunk;)
        {
            if (used == lengths[part])
            {
                ++part;
                used = 0;
                continue;
            }
            size_t count = lengths[part] - used;
            if (count > chunk - copied)
                count = chunk - copied;
            memcpy(r->message + copied, parts[part] + used, count);
            copied += count;
            used += count;
        }
        const uint8_t *data = r->message;
        size_t n = chunk;
        int rc = RTAA_OK;
        if (encrypted)
        {
            rc = rtaa_tls_encrypt(r->tls, data, n);
            if (!rc)
                rc = rtaa_tls_output(r->tls, r->cipher, sizeof(r->cipher), &n);
            data = r->cipher;
        }
        uint8_t flags = (encrypted ? RTAA_ENCRYPTED : 0) | (control ? RTAA_CONTROL : 0);
        if (!offset)
            flags |= RTAA_FIRST;
        if (offset + chunk == total)
            flags |= RTAA_LAST;
        if (!rc)
            rc = raw_fragment(r, id, flags, (uint32_t)total, data, n);
        if (rc)
            return rc; /* Never emit the tail of a failed message. */
        offset += chunk;
    }
    return RTAA_OK;
}
static int send_body(rtaa_receiver *r, uint8_t id, uint16_t message, const uint8_t *body,
                     size_t size, bool encrypted, bool control)
{
    return send_parts(r, id, message, NULL, 0, body, size, encrypted, control);
}
static int send_pb(rtaa_receiver *r, uint8_t id, uint16_t message, const pb_msgdesc_t *fields,
                   const void *value, bool encrypted, bool control)
{
    uint8_t buffer[4096];
    pb_ostream_t out = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&out, fields, value))
        return RTAA_EBOUNDS;
    return send_body(r, id, message, buffer, out.bytes_written, encrypted, control);
}
static int status(rtaa_receiver *r, uint8_t id, uint16_t message, bool control)
{
    aap_Status value = {0};
    return send_pb(r, id, message, aap_Status_fields, &value, true, control);
}
static int handshake(rtaa_receiver *r)
{
    int hs = rtaa_tls_handshake(r->tls);
    if (hs < 0)
        return hs;
    size_t n;
    int rc = rtaa_tls_output(r->tls, r->cipher, sizeof(r->cipher) - 2, &n);
    if (!rc && n)
        rc = send_body(r, 0, 3, r->cipher, n, false, true);
    if (rc)
        return rc;
    if (hs == RTAA_OK)
    {
        aap_Status auth = {0};
        rc = send_pb(r, 0, 4, aap_Status_fields, &auth, false, true);
        if (!rc)
            transition(r, RTAA_DISCOVERY, 0);
    }
    return rc;
}
static void audio_format(rtaa_role role, uint32_t *rate, uint8_t *channels)
{
    *rate = role == RTAA_MEDIA ? 48000 : 16000;
    *channels = role == RTAA_MEDIA ? 2 : 1;
}
static void copy_string(char *out, const char *in)
{
    memcpy(out, in, strlen(in) + 1);
}
static int discovery(rtaa_receiver *r)
{
    aap_Discovery d = aap_Discovery_init_zero;
    d.has_head_unit_name = d.has_manufacturer = d.has_model = d.has_sw_version = true;
    copy_string(d.head_unit_name, r->c.name);
    copy_string(d.manufacturer, r->c.manufacturer);
    copy_string(d.model, r->c.model);
    copy_string(d.sw_version, "0.1.0-dev");
    d.has_driver_position = true;
    d.driver_position = 0;
    for (unsigned i = 0; i < RTAA_ROLE_COUNT; ++i)
    {
        if (!r->c.channels[i].id)
            continue;
        aap_Channel *c = &d.channels[d.channels_count++];
        c->channel_id = r->c.channels[i].id;
        if (i <= RTAA_SPEECH)
        {
            c->has_av = true;
            c->av.has_stream_type = true;
            c->av.stream_type = i == RTAA_VIDEO ? 3 : 1;
            if (i == RTAA_VIDEO)
            {
                c->av.video_configs_count = 1;
                aap_VideoConfig *v = &c->av.video_configs[0];
                v->has_video_resolution = v->has_video_fps = v->has_margin_width =
                    v->has_margin_height = v->has_dpi = true;
                v->video_resolution = 2;
                v->video_fps = 1;
                v->margin_width = r->c.geometry.margin_width;
                v->margin_height = r->c.geometry.margin_height;
                v->dpi = 160;
            }
            else
            {
                c->av.has_audio_type = true;
                c->av.audio_type = i == RTAA_MEDIA ? 3 : i == RTAA_SYSTEM ? 2 : 1;
                c->av.audio_configs_count = 1;
                uint32_t rate;
                uint8_t channels;
                audio_format((rtaa_role)i, &rate, &channels);
                c->av.audio_configs[0] = (aap_AudioConfig){rate, 16, channels};
            }
        }
        else if (i == RTAA_MIC)
        {
            c->has_av_input = true;
            c->av_input.has_stream_type = c->av_input.has_audio_config = true;
            c->av_input.stream_type = 1;
            c->av_input.audio_config = (aap_AudioConfig){16000, 16, 1};
        }
        else if (i == RTAA_INPUT)
        {
            c->has_input = true;
            c->input.has_touchscreen = true;
            c->input.touchscreen.width = r->c.geometry.frame_width;
            c->input.touchscreen.height = r->c.geometry.frame_height;
            c->input.keycodes_count = r->c.input_keycode_count;
            memcpy(c->input.keycodes, r->c.input_keycodes,
                   r->c.input_keycode_count * sizeof(uint32_t));
        }
        else if (i == RTAA_SENSOR)
        {
            c->has_sensor = true;
            c->sensor.sensors_count = 2;
            c->sensor.sensors[0].type = 10;
            c->sensor.sensors[1].type = 13;
        }
        else if (i == RTAA_MEDIA_INFO)
            c->has_media_info = true;
        else if (i == RTAA_PHONE)
            c->has_phone = true;
        else if (i == RTAA_NAVIGATION)
        {
            c->has_navigation = true;
            c->navigation.minimum_interval_ms =
                r->c.navigation_interval_ms ? r->c.navigation_interval_ms : 1000;
            c->navigation.type = 1;
        }
    }
    return send_pb(r, 0, 6, aap_Discovery_fields, &d, true, true);
}
static int role_by_id(rtaa_receiver *r, uint8_t id)
{
    if (!id)
        return -1;
    for (unsigned i = 0; i < RTAA_ROLE_COUNT; ++i)
        if (r->c.channels[i].id == id)
            return (int)i;
    return -1;
}
static int control_message(rtaa_receiver *r, uint16_t message, const uint8_t *p, size_t n)
{
    if (message == 13 || message == 17 || message == 23)
    {
        if (r->state != RTAA_CONNECTED)
            return RTAA_ESTATE;
        if (message == 13)
        {
            aap_NavigationFocus v = aap_NavigationFocus_init_zero;
            int rc = decode(aap_NavigationFocus_fields, &v, p, n);
            if (rc || !v.has_type || (v.type != 1 && v.type != 2))
                return RTAA_EPROTO;
            int owner =
                r->c.sink.navigation_focus ? r->c.sink.navigation_focus(r->c.sink.ctx, v.type) : 1;
            if (owner < 0)
                return owner;
            if (owner != 1 && owner != 2)
                return RTAA_EINVAL;
            v.type = (uint32_t)owner;
            return send_pb(r, 0, 14, aap_NavigationFocus_fields, &v, true, true);
        }
        if (message == 17)
        {
            aap_VoiceSession v = aap_VoiceSession_init_zero;
            int rc = decode(aap_VoiceSession_fields, &v, p, n);
            if (rc || !v.has_session_type || (v.session_type != 1 && v.session_type != 2))
                return RTAA_EPROTO;
            if (r->c.sink.voice_session)
                r->c.sink.voice_session(r->c.sink.ctx, v.session_type == 1);
            return RTAA_OK; /* Notification, no fabricated ACK. */
        }
        aap_BatteryStatus v = aap_BatteryStatus_init_zero;
        int rc = decode(aap_BatteryStatus_fields, &v, p, n);
        if (rc || (v.has_battery_level && v.battery_level > 100))
            return RTAA_EPROTO;
        rtaa_battery_status battery = {v.has_battery_level,    v.has_time_remaining_s,
                                       v.has_critical_battery, v.battery_level,
                                       v.time_remaining_s,     v.critical_battery};
        if (r->c.sink.battery_status)
            r->c.sink.battery_status(r->c.sink.ctx, &battery);
        return RTAA_OK;
    }
    if (message == 5)
    {
        if (r->state != RTAA_DISCOVERY)
            return RTAA_ESTATE;
        /* DiscoveryRequest is an extensible phone identity message. Nanopb
         * checks unknown fields' wire lengths rather than ignoring raw bytes. */
        aap_Empty empty = aap_Empty_init_zero;
        int rc = decode(aap_Empty_fields, &empty, p, n);
        if (!rc)
            rc = discovery(r);
        if (!rc)
            transition(r, RTAA_CONNECTED, 0);
        return rc;
    }
    if (message == 11 || message == 12)
    {
        aap_Ping v = aap_Ping_init_zero;
        int rc = decode(aap_Ping_fields, &v, p, n);
        if (rc)
            return rc;
        if (message == 11)
            return send_pb(r, 0, 12, aap_Ping_fields, &v, true, true);
        if (!r->ping_pending || (uint64_t)v.timestamp != r->ping_token)
            return RTAA_EPROTO;
        r->ping_pending = false;
        return RTAA_OK;
    }
    if (message == 7)
    {
        if (r->state != RTAA_CONNECTED)
            return RTAA_ESTATE;
        aap_ChannelOpen open = aap_ChannelOpen_init_zero;
        int rc = decode(aap_ChannelOpen_fields, &open, p, n);
        if (rc || open.channel_id <= 0 || open.channel_id > 255)
            return RTAA_EPROTO;
        int role = role_by_id(r, (uint8_t)open.channel_id);
        if (role < 0 || r->channels[role].open)
            return RTAA_EPROTO;
        rc = status(r, (uint8_t)open.channel_id, 8, true);
        if (!rc)
            r->channels[role].open = true;
        return rc;
    }
    if (message == 18)
    {
        aap_FocusRequest v = aap_FocusRequest_init_zero;
        int rc = decode(aap_FocusRequest_fields, &v, p, n);
        if (rc || !v.has_type || v.type < 1 || v.type > 4)
            return RTAA_EPROTO;
        if (!r->c.sink.audio_focus)
            return RTAA_ENOTSUP;
        int focus = r->c.sink.audio_focus(r->c.sink.ctx, v.type);
        if (focus < 0)
            return focus;
        if (focus < 1 || focus > 7)
            return RTAA_EINVAL;
        aap_FocusResponse out = {(uint32_t)focus};
        return send_pb(r, 0, 19, aap_FocusResponse_fields, &out, true, true);
    }
    if (message == 15)
    {
        aap_Shutdown v = aap_Shutdown_init_zero;
        int rc = decode(aap_Shutdown_fields, &v, p, n);
        if (!rc)
            rc = send_body(r, 0, 16, NULL, 0, true, true);
        if (!rc)
        {
            retire(r);
            transition(r, RTAA_CLOSED, 0);
        }
        return rc;
    }
    if (message == 16 && r->state == RTAA_CLOSING)
    {
        aap_Empty empty = aap_Empty_init_zero;
        if (decode(aap_Empty_fields, &empty, p, n))
            return RTAA_EPROTO;
        retire(r);
        transition(r, RTAA_CLOSED, 0);
        return RTAA_OK;
    }
    return RTAA_ENOTSUP;
}
static int av_message(rtaa_receiver *r, rtaa_role role, uint16_t message, const uint8_t *p,
                      size_t n)
{
    channel *c = &r->channels[role];
    uint8_t id = c->assembly.channel;
    if (message == 0x8000)
    {
        if (c->setup || c->streaming)
            return RTAA_ESTATE;
        aap_SetupRequest v = aap_SetupRequest_init_zero;
        int rc = decode(aap_SetupRequest_fields, &v, p, n);
        if (rc || v.codec != (role == RTAA_VIDEO ? 3u : 1u))
            return RTAA_ENOTSUP;
        aap_SetupResponse out = {2, true, 1, 1, {0}};
        rc = send_pb(r, id, 0x8003, aap_SetupResponse_fields, &out, true, false);
        if (!rc)
            c->setup = true;
        return rc;
    }
    if (message == 0x8001)
    {
        if (!c->setup || c->streaming)
            return RTAA_ESTATE;
        aap_Start v = aap_Start_init_zero;
        int rc = decode(aap_Start_fields, &v, p, n);
        if (rc || v.config != 0)
            return RTAA_EPROTO;
        uint32_t rate;
        uint8_t channels;
        audio_format(role, &rate, &channels);
        rc = r->c.sink.start(r->c.sink.ctx, role, role == RTAA_VIDEO ? 0 : rate,
                             role == RTAA_VIDEO ? 0 : channels, role == RTAA_VIDEO ? 0 : 16);
        if (!rc)
        {
            c->streaming = true;
            c->session = v.session;
        }
        return rc;
    }
    if (message == 0x8002)
    {
        if (!c->streaming)
            return RTAA_ESTATE;
        aap_Empty empty = aap_Empty_init_zero;
        if (decode(aap_Empty_fields, &empty, p, n))
            return RTAA_EPROTO;
        if (r->c.sink.stop)
            r->c.sink.stop(r->c.sink.ctx, role);
        c->streaming = false;
        return RTAA_OK;
    }
    if (message == 0x8007 && role == RTAA_VIDEO)
    {
        aap_VideoFocusRequest v = aap_VideoFocusRequest_init_zero;
        int rc = decode(aap_VideoFocusRequest_fields, &v, p, n);
        if (rc || !v.has_mode || (v.mode != 1 && v.mode != 2))
            return RTAA_EPROTO;
        if (!r->c.sink.video_focus)
            return RTAA_ENOTSUP;
        rc = r->c.sink.video_focus(r->c.sink.ctx, v.mode == 1);
        if (rc)
            return rc;
        aap_VideoFocus out = {v.mode, true, false};
        return send_pb(r, id, 0x8008, aap_VideoFocus_fields, &out, true, false);
    }
    if (message == 0 || message == 1)
    {
        if (!c->streaming)
            return RTAA_ESTATE;
        uint64_t timestamp = 0;
        if (!message)
        {
            if (n < 8)
                return RTAA_EPROTO;
            for (unsigned i = 0; i < 8; ++i)
                timestamp = (timestamp << 8) | p[i];
            p += 8;
            n -= 8;
        }
        if (!n || (role != RTAA_VIDEO && n % (role == RTAA_MEDIA ? 4u : 2u)))
            return RTAA_EPROTO;
        int rc = r->c.sink.data(r->c.sink.ctx, role, p, n, timestamp);
        if (rc)
            return rc;
        if (role == RTAA_VIDEO)
            r->stats.video_bytes += n;
        else
            r->stats.audio_bytes += n;
        aap_Ack ack = aap_Ack_init_zero;
        ack.session_id = c->session;
        ack.has_ack_count = true;
        ack.ack_count = 1;
        rc = send_pb(r, id, 0x8004, aap_Ack_fields, &ack, true, false);
        if (!rc)
            ++r->stats.acks;
        return rc;
    }
    return RTAA_ENOTSUP;
}
static int service_message(rtaa_receiver *r, rtaa_role role, uint16_t message, const uint8_t *p,
                           size_t n)
{
    channel *c = &r->channels[role];
    uint8_t id = c->assembly.channel;
    if (!c->open || r->state != RTAA_CONNECTED)
        return RTAA_ESTATE;
    if (role <= RTAA_SPEECH)
        return av_message(r, role, message, p, n);
    if (role == RTAA_NAVIGATION || role == RTAA_PHONE)
        return rtaa_status_message(&r->c.sink, role, message, p, n);
    if (role == RTAA_MEDIA_INFO && message == 0x8001)
    {
        aap_MediaStatus v = aap_MediaStatus_init_zero;
        int rc = decode(aap_MediaStatus_fields, &v, p, n);
        if (rc || (v.has_state && v.state > 4))
            return RTAA_EPROTO;
        rtaa_media_status out = {0};
        out.has_state = v.has_state;
        out.has_source_app = v.has_source_app;
        out.has_position_seconds = v.has_position_seconds;
        out.has_shuffle = v.has_shuffle;
        out.has_repeat = v.has_repeat;
        out.has_repeat_one = v.has_repeat_one;
        out.state = v.state;
        copy_string(out.source_app, v.source_app);
        out.position_seconds = v.position_seconds;
        out.shuffle = v.shuffle;
        out.repeat = v.repeat;
        out.repeat_one = v.repeat_one;
        return r->c.sink.media_status(r->c.sink.ctx, &out);
    }
    if (role == RTAA_MEDIA_INFO && message == 0x8003)
    {
        aap_MediaMetadata v = aap_MediaMetadata_init_zero;
        int rc = decode(aap_MediaMetadata_fields, &v, p, n);
        if (rc)
            return rc;
        rtaa_media_metadata out = {0};
        out.has_title = v.has_title;
        out.has_artist = v.has_artist;
        out.has_album = v.has_album;
        copy_string(out.title, v.title);
        copy_string(out.artist, v.artist);
        copy_string(out.album, v.album);
        return r->c.sink.media_metadata(r->c.sink.ctx, &out);
    }
    if (role == RTAA_INPUT && message == 0x8002)
    {
        if (c->bound)
            return RTAA_ESTATE;
        aap_Binding v = aap_Binding_init_zero;
        int rc = decode(aap_Binding_fields, &v, p, n);
        uint32_t keys = 0;
        if (rc)
            return rc;
        for (pb_size_t i = 0; i < v.keycodes_count; ++i)
        {
            uint32_t mask = key_mask(r, v.keycodes[i]);
            if (!mask || (keys & mask))
                return RTAA_EPROTO;
            keys |= mask;
        }
        if (!rc)
            rc = status(r, id, 0x8003, false);
        if (!rc)
        {
            c->bound = true;
            r->bound_keys = keys;
        }
        return rc;
    }
    if (role == RTAA_SENSOR && message == 0x8001)
    {
        aap_SensorStart v = aap_SensorStart_init_zero;
        int rc = decode(aap_SensorStart_fields, &v, p, n);
        if (rc || v.refresh_interval < 0 || (v.type != 10 && v.type != 13))
            return RTAA_EPROTO;
        if (r->c.sink.sensor_start)
            rc = r->c.sink.sensor_start(r->c.sink.ctx, v.type, v.refresh_interval);
        if (rc)
            return rc;
        /* Record ownership before ACK: a write failure must stop a scheduler
         * which was already started, even on its first subscription. */
        if (v.type == 10)
            r->night_subscribed = true;
        else
            r->driving_subscribed = true;
        return status(r, id, 0x8002, false);
    }
    if (role == RTAA_MIC && message == 0x8005)
    {
        aap_MicOpen v = aap_MicOpen_init_zero;
        int rc = decode(aap_MicOpen_fields, &v, p, n);
        if (rc || !v.has_open ||
            (v.open && (!v.has_max_unacked || v.max_unacked < 1 || v.max_unacked > 64)))
            return RTAA_EPROTO;
        if (!r->c.sink.microphone)
            return RTAA_ENOTSUP;
        if (v.open == r->mic_open)
            return RTAA_ESTATE;
        if (v.open && c->session == INT32_MAX)
            return RTAA_EBOUNDS;
        rc = r->c.sink.microphone(r->c.sink.ctx, v.open, 16000, v.ec, v.anc);
        if (rc)
            return rc;
        r->mic_open = v.open;
        c->window = (uint32_t)v.max_unacked;
        c->outstanding = 0;
        if (v.open)
            ++c->session;
        aap_MicResponse out = {true, c->session, true, 0};
        return send_pb(r, id, 0x8006, aap_MicResponse_fields, &out, true, false);
    }
    if (role == RTAA_MIC && message == 0x8004)
    {
        aap_Ack ack = aap_Ack_init_zero;
        int rc = decode(aap_Ack_fields, &ack, p, n);
        uint32_t count =
            ack.receive_timestamps_count ? ack.receive_timestamps_count : ack.ack_count;
        if (rc || ack.session_id != c->session || !count || count > c->outstanding)
            return RTAA_EPROTO;
        c->outstanding -= count;
        return RTAA_OK;
    }
    return RTAA_ENOTSUP;
}
static int on_frame(void *ctx, const rtaa_frame *frame)
{
    rtaa_receiver *r = ctx;
    rtaa_frame f = *frame;
    bool encrypted = (f.flags & RTAA_ENCRYPTED) != 0;
    if (r->state == RTAA_FAILED || r->state == RTAA_CLOSED)
        return RTAA_ESTATE;
    if (encrypted)
    {
        if (!rtaa_tls_ready(r->tls))
            return RTAA_ESECURITY;
        int rc = rtaa_tls_ingest(r->tls, f.data, f.size);
        size_t n = 0;
        do
        {
            size_t count = 0;
            if (!rc && n < sizeof(r->plain))
                rc = rtaa_tls_decrypt(r->tls, r->plain + n, sizeof(r->plain) - n, &count);
            if (rc || !count)
                return RTAA_ESECURITY;
            n += count;
            if (n == sizeof(r->plain) && rtaa_tls_pending(r->tls))
                return RTAA_EBOUNDS;
        } while (rtaa_tls_pending(r->tls));
        /* Multiple complete TLS records may carry one AAP fragment. Never
         * associate a delayed partial record with a different channel. */
        f.data = r->plain;
        f.size = n;
    }
    else if (f.channel || r->state >= RTAA_DISCOVERY)
        return RTAA_ESECURITY;
    int role = role_by_id(r, f.channel);
    if (f.channel && role < 0)
        return RTAA_EPROTO;
    rtaa_assembly *a = f.channel ? &r->channels[role].assembly : &r->control;
    bool complete;
    int rc = rtaa_assembly_push(a, &f, &complete);
    if (rc || !complete)
        return rc;
    ++r->stats.messages;
    uint16_t message = (uint16_t)(((uint16_t)a->data[0] << 8) | a->data[1]);
    const uint8_t *p = a->data + 2;
    size_t n = a->used - 2;
    r->last_rx = now(r);
    if (!encrypted)
    {
        if (r->state == RTAA_VERSION && !f.channel && message == 2)
        {
            unsigned requested_minor = r->c.protocol_minor ? r->c.protocol_minor : 1;
            if (n != 6 || p[0] || p[1] != 1 || p[2] || p[3] < 1 || p[3] > requested_minor || p[4] ||
                p[5])
                return RTAA_EPROTO;
            r->protocol_minor = p[3];
            transition(r, RTAA_TLS, 0);
            return handshake(r);
        }
        if (r->state == RTAA_TLS && !f.channel && message == 3)
        {
            rc = rtaa_tls_ingest(r->tls, p, n);
            return rc ? rc : handshake(r);
        }
        return RTAA_ESTATE;
    }
    /* Control-flag messages (ChannelOpenRequest) can be carried on the target
     * channel, as in aasdk. Validate the requested ID against that envelope. */
    if (f.channel && message == 7)
    {
        aap_ChannelOpen v = aap_ChannelOpen_init_zero;
        rc = decode(aap_ChannelOpen_fields, &v, p, n);
        if (rc || v.channel_id != f.channel)
            return RTAA_EPROTO;
        return control_message(r, message, p, n);
    }
    if (message == 9)
    {
        /* Legacy transport control close has an empty protobuf body; the
         * target is its envelope channel, not an invented body field. */
        if (!f.channel || !(f.flags & RTAA_CONTROL) || r->state != RTAA_CONNECTED ||
            !r->channels[role].open)
            return RTAA_ESTATE;
        aap_Empty empty = aap_Empty_init_zero;
        rc = decode(aap_Empty_fields, &empty, p, n);
        if (!rc)
            retire_channel(r, (rtaa_role)role);
        return rc;
    }
    return f.channel ? service_message(r, (rtaa_role)role, message, p, n)
                     : control_message(r, message, p, n);
}
static bool valid_name(const char *p)
{
    if (!p)
        return false;
    for (size_t i = 0; i < 96; ++i)
        if (!p[i])
            return i != 0;
    return false;
}
int rtaa_receiver_create(const rtaa_receiver_config *config, rtaa_receiver **out)
{
    if (!out)
        return RTAA_EINVAL;
    *out = NULL;
    if (!config || !config->memory.allocate || !config->memory.release ||
        !config->transport.write || !config->transport.now_ms || !config->io_timeout_ms ||
        !config->session_timeout_ms || !config->heartbeat_ms ||
        config->heartbeat_ms >= config->session_timeout_ms ||
        rtaa_geometry_validate(&config->geometry) || config->geometry.frame_width != 1280 ||
        config->geometry.frame_height != 720 || config->protocol_minor > 7 ||
        config->navigation_interval_ms < 0 || !valid_name(config->name) ||
        (!!config->sink.sensor_start != !!config->sink.sensor_stop) ||
        !valid_name(config->manufacturer) || !valid_name(config->model))
        return RTAA_EINVAL;
    size_t total = sizeof(rtaa_receiver) + rtaa_tls_storage_size();
    if (config->input_keycode_count > 32)
        return RTAA_EBOUNDS;
    for (unsigned i = 0; i < config->input_keycode_count; ++i)
    {
        if (!config->input_keycodes[i] || config->input_keycodes[i] > INT32_MAX)
            return RTAA_EINVAL;
        for (unsigned j = 0; j < i; ++j)
            if (config->input_keycodes[i] == config->input_keycodes[j])
                return RTAA_EINVAL;
    }
    bool any = false;
    for (unsigned i = 0; i < RTAA_ROLE_COUNT; ++i)
    {
        const rtaa_channel_config *c = &config->channels[i];
        if (!c->id)
            continue;
        any = true;
        if (c->message_capacity < 256 || c->message_capacity > 4u * 1024u * 1024u ||
            c->message_capacity > SIZE_MAX - total)
            return RTAA_EBOUNDS;
        total += c->message_capacity;
        for (unsigned j = 0; j < i; ++j)
            if (config->channels[j].id == c->id)
                return RTAA_EINVAL;
        if (i <= RTAA_SPEECH && (!config->sink.start || !config->sink.data || !config->sink.stop))
            return RTAA_EINVAL;
        if (i == RTAA_MIC && !config->sink.microphone)
            return RTAA_EINVAL;
        if (i == RTAA_MEDIA_INFO && (!config->sink.media_status || !config->sink.media_metadata ||
                                     !config->sink.media_info_reset))
            return RTAA_EINVAL;
        if (i == RTAA_PHONE && (!config->sink.phone_status || !config->sink.phone_reset))
            return RTAA_EINVAL;
        if (i == RTAA_NAVIGATION &&
            (!config->sink.navigation_state || !config->sink.navigation_notification ||
             !config->sink.navigation_position || !config->sink.navigation_turn ||
             !config->sink.navigation_reset))
            return RTAA_EINVAL;
    }
    if (!any || total > config->memory_limit)
        return RTAA_EBOUNDS;
    rtaa_receiver *r = config->memory.allocate(config->memory.ctx, sizeof(*r));
    if (!r)
        return RTAA_EBOUNDS;
    memset(r, 0, sizeof(*r));
    r->c = *config;
    if (!r->c.input_keycode_count)
    {
        r->c.input_keycode_count = sizeof(input_keys) / sizeof(input_keys[0]);
        memcpy(r->c.input_keycodes, input_keys, sizeof(input_keys));
    }
    r->control = (rtaa_assembly){0, 0, r->control_data, sizeof(r->control_data), 0, 0, false};
    for (unsigned i = 0; i < RTAA_ROLE_COUNT; ++i)
    {
        channel *c = &r->channels[i];
        c->assembly.channel = config->channels[i].id;
        if (!c->assembly.channel)
            continue;
        c->assembly.capacity = config->channels[i].message_capacity;
        c->assembly.data = config->memory.allocate(config->memory.ctx, c->assembly.capacity);
        if (!c->assembly.data)
        {
            rtaa_receiver_destroy(r);
            return RTAA_EBOUNDS;
        }
    }
    int rc = rtaa_tls_create(&config->tls, &r->tls);
    if (rc)
    {
        rtaa_receiver_destroy(r);
        return rc;
    }
    *out = r;
    return RTAA_OK;
}
void rtaa_receiver_destroy(rtaa_receiver *r)
{
    if (!r)
        return;
    if (r->state != RTAA_IDLE)
        retire(r);
    rtaa_tls_destroy(r->tls);
    for (unsigned i = 0; i < RTAA_ROLE_COUNT; ++i)
    {
        if (r->channels[i].assembly.data)
            r->c.memory.release(r->c.memory.ctx, r->channels[i].assembly.data);
    }
    rtaa_allocator m = r->c.memory;
    memset(r, 0, sizeof(*r));
    m.release(m.ctx, r);
}
int rtaa_receiver_start(rtaa_receiver *r)
{
    if (!r || r->state != RTAA_IDLE)
        return RTAA_ESTATE;
    rtaa_parser_reset(&r->parser);
    r->last_rx = now(r);
    transition(r, RTAA_VERSION, 0);
    uint8_t version[] = {0, 1, 0, r->c.protocol_minor ? (uint8_t)r->c.protocol_minor : 1};
    int rc = send_body(r, 0, 1, version, sizeof(version), false, false);
    return rc ? fail(r, rc) : RTAA_OK;
}
int rtaa_receiver_feed(rtaa_receiver *r, const uint8_t *p, size_t n)
{
    if (!r || r->state == RTAA_IDLE || r->state == RTAA_CLOSED || r->state == RTAA_FAILED)
        return RTAA_ESTATE;
    int rc = rtaa_parser_feed(&r->parser, p, n, on_frame, r);
    return rc ? fail(r, rc) : RTAA_OK;
}
int rtaa_receiver_poll(rtaa_receiver *r)
{
    if (!r || r->state == RTAA_IDLE || r->state == RTAA_CLOSED || r->state == RTAA_FAILED)
        return RTAA_ESTATE;
    uint64_t t = now(r);
    if ((r->state != RTAA_CONNECTED && t - r->stage_time >= r->c.session_timeout_ms) ||
        (r->state == RTAA_CONNECTED && t - r->last_rx >= r->c.session_timeout_ms) ||
        (r->ping_pending && t - r->ping_time >= r->c.session_timeout_ms))
        return fail(r, RTAA_ETIMEOUT);
    if (r->state == RTAA_CONNECTED && !r->ping_pending && t - r->ping_time >= r->c.heartbeat_ms)
    {
        aap_Ping ping = {(int64_t)t};
        r->ping_token = t;
        int rc = send_pb(r, 0, 11, aap_Ping_fields, &ping, true, true);
        if (rc)
            return fail(r, rc);
        r->ping_pending = true;
        r->ping_time = t;
    }
    if (!r->c.transport.read)
        return RTAA_OK;
    uint8_t buffer[2048];
    int n = r->c.transport.read(r->c.transport.ctx, buffer, sizeof(buffer), r->c.io_timeout_ms);
    if (n < 0)
        return fail(r, n);
    if ((size_t)n > sizeof(buffer))
        return fail(r, RTAA_EIO);
    return n ? rtaa_receiver_feed(r, buffer, (size_t)n) : RTAA_AGAIN;
}
static bool ready(rtaa_receiver *r, rtaa_role role)
{
    return r && r->state == RTAA_CONNECTED && r->c.channels[role].id && r->channels[role].open;
}
static int touch_event(rtaa_receiver *r, uint8_t action, uint16_t id, uint16_t x, uint16_t y,
                       uint64_t us)
{
    aap_InputEvent v = aap_InputEvent_init_zero;
    v.timestamp = us;
    v.has_touch = true;
    v.touch.locations_count = 1;
    v.touch.locations[0] = (aap_TouchLocation){x, y, id};
    v.touch.has_action = v.touch.has_action_index = true;
    v.touch.action = action;
    int rc =
        send_pb(r, r->c.channels[RTAA_INPUT].id, 0x8001, aap_InputEvent_fields, &v, true, false);
    if (rc)
        return fail(r, rc);
    r->touching = action != 1;
    r->touch_id = id;
    r->touch_x = x;
    r->touch_y = y;
    return RTAA_OK;
}
int rtaa_receiver_touch(rtaa_receiver *r, uint8_t action, uint16_t id, uint16_t x, uint16_t y,
                        uint64_t us)
{
    if (!ready(r, RTAA_INPUT) || !r->channels[RTAA_INPUT].bound)
        return RTAA_ESTATE;
    if (action > 2)
        return RTAA_EINVAL;
    if ((!action && r->touching) || (action && (!r->touching || r->touch_id != id)))
        return RTAA_ESTATE;
    uint16_t ox, oy;
    int rc = rtaa_touch_map(&r->c.geometry, x, y, &ox, &oy);
    if (rc == RTAA_EBOUNDS && action == 1)
        return touch_event(r, 1, id, r->touch_x, r->touch_y, us);
    return rc ? rc : touch_event(r, action, id, ox, oy, us);
}
int rtaa_receiver_touch_cancel(rtaa_receiver *r, uint64_t us)
{
    if (!ready(r, RTAA_INPUT) || !r->channels[RTAA_INPUT].bound)
        return RTAA_ESTATE;
    return r->touching ? touch_event(r, 1, r->touch_id, r->touch_x, r->touch_y, us) : RTAA_OK;
}
int rtaa_receiver_key(rtaa_receiver *r, uint32_t key, bool pressed, uint64_t us)
{
    if (!ready(r, RTAA_INPUT) || !r->channels[RTAA_INPUT].bound)
        return RTAA_ESTATE;
    uint32_t mask = key_mask(r, key);
    if (!(r->bound_keys & mask))
        return RTAA_ENOTSUP;
    if (key == 65536)
        return RTAA_EINVAL; /* Rotary is a relative axis, not a button. */
    if (((r->pressed_keys & mask) != 0) == pressed)
        return RTAA_ESTATE;
    aap_InputEvent v = aap_InputEvent_init_zero;
    v.timestamp = us;
    v.has_button = true;
    v.button.buttons_count = 1;
    v.button.buttons[0].keycode = key;
    v.button.buttons[0].pressed = pressed;
    int rc =
        send_pb(r, r->c.channels[RTAA_INPUT].id, 0x8001, aap_InputEvent_fields, &v, true, false);
    if (rc)
        return fail(r, rc);
    if (pressed)
        r->pressed_keys |= mask;
    else
        r->pressed_keys &= ~mask;
    return RTAA_OK;
}
int rtaa_receiver_rotary(rtaa_receiver *r, int32_t delta, uint64_t us)
{
    if (!ready(r, RTAA_INPUT) || !r->channels[RTAA_INPUT].bound)
        return RTAA_ESTATE;
    if (!(r->bound_keys & key_mask(r, 65536)))
        return RTAA_ENOTSUP;
    if (!delta)
        return RTAA_EINVAL;
    aap_InputEvent v = aap_InputEvent_init_zero;
    v.timestamp = us;
    v.has_relative = true;
    v.relative.events_count = 1;
    v.relative.events[0] = (aap_RelativeInput){65536, delta};
    int rc =
        send_pb(r, r->c.channels[RTAA_INPUT].id, 0x8001, aap_InputEvent_fields, &v, true, false);
    return rc ? fail(r, rc) : RTAA_OK;
}
int rtaa_receiver_night(rtaa_receiver *r, bool night)
{
    if (!ready(r, RTAA_SENSOR) || !r->night_subscribed)
        return RTAA_ESTATE;
    aap_SensorEvent v = aap_SensorEvent_init_zero;
    v.night_count = 1;
    v.night[0].night = night;
    int rc =
        send_pb(r, r->c.channels[RTAA_SENSOR].id, 0x8003, aap_SensorEvent_fields, &v, true, false);
    return rc ? fail(r, rc) : RTAA_OK;
}
int rtaa_receiver_driving(rtaa_receiver *r, uint32_t restrictions)
{
    if (!ready(r, RTAA_SENSOR) || !r->driving_subscribed)
        return RTAA_ESTATE;
    aap_SensorEvent v = aap_SensorEvent_init_zero;
    v.driving_count = 1;
    v.driving[0].status = restrictions;
    int rc =
        send_pb(r, r->c.channels[RTAA_SENSOR].id, 0x8003, aap_SensorEvent_fields, &v, true, false);
    return rc ? fail(r, rc) : RTAA_OK;
}
int rtaa_receiver_microphone(rtaa_receiver *r, const uint8_t *pcm, size_t n, uint64_t us)
{
    if (!ready(r, RTAA_MIC) || !r->mic_open)
        return RTAA_ESTATE;
    channel *c = &r->channels[RTAA_MIC];
    if (!pcm || !n || n % 2 || n > 65518)
        return RTAA_EINVAL;
    if (c->outstanding >= c->window)
        return RTAA_AGAIN;
    uint8_t buffer[8];
    for (unsigned i = 0; i < 8; ++i)
        buffer[i] = (uint8_t)(us >> (56 - 8 * i));
    int rc = send_parts(r, c->assembly.channel, 0, buffer, sizeof(buffer), pcm, n, true, false);
    if (rc)
        return fail(r, rc);
    ++c->outstanding;
    r->stats.microphone_bytes += n;
    return RTAA_OK;
}
int rtaa_receiver_shutdown(rtaa_receiver *r)
{
    if (!r || r->state != RTAA_CONNECTED)
        return RTAA_ESTATE;
    aap_Shutdown v = {1};
    int rc = send_pb(r, 0, 15, aap_Shutdown_fields, &v, true, true);
    if (rc)
        return fail(r, rc);
    transition(r, RTAA_CLOSING, 0);
    return RTAA_OK;
}
rtaa_state rtaa_receiver_state(const rtaa_receiver *r)
{
    return r ? r->state : RTAA_FAILED;
}
uint16_t rtaa_receiver_protocol_minor(const rtaa_receiver *r)
{
    return r ? r->protocol_minor : 0;
}
int rtaa_receiver_call_available(rtaa_receiver *r, bool available)
{
    if (!r || r->state != RTAA_CONNECTED)
        return RTAA_ESTATE;
    aap_CallAvailability v = {true, available};
    int rc = send_pb(r, 0, 24, aap_CallAvailability_fields, &v, true, true);
    return rc ? fail(r, rc) : RTAA_OK;
}
int rtaa_receiver_close_channel(rtaa_receiver *r, rtaa_role role)
{
    if ((unsigned)role >= RTAA_ROLE_COUNT)
        return RTAA_EINVAL;
    if (!ready(r, role))
        return RTAA_ESTATE;
    int rc = send_body(r, r->c.channels[role].id, 9, NULL, 0, true, true);
    if (rc)
        return fail(r, rc);
    retire_channel(r, role);
    return RTAA_OK;
}
int rtaa_receiver_navigation(rtaa_receiver *r, bool enabled)
{
    if (!ready(r, RTAA_NAVIGATION))
        return RTAA_ESTATE;
    if (r->navigation_enabled == enabled)
        return RTAA_OK;
    aap_Empty v = aap_Empty_init_zero;
    int rc = send_pb(r, r->c.channels[RTAA_NAVIGATION].id, enabled ? 0x8001 : 0x8002,
                     aap_Empty_fields, &v, true, false);
    if (rc)
        return fail(r, rc);
    r->navigation_enabled = enabled;
    return RTAA_OK;
}
int rtaa_receiver_phone_input(rtaa_receiver *r, uint32_t action, const char *caller_id,
                              const char *display_name)
{
    if (!ready(r, RTAA_PHONE))
        return RTAA_ESTATE;
    if (action > 7)
        return RTAA_EINVAL;
    aap_PhoneInput v = aap_PhoneInput_init_zero;
    if ((caller_id && strlen(caller_id) >= sizeof(v.caller_id)) ||
        (display_name && strlen(display_name) >= sizeof(v.display_name)))
        return RTAA_EBOUNDS;
    v.input_type.action = action;
    if ((v.has_caller_id = caller_id != NULL))
        copy_string(v.caller_id, caller_id);
    if ((v.has_display_name = display_name != NULL))
        copy_string(v.display_name, display_name);
    int rc =
        send_pb(r, r->c.channels[RTAA_PHONE].id, 0x8002, aap_PhoneInput_fields, &v, true, false);
    return rc ? fail(r, rc) : RTAA_OK;
}
int rtaa_receiver_error(const rtaa_receiver *r)
{
    return r ? r->error : RTAA_EINVAL;
}
void rtaa_receiver_statistics(const rtaa_receiver *r, rtaa_statistics *out)
{
    if (r && out)
        *out = r->stats;
}
