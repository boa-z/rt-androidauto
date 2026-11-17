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
    uint8_t writes[65536], body[8192];
    size_t writes_size, body_size;
    uint8_t channel;
    uint16_t message;
    uint64_t time;
    unsigned starts, stops, media, cancelled, closed, mic_changes;
    bool reject;
    int write_error;
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
    if (n > 17)
        n = 17; /* force partial USB writes */
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
static int send_message(fixture *f, rtaa_receiver *r, uint8_t id, uint16_t message,
                        const uint8_t *p, size_t n)
{
    uint8_t plain[8192];
    assert(n < sizeof(plain) - 2);
    plain[0] = (uint8_t)(message >> 8);
    plain[1] = (uint8_t)message;
    if (n)
        memcpy(plain + 2, p, n);
    assert(!f->server.output_size);
    assert(mbedtls_ssl_write(&f->server.ssl, plain, n + 2) == (int)n + 2);
    int rc = inject(r, id, RTAA_ENCRYPTED, f->server.output, f->server.output_size);
    f->server.output_size = 0;
    return rc;
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
    c.sink =
        (rtaa_sink){f, start_sink, data_sink, stop_sink, audio_focus, video_focus, mic_sink, NULL};
    c.geometry = (rtaa_geometry){1280, 720, 512, 120, 0, 0, 768, 600};
    /* Deliberately use noncanonical, noncontiguous IDs, including >127. */
    const uint8_t ids[] = {197, 9, 71, 111, 23, 87, 201};
    for (unsigned i = 0; i < RTAA_ROLE_COUNT; ++i)
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
static void connect(fixture *f, rtaa_receiver *r)
{
    if (rtaa_receiver_state(r) == RTAA_IDLE)
        assert(!rtaa_receiver_start(r));
    drain(f);
    assert(f->message == 1);
    uint8_t version[] = {0, 2, 0, 1, 0, 1, 0, 0};
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
    assert(d.channels_count == 7);
    assert(d.channels[0].channel_id == 197 &&
           d.channels[0].av.video_configs[0].margin_width == 512);
    assert(rtaa_receiver_state(r) == RTAA_CONNECTED);
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
    free(f);
    printf("receiver: PASS; core storage=%zu bytes\n", rtaa_receiver_storage_size());
    return 0;
}
