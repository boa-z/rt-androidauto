/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtaa/rtthread.h"
#include <assert.h>
#include <stdio.h>
static uint32_t tick;
uint32_t rt_tick_get(void)
{
    return tick;
}
int main(void)
{
    rtaa_rtthread_clock clock = {0};
    tick = UINT32_MAX - 2;
    assert(rtaa_rtthread_now_ms(&clock) == UINT32_MAX - 2u);
    tick = 3;
    assert(rtaa_rtthread_now_ms(&clock) == (uint64_t)UINT32_MAX + 4);
    tick = 1003;
    assert(rtaa_rtthread_now_ms(&clock) == (uint64_t)UINT32_MAX + 1004);
    rtaa_allocator m = rtaa_rtthread_allocator();
    unsigned char *p = m.allocate(m.ctx, 10);
    assert(p);
    for (unsigned i = 0; i < 10; ++i)
        assert(!p[i]);
    m.release(m.ctx, p);
    puts("RT-Thread port: PASS");
    return 0;
}
