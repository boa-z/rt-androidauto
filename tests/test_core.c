/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls;
static int arbitrary_frame(void *ctx, const rtaa_frame *f)
{
    (void)ctx;
    (void)f;
    return 0;
}
static int frame(void *ctx, const rtaa_frame *f)
{
    (void)ctx;
    assert(f->channel == 197);
    assert(f->size == 7);
    assert(!memcmp(f->data, "payload", 7));
    ++calls;
    return 0;
}
static void framing(void)
{
    rtaa_frame f = {197, RTAA_FIRST | RTAA_LAST | RTAA_ENCRYPTED, 0, (const uint8_t *)"payload", 7};
    uint8_t data[64];
    size_t n;
    assert(!rtaa_frame_encode(&f, data, sizeof(data), &n));
    for (size_t step = 1; step <= n; ++step)
    {
        rtaa_parser p;
        rtaa_parser_reset(&p);
        unsigned before = calls;
        for (size_t i = 0; i < n;)
        {
            size_t chunk = step < n - i ? step : n - i;
            assert(!rtaa_parser_feed(&p, data + i, chunk, frame, NULL));
            i += chunk;
        }
        assert(calls == before + 1);
    }
    rtaa_parser p;
    rtaa_parser_reset(&p);
    uint8_t bad[] = {0, 0xff, 0, 2, 0, 1};
    assert(rtaa_parser_feed(&p, bad, sizeof(bad), frame, NULL) == RTAA_EPROTO);
    assert(rtaa_parser_feed(&p, data, n, frame, NULL) == RTAA_ESTATE);
    f.size = SIZE_MAX;
    assert(rtaa_frame_encode(&f, data, sizeof(data), &n) == RTAA_EBOUNDS);
    /* Bounded random stream parser, no synthetic implementation. */
    uint32_t rng = 123456789;
    for (unsigned i = 0; i < 20000; ++i)
    {
        uint8_t bytes[32];
        for (unsigned j = 0; j < sizeof(bytes); ++j)
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            bytes[j] = (uint8_t)rng;
        }
        rtaa_parser_reset(&p);
        /* Use a nonasserting callback for arbitrary well-formed random frames. */
        (void)rtaa_parser_feed(&p, bytes, sizeof(bytes), arbitrary_frame, NULL);
    }
}
static void assembly(void)
{
    uint8_t one[16], two[16];
    rtaa_assembly a = {1, 0, one, sizeof(one), 0, 0, false};
    rtaa_assembly b = {99, 0, two, sizeof(two), 0, 0, false};
    bool complete;
    rtaa_frame f = {1, RTAA_FIRST, 6, (const uint8_t *)"ab", 2};
    assert(!rtaa_assembly_push(&a, &f, &complete) && !complete);
    f.channel = 99;
    assert(!rtaa_assembly_push(&b, &f, &complete) && !complete);
    f.channel = 1;
    f.flags = 0;
    f.data = (const uint8_t *)"cd";
    assert(!rtaa_assembly_push(&a, &f, &complete));
    f.flags = RTAA_LAST;
    f.data = (const uint8_t *)"ef";
    assert(!rtaa_assembly_push(&a, &f, &complete) && complete);
    assert(!memcmp(a.data, "abcdef", 6));
    f.channel = 99;
    f.data = (const uint8_t *)"efgh";
    f.size = 4;
    assert(!rtaa_assembly_push(&b, &f, &complete) && complete);
    assert(rtaa_assembly_push(&b, &f, &complete) == RTAA_EPROTO);
    rtaa_assembly_reset(&a);
    f.channel = 1;
    f.flags = RTAA_FIRST;
    f.total = UINT32_MAX;
    assert(rtaa_assembly_push(&a, &f, &complete) == RTAA_EBOUNDS);
}
typedef struct
{
    unsigned step;
    uint64_t time;
    bool zero;
    unsigned cancelled;
} fake;
static int control(void *ctx, uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                   uint8_t *p, uint16_t n, uint32_t timeout)
{
    fake *f = ctx;
    assert(!value && timeout);
    if (!f->step)
    {
        assert(type == 0xc0 && request == 51 && n == 2);
        p[0] = 2;
        p[1] = 0;
    }
    else if (f->step <= 6)
    {
        assert(type == 0x40 && request == 52 && index == f->step - 1);
        assert(n > 1 && !p[n - 1]);
    }
    else
    {
        assert(type == 0x40 && request == 53 && !n);
    }
    ++f->step;
    return n;
}
static void cancel(void *ctx)
{
    ++((fake *)ctx)->cancelled;
}
static uint64_t clock_ms(void *ctx)
{
    return ((fake *)ctx)->time++;
}
static int write_bytes(void *ctx, const uint8_t *p, size_t n, uint32_t timeout)
{
    fake *f = ctx;
    assert(p && timeout);
    return f->zero ? 0 : n > 3 ? 3 : (int)n;
}
static void aoa(void)
{
    fake f = {0};
    rtaa_aoa a = {0};
    rtaa_usb_control usb = {&f, control, cancel};
    const char *strings[] = {"maker", "model", "Android Auto", "1", "https://example.org", "unit"};
    assert(!rtaa_aoa_begin(&a, &usb, strings, 100));
    assert(f.step == 8);
    uint8_t descriptor[] = {9, 2, 32, 0, 1,    1, 0, 0x80, 50, 9, 4, 0,    0, 2, 0xff, 0xff,
                            0, 0, 7,  5, 0x81, 2, 0, 2,    0,  7, 5, 0x02, 2, 0, 2,    0};
    assert(!rtaa_aoa_bind(&a, 0x18d1, 0x2d01, descriptor, sizeof(descriptor), 200));
    assert(a.state == RTAA_AOA_READY && a.endpoint_in == 0x81 && a.endpoint_out == 2);
    rtaa_aoa_detach(&a);
    assert(f.cancelled == 1);
    descriptor[18] = 0;
    assert(rtaa_aoa_bind(&a, 0x18d1, 0x2d00, descriptor, sizeof(descriptor), 300) == RTAA_EPROTO);
    a.state = RTAA_AOA_REENUMERATING;
    a.deadline = 10;
    assert(rtaa_aoa_poll(&a, 10) == RTAA_ETIMEOUT);
    assert(!rtaa_aoa_accessory(0x18d1, 0x2d02));
}
static void transport(void)
{
    fake f = {0};
    rtaa_transport t = {&f, NULL, write_bytes, clock_ms, cancel, NULL};
    assert(!rtaa_write_all(&t, (const uint8_t *)"0123456789", 10, 20));
    f.zero = true;
    assert(rtaa_write_all(&t, (const uint8_t *)"x", 1, 20) == RTAA_ETIMEOUT);
    rtaa_geometry g = {1280, 720, 512, 120, 0, 0, 768, 600};
    uint16_t x, y;
    assert(!rtaa_touch_map(&g, 0, 0, &x, &y) && x == 256 && y == 60);
    assert(!rtaa_touch_map(&g, 767, 599, &x, &y) && x == 1023 && y == 659);
    assert(rtaa_touch_map(&g, 768, 0, &x, &y) == RTAA_EBOUNDS);
    g.margin_width = 256;
    g.viewport_width = 1024;
    assert(!rtaa_touch_map(&g, 1023, 599, &x, &y) && x == 1151 && y == 659);
}
int main(void)
{
    framing();
    assembly();
    aoa();
    transport();
    puts("core: PASS");
    return 0;
}
