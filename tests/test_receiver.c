/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/receiver.h"
#include "rtaa/connection.h"
#include "peer.h"
#include "aap.pb.h"
#include <pb_encode.h>
#include <pb_decode.h>
#include <stdio.h>
typedef struct
{
    peer server;
    rtaa_parser parser;
    uint8_t writes[131072], body[65536], assembled[65538];
    rtaa_assembly assembly;
    size_t writes_size, body_size;
    uint8_t channel;
    uint16_t message;
    uint64_t time;
    unsigned starts, stops, media, cancelled, closed, mic_changes;
    bool reject;
    int write_error;
    size_t fail_after_bytes;
    unsigned fragments, navigation_requests, voice_changes, batteries;
    int navigation_owner;
    bool voice_active;
    rtaa_battery_status battery;
    unsigned sensor_starts, sensor_stops;
    uint32_t sensor_type;
    int64_t sensor_interval;
    bool info_enabled;
    unsigned media_statuses, metadata_updates, info_resets;
    rtaa_media_status media_status;
    rtaa_media_metadata metadata;
    bool status_services;
    unsigned phone_updates, phone_resets, nav_states, nav_notifications, nav_positions, nav_turns,
        nav_resets;
    size_t calls_count, nav_steps, nav_destinations;
    uint32_t call_state, nav_state, lane_shape;
    bool state_present, signal_present, instruction_present, distance_present, eta_present;
    int32_t distance_value;
    int64_t eta_seconds;
    char status_text[128];
} fixture;
static uint64_t milliseconds(void *ctx)
{
    return ((fixture *)ctx)->time;
}
static int write_transport(void *ctx, const uint8_t *p, size_t n, uint32_t timeout)
{
    fixture *f = ctx;
    assert(timeout && n);
    if (f->write_error)
        return f->write_error;
    if (f->fail_after_bytes && f->writes_size >= f->fail_after_bytes)
        return RTAA_EIO;
    if (n > 17)
        n = 17; /* force partial USB writes */
    if (f->fail_after_bytes && n > f->fail_after_bytes - f->writes_size)
        n = f->fail_after_bytes - f->writes_size;
    assert(n <= sizeof(f->writes) - f->writes_size);
    memcpy(f->writes + f->writes_size, p, n);
    f->writes_size += n;
    return (int)n;
}
static void cancel_transport(void *ctx)
{
    ++((fixture *)ctx)->cancelled;
}
static void close_transport(void *ctx)
{
    ++((fixture *)ctx)->closed;
}
static int start_sink(void *ctx, rtaa_role role, uint32_t rate, uint8_t channels, uint8_t bits)
{
    fixture *f = ctx;
    ++f->starts;
    if (role != RTAA_VIDEO)
        assert(rate && channels && bits == 16);
    return 0;
}
static int data_sink(void *ctx, rtaa_role role, const uint8_t *p, size_t n, uint64_t us)
{
    fixture *f = ctx;
    (void)role;
    assert(p && n == 4 && us == 123);
    ++f->media;
    return f->reject ? RTAA_EIO : 0;
}
static void stop_sink(void *ctx, rtaa_role role)
{
    (void)role;
    ++((fixture *)ctx)->stops;
}
static int audio_focus(void *ctx, unsigned type)
{
    (void)ctx;
    return type == 4 ? 3 : 1;
}
static int video_focus(void *ctx, bool focused)
{
    (void)ctx;
    assert(focused);
    return 0;
}
static int mic_sink(void *ctx, bool open, uint32_t rate, bool ec, bool anc)
{
    (void)open;
    (void)ec;
    (void)anc;
    assert(rate == 16000);
    ++((fixture *)ctx)->mic_changes;
    return 0;
}
static int navigation_focus(void *ctx, unsigned requested)
{
    fixture *f = ctx;
    assert(requested == 1 || requested == 2);
    ++f->navigation_requests;
    return f->navigation_owner;
}
static void voice_session(void *ctx, bool started)
{
    fixture *f = ctx;
    ++f->voice_changes;
    f->voice_active = started;
}
static void battery_status(void *ctx, const rtaa_battery_status *status)
{
    fixture *f = ctx;
    ++f->batteries;
    f->battery = *status;
}
static int sensor_start(void *ctx, uint32_t type, int64_t interval)
{
    fixture *f = ctx;
    ++f->sensor_starts;
    f->sensor_type = type;
    f->sensor_interval = interval;
    return 0;
}
static void sensor_stop(void *ctx, uint32_t type)
{
    fixture *f = ctx;
    assert(type == 10 || type == 13);
    ++f->sensor_stops;
}
static int media_status(void *ctx, const rtaa_media_status *v)
{
    fixture *f = ctx;
    ++f->media_statuses;
    f->media_status = *v;
    return f->reject ? RTAA_EIO : 0;
}
static int media_metadata(void *ctx, const rtaa_media_metadata *v)
{
    fixture *f = ctx;
    ++f->metadata_updates;
    f->metadata = *v;
    return f->reject ? RTAA_EIO : 0;
}
static void media_info_reset(void *ctx)
{
    fixture *f = ctx;
    ++f->info_resets;
    memset(&f->media_status, 0, sizeof(f->media_status));
    memset(&f->metadata, 0, sizeof(f->metadata));
}
static int phone_status(void *ctx, const rtaa_phone_status *v)
{
    fixture *f = ctx;
    ++f->phone_updates;
    f->calls_count = v->calls_count;
    f->signal_present = v->has_signal_strength;
    if (v->calls_count)
    {
        f->call_state = v->calls[0].state;
        assert(v->calls[0].number && !strcmp(v->calls[0].number, "+123"));
        assert(v->calls[0].display_name && !v->calls[0].display_name[0]);
        assert(!v->calls[0].contact_id && v->calls[0].duration_seconds == 0);
    }
    return f->reject ? RTAA_EIO : 0;
}
static void phone_reset(void *ctx)
{
    fixture *f = ctx;
    ++f->phone_resets;
    f->calls_count = 0;
}
static int nav_state(void *ctx, const rtaa_navigation_state *v)
{
    fixture *f = ctx;
    ++f->nav_states;
    f->state_present = v->has_state;
    f->nav_state = v->state;
    return f->reject ? RTAA_EIO : 0;
}
static int nav_notification(void *ctx, const rtaa_navigation_notification *v)
{
    fixture *f = ctx;
    ++f->nav_notifications;
    f->nav_steps = v->steps_count;
    f->nav_destinations = v->destinations_count;
    if (v->steps_count)
    {
        const rtaa_nav_step *s = &v->steps[0];
        assert(s->has_maneuver && s->maneuver.has_type && s->maneuver.type == 7);
        assert(s->maneuver.has_exit_number && !s->maneuver.exit_number);
        assert(s->maneuver.has_turn_angle && s->maneuver.turn_angle == -90);
        f->instruction_present = s->has_instruction;
        assert(s->instruction);
        strcpy(f->status_text, s->instruction);
        assert(s->lanes_count == 1 && s->lanes[0].directions_count == 2);
        f->lane_shape = s->lanes[0].directions[0].shape;
        assert(s->lanes[0].directions[0].has_recommended && !s->lanes[0].directions[0].recommended);
        assert(!s->lanes[0].directions[1].has_shape);
        assert(s->has_road_info && s->road_names_count == 1 && !strcmp(s->road_names[0], "Road"));
    }
    if (v->destinations_count)
    {
        const rtaa_nav_destination *d = &v->destinations[0];
        assert(d->address && !d->address[0] && d->has_charging_station);
        assert(d->has_connector_count && d->connector_count == 2);
        assert(d->has_available_count && !d->available_count);
        assert(d->has_max_power_kw && d->max_power_kw == 50);
    }
    return f->reject ? RTAA_EIO : 0;
}
static int nav_position(void *ctx, const rtaa_navigation_position *v)
{
    fixture *f = ctx;
    ++f->nav_positions;
    f->distance_present = v->has_step_distance && v->has_distance;
    f->distance_value = v->distance.value;
    if (v->distance.display_text)
        strcpy(f->status_text, v->distance.display_text);
    if (v->destinations_count)
    {
        f->eta_present = v->destinations[0].has_time_to_arrival;
        f->eta_seconds = v->destinations[0].time_to_arrival_seconds;
        assert(v->destinations[0].estimated_time_of_arrival &&
               !strcmp(v->destinations[0].estimated_time_of_arrival, "12:30"));
    }
    return f->reject ? RTAA_EIO : 0;
}
static int nav_turn(void *ctx, const rtaa_navigation_turn *v)
{
    fixture *f = ctx;
    ++f->nav_turns;
    if (v->road_name)
        strcpy(f->status_text, v->road_name);
    return f->reject ? RTAA_EIO : 0;
}
static void nav_reset(void *ctx)
{
    fixture *f = ctx;
    ++f->nav_resets;
    f->status_text[0] = 0;
    f->state_present = false;
}
static int from_receiver(void *ctx, const rtaa_frame *frame)
{
    fixture *f = ctx;
    const uint8_t *p = frame->data;
    size_t n = frame->size;
    uint8_t plain[RTAA_PLAIN_MAX];
    if (frame->flags & RTAA_ENCRYPTED)
    {
        peer_ingest(&f->server, p, n);
        int got = mbedtls_ssl_read(&f->server.ssl, plain, sizeof(plain));
        assert(got > 1);
        p = plain;
        n = (size_t)got;
    }
    ++f->fragments;
    rtaa_frame plain_frame = *frame;
    plain_frame.data = p;
    plain_frame.size = n;
    if (!f->assembly.active)
        f->assembly.channel = frame->channel;
    f->assembly.data = f->assembled;
    f->assembly.capacity = sizeof(f->assembled);
    bool complete;
    assert(!rtaa_assembly_push(&f->assembly, &plain_frame, &complete));
    if (!complete)
        return 0;
    p = f->assembled;
    n = f->assembly.used;
    assert(n >= 2);
    f->message = (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
    f->channel = frame->channel;
    n -= 2;
    p += 2;
    if (f->message == 3 && !(frame->flags & RTAA_ENCRYPTED))
        peer_ingest(&f->server, p, n);
    assert(n <= sizeof(f->body));
    memcpy(f->body, p, n);
    f->body_size = n;
    return 0;
}
static void drain(fixture *f)
{
    assert(!rtaa_parser_feed(&f->parser, f->writes, f->writes_size, from_receiver, f));
    f->writes_size = 0;
}
static int inject(rtaa_receiver *r, uint8_t id, uint8_t flags, const uint8_t *p, size_t n)
{
    uint8_t wire[RTAA_FRAME_MAX + 8];
    size_t size;
    rtaa_frame frame = {id, flags | RTAA_FIRST | RTAA_LAST, 0, p, n};
    assert(!rtaa_frame_encode(&frame, wire, sizeof(wire), &size));
    /* Feed byte-by-byte through the production stream parser. */
    for (size_t i = 0; i < size; ++i)
    {
        int rc = rtaa_receiver_feed(r, wire + i, 1);
        if (rc)
            return rc;
    }
    return 0;
}
static int send_message_flags(fixture *f, rtaa_receiver *r, uint8_t id, uint16_t message,
                              const uint8_t *p, size_t n, uint8_t flags)
{
    uint8_t plain[8192];
    assert(n < sizeof(plain) - 2);
    plain[0] = (uint8_t)(message >> 8);
    plain[1] = (uint8_t)message;
    if (n)
        memcpy(plain + 2, p, n);
    assert(!f->server.output_size);
    assert(mbedtls_ssl_write(&f->server.ssl, plain, n + 2) == (int)n + 2);
    int rc = inject(r, id, RTAA_ENCRYPTED | flags, f->server.output, f->server.output_size);
    f->server.output_size = 0;
    return rc;
}
static int send_message(fixture *f, rtaa_receiver *r, uint8_t id, uint16_t message,
                        const uint8_t *p, size_t n)
{
    return send_message_flags(f, r, id, message, p, n, 0);
}
static int send_proto(fixture *f, rtaa_receiver *r, uint8_t id, uint16_t message,
                      const pb_msgdesc_t *desc, const void *value)
{
    uint8_t body[4096];
    pb_ostream_t out = pb_ostream_from_buffer(body, sizeof(body));
    assert(pb_encode(&out, desc, value));
    return send_message(f, r, id, message, body, out.bytes_written);
}
static rtaa_receiver_config configuration(fixture *f)
{
    memset(f, 0, sizeof(*f));
    peer_init(&f->server);
    rtaa_parser_reset(&f->parser);
    rtaa_receiver_config c = {0};
    c.memory = (rtaa_allocator){NULL, allocate, release};
    c.tls = tls_config(&f->server);
    c.transport =
        (rtaa_transport){f, NULL, write_transport, milliseconds, cancel_transport, close_transport};
    c.sink = (rtaa_sink){.ctx = f,
                         .start = start_sink,
                         .data = data_sink,
                         .stop = stop_sink,
                         .audio_focus = audio_focus,
                         .video_focus = video_focus,
                         .microphone = mic_sink,
                         .navigation_focus = navigation_focus,
                         .voice_session = voice_session,
                         .battery_status = battery_status};
    f->navigation_owner = 2;
    c.geometry = (rtaa_geometry){1280, 720, 512, 120, 0, 0, 768, 600};
    /* Deliberately use noncanonical, noncontiguous IDs, including >127. */
    const uint8_t ids[] = {197, 9, 71, 111, 23, 87, 201};
    for (unsigned i = 0; i < sizeof(ids); ++i)
        c.channels[i] = (rtaa_channel_config){ids[i], 32768};
    c.name = "rt-androidauto test";
    c.manufacturer = "test";
    c.model = "host";
    c.io_timeout_ms = 100;
    c.session_timeout_ms = 5000;
    c.heartbeat_ms = 1000;
    c.memory_limit = 1024 * 1024;
    return c;
}
static rtaa_receiver *create(fixture *f)
{
    rtaa_receiver_config c = configuration(f);
    rtaa_receiver *r;
    assert(!rtaa_receiver_create(&c, &r));
    return r;
}
static void connect_version(fixture *f, rtaa_receiver *r, uint8_t minor)
{
    if (rtaa_receiver_state(r) == RTAA_IDLE)
        assert(!rtaa_receiver_start(r));
    drain(f);
    assert(f->message == 1);
    uint8_t version[] = {0, 2, 0, 1, 0, minor, 0, 0};
    assert(!inject(r, 0, 0, version, sizeof(version)));
    for (unsigned i = 0; i < 40 && rtaa_receiver_state(r) != RTAA_DISCOVERY; ++i)
    {
        drain(f);
        peer_step(&f->server);
        if (f->server.output_size)
        {
            uint8_t data[8192] = {0, 3};
            size_t n = f->server.output_size;
            assert(n < sizeof(data) - 2);
            memcpy(data + 2, f->server.output, n);
            f->server.output_size = 0;
            assert(!inject(r, 0, RTAA_CONTROL, data, n + 2));
        }
    }
    assert(rtaa_receiver_state(r) == RTAA_DISCOVERY);
    drain(f);
    assert(f->message == 4);
    assert(!send_message(f, r, 0, 5, NULL, 0));
    drain(f);
    assert(f->message == 6);
    aap_Discovery d = aap_Discovery_init_zero;
    pb_istream_t in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_Discovery_fields, &d));
    assert(d.channels_count == 7u + (f->info_enabled ? 1u : 0u) + (f->status_services ? 2u : 0u));
    if (f->info_enabled)
        assert(d.channels[7].channel_id == 155 && d.channels[7].has_media_info &&
               !d.channels[7].has_sensor && !d.channels[7].has_av);
    if (f->status_services)
    {
        unsigned index = f->info_enabled ? 8 : 7;
        assert(d.channels[index].channel_id == 163 && d.channels[index].has_navigation &&
               d.channels[index].navigation.type == 1 &&
               d.channels[index].navigation.minimum_interval_ms == 1500);
        assert(d.channels[index + 1].channel_id == 229 && d.channels[index + 1].has_phone);
    }
    assert(d.channels[0].channel_id == 197 &&
           d.channels[0].av.video_configs[0].margin_width == 512);
    assert(rtaa_receiver_state(r) == RTAA_CONNECTED);
    assert(rtaa_receiver_protocol_minor(r) == minor);
}
static void connect(fixture *f, rtaa_receiver *r)
{
    connect_version(f, r, 1);
}
static void open_channel(fixture *f, rtaa_receiver *r, uint8_t id)
{
    aap_ChannelOpen v = {-1, id};
    assert(!send_proto(f, r, id, 7, aap_ChannelOpen_fields, &v));
    drain(f);
    assert(f->message == 8 && f->channel == id);
}
typedef struct
{
    fixture *f;
    unsigned opens, abandoned;
    int result;
    bool late;
    size_t live, allocations, fail_at;
} connector_fixture;
static void *connection_allocate(void *ctx, size_t size)
{
    connector_fixture *f = ctx;
    if (++f->allocations == f->fail_at)
        return NULL;
    void *p = calloc(1, size);
    assert(p);
    ++f->live;
    return p;
}
static void connection_release(void *ctx, void *p)
{
    connector_fixture *f = ctx;
    assert(p && f->live);
    --f->live;
    free(p);
}
static uint64_t connector_clock(void *ctx)
{
    return ((connector_fixture *)ctx)->f->time;
}
static int connector_open(void *ctx, uint32_t remaining, rtaa_transport *out)
{
    connector_fixture *f = ctx;
    ++f->opens;
    assert(remaining && remaining <= 100);
    /* Opening a new peer must not overlap any previous owned transport. */
    assert(f->opens == 1 || f->f->closed || f->abandoned);
    if (f->result)
        return f->result;
    if (f->late)
        f->f->time += remaining;
    peer_free(&f->f->server);
    peer_init(&f->f->server);
    rtaa_parser_reset(&f->f->parser);
    f->f->writes_size = 0;
    *out = (rtaa_transport){f->f,           NULL, write_transport, milliseconds, cancel_transport,
                            close_transport};
    return RTAA_OK;
}
static void connector_abandon(void *ctx)
{
    ++((connector_fixture *)ctx)->abandoned;
}
static rtaa_connection_config connection_config(connector_fixture *f, fixture *session)
{
    memset(f, 0, sizeof(*f));
    f->f = session;
    rtaa_connection_config c = {0};
    c.receiver = configuration(session);
    c.receiver.memory = (rtaa_allocator){f, connection_allocate, connection_release};
    c.receiver.tls.memory = c.receiver.memory;
    c.connector = (rtaa_connector){f, connector_open, connector_abandon, connector_clock};
    c.max_attempts = 3;
    c.open_timeout_ms = 100;
    c.retry_delay_ms = 10;
    c.retry_delay_max_ms = 15;
    return c;
}
static void expect_status(rtaa_connection *c, rtaa_connection_state state, uint32_t attempts,
                          int error)
{
    rtaa_connection_status status;
    rtaa_connection_get_status(c, &status);
    assert(status.state == state && status.attempts == attempts && status.last_error == error);
}
static void test_connection(fixture *session)
{
    connector_fixture f;
    rtaa_connection_config config = connection_config(&f, session);
    rtaa_connection *c;
    assert(!rtaa_connection_create(&config, &c));
    assert(!rtaa_connection_start(c));
    assert(rtaa_connection_start(c) == RTAA_ESTATE);
    assert(!rtaa_connection_poll(c));
    connect(session, rtaa_connection_receiver(c)); /* real TLS + discovery */
    session->time = 5000;
    assert(rtaa_connection_poll(c) == RTAA_AGAIN);
    expect_status(c, RTAA_CONNECTION_WAITING, 1, RTAA_ETIMEOUT);
    assert(session->closed == 1 && session->cancelled == 1 && f.live == 1);
    session->time = 5009;
    assert(rtaa_connection_poll(c) == RTAA_AGAIN && f.opens == 1);
    session->time = 5010;
    assert(!rtaa_connection_poll(c));
    connect(session, rtaa_connection_receiver(c));
    /* A send failure outside poll retains EIO and closes the old session before
     * the connection layer retries. A successful connect must not reset count. */
    open_channel(session, rtaa_connection_receiver(c), 87);
    aap_Binding binding = aap_Binding_init_zero;
    assert(!send_proto(session, rtaa_connection_receiver(c), 87, 0x8002, aap_Binding_fields,
                       &binding));
    drain(session);
    session->write_error = RTAA_EIO;
    assert(rtaa_receiver_touch(rtaa_connection_receiver(c), 0, 0, 1, 1, 1) == RTAA_EIO);
    assert(session->closed == 2);
    assert(rtaa_connection_poll(c) == RTAA_AGAIN);
    expect_status(c, RTAA_CONNECTION_WAITING, 2, RTAA_EIO);
    session->time = 5024;
    assert(rtaa_connection_poll(c) == RTAA_AGAIN && f.opens == 2);
    session->time = 5025;
    session->write_error = 0;
    assert(!rtaa_connection_poll(c));
    connect(session, rtaa_connection_receiver(c));
    session->time += 5000;
    assert(rtaa_connection_poll(c) == RTAA_ETIMEOUT);
    expect_status(c, RTAA_CONNECTION_EXHAUSTED, 3, RTAA_ETIMEOUT);
    assert(f.opens == 3 && !f.abandoned && session->closed == 3 && f.live == 1);
    assert(rtaa_connection_poll(c) == RTAA_ESTATE);
    /* Explicit restart resets the bounded cycle. Peer shutdown does not retry. */
    assert(!rtaa_connection_start(c) && !rtaa_connection_poll(c));
    connect(session, rtaa_connection_receiver(c));
    aap_Shutdown shutdown = {1};
    assert(
        !send_proto(session, rtaa_connection_receiver(c), 0, 15, aap_Shutdown_fields, &shutdown));
    drain(session);
    assert(!rtaa_connection_poll(c));
    expect_status(c, RTAA_CONNECTION_STOPPED, 1, 0);
    assert(session->closed == 4 && !rtaa_connection_receiver(c));
    rtaa_connection_stop(c);
    rtaa_connection_destroy(c);
    assert(!f.live && session->closed == 4);
    peer_free(&session->server);

    /* Pending-open deadlines, capped backoff, cancellation and no busy loop. */
    config = connection_config(&f, session);
    assert(!rtaa_connection_create(&config, &c));
    f.result = RTAA_AGAIN;
    assert(!rtaa_connection_start(c));
    assert(rtaa_connection_poll(c) == RTAA_AGAIN);
    session->time = 100;
    assert(rtaa_connection_poll(c) == RTAA_AGAIN && f.abandoned == 1);
    session->time = 110;
    f.result = RTAA_EIO;
    assert(rtaa_connection_poll(c) == RTAA_AGAIN && f.abandoned == 2);
    session->time = 124;
    assert(rtaa_connection_poll(c) == RTAA_AGAIN && f.opens == 2);
    session->time = 125;
    assert(rtaa_connection_poll(c) == RTAA_EIO && f.abandoned == 3);
    expect_status(c, RTAA_CONNECTION_EXHAUSTED, 3, RTAA_EIO);
    assert(!rtaa_connection_start(c));
    rtaa_connection_stop(c); /* even before the first open step */
    rtaa_connection_stop(c);
    assert(f.abandoned == 4 && !rtaa_connection_receiver(c));
    rtaa_connection_destroy(c);
    assert(!f.live && !session->closed);
    peer_free(&session->server);

    /* Failed/invalid opens and over-deadline success retire exactly one owner. */
    const int terminal[] = {RTAA_ESECURITY, RTAA_EPROTO, RTAA_EINVAL, RTAA_ECANCEL, 2};
    for (unsigned i = 0; i < sizeof(terminal) / sizeof(terminal[0]); ++i)
    {
        config = connection_config(&f, session);
        f.result = terminal[i];
        assert(!rtaa_connection_create(&config, &c) && !rtaa_connection_start(c));
        int expected = terminal[i] < 0 ? terminal[i] : RTAA_EINVAL;
        assert(rtaa_connection_poll(c) == expected && f.abandoned == 1);
        expect_status(c, RTAA_CONNECTION_EXHAUSTED, 1, expected);
        rtaa_connection_destroy(c);
        assert(!f.live && !session->closed);
        peer_free(&session->server);
    }
    config = connection_config(&f, session);
    f.late = true;
    assert(!rtaa_connection_create(&config, &c) && !rtaa_connection_start(c));
    assert(rtaa_connection_poll(c) == RTAA_AGAIN);
    assert(session->closed == 1 && !f.abandoned);
    rtaa_connection_destroy(c);
    assert(!f.live);
    peer_free(&session->server);

    /* Fail every SDK allocation (controller, receiver, channels, TLS wrapper).
     * mbedTLS's internal allocator is separately covered by the TLS backend. */
    size_t count = 0;
    for (size_t failure_at = 0; failure_at <= count; ++failure_at)
    {
        config = connection_config(&f, session);
        f.fail_at = failure_at;
        int rc = rtaa_connection_create(&config, &c);
        if (!rc)
        {
            assert(!rtaa_connection_start(c));
            rc = rtaa_connection_poll(c);
            if (!failure_at)
                count = f.allocations;
            else
                assert(rc == RTAA_EBOUNDS && session->closed == 1);
            rtaa_connection_destroy(c);
        }
        else
            assert(failure_at == 1 && rc == RTAA_EBOUNDS && !session->closed);
        assert(!f.live);
        peer_free(&session->server);
    }
    config = connection_config(&f, session);
    assert(!rtaa_connection_create(&config, &c) && !rtaa_connection_start(c));
    assert(!rtaa_connection_poll(c));
    /* Production frame parser failure through the connection API is terminal. */
    const uint8_t bad[] = {0, 0x80, 0, 0};
    assert(rtaa_connection_feed(c, bad, sizeof(bad)) == RTAA_EPROTO);
    expect_status(c, RTAA_CONNECTION_EXHAUSTED, 1, RTAA_EPROTO);
    assert(session->closed == 1 && f.live == 1);
    rtaa_connection_destroy(c);
    assert(!f.live);
    peer_free(&session->server);
}
static void test_control_extensions(fixture *f)
{
    rtaa_receiver_config c = configuration(f);
    c.protocol_minor = 4;
    rtaa_receiver *r;
    assert(!rtaa_receiver_create(&c, &r));
    assert(!rtaa_receiver_protocol_minor(r));
    connect_version(f, r, 4);
    aap_NavigationFocus focus = {true, 2};
    assert(!send_proto(f, r, 0, 13, aap_NavigationFocus_fields, &focus));
    drain(f);
    pb_istream_t in = pb_istream_from_buffer(f->body, f->body_size);
    assert(f->message == 14 && f->channel == 0 &&
           pb_decode(&in, aap_NavigationFocus_fields, &focus) && focus.type == 2);
    f->navigation_owner = 1;
    assert(!send_proto(f, r, 0, 13, aap_NavigationFocus_fields, &focus));
    drain(f);
    in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_NavigationFocus_fields, &focus) && focus.type == 1);
    assert(f->navigation_requests == 2);
    /* Independent literal wire fixtures, including an unknown optional field. */
    const uint8_t voice[] = {0x08, 1, 0x78, 9};
    assert(!send_message(f, r, 0, 17, voice, sizeof(voice)));
    assert(f->voice_active && f->voice_changes == 1 && !f->writes_size);
    const uint8_t stop_voice[] = {0x08, 2};
    assert(!send_message(f, r, 0, 17, stop_voice, sizeof(stop_voice)));
    assert(!f->voice_active && f->voice_changes == 2 && !f->writes_size);
    const uint8_t battery[] = {0x08, 0, 0x10, 60, 0x18, 1};
    assert(!send_message(f, r, 0, 23, battery, sizeof(battery)));
    assert(f->batteries == 1 && f->battery.has_level && f->battery.level == 0 &&
           f->battery.has_time_remaining && f->battery.time_remaining_s == 60 &&
           f->battery.has_critical && f->battery.critical && !f->writes_size);
    assert(!send_message(f, r, 0, 23, NULL, 0));
    assert(f->batteries == 2 && !f->battery.has_level && !f->battery.has_critical);
    assert(!rtaa_receiver_call_available(r, false));
    drain(f);
    assert(f->message == 24 && f->body_size == 2 && f->body[0] == 8 && !f->body[1]);
    /* Two complete TLS records in one AAP fragment still form one message. */
    const uint8_t ping[] = {0, 11, 8, 42};
    assert(mbedtls_ssl_write(&f->server.ssl, ping, 1) == 1);
    assert(mbedtls_ssl_write(&f->server.ssl, ping + 1, sizeof(ping) - 1) == 3);
    assert(!inject(r, 0, RTAA_ENCRYPTED | RTAA_CONTROL, f->server.output, f->server.output_size));
    f->server.output_size = 0;
    drain(f);
    assert(f->message == 12 && f->body_size == 2 && f->body[1] == 42);
    open_channel(f, r, 87);
    aap_Binding binding = aap_Binding_init_zero;
    binding.keycodes_count = 1;
    binding.keycodes[0] = 4;
    assert(!send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding));
    drain(f);
    assert(!rtaa_receiver_key(r, 4, true, 1));
    drain(f);
    assert(!rtaa_receiver_close_channel(r, RTAA_INPUT));
    drain(f);
    assert(f->message == 9 && f->channel == 87 && !f->body_size && !f->closed);
    assert(rtaa_receiver_key(r, 4, false, 2) == RTAA_ESTATE);
    open_channel(f, r, 87);
    assert(rtaa_receiver_key(r, 4, true, 3) == RTAA_ESTATE); /* must rebind */
    assert(!send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding));
    drain(f);
    assert(!rtaa_receiver_key(r, 4, true, 4)); /* old pressed state was cleared */
    drain(f);
    open_channel(f, r, 201);
    aap_SensorStart sensor = {10, 100};
    assert(!send_proto(f, r, 201, 0x8001, aap_SensorStart_fields, &sensor));
    drain(f);
    assert(!rtaa_receiver_night(r, true));
    drain(f);
    assert(!send_message_flags(f, r, 201, 9, NULL, 0, RTAA_CONTROL));
    open_channel(f, r, 201);
    assert(rtaa_receiver_night(r, true) == RTAA_ESTATE); /* must resubscribe */
    open_channel(f, r, 197);
    aap_SetupRequest setup = {3};
    aap_Start start = {42, 0};
    assert(!send_proto(f, r, 197, 0x8000, aap_SetupRequest_fields, &setup));
    drain(f);
    assert(!send_proto(f, r, 197, 0x8001, aap_Start_fields, &start));
    assert(!send_message_flags(f, r, 197, 9, NULL, 0, RTAA_CONTROL));
    assert(f->stops == 1 && !f->closed && rtaa_receiver_state(r) == RTAA_CONNECTED);
    open_channel(f, r, 197);
    assert(!send_proto(f, r, 197, 0x8000, aap_SetupRequest_fields, &setup));
    drain(f);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);

    c = configuration(f);
    c.protocol_minor = 4;
    c.sink.navigation_focus = NULL;
    assert(!rtaa_receiver_create(&c, &r));
    connect(f, r);                                  /* downgrade to 1.1 */
    assert(!rtaa_receiver_call_available(r, true)); /* explicit application opt-in */
    drain(f);
    assert(f->message == 24 && f->body_size == 2 && f->body[1] == 1);
    focus = (aap_NavigationFocus){true, 2};
    assert(!send_proto(f, r, 0, 13, aap_NavigationFocus_fields, &focus));
    drain(f);
    in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_NavigationFocus_fields, &focus) && focus.type == 1);
    focus.type = 0;
    assert(send_proto(f, r, 0, 13, aap_NavigationFocus_fields, &focus) == RTAA_EPROTO);
    assert(f->closed == 1);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);

    c = configuration(f);
    c.protocol_minor = 8;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EINVAL && !r);
    c.protocol_minor = 4;
    assert(!rtaa_receiver_create(&c, &r));
    assert(!rtaa_receiver_start(r));
    drain(f);
    assert(f->message == 1 && f->body_size == 4 && f->body[3] == 4);
    const uint8_t bad_version[] = {0, 2, 0, 1, 0, 5, 0, 0};
    assert(inject(r, 0, 0, bad_version, sizeof(bad_version)) == RTAA_EPROTO);
    assert(f->closed == 1);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
}
static void test_extension_rejection(fixture *f)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        rtaa_receiver *r = create(f);
        connect(f, r);
        int rc;
        if (i == 0)
        {
            const uint8_t bad[] = {8, 0};
            rc = send_message(f, r, 0, 17, bad, sizeof(bad));
        }
        else if (i == 1)
        {
            const uint8_t bad[] = {8, 101};
            rc = send_message(f, r, 0, 23, bad, sizeof(bad));
        }
        else if (i == 2)
        {
            open_channel(f, r, 87);
            rc = send_message(f, r, 87, 9, NULL, 0); /* missing control flag */
        }
        else
        {
            const uint8_t ping[] = {0, 11, 8, 42};
            assert(mbedtls_ssl_write(&f->server.ssl, ping, sizeof(ping)) == 4);
            assert(mbedtls_ssl_write(&f->server.ssl, ping, sizeof(ping)) == 4);
            /* Truncated second record must not become another channel's data. */
            rc = inject(r, 0, RTAA_ENCRYPTED, f->server.output, f->server.output_size - 1);
            f->server.output_size = 0;
        }
        assert(rc < 0 && rtaa_receiver_state(r) == RTAA_FAILED && f->closed == 1);
        assert(!f->voice_changes && !f->batteries && !f->writes_size);
        rtaa_receiver_destroy(r);
        peer_free(&f->server);
    }
}
static void test_fragmented_microphone(fixture *f)
{
    rtaa_receiver *r = create(f);
    connect(f, r);
    open_channel(f, r, 23);
    aap_MicOpen mic = {true, true, true, false, true, false, true, 8};
    assert(!send_proto(f, r, 23, 0x8005, aap_MicOpen_fields, &mic));
    drain(f);
    uint8_t *pcm = malloc(65520);
    assert(pcm);
    for (size_t i = 0; i < 65520; ++i)
        pcm[i] = (uint8_t)(i * 31u);
    const size_t sizes[] = {16374, 16376, 32758, 32760, 65518};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        unsigned before = f->fragments;
        assert(!rtaa_receiver_microphone(r, pcm, sizes[i], 123));
        drain(f);
        assert(f->fragments - before == (sizes[i] + 10 + RTAA_PLAIN_MAX - 1) / RTAA_PLAIN_MAX);
        assert(f->message == 0 && f->channel == 23 && f->body_size == sizes[i] + 8);
        assert(f->body[7] == 123 && !memcmp(f->body + 8, pcm, sizes[i]));
    }
    assert(rtaa_receiver_microphone(r, pcm, 65520, 1) == RTAA_EINVAL && !f->writes_size);
    assert(!send_message_flags(f, r, 23, 9, NULL, 0, RTAA_CONTROL));
    assert(f->mic_changes == 2 && !f->closed);
    open_channel(f, r, 23);
    assert(!send_proto(f, r, 23, 0x8005, aap_MicOpen_fields, &mic));
    drain(f);
    assert(!rtaa_receiver_microphone(r, pcm, 2, 124));
    drain(f);
    aap_Ack old = aap_Ack_init_zero;
    old.session_id = 1;
    old.has_ack_count = true;
    old.ack_count = 1;
    assert(send_proto(f, r, 23, 0x8004, aap_Ack_fields, &old) == RTAA_EPROTO);
    assert(f->closed == 1 && f->mic_changes == 4);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
    r = create(f);
    connect(f, r);
    open_channel(f, r, 23);
    assert(!send_proto(f, r, 23, 0x8005, aap_MicOpen_fields, &mic));
    drain(f);
    f->fail_after_bytes = 20000; /* fail midway through the second frame */
    assert(rtaa_receiver_microphone(r, pcm, 65518, 125) == RTAA_EIO);
    assert(f->writes_size == 20000 && f->closed == 1 && f->mic_changes == 2 &&
           rtaa_receiver_state(r) == RTAA_FAILED);
    rtaa_statistics stats;
    rtaa_receiver_statistics(r, &stats);
    assert(!stats.microphone_bytes && stats.errors == 1);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
    free(pcm);
}
static void test_configurable_input_and_sensors(fixture *f)
{
    rtaa_receiver_config c = configuration(f);
    c.input_keycode_count = 32;
    for (unsigned i = 0; i < 31; ++i)
        c.input_keycodes[i] = i + 1;
    c.input_keycodes[31] = 65536; /* exercise bit 31, not an 8-bit key mask */
    c.sink.sensor_start = sensor_start;
    c.sink.sensor_stop = sensor_stop;
    rtaa_receiver *r;
    assert(!rtaa_receiver_create(&c, &r));
    connect(f, r);
    aap_Discovery discovery = aap_Discovery_init_zero;
    pb_istream_t in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_Discovery_fields, &discovery));
    assert(discovery.channels[RTAA_INPUT].input.keycodes_count == 32);
    assert(discovery.channels[RTAA_INPUT].input.keycodes[31] == 65536);
    open_channel(f, r, 87);
    assert(rtaa_receiver_rotary(r, -3, 10) == RTAA_ESTATE);
    aap_Binding binding = aap_Binding_init_zero;
    binding.keycodes_count = 32;
    memcpy(binding.keycodes, c.input_keycodes, sizeof(c.input_keycodes));
    assert(!send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding));
    drain(f);
    assert(!rtaa_receiver_rotary(r, -3, 10));
    drain(f);
    aap_InputEvent event = aap_InputEvent_init_zero;
    in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_InputEvent_fields, &event));
    assert(event.has_relative && event.relative.events_count == 1 &&
           event.relative.events[0].scan_code == 65536 && event.relative.events[0].delta == -3);
    assert(rtaa_receiver_rotary(r, 0, 11) == RTAA_EINVAL && !f->writes_size);
    assert(rtaa_receiver_key(r, 65536, true, 11) == RTAA_EINVAL && !f->writes_size);
    assert(!rtaa_receiver_key(r, 22, true, 12)); /* previously missing DPAD_RIGHT */
    drain(f);
    assert(!rtaa_receiver_key(r, 22, false, 13));
    drain(f);
    open_channel(f, r, 201);
    aap_SensorStart sensor = {13, INT64_C(10000000000)};
    assert(!send_proto(f, r, 201, 0x8001, aap_SensorStart_fields, &sensor));
    drain(f);
    assert(f->sensor_starts == 1 && f->sensor_type == 13 &&
           f->sensor_interval == INT64_C(10000000000));
    sensor.refresh_interval = 0;
    assert(!send_proto(f, r, 201, 0x8001, aap_SensorStart_fields, &sensor));
    drain(f);
    assert(f->sensor_starts == 2 && !f->sensor_interval);
    assert(!rtaa_receiver_close_channel(r, RTAA_SENSOR));
    drain(f);
    assert(f->sensor_stops == 1);
    open_channel(f, r, 201);
    sensor.type = 10;
    f->write_error = RTAA_EIO;
    assert(send_proto(f, r, 201, 0x8001, aap_SensorStart_fields, &sensor) == RTAA_EIO);
    assert(f->sensor_starts == 3 && f->sensor_stops == 2 && f->closed == 1);
    rtaa_receiver_destroy(r);
    assert(f->sensor_stops == 2);
    peer_free(&f->server);
    c = configuration(f);
    c.input_keycode_count = 33;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EBOUNDS);
    c.input_keycode_count = 2;
    c.input_keycodes[0] = c.input_keycodes[1] = 85;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EINVAL);
    c.input_keycode_count = 0;
    c.sink.sensor_start = sensor_start;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EINVAL);
    peer_free(&f->server);
}
static rtaa_receiver *create_media_info(fixture *f)
{
    rtaa_receiver_config c = configuration(f);
    c.channels[RTAA_MEDIA_INFO] = (rtaa_channel_config){155, 32768};
    rtaa_receiver *r = NULL;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EINVAL);
    c.sink.media_status = media_status;
    c.sink.media_metadata = media_metadata;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EINVAL);
    c.sink.media_info_reset = media_info_reset;
    assert(!rtaa_receiver_create(&c, &r));
    f->info_enabled = true;
    return r;
}
static void test_media_info(fixture *f)
{
    rtaa_receiver *r = create_media_info(f);
    connect(f, r);
    open_channel(f, r, 155);
    /* Independently documented captured status: playing, YouTube Music, 80s,
     * explicitly false shuffle/repeat/repeat-one. */
    const uint8_t captured[] = {0x08, 2,    0x12, 13,   'Y', 'o',  'u', 'T', 'u',
                                'b',  'e',  ' ',  'M',  'u', 's',  'i', 'c', 0x18,
                                80,   0x20, 0,    0x28, 0,   0x30, 0};
    assert(!send_message(f, r, 155, 0x8001, captured, sizeof(captured)));
    assert(f->media_statuses == 1 && f->media_status.has_state && f->media_status.state == 2 &&
           f->media_status.has_source_app && !strcmp(f->media_status.source_app, "YouTube Music") &&
           f->media_status.has_position_seconds && f->media_status.position_seconds == 80 &&
           f->media_status.has_shuffle && !f->media_status.shuffle && f->media_status.has_repeat &&
           !f->media_status.repeat && f->media_status.has_repeat_one &&
           !f->media_status.repeat_one && !f->writes_size);
    assert(!send_message(f, r, 155, 0x8001, NULL, 0));
    assert(f->media_statuses == 2 && !f->media_status.has_state &&
           !f->media_status.has_source_app && !f->media_status.has_position_seconds &&
           !f->media_status.has_shuffle && !f->media_status.has_repeat &&
           !f->media_status.has_repeat_one);
    /* Optional empty title is present; unknown art/string/numeric fields skip. */
    const uint8_t text[] = {0x0a, 0,    0x12, 3,   'A', 'B',  'C', 0x1a, 3,    'D', 'E',  'F', 0x22,
                            4,    0x89, 'P',  'N', 'G', 0x2a, 1,   'x',  0x30, 1,   0x38, 0};
    assert(!send_message(f, r, 155, 0x8003, text, sizeof(text)));
    assert(f->metadata_updates == 1 && f->metadata.has_title && !f->metadata.title[0] &&
           f->metadata.has_artist && !strcmp(f->metadata.artist, "ABC") && f->metadata.has_album &&
           !strcmp(f->metadata.album, "DEF"));
    aap_MediaMetadata v = aap_MediaMetadata_init_zero;
    v.has_title = true;
    memset(v.title, 'x', sizeof(v.title) - 1);
    assert(!send_proto(f, r, 155, 0x8003, aap_MediaMetadata_fields, &v));
    assert(strlen(f->metadata.title) == 255 && !f->metadata.has_artist);
    assert(!rtaa_receiver_close_channel(r, RTAA_MEDIA_INFO));
    drain(f);
    assert(f->info_resets == 1 && !f->metadata.has_title);
    open_channel(f, r, 155);
    assert(!send_message_flags(f, r, 155, 9, NULL, 0, RTAA_CONTROL));
    assert(f->info_resets == 2 && rtaa_receiver_state(r) == RTAA_CONNECTED);
    open_channel(f, r, 155);
    assert(!send_message(f, r, 155, 0x8003, NULL, 0));
    assert(!f->metadata.has_title && !f->metadata.has_artist && !f->metadata.has_album);
    f->reject = true;
    assert(send_message(f, r, 155, 0x8001, captured, sizeof(captured)) == RTAA_EIO);
    assert(f->info_resets == 3 && rtaa_receiver_state(r) == RTAA_FAILED);
    rtaa_receiver_destroy(r);
    assert(f->info_resets == 3);
    peer_free(&f->server);
    /* Malformed, oversized, invalid-state, wrong-direction and unopened traffic
     * must fail before application delivery. */
    const uint8_t invalid_state[] = {8, 5};
    const uint8_t truncated[] = {0x22, 4, 1};
    uint8_t oversized[259] = {0x0a, 0x80, 2};
    memset(oversized + 3, 'z', 256);
    for (unsigned i = 0; i < 5; ++i)
    {
        r = create_media_info(f);
        connect(f, r);
        if (i != 4)
            open_channel(f, r, 155);
        int rc = i == 0   ? send_message(f, r, 155, 0x8001, invalid_state, sizeof(invalid_state))
                 : i == 1 ? send_message(f, r, 155, 0x8003, truncated, sizeof(truncated))
                 : i == 2 ? send_message(f, r, 155, 0x8003, oversized, sizeof(oversized))
                          : send_message(f, r, 155, 0x8002, NULL, 0);
        assert(rc == (i < 3 ? RTAA_EPROTO : i == 3 ? RTAA_ENOTSUP : RTAA_ESTATE));
        assert(!f->media_statuses && !f->metadata_updates && f->info_resets == (i == 4 ? 0 : 1));
        rtaa_receiver_destroy(r);
        peer_free(&f->server);
    }
    r = create(f);
    connect(f, r);
    assert(send_message(f, r, 155, 0x8001, NULL, 0) == RTAA_EPROTO);
    assert(!f->media_statuses && !f->info_resets);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
}
static rtaa_receiver *create_status_services(fixture *f)
{
    rtaa_receiver_config c = configuration(f);
    c.channels[RTAA_MEDIA_INFO] = (rtaa_channel_config){155, 32768};
    c.sink.media_status = media_status;
    c.sink.media_metadata = media_metadata;
    c.sink.media_info_reset = media_info_reset;
    c.channels[RTAA_NAVIGATION] = (rtaa_channel_config){163, 32768};
    c.channels[RTAA_PHONE] = (rtaa_channel_config){229, 32768};
    c.navigation_interval_ms = 1500;
    rtaa_receiver *r = NULL;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EINVAL);
    c.sink.phone_status = phone_status;
    c.sink.phone_reset = phone_reset;
    c.sink.navigation_state = nav_state;
    c.sink.navigation_notification = nav_notification;
    c.sink.navigation_position = nav_position;
    c.sink.navigation_turn = nav_turn;
    assert(rtaa_receiver_create(&c, &r) == RTAA_EINVAL);
    c.sink.navigation_reset = nav_reset;
    assert(!rtaa_receiver_create(&c, &r));
    f->status_services = true;
    f->info_enabled = true;
    return r;
}
static void test_status_services(fixture *f)
{
    rtaa_receiver *r = create_status_services(f);
    connect(f, r);
    assert(rtaa_receiver_navigation(r, true) == RTAA_ESTATE);
    assert(rtaa_receiver_phone_input(r, 5, NULL, NULL) == RTAA_ESTATE);
    open_channel(f, r, 163);
    open_channel(f, r, 229);
    assert(!rtaa_receiver_navigation(r, true));
    drain(f);
    assert(f->channel == 163 && f->message == 0x8001 && !f->body_size);
    assert(!rtaa_receiver_navigation(r, true) && !f->writes_size);
    assert(!rtaa_receiver_navigation(r, false));
    drain(f);
    assert(f->message == 0x8002 && !f->body_size);
    assert(!rtaa_receiver_navigation(r, false) && !f->writes_size);
    assert(!rtaa_receiver_phone_input(r, 7, "", NULL));
    drain(f);
    aap_PhoneInput input = aap_PhoneInput_init_zero;
    pb_istream_t in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_PhoneInput_fields, &input));
    assert(f->channel == 229 && f->message == 0x8002 && input.input_type.action == 7 &&
           input.has_caller_id && !input.caller_id[0] && !input.has_display_name);
    assert(rtaa_receiver_phone_input(r, 8, NULL, NULL) == RTAA_EINVAL && !f->writes_size);
    char long_text[129];
    memset(long_text, 'a', sizeof(long_text) - 1);
    long_text[128] = 0;
    assert(rtaa_receiver_phone_input(r, 5, long_text, NULL) == RTAA_EBOUNDS && !f->writes_size);
    long_text[127] = 0;
    assert(!rtaa_receiver_phone_input(r, 5, NULL, long_text));
    drain(f);
    in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_PhoneInput_fields, &input) && input.has_display_name &&
           strlen(input.display_name) == 127);
    /* Required call fields, present empty name, absent contact and skipped photo. */
    const uint8_t call[] = {0x0a, 15,  8,  4, 16, 0, 26,   4,  '+', '1',
                            '2',  '3', 34, 0, 50, 1, 0xff, 16, 0};
    assert(!send_message(f, r, 229, 0x8001, call, sizeof(call)));
    assert(f->phone_updates == 1 && f->calls_count == 1 && f->call_state == 4 && f->signal_present);
    assert(!send_message(f, r, 229, 0x8001, NULL, 0));
    assert(f->phone_updates == 2 && !f->calls_count && !f->signal_present);
    aap_NavigationState state = {true, 1};
    assert(!send_proto(f, r, 163, 0x8003, aap_NavigationState_fields, &state));
    assert(f->nav_states == 1 && f->state_present && f->nav_state == 1);
    assert(!send_message(f, r, 163, 0x8003, NULL, 0));
    assert(!f->state_present);
    aap_NavigationNotification nav = aap_NavigationNotification_init_zero;
    nav.steps_count = nav.destinations_count = 1;
    aap_NavStep *step = &nav.steps[0];
    step->has_maneuver = step->has_instruction = step->has_road_info = true;
    step->maneuver = (aap_NavManeuver){true, 7, true, 0, true, -90};
    step->instruction.has_text = true;
    strcpy(step->instruction.text, "Turn left");
    step->lanes_count = 1;
    step->lanes[0].directions_count = 2;
    step->lanes[0].directions[0] = (aap_NavDirection){true, 4, true, false};
    step->road_info.road_names_count = 1;
    strcpy(step->road_info.road_names[0], "Road");
    nav.destinations[0].has_address = nav.destinations[0].has_charging_station = true;
    nav.destinations[0].charging_station = (aap_NavChargingStation){true, 2, true, 0, true, 50};
    assert(!send_proto(f, r, 163, 0x8006, aap_NavigationNotification_fields, &nav));
    assert(f->nav_notifications == 1 && f->nav_steps == 1 && f->nav_destinations == 1 &&
           f->instruction_present && f->lane_shape == 4 && !strcmp(f->status_text, "Turn left"));
    assert(!send_message(f, r, 163, 0x8006, NULL, 0));
    assert(f->nav_notifications == 2 && !f->nav_steps && !f->nav_destinations);
    /* Independent upstream captured stationary position: text "0", feet. */
    const uint8_t stationary[] = {0x0a, 7, 0x0a, 5, 0x12, 1, '0', 0x18, 6};
    assert(!send_message(f, r, 163, 0x8007, stationary, sizeof(stationary)));
    assert(f->nav_positions == 1 && f->distance_present && !strcmp(f->status_text, "0"));
    aap_NavigationPosition position = aap_NavigationPosition_init_zero;
    position.has_step_distance = position.step_distance.has_distance = true;
    position.step_distance.distance = (aap_NavDistance){true, 0, true, "0", true, 1};
    position.step_distance.has_time_to_step_seconds = true;
    position.step_distance.time_to_step_seconds = INT64_C(5000000000);
    position.destinations_count = 1;
    position.destinations[0].has_time_to_arrival_seconds = true;
    position.destinations[0].time_to_arrival_seconds = INT64_C(5000000001);
    position.destinations[0].has_estimated_time_of_arrival = true;
    strcpy(position.destinations[0].estimated_time_of_arrival, "12:30");
    assert(!send_proto(f, r, 163, 0x8007, aap_NavigationPosition_fields, &position));
    assert(f->eta_present && f->eta_seconds == INT64_C(5000000001) && !f->distance_value);
    const uint8_t legacy[] = {0x0a, 1, 'R', 0x22, 1, 0xff, 0x28, 0};
    assert(!send_message(f, r, 163, 0x8004, legacy, sizeof(legacy)));
    assert(f->nav_turns == 1 && !strcmp(f->status_text, "R"));
    assert(!rtaa_receiver_close_channel(r, RTAA_NAVIGATION));
    drain(f);
    assert(f->nav_resets == 1 && !f->phone_resets && !f->status_text[0]);
    open_channel(f, r, 163);
    assert(!rtaa_receiver_navigation(r, true));
    drain(f);
    assert(f->message == 0x8001);
    assert(!send_message_flags(f, r, 229, 9, NULL, 0, RTAA_CONTROL));
    assert(f->phone_resets == 1 && rtaa_receiver_state(r) == RTAA_CONNECTED);
    open_channel(f, r, 229);
    rtaa_receiver_destroy(r);
    assert(f->phone_resets == 2 && f->nav_resets == 2);
    peer_free(&f->server);
    /* Required fields, enum validation, repeated bounds, direction checks and
     * malformed nested data must fail before any callback. */
    const uint8_t bad_required[] = {10, 2, 8, 1};
    const uint8_t bad_call_state[] = {10, 4, 8, 7, 16, 0};
    const uint8_t bad_state[] = {8, 4};
    const uint8_t bad_lane[] = {10, 6, 26, 4, 10, 2, 8, 10};
    const uint8_t bad_unit[] = {10, 4, 10, 2, 24, 8};
    const uint8_t too_many_steps[] = {10, 0, 10, 0, 10, 0, 10, 0, 10, 0};
    const uint8_t too_many_calls[] = {10, 4,  8, 1,  16, 0, 10, 4,  8, 1,  16, 0, 10, 4,  8,
                                      1,  16, 0, 10, 4,  8, 1,  16, 0, 10, 4,  8, 1,  16, 0};
    const uint8_t malformed[] = {10, 2, 10};
    struct
    {
        uint8_t id;
        uint16_t msg;
        const uint8_t *p;
        size_t n;
        int expected;
    } cases[] = {{229, 0x8001, bad_required, sizeof(bad_required), RTAA_EPROTO},
                 {229, 0x8001, bad_call_state, sizeof(bad_call_state), RTAA_EPROTO},
                 {163, 0x8003, bad_state, sizeof(bad_state), RTAA_EPROTO},
                 {163, 0x8006, bad_lane, sizeof(bad_lane), RTAA_EPROTO},
                 {163, 0x8007, bad_unit, sizeof(bad_unit), RTAA_EPROTO},
                 {163, 0x8006, too_many_steps, sizeof(too_many_steps), RTAA_EPROTO},
                 {229, 0x8001, too_many_calls, sizeof(too_many_calls), RTAA_EPROTO},
                 {163, 0x8006, malformed, sizeof(malformed), RTAA_EPROTO},
                 {229, 0x8002, NULL, 0, RTAA_ENOTSUP},
                 {163, 0x8001, NULL, 0, RTAA_ENOTSUP}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        r = create_status_services(f);
        connect(f, r);
        open_channel(f, r, cases[i].id);
        assert(send_message(f, r, cases[i].id, cases[i].msg, cases[i].p, cases[i].n) ==
               cases[i].expected);
        assert(!f->phone_updates && !f->nav_states && !f->nav_notifications && !f->nav_positions);
        rtaa_receiver_destroy(r);
        assert(f->phone_resets + f->nav_resets == 1);
        peer_free(&f->server);
    }
    for (unsigned i = 0; i < 5; ++i)
    {
        r = create_status_services(f);
        connect(f, r);
        open_channel(f, r, i == 0 ? 229 : 163);
        const uint16_t messages[] = {0x8001, 0x8003, 0x8004, 0x8006, 0x8007};
        f->reject = true;
        assert(send_message(f, r, i == 0 ? 229 : 163, messages[i], NULL, 0) == RTAA_EIO);
        assert(f->phone_resets + f->nav_resets == 1);
        rtaa_receiver_destroy(r);
        peer_free(&f->server);
    }
    r = create_status_services(f);
    connect(f, r);
    open_channel(f, r, 163);
    f->write_error = RTAA_EIO;
    assert(rtaa_receiver_navigation(r, true) == RTAA_EIO && f->nav_resets == 1);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
}
int main(void)
{
    fixture *f = calloc(1, sizeof(*f));
    assert(f);
    rtaa_receiver *r = create(f);
    connect(f, r);
    open_channel(f, r, 197);
    aap_SetupRequest setup = {3};
    assert(!send_proto(f, r, 197, 0x8000, aap_SetupRequest_fields, &setup));
    drain(f);
    aap_Start start = {42, 0};
    assert(!send_proto(f, r, 197, 0x8001, aap_Start_fields, &start));
    const uint8_t media[] = {0, 0, 0, 0, 0, 0, 0, 123, 0, 0, 1, 0x65};
    assert(!send_message(f, r, 197, 0, media, sizeof(media)));
    drain(f);
    aap_Ack ack = aap_Ack_init_zero;
    pb_istream_t in = pb_istream_from_buffer(f->body, f->body_size);
    assert(f->message == 0x8004 && pb_decode(&in, aap_Ack_fields, &ack) && ack.session_id == 42);
    const uint8_t audio_ids[] = {9, 71, 111};
    for (unsigned i = 0; i < 3; ++i)
    {
        open_channel(f, r, audio_ids[i]);
        setup.codec = 1;
        assert(!send_proto(f, r, audio_ids[i], 0x8000, aap_SetupRequest_fields, &setup));
        drain(f);
        assert(!send_proto(f, r, audio_ids[i], 0x8001, aap_Start_fields, &start));
        assert(!send_message(f, r, audio_ids[i], 0, media, sizeof(media)));
        drain(f);
    }
    open_channel(f, r, 87);
    aap_Binding binding = aap_Binding_init_zero;
    binding.keycodes_count = 1;
    binding.keycodes[0] = 4;
    assert(!send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding));
    drain(f);
    assert(rtaa_receiver_touch(r, 0, 0, 768, 1, 99) == RTAA_EBOUNDS && !f->writes_size);
    assert(rtaa_receiver_touch(r, 2, 0, 1, 1, 99) == RTAA_ESTATE && !f->writes_size);
    assert(rtaa_receiver_touch(r, 1, 0, 1, 1, 99) == RTAA_ESTATE && !f->writes_size);
    assert(!rtaa_receiver_touch(r, 0, 0, 0, 0, 99));
    drain(f);
    aap_InputEvent input = aap_InputEvent_init_zero;
    in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_InputEvent_fields, &input) && input.touch.locations[0].x == 256);
    assert(rtaa_receiver_touch(r, 0, 0, 1, 1, 100) == RTAA_ESTATE && !f->writes_size);
    assert(rtaa_receiver_touch(r, 2, 1, 1, 1, 100) == RTAA_ESTATE && !f->writes_size);
    assert(rtaa_receiver_touch(r, 2, 0, 768, 1, 100) == RTAA_EBOUNDS && !f->writes_size);
    assert(!rtaa_receiver_touch(r, 2, 0, 767, 599, 100));
    drain(f);
    assert(!rtaa_receiver_touch(r, 1, 0, 1023, 599, 101));
    drain(f);
    input = (aap_InputEvent)aap_InputEvent_init_zero;
    in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_InputEvent_fields, &input));
    assert(input.touch.action == 1 && input.touch.action_index == 0 &&
           input.touch.locations[0].x == 1023 && input.touch.locations[0].y == 659);
    assert(!rtaa_receiver_touch_cancel(r, 102) && !f->writes_size);
    assert(!rtaa_receiver_touch(r, 0, 7, 1, 1, 103));
    drain(f);
    assert(!rtaa_receiver_touch_cancel(r, 104));
    drain(f);
    input = (aap_InputEvent)aap_InputEvent_init_zero;
    in = pb_istream_from_buffer(f->body, f->body_size);
    assert(pb_decode(&in, aap_InputEvent_fields, &input));
    assert(input.touch.action == 1 && input.touch.locations[0].pointer_id == 7);
    assert(rtaa_receiver_key(r, 3, true, 105) == RTAA_ENOTSUP && !f->writes_size);
    assert(rtaa_receiver_key(r, 4, false, 105) == RTAA_ESTATE && !f->writes_size);
    assert(!rtaa_receiver_key(r, 4, true, 100));
    drain(f);
    assert(rtaa_receiver_key(r, 4, true, 101) == RTAA_ESTATE && !f->writes_size);
    assert(!rtaa_receiver_key(r, 4, false, 102));
    drain(f);
    open_channel(f, r, 201);
    aap_SensorStart sensor = {10, 100};
    assert(!send_proto(f, r, 201, 0x8001, aap_SensorStart_fields, &sensor));
    drain(f);
    assert(!rtaa_receiver_night(r, true));
    drain(f);
    aap_FocusRequest focus = {true, 3};
    assert(!send_proto(f, r, 0, 18, aap_FocusRequest_fields, &focus));
    drain(f);
    open_channel(f, r, 23);
    aap_MicOpen mic = {true, true, true, false, true, false, true, 1};
    assert(!send_proto(f, r, 23, 0x8005, aap_MicOpen_fields, &mic));
    drain(f);
    const uint8_t pcm[] = {0, 0, 1, 0};
    assert(!rtaa_receiver_microphone(r, pcm, sizeof(pcm), 123));
    drain(f);
    assert(rtaa_receiver_microphone(r, pcm, sizeof(pcm), 124) == RTAA_AGAIN);
    ack = (aap_Ack)aap_Ack_init_zero;
    ack.session_id = 1;
    ack.has_ack_count = true;
    ack.ack_count = 1;
    assert(!send_proto(f, r, 23, 0x8004, aap_Ack_fields, &ack));
    assert(!rtaa_receiver_microphone(r, pcm, sizeof(pcm), 125));
    drain(f);
    assert(!rtaa_receiver_shutdown(r));
    drain(f);
    assert(f->message == 15);
    assert(!send_message(f, r, 0, 16, NULL, 0));
    assert(rtaa_receiver_state(r) == RTAA_CLOSED);
    assert(f->closed == 1 && f->cancelled == 1 && f->stops == 4 && f->mic_changes == 2);
    rtaa_statistics stats;
    rtaa_receiver_statistics(r, &stats);
    assert(stats.acks == 4 && !stats.errors);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
    r = create(f);
    assert(!rtaa_receiver_start(r));
    f->time = 5000;
    assert(rtaa_receiver_poll(r) == RTAA_ETIMEOUT && rtaa_receiver_state(r) == RTAA_FAILED);
    assert(f->closed == 1);
    rtaa_receiver_destroy(r);
    assert(f->closed == 1);
    peer_free(&f->server);
    r = create(f);
    connect(f, r);
    open_channel(f, r, 197);
    setup.codec = 3;
    assert(!send_proto(f, r, 197, 0x8000, aap_SetupRequest_fields, &setup));
    drain(f);
    assert(!send_proto(f, r, 197, 0x8001, aap_Start_fields, &start));
    f->reject = true;
    assert(send_message(f, r, 197, 0, media, sizeof(media)) == RTAA_EIO && !f->writes_size);
    assert(rtaa_receiver_state(r) == RTAA_FAILED);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
    r = create(f);
    connect(f, r);
    open_channel(f, r, 87);
    binding = (aap_Binding)aap_Binding_init_zero;
    assert(!send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding));
    drain(f);
    assert(rtaa_receiver_key(r, 4, true, 1) == RTAA_ENOTSUP && !f->writes_size);
    assert(!rtaa_receiver_touch(r, 0, 0, 1, 1, 1));
    drain(f);
    assert(send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding) == RTAA_ESTATE);
    assert(rtaa_receiver_state(r) == RTAA_FAILED && f->closed == 1);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
    r = create(f);
    connect(f, r);
    open_channel(f, r, 87);
    binding.keycodes_count = 2;
    binding.keycodes[0] = binding.keycodes[1] = 4;
    assert(send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding) == RTAA_EPROTO);
    assert(!f->writes_size && f->closed == 1);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
    r = create(f);
    connect(f, r);
    open_channel(f, r, 87);
    binding.keycodes_count = 1;
    binding.keycodes[0] = 999;
    assert(send_proto(f, r, 87, 0x8002, aap_Binding_fields, &binding) == RTAA_EPROTO);
    assert(!f->writes_size && f->closed == 1);
    rtaa_receiver_destroy(r);
    peer_free(&f->server);
    test_connection(f);
    test_control_extensions(f);
    test_extension_rejection(f);
    test_fragmented_microphone(f);
    test_configurable_input_and_sensors(f);
    test_media_info(f);
    test_status_services(f);
    free(f);
    printf("receiver: PASS; core storage=%zu bytes\n", rtaa_receiver_storage_size());
    return 0;
}
