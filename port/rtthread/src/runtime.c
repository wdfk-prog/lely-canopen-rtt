/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 */

/**
 * @file runtime.c
 * @brief RT-Thread single-owner Lely runtime lifecycle and event dispatcher.
 *
 * @author wdfk-prog
 */

#include "internal.h"

/**
 * @brief Drain all tasks currently runnable on the owner event loop.
 *
 * The no-thread build requires the owner to serialize every Lely callback.
 * Draining is therefore always performed from the owner thread and never from
 * ISR/driver callback context.
 *
 * @param runtime Runtime instance owning the event loop.
 */
static void
lely_rtt_drain_loop(struct lely_rtt_runtime *runtime)
{
    if (!runtime || !runtime->loop)
        return;

    while (ev_loop_poll_one(runtime->loop))
        ;
}

/**
 * @brief Latch the first asynchronous owner/runtime error.
 *
 * Keeping the first error preserves the root cause when later cleanup steps
 * also fail. RT_EOK never overwrites an existing error.
 *
 * @param runtime Runtime instance.
 * @param err RT-Thread error to latch.
 */
static void
lely_rtt_latch_error(struct lely_rtt_runtime *runtime, rt_err_t err)
{
    if (runtime && err != RT_EOK && runtime->runtime_error == RT_EOK)
        runtime->runtime_error = err;
}

/**
 * @brief Reset external-callback lifetime state before producers are published.
 * @param runtime Runtime instance.
 */
void
lely_rtt_callbacks_init(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    /* No callback may be published before this state has been reset. */
    rt_atomic_store(&runtime->callback_refs, 0);
    rt_atomic_store(&runtime->callback_quiescing, 0);
}

/**
 * @brief Acquire a runtime lifetime pin for one external callback.
 *
 * The double-check around the atomic increment closes the race with cleanup:
 * a callback either owns a pin before quiesce or immediately releases the
 * speculative pin and exits without touching runtime-owned objects.
 *
 * @param runtime Runtime instance.
 * @return RT_TRUE when the pin is held; RT_FALSE when callback admission closed.
 */
rt_bool_t
lely_rtt_callback_acquire(struct lely_rtt_runtime *runtime)
{
    if (!runtime || rt_atomic_load(&runtime->callback_quiescing))
        return RT_FALSE;

    rt_atomic_add(&runtime->callback_refs, 1);

    /*
     * Close the check/increment race with owner cleanup. If quiesce started
     * between the first check and the increment, drop the speculative pin and
     * let the callback return without touching event/Lely state.
     */
    if (rt_atomic_load(&runtime->callback_quiescing)) {
        lely_rtt_callback_release(runtime);
        return RT_FALSE;
    }

    return RT_TRUE;
}

/**
 * @brief Release one external-callback lifetime pin.
 * @param runtime Runtime instance.
 */
void
lely_rtt_callback_release(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    rt_atomic_sub(&runtime->callback_refs, 1);
}

/**
 * @brief Close callback admission before hardware producers are detached.
 * @param runtime Runtime instance.
 */
void
lely_rtt_callbacks_quiesce_begin(struct lely_rtt_runtime *runtime)
{
    if (runtime)
        rt_atomic_store(&runtime->callback_quiescing, 1);
}

/**
 * @brief Wait cooperatively for all callbacks admitted before quiesce to exit.
 *
 * The owner uses a 1 ms RT-Thread delay between atomic refcount samples. This
 * intentionally avoids both a tight busy loop and the completion dependency
 * which this revision removes. The external stop() caller remains bounded by
 * stop_timeout_ms and must not destroy runtime storage until EXIT is observed.
 *
 * @param runtime Runtime instance.
 */
void
lely_rtt_callbacks_wait_idle(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    /*
     * A callback already admitted before quiesce may still be executing in
     * ISR/driver/timer context. Sleep rather than yield/busy-spin while waiting
     * for its lifetime pin to drain. This wait is intentionally ownership-safe:
     * the external stop() caller has its own bounded EXIT wait and may time out
     * without freeing runtime storage if a callback is unexpectedly stuck.
     */
    while (rt_atomic_load(&runtime->callback_refs) != 0)
        rt_thread_mdelay(1);
}

/**
 * @brief Destroy owner-thread resources in reverse dependency order.
 *
 * External callback admission is closed first; CAN/timer producers are then
 * detached/stopped and the owner waits for already-pinned callback bodies to
 * return without busy-spinning. Only then can Lely objects be released.
 *
 * @param runtime Runtime instance owned by the current owner thread.
 */
static void
lely_rtt_owner_cleanup(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    /*
     * Freeze the external-callback admission boundary before detaching hardware
     * producers. A callback that raced this transition either holds a ref and
     * is waited below, or observes quiescing and returns without using runtime.
     */
    lely_rtt_callbacks_quiesce_begin(runtime);
    lely_rtt_can_quiesce(runtime);

    if (runtime->deadline_timer_initialized)
        rt_timer_stop(&runtime->deadline_timer);

    /*
     * CAN unregister/timer stop do not prove an already-entered callback has
     * returned. The reference-count drain establishes that lifetime guarantee.
     */
    lely_rtt_callbacks_wait_idle(runtime);

    /* io_ctx_shutdown() may enqueue cancellation/completion work for the loop. */
    if (runtime->ctx)
        io_ctx_shutdown(runtime->ctx);
    lely_rtt_drain_loop(runtime);

    if (runtime->can_net) {
        io_can_net_destroy(runtime->can_net);
        runtime->can_net = RT_NULL;
    }

    lely_rtt_can_fini(runtime);
    lely_rtt_timer_fini(runtime);

    if (runtime->loop) {
        lely_rtt_drain_loop(runtime);
        ev_loop_destroy(runtime->loop);
        runtime->loop = RT_NULL;
    }

    if (runtime->ctx) {
        io_ctx_destroy(runtime->ctx);
        runtime->ctx = RT_NULL;
    }
}

/**
 * @brief Construct the Lely context, loop, passive timer, CAN channel and network.
 *
 * This function is called only by the owner thread. Publishing READY before it
 * finishes would allow the caller to use a partially initialized runtime.
 *
 * @param runtime Runtime instance.
 * @return RT_EOK on success or the first initialization error.
 */
static rt_err_t
lely_rtt_owner_init(struct lely_rtt_runtime *runtime)
{
    rt_err_t err;

    LELY_RTT_LOG_D("owner init begin: can=%s bitrate=%u",
            runtime->config.can_name, (unsigned int)runtime->config.can_bitrate);

    runtime->ctx = io_ctx_create();
    if (!runtime->ctx) {
        LELY_RTT_LOG_E("io_ctx_create failed");
        return -RT_ERROR;
    }

    runtime->loop = ev_loop_create(RT_NULL, 0, 0);
    if (!runtime->loop) {
        LELY_RTT_LOG_E("ev_loop_create failed");
        return -RT_ERROR;
    }

    /* Reset callback lifetime state before CAN and timer producers are published. */
    lely_rtt_callbacks_init(runtime);

    err = lely_rtt_timer_init(runtime);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("timer init failed: %d", err);
        return err;
    }

    err = lely_rtt_can_init(runtime);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("CAN init failed: %d", err);
        return err;
    }

    runtime->can_net = io_can_net_create(ev_loop_get_exec(runtime->loop),
            runtime->timer, runtime->can_chan, 0, -1);
    if (!runtime->can_net) {
        LELY_RTT_LOG_E("io_can_net_create failed");
        return -RT_ERROR;
    }

    io_can_net_start(runtime->can_net);

    /*
     * rt_device_set_rx_indicate() only installs a callback; it does not replay
     * an indication for frames already buffered while CAN/filter setup ran.
     * Probe the FIFO once after io_can_net is ready so startup traffic cannot
     * remain stranded until an unrelated later frame arrives.
     */
    lely_rtt_can_drain_rx(runtime);

    /* Seed timer/status state before READY so the runtime starts coherently. */
    lely_rtt_timer_advance(runtime);
    if (runtime->runtime_error != RT_EOK)
        return runtime->runtime_error;
    lely_rtt_can_process_status(runtime);
    lely_rtt_drain_loop(runtime);

    LELY_RTT_LOG_I("owner ready: can=%s", runtime->config.can_name);
    return RT_EOK;
}

/**
 * @brief Owner thread entry and only dispatcher allowed to execute Lely work.
 *
 * The work loop intentionally waits forever: RX, timer, CAN-status and STOP
 * are all explicit wake sources, so periodic polling would only waste CPU.
 * Lifecycle callers do not wait forever; start()/stop() use bounded waits for
 * READY/EXIT acknowledgements.
 *
 * @param parameter Pointer to struct lely_rtt_runtime.
 */
static void
lely_rtt_owner_entry(void *parameter)
{
    struct lely_rtt_runtime *runtime = parameter;
    rt_uint32_t events;
    rt_err_t err;

    runtime->init_result = lely_rtt_owner_init(runtime);
    if (runtime->init_result != RT_EOK) {
        LELY_RTT_LOG_E("owner initialization failed: %d", runtime->init_result);
        lely_rtt_owner_cleanup(runtime);

        /* READY publishes init_result; EXIT proves cleanup has completed. */
        rt_event_send(&runtime->event,
                LELY_RTT_EVENT_READY | LELY_RTT_EVENT_EXIT);
        return;
    }

    runtime->running = RT_TRUE;
    rt_event_send(&runtime->event, LELY_RTT_EVENT_READY);

    for (;;) {
        events = 0;
        err = rt_event_recv(&runtime->event, LELY_RTT_EVENT_OWNER_MASK,
                RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                RT_WAITING_FOREVER, &events);
        if (err != RT_EOK) {
            /* A permanent IPC error must terminate instead of busy-looping. */
            LELY_RTT_LOG_E("owner event receive failed: %d", err);
            lely_rtt_latch_error(runtime, err);
            break;
        }

        if (events & LELY_RTT_EVENT_STOP)
            break;

        if (events & LELY_RTT_EVENT_RX_READY)
            lely_rtt_can_drain_rx(runtime);
        if (events & LELY_RTT_EVENT_CAN_STATUS)
            lely_rtt_can_process_status(runtime);
        if (events & LELY_RTT_EVENT_TIMER_DUE)
            lely_rtt_timer_advance(runtime);

        /*
         * BSPs without status indications are sampled opportunistically after
         * real owner wakeups. This avoids a second periodic polling thread.
         */
        if (!runtime->config.use_status_indication
                && (events & (LELY_RTT_EVENT_RX_READY | LELY_RTT_EVENT_TIMER_DUE)))
            lely_rtt_can_process_status(runtime);

        lely_rtt_drain_loop(runtime);
    }

    LELY_RTT_LOG_I("owner stopping");
    runtime->running = RT_FALSE;
    lely_rtt_owner_cleanup(runtime);

    /* EXIT is the final runtime access made by the owner thread. */
    rt_event_send(&runtime->event, LELY_RTT_EVENT_EXIT);
}

/**
 * @brief Validate required resources, CAN options and lifecycle timeout inputs.
 * @param config Runtime configuration supplied by the application.
 * @return RT_EOK when valid or -RT_EINVAL for an unsupported combination.
 */
static rt_err_t
lely_rtt_validate_config(const struct lely_rtt_runtime_config *config)
{
    if (!config || !config->can_name || !config->can_name[0])
        return -RT_EINVAL;

    if (!config->can_bitrate || !config->rx_batch || !config->thread_stack_size
            || !config->thread_timeslice || !config->start_timeout_ms
            || !config->stop_timeout_ms)
        return -RT_EINVAL;

    if (config->can_brs && !config->can_fd)
        return -RT_EINVAL;
    if (config->thread_priority >= RT_THREAD_PRIORITY_MAX)
        return -RT_EINVAL;
    if (config->can_fd_len_mode != LELY_RTT_CANFD_LEN_BYTES
            && config->can_fd_len_mode != LELY_RTT_CANFD_LEN_DLC)
        return -RT_EINVAL;

#ifndef PKG_LELY_USING_CANFD
    if (config->can_fd || config->can_brs)
        return -RT_EINVAL;
#endif /* PKG_LELY_USING_CANFD */

    return RT_EOK;
}

/**
 * @brief Consume stale event bits left by a previous completed or failed start.
 *
 * The event object intentionally persists for the lifetime of the runtime, so
 * a restart must begin from an empty event state. A zero-time receive is used
 * only for draining; it never blocks the caller.
 *
 * @param runtime Runtime instance containing the persistent event object.
 */
static void
lely_rtt_clear_events(struct lely_rtt_runtime *runtime)
{
    const rt_uint32_t mask = LELY_RTT_EVENT_OWNER_MASK
            | LELY_RTT_EVENT_READY | LELY_RTT_EVENT_EXIT;
    rt_uint32_t events;

    do {
        events = 0;
    } while (rt_event_recv(&runtime->event, mask,
                    RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                    RT_WAITING_NO, &events) == RT_EOK);
}

/**
 * @brief Allocate a stopped runtime and initialize its persistent event object.
 * @param config Validated runtime configuration copied into the new instance.
 * @return Runtime handle or RT_NULL on validation/allocation/event-init failure.
 */
lely_rtt_runtime_t *
lely_rtt_runtime_create(const struct lely_rtt_runtime_config *config)
{
    struct lely_rtt_runtime *runtime;

    lely_rtt_log_init();

    if (lely_rtt_validate_config(config) != RT_EOK) {
        LELY_RTT_LOG_E("runtime create rejected invalid configuration");
        return RT_NULL;
    }

    runtime = rt_calloc(1, sizeof(*runtime));
    if (!runtime) {
        LELY_RTT_LOG_E("runtime allocation failed");
        return RT_NULL;
    }

    /* Configuration is immutable after create; referenced strings remain caller-owned. */
    runtime->config = *config;

    if (rt_event_init(&runtime->event, "lelyevt", RT_IPC_FLAG_FIFO) != RT_EOK) {
        LELY_RTT_LOG_E("runtime event initialization failed");
        rt_free(runtime);
        return RT_NULL;
    }

    runtime->event_initialized = RT_TRUE;
    LELY_RTT_LOG_D("runtime created: can=%s", runtime->config.can_name);
    return runtime;
}

/**
 * @brief Start the owner thread and wait for the bounded READY handshake.
 * @param runtime Stopped runtime instance.
 * @return RT_EOK, a bounded wait error, or the owner initialization error.
 */
rt_err_t
lely_rtt_runtime_start(lely_rtt_runtime_t *runtime)
{
    rt_uint32_t events = 0;
    rt_int32_t start_ticks;
    rt_int32_t stop_ticks;
    rt_err_t err;

    if (!runtime || !runtime->event_initialized || runtime->owner_thread) {
        LELY_RTT_LOG_W("runtime start rejected: invalid state");
        return -RT_EINVAL;
    }

    lely_rtt_clear_events(runtime);

    runtime->init_result = -RT_ERROR;
    runtime->runtime_error = RT_EOK;
    runtime->running = RT_FALSE;

    runtime->owner_thread = rt_thread_create("lelyown", lely_rtt_owner_entry,
            runtime, runtime->config.thread_stack_size,
            runtime->config.thread_priority, runtime->config.thread_timeslice);
    if (!runtime->owner_thread) {
        LELY_RTT_LOG_E("owner thread creation failed");
        return -RT_ENOMEM;
    }

    err = rt_thread_startup(runtime->owner_thread);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("owner thread startup failed: %d", err);
        rt_thread_delete(runtime->owner_thread);
        runtime->owner_thread = RT_NULL;
        return err;
    }

    start_ticks = lely_rtt_timeout_ticks(runtime->config.start_timeout_ms);
    err = rt_event_recv(&runtime->event, LELY_RTT_EVENT_READY,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
            start_ticks, &events);
    if (err != RT_EOK) {
        /*
         * Initialization may still be running. Latch STOP so a later successful
         * init enters cleanup immediately; ownership stays non-NULL until EXIT.
         */
        LELY_RTT_LOG_E("runtime READY wait failed: %d", err);
        rt_event_send(&runtime->event, LELY_RTT_EVENT_STOP);
        return err;
    }

    if (runtime->init_result == RT_EOK) {
        LELY_RTT_LOG_I("runtime started");
        return RT_EOK;
    }

    /* Failed initialization already entered owner cleanup; wait only a bounded time. */
    stop_ticks = lely_rtt_timeout_ticks(runtime->config.stop_timeout_ms);
    err = rt_event_recv(&runtime->event, LELY_RTT_EVENT_EXIT,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
            stop_ticks, &events);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("runtime startup cleanup wait failed: %d", err);
        return err;
    }

    runtime->owner_thread = RT_NULL;
    LELY_RTT_LOG_E("runtime start failed: %d", runtime->init_result);
    return runtime->init_result;
}

/**
 * @brief Request STOP and wait for the bounded EXIT handshake.
 * @param runtime Started or stopping runtime instance.
 * @return RT_EOK, -RT_EINVAL, a bounded wait error, or a latched runtime error.
 */
rt_err_t
lely_rtt_runtime_stop(lely_rtt_runtime_t *runtime)
{
    rt_uint32_t events = 0;
    rt_int32_t stop_ticks;
    rt_err_t err;

    if (!runtime || !runtime->event_initialized) {
        LELY_RTT_LOG_W("runtime stop rejected: invalid state");
        return -RT_EINVAL;
    }
    if (!runtime->owner_thread) {
        LELY_RTT_LOG_D("runtime stop: already stopped");
        return RT_EOK;
    }
    if (rt_thread_self() == runtime->owner_thread) {
        LELY_RTT_LOG_E("runtime stop rejected from owner thread");
        return -RT_EINVAL;
    }

    err = rt_event_send(&runtime->event, LELY_RTT_EVENT_STOP);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("runtime STOP event send failed: %d", err);
        return err;
    }

    stop_ticks = lely_rtt_timeout_ticks(runtime->config.stop_timeout_ms);
    err = rt_event_recv(&runtime->event, LELY_RTT_EVENT_EXIT,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
            stop_ticks, &events);
    if (err != RT_EOK) {
        /*
         * Timeout is fail-safe: owner/runtime storage remains intact. The caller
         * may invoke stop() again after the owner finishes the outstanding work.
         */
        LELY_RTT_LOG_E("runtime EXIT wait failed: %d", err);
        return err;
    }

    /* Dynamic RT-Thread thread memory is reclaimed later by the defunct path. */
    runtime->owner_thread = RT_NULL;
    if (runtime->runtime_error != RT_EOK)
        LELY_RTT_LOG_E("runtime stopped with error: %d", runtime->runtime_error);
    else
        LELY_RTT_LOG_I("runtime stopped");
    return runtime->runtime_error;
}

/**
 * @brief Detach the persistent event object and free a fully stopped runtime.
 * @param runtime Stopped runtime instance; RT_NULL is accepted.
 */
void
lely_rtt_runtime_destroy(lely_rtt_runtime_t *runtime)
{
    if (!runtime)
        return;
    if (runtime->owner_thread) {
        LELY_RTT_LOG_W("runtime destroy ignored while owner is active");
        return;
    }

    if (runtime->event_initialized) {
        rt_event_detach(&runtime->event);
        runtime->event_initialized = RT_FALSE;
    }

    LELY_RTT_LOG_D("runtime destroyed");
    rt_free(runtime);
}
