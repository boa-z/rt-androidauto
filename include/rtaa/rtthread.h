/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef RTAA_RTTHREAD_H
#define RTAA_RTTHREAD_H
#include "rtaa/tls.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        uint64_t ticks;
        uint32_t last;
        bool initialized;
    } rtaa_rtthread_clock;
    /* Single owner; call at least once per 32-bit RT tick wrap. */
    uint64_t rtaa_rtthread_now_ms(void *clock);
    rtaa_allocator rtaa_rtthread_allocator(void);
#ifdef __cplusplus
}
#endif
#endif
