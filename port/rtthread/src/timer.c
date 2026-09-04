/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 */

/**
 * @file timer.c
 * @brief Lely io_user_timer to RT-Thread one-shot timer bridge.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#include <stdint.h>

/**
 * @brief Compare two normalized absolute timespec values.
 * @param lhs Left-hand time value.
 * @param rhs Right-hand time value.
 * @return Negative, zero or positive when lhs is before, equal to or after rhs.
 */
static int
lely_rtt_timespec_cmp(const struct timespec *lhs, const struct timespec *rhs)
{
    if (lhs->tv_sec < rhs->tv_sec)
        return -1;
    if (lhs->tv_sec > rhs->tv_sec)
        return 1;
    if (lhs->tv_nsec < rhs->tv_nsec)
        return -1;
    if (lhs->tv_nsec > rhs->tv_nsec)
        return 1;
    return 0;
}

/**
 * @brief Convert a future absolute Lely deadline into a safe RT one-shot delay.
 *
 * The delay is rounded up so RT-Thread never wakes Lely before the requested
 * deadline. RT-Thread timers require values below half the native tick range;
 * longer deadlines are therefore approached through intermediate wakeups.
 *
 * @param now Current monotonic time.
 * @param next Future absolute Lely deadline.
 * @return Positive one-shot delay in RT-Thread ticks.
 */
static rt_tick_t
lely_rtt_deadline_ticks(const struct timespec *now,
        const struct timespec *next)
{
    rt_uint64_t sec;
    rt_uint64_t nsec;
    rt_uint64_t ticks;
    const rt_uint64_t max_ticks = (rt_uint64_t)(RT_TICK_MAX / 2 - 1);

    sec = (rt_uint64_t)(next->tv_sec - now->tv_sec);
    if (next->tv_nsec >= now->tv_nsec) {
        nsec = (rt_uint64_t)(next->tv_nsec - now->tv_nsec);
    } else {
        sec--;
        nsec = UINT64_C(1000000000) + (rt_uint64_t)next->tv_nsec
                - (rt_uint64_t)now->tv_nsec;
    }

    if (sec > max_ticks / RT_TICK_PER_SECOND)
        return (rt_tick_t)max_ticks;

    ticks = sec * RT_TICK_PER_SECOND;
    ticks += (nsec * RT_TICK_PER_SECOND + UINT64_C(999999999))
            / UINT64_C(1000000000);
    if (!ticks)
        ticks = 1;
    if (ticks > max_ticks)
        ticks = max_ticks;

    return (rt_tick_t)ticks;
}

/**
 * @brief RT one-shot deadline callback.
 *
 * Hard/soft timer context is not a Lely owner context. The callback therefore
 * only sets TIMER_DUE and returns immediately; io_clock_settime() runs later in
 * the owner thread.
 *
 * @param parameter Pointer to struct lely_rtt_runtime.
 */
static void
lely_rtt_deadline_timeout(void *parameter)
{
    struct lely_rtt_runtime *runtime = parameter;

    if (!runtime || !lely_rtt_callback_acquire(runtime))
        return;

    if (runtime->event_initialized)
        rt_event_send(&runtime->event, LELY_RTT_EVENT_TIMER_DUE);

    lely_rtt_callback_release(runtime);
}

/**
 * @brief Program or disarm the RT one-shot timer for Lely's next deadline.
 *
 * A zero deadline disarms the RT one-shot timer. Expired deadlines wake the
 * owner immediately. Future deadlines are converted to a bounded RT tick delay.
 *
 * @param next Next absolute deadline; all-zero disarms the bridge.
 * @param arg Pointer to struct lely_rtt_runtime.
 */
static void
lely_rtt_timer_setnext(const struct timespec *next, void *arg)
{
    struct lely_rtt_runtime *runtime = arg;
    struct timespec now;
    rt_tick_t ticks;

    if (!runtime || !runtime->deadline_timer_initialized)
        return;

    /* A zero absolute deadline means that the passive timer has no waiter. */
    if (!next->tv_sec && !next->tv_nsec) {
        rt_timer_stop(&runtime->deadline_timer);
        return;
    }

    if (lely_rtt_time_now(runtime, &now) != RT_EOK)
        return;

    if (lely_rtt_timespec_cmp(next, &now) <= 0) {
        rt_timer_stop(&runtime->deadline_timer);
        rt_event_send(&runtime->event, LELY_RTT_EVENT_TIMER_DUE);
        return;
    }

    ticks = lely_rtt_deadline_ticks(&now, next);
    rt_timer_stop(&runtime->deadline_timer);
    rt_timer_control(&runtime->deadline_timer, RT_TIMER_CTRL_SET_TIME, &ticks);
    if (rt_timer_start(&runtime->deadline_timer) != RT_EOK) {
        /*
         * Fail closed. Reposting TIMER_DUE here would call setnext() again and
         * could create an owner-thread self-wakeup loop after an arm failure.
         */
        LELY_RTT_LOG_E("deadline timer start failed");
        if (runtime->runtime_error == RT_EOK)
            runtime->runtime_error = -RT_ERROR;
        rt_event_send(&runtime->event, LELY_RTT_EVENT_STOP);
    }
}

/**
 * @brief Initialize the RT one-shot timer and passive Lely user timer.
 * @param runtime Runtime instance owned by the Lely owner thread.
 * @return RT_EOK on success or an initialization error.
 */
rt_err_t
lely_rtt_timer_init(struct lely_rtt_runtime *runtime)
{
    struct timespec now;

    if (!runtime || !runtime->ctx || !runtime->loop)
        return -RT_EINVAL;

    rt_timer_init(&runtime->deadline_timer, "lelytmr",
            lely_rtt_deadline_timeout, runtime, 1, RT_TIMER_FLAG_ONE_SHOT);
    runtime->deadline_timer_initialized = RT_TRUE;

    runtime->timer = io_user_timer_create(runtime->ctx,
            ev_loop_get_exec(runtime->loop), lely_rtt_timer_setnext, runtime);
    if (!runtime->timer) {
        LELY_RTT_LOG_E("io_user_timer_create failed");
        return -RT_ERROR;
    }

    /* Seed the passive clock before any io_can_net deadline can be scheduled. */
    if (lely_rtt_time_now(runtime, &now) != RT_EOK) {
        LELY_RTT_LOG_E("initial monotonic time sample failed");
        return -RT_ERROR;
    }
    if (io_clock_settime(io_timer_get_clock(runtime->timer), &now) == -1) {
        LELY_RTT_LOG_E("initial Lely clock update failed");
        return -RT_ERROR;
    }

    LELY_RTT_LOG_D("timer bridge ready");
    return RT_EOK;
}

/**
 * @brief Advance the passive Lely clock to the current RT monotonic time.
 * @param runtime Runtime instance; must be called by the owner thread.
 */
void
lely_rtt_timer_advance(struct lely_rtt_runtime *runtime)
{
    struct timespec now;

    if (!runtime || !runtime->timer)
        return;

    /* io_clock_settime() runs only on the owner, preserving no-thread access. */
    if (lely_rtt_time_now(runtime, &now) == RT_EOK)
        io_clock_settime(io_timer_get_clock(runtime->timer), &now);
}

/**
 * @brief Stop/detach the RT timer and destroy the passive Lely timer.
 * @param runtime Runtime instance being shut down.
 */
void
lely_rtt_timer_fini(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    if (runtime->deadline_timer_initialized) {
        rt_timer_stop(&runtime->deadline_timer);
        rt_timer_detach(&runtime->deadline_timer);
        runtime->deadline_timer_initialized = RT_FALSE;
    }

    if (runtime->timer) {
        io_user_timer_destroy(runtime->timer);
        runtime->timer = RT_NULL;
    }
}
