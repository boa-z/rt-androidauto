/* SPDX-License-Identifier: GPL-3.0-only
 * C11 adaptation of AllPlay core/projection/androidauto/aap.cpp and aap.h.
 * Original author: blt. Original source headers retained in third_party/upstream.
 * Changes: fixed bounds, streaming parsing, sticky errors and per-channel assembly.
 */
#include "rtaa/core.h"
#include <string.h>

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static bool extended(uint8_t flags)
{
    return (flags & (RTAA_FIRST | RTAA_LAST)) == RTAA_FIRST;
}
void rtaa_parser_reset(rtaa_parser *p)
{
    if (p)
    {
        p->used = 0;
        p->needed = 4;
        p->failed = false;
    }
}
int rtaa_parser_feed(rtaa_parser *p, const uint8_t *data, size_t size, rtaa_frame_fn callback,
                     void *ctx)
{
    if (!p || (!data && size) || !callback)
        return RTAA_EINVAL;
    if (p->failed || p->needed < 4 || p->needed > sizeof(p->bytes))
        return RTAA_ESTATE;
    while (size)
    {
        size_t n = p->needed - p->used;
        if (n > size)
            n = size;
        memcpy(p->bytes + p->used, data, n);
        p->used += n;
        data += n;
        size -= n;
        if (p->used < p->needed)
            continue;
        if (p->needed == 4)
        {
            size_t payload = ((size_t)p->bytes[2] << 8) | p->bytes[3];
            if ((p->bytes[1] & 0xf0) || !payload || payload > RTAA_FRAME_MAX)
            {
                p->failed = true;
                return RTAA_EPROTO;
            }
            p->needed = (extended(p->bytes[1]) ? 8 : 4) + payload;
            continue;
        }
        rtaa_frame f;
        size_t header = extended(p->bytes[1]) ? 8 : 4;
        f.channel = p->bytes[0];
        f.flags = p->bytes[1];
        f.total = header == 8 ? be32(p->bytes + 4) : 0;
        f.data = p->bytes + header;
        f.size = p->needed - header;
        int rc = callback(ctx, &f);
        p->used = 0;
        p->needed = 4;
        if (rc != RTAA_OK)
        {
            p->failed = true;
            return rc;
        }
    }
    return RTAA_OK;
}
int rtaa_frame_encode(const rtaa_frame *f, uint8_t *out, size_t capacity, size_t *written)
{
    if (!f || !out || !written || !f->data || !f->size || (f->flags & 0xf0))
        return RTAA_EINVAL;
    size_t header = extended(f->flags) ? 8 : 4;
    if (f->size > RTAA_FRAME_MAX || capacity < header || f->size > capacity - header)
        return RTAA_EBOUNDS;
    if (header == 8 && f->total < 2)
        return RTAA_EINVAL;
    out[0] = f->channel;
    out[1] = f->flags;
    out[2] = (uint8_t)(f->size >> 8);
    out[3] = (uint8_t)f->size;
    if (header == 8)
    {
        out[4] = (uint8_t)(f->total >> 24);
        out[5] = (uint8_t)(f->total >> 16);
        out[6] = (uint8_t)(f->total >> 8);
        out[7] = (uint8_t)f->total;
    }
    memcpy(out + header, f->data, f->size);
    *written = header + f->size;
    return RTAA_OK;
}
void rtaa_assembly_reset(rtaa_assembly *a)
{
    if (a)
    {
        a->used = a->total = 0;
        a->active = false;
    }
}
int rtaa_assembly_push(rtaa_assembly *a, const rtaa_frame *f, bool *complete)
{
    if (!a || !f || !complete || !a->data || !f->data || !f->size)
        return RTAA_EINVAL;
    *complete = false;
    if (f->channel != a->channel || (f->flags & 0xf0))
        return RTAA_EPROTO;
    uint8_t attributes = f->flags & (RTAA_ENCRYPTED | RTAA_CONTROL);
    if (f->flags & RTAA_FIRST)
    {
        if (a->active)
            return RTAA_EPROTO;
        a->used = 0;
        a->total = (f->flags & RTAA_LAST) ? f->size : f->total;
        if (a->total < 2 || a->total > a->capacity)
            return RTAA_EBOUNDS;
        a->attributes = attributes;
        a->active = true;
    }
    else if (!a->active || attributes != a->attributes)
        return RTAA_EPROTO;
    if (a->used > a->total || f->size > a->total - a->used)
        return RTAA_EBOUNDS;
    memcpy(a->data + a->used, f->data, f->size);
    a->used += f->size;
    if (f->flags & RTAA_LAST)
    {
        if (a->used != a->total)
            return RTAA_EPROTO;
        a->active = false;
        *complete = true;
    }
    else if (a->used == a->total)
        return RTAA_EPROTO;
    return RTAA_OK;
}
