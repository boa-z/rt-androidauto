/* Test OS seam; the tested clock/allocator implementation is the real port. */
#ifndef RTAA_TEST_RTTHREAD_H
#define RTAA_TEST_RTTHREAD_H
#include <stdint.h>
#include <stdlib.h>
#define RT_TICK_PER_SECOND 1000
uint32_t rt_tick_get(void);
#define rt_calloc calloc
#define rt_free free
#endif
