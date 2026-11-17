/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/core.h"

int rtaa_write_all(const rtaa_transport *t, const uint8_t *data, size_t size, uint32_t timeout)
{
    if (!t || !t->write || !t->now_ms || (!data && size) || !timeout)
        return RTAA_EINVAL;
    uint64_t start = t->now_ms(t->ctx);
    while (size)
    {
        uint64_t elapsed = t->now_ms(t->ctx) - start;
        if (elapsed >= timeout)
            return RTAA_ETIMEOUT;
        int n = t->write(t->ctx, data, size, timeout - (uint32_t)elapsed);
        if (n < 0)
            return n;
        /* Zero progress is a timeout; never spin indefinitely on a broken port. */
        if (!n)
            return RTAA_ETIMEOUT;
        if ((size_t)n > size)
            return RTAA_EIO;
        data += n;
        size -= (size_t)n;
    }
    return RTAA_OK;
}
int rtaa_geometry_validate(const rtaa_geometry *g)
{
    if (!g || !g->frame_width || !g->frame_height || !g->viewport_width || !g->viewport_height ||
        g->margin_width >= g->frame_width || g->margin_height >= g->frame_height ||
        (g->margin_width & 1) || (g->margin_height & 1) ||
        (uint32_t)g->viewport_x + g->viewport_width > 65536u ||
        (uint32_t)g->viewport_y + g->viewport_height > 65536u)
        return RTAA_EINVAL;
    return RTAA_OK;
}
int rtaa_touch_map(const rtaa_geometry *g, uint16_t x, uint16_t y, uint16_t *ox, uint16_t *oy)
{
    if (!ox || !oy || rtaa_geometry_validate(g))
        return RTAA_EINVAL;
    if (x < g->viewport_x || y < g->viewport_y ||
        (uint32_t)x - g->viewport_x >= g->viewport_width ||
        (uint32_t)y - g->viewport_y >= g->viewport_height)
        return RTAA_EBOUNDS;
    uint32_t w = g->frame_width - g->margin_width;
    uint32_t h = g->frame_height - g->margin_height;
    *ox = (uint16_t)(g->margin_width / 2 + (uint32_t)(x - g->viewport_x) * w / g->viewport_width);
    *oy = (uint16_t)(g->margin_height / 2 + (uint32_t)(y - g->viewport_y) * h / g->viewport_height);
    return RTAA_OK;
}
