/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/rtthread.h"
#include <rtthread.h>
static void *allocate(void *ctx, size_t size)
{
    (void)ctx;
    return rt_calloc(1, size);
}
static void release(void *ctx, void *p)
{
    (void)ctx;
    rt_free(p);
}
rtaa_allocator rtaa_rtthread_allocator(void)
{
    return (rtaa_allocator){NULL, allocate, release};
}
uint64_t rtaa_rtthread_now_ms(void *ctx)
{
    rtaa_rtthread_clock *c = ctx;
    if (!c)
        return 0;
    uint32_t tick = (uint32_t)rt_tick_get();
    if (!c->initialized)
    {
        c->ticks = tick;
        c->initialized = true;
    }
    else
        c->ticks += (uint32_t)(tick - c->last);
    c->last = tick;
    return c->ticks / RT_TICK_PER_SECOND * 1000 +
           c->ticks % RT_TICK_PER_SECOND * 1000 / RT_TICK_PER_SECOND;
}
