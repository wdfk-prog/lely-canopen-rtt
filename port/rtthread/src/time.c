/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 */

/**
 * @file time.c
 * @brief RT-Thread monotonic uptime and bounded lifecycle timeout conversion.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#include <stdint.h>

/**
 * @brief Convert a positive millisecond lifecycle timeout to safe RT ticks.
 *
 * The result is rounded up and clamped below both INT32_MAX and the RT timer
 * half-range limit used by event waits across tick wrap.
 *
 * @param timeout_ms Positive timeout in milliseconds.
 * @return Positive RT-Thread tick count suitable for rt_event_recv().
 */
rt_int32_t
lely_rtt_timeout_ticks(rt_uint32_t timeout_ms)
{
    rt_uint64_t ticks;
    rt_uint64_t max_ticks = (rt_uint64_t)INT32_MAX;
    const rt_uint64_t timer_max = (rt_uint64_t)(RT_TICK_MAX / 2 - 1);

    /* Round up so a positive millisecond timeout never becomes non-blocking. */
    ticks = ((rt_uint64_t)timeout_ms * RT_TICK_PER_SECOND + UINT64_C(999))
            / UINT64_C(1000);
    if (!ticks)
        ticks = 1;

    /*
     * Bounded lifecycle event waits arm the current thread's RT timer internally.
     * RT-Thread requires timer delays to be strictly below half the native
     * tick range so wrap-around ordering remains unambiguous.
     */
    if (max_ticks > timer_max)
        max_ticks = timer_max;
    if (ticks > max_ticks)
        ticks = max_ticks;

    return (rt_int32_t)ticks;
}

/**
 * @brief Extend rt_tick_get() into the monotonic timespec used by Lely.
 * @param runtime Runtime instance containing wrap-extension state.
 * @param tp Output monotonic time; this is uptime, not wall-clock UTC.
 * @return RT_EOK on success or -RT_EINVAL for invalid arguments.
 */
rt_err_t
lely_rtt_time_now(struct lely_rtt_runtime *runtime, struct timespec *tp)
{
    rt_tick_t now;
    rt_tick_t delta;
    rt_uint64_t ticks;
    rt_uint64_t rem;

    if (!runtime || !tp || RT_TICK_PER_SECOND == 0)
        return -RT_EINVAL;

    now = rt_tick_get();
    if (!runtime->time.initialized) {
        /* First sample defines monotonic time zero for this runtime instance. */
        runtime->time.last_tick = now;
        runtime->time.ticks = 0;
        runtime->time.initialized = RT_TRUE;
    } else {
        /*
         * rt_tick_t is unsigned on supported RT-Thread targets. Unsigned
         * subtraction therefore preserves one wrap interval without adding a
         * wall-clock dependency. The owner must sample at least once per full
         * rt_tick_t wrap interval for the extension to remain unambiguous.
         */
        delta = now - runtime->time.last_tick;
        runtime->time.last_tick = now;
        runtime->time.ticks += (rt_uint64_t)delta;
    }

    ticks = runtime->time.ticks;
    tp->tv_sec = (time_t)(ticks / RT_TICK_PER_SECOND);
    rem = ticks % RT_TICK_PER_SECOND;
    tp->tv_nsec = (long)((rem * UINT64_C(1000000000)) / RT_TICK_PER_SECOND);

    return RT_EOK;
}
