/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 * 2026-09-04     wdfk-prog         add owner CANopen node and command ingress
 * 2026-09-05     wdfk-prog         correct B4 role to a local NMT Master
 * 2026-09-05     wdfk-prog         integrate Master command and SDO ingress
 * 2026-09-06     wdfk-prog         sync passive timer clock before owner work
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


#if defined(PKG_LELY_USING_MASTER_COMMAND)
/** @brief Low-rate fallback that bounds a lost Master-command wakeup. */
#define LELY_RTT_COMMAND_SAFETY_POLL_MS 1000u
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

/** @brief Packed remote-state current-state field mask. */
#define LELY_RTT_REMOTE_STATE_CURRENT_MASK 0x000000ffu
/** @brief Packed remote-state last-observed-state field shift. */
#define LELY_RTT_REMOTE_STATE_LAST_SHIFT 8u
/** @brief Packed remote-state heartbeat-timeout flag. */
#define LELY_RTT_REMOTE_STATE_TIMEOUT 0x00010000u
/** @brief Packed boot-result valid flag. */
#define LELY_RTT_REMOTE_BOOT_VALID 0x00010000u
/** @brief Packed boot-result error-status field shift. */
#define LELY_RTT_REMOTE_BOOT_ERROR_SHIFT 8u

/**
 * @brief Pack one remote NMT state snapshot into an atomic word.
 * @param current State currently published to readers.
 * @param last Last state observed from a state indication.
 * @param timed_out RT_TRUE while heartbeat monitoring reports a timeout.
 * @return Packed snapshot value.
 */
static rt_atomic_t
lely_rtt_remote_state_pack(rt_uint8_t current, rt_uint8_t last,
        rt_bool_t timed_out)
{
    rt_uint32_t value = (rt_uint32_t)current
            | ((rt_uint32_t)last << LELY_RTT_REMOTE_STATE_LAST_SHIFT);

    if (timed_out)
        value |= LELY_RTT_REMOTE_STATE_TIMEOUT;
    return (rt_atomic_t)value;
}

/**
 * @brief Reset all application-visible CANopen snapshots for a new owner run.
 * @param runtime Runtime instance.
 */
static void
lely_rtt_master_snapshots_reset(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    rt_atomic_store(&runtime->local_node_id, 0);
    rt_atomic_store(&runtime->local_nmt_state,
            LELY_RTT_NMT_STATE_UNAVAILABLE);
    for (id = 0; id <= CO_NUM_NODES; id++) {
        rt_atomic_store(&runtime->remote_nmt_state[id],
                lely_rtt_remote_state_pack(LELY_RTT_NMT_STATE_UNAVAILABLE,
                        LELY_RTT_NMT_STATE_UNAVAILABLE, RT_FALSE));
        rt_atomic_store(&runtime->remote_boot_result[id], 0);
    }
}

/**
 * @brief Preserve Lely state processing and publish local/remote state snapshots.
 *
 * The callback executes only in the owner thread. co_nmt_set_st_ind() replaces
 * Lely's default handler, so this wrapper always chains through co_nmt_on_st()
 * before publishing snapshots. For a remote Boot-up, an application-owned
 * default CSDO is retired immediately before that call so Lely NMT boot can
 * create its own default CSDO without a duplicate receiver.
 *
 * @param nmt Local Master NMT service.
 * @param id Node-ID reported by the NMT service.
 * @param st New NMT state.
 * @param data Runtime instance owning the NMT service.
 */
static void
lely_rtt_master_state_ind(co_nmt_t *nmt, co_unsigned8_t id,
        co_unsigned8_t st, void *data)
{
    struct lely_rtt_runtime *runtime = data;
    const rt_uint8_t state = st & ~CO_NMT_ST_TOGGLE;

    if (!nmt)
        return;

#if defined(PKG_LELY_USING_MASTER_SDO)
    /*
     * co_nmt_on_st() can synchronously start NMT boot on remote Boot-up.
     * M2 therefore performs one narrow pre-chain arbitration step: retire only
     * the application-owned default CSDO before Lely creates its own default
     * CSDO on the same CiA 301 response COB-ID. This does not process or publish
     * the NMT state; co_nmt_on_st() remains the NMT state-machine handler below.
     */
    if (runtime && id && id <= CO_NUM_NODES
            && id != co_nmt_get_id(nmt) && state == CO_NMT_ST_BOOTUP)
        lely_rtt_master_sdo_before_boot(runtime, id);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

    co_nmt_on_st(nmt, id, st);

    if (!runtime)
        return;

    if (id == co_nmt_get_id(nmt)) {
        rt_atomic_store(&runtime->local_nmt_state, state);
        return;
    }
    if (!id || id > CO_NUM_NODES)
        return;

#if defined(PKG_LELY_USING_MASTER_SDO)
    /* Apply the post-NMT state gate after Lely default processing. */
    lely_rtt_master_sdo_on_nmt_state(runtime, id, state);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

    rt_atomic_store(&runtime->remote_nmt_state[id],
            lely_rtt_remote_state_pack(state, state, RT_FALSE));
    if (state == CO_NMT_ST_BOOTUP)
        rt_atomic_store(&runtime->remote_boot_result[id], 0);
}

/**
 * @brief Preserve Lely heartbeat handling and publish timeout/recovery state.
 *
 * A timeout makes the public remote-state snapshot temporarily unavailable.
 * The last state observed by lely_rtt_master_state_ind() is retained in the
 * same atomic word and restored when heartbeat monitoring reports recovery.
 * No Lely object is exposed to the application thread.
 *
 * @param nmt Local Master NMT service.
 * @param id Remote Node-ID.
 * @param state Heartbeat event occurrence/resolution state.
 * @param reason Heartbeat event reason.
 * @param data Runtime instance owning the NMT service.
 */
static void
lely_rtt_master_hb_ind(co_nmt_t *nmt, co_unsigned8_t id, int state,
        int reason, void *data)
{
    struct lely_rtt_runtime *runtime = data;
    rt_uint32_t snapshot;
    rt_uint8_t last;

    if (!nmt)
        return;

    co_nmt_on_hb(nmt, id, state, reason);

    if (!runtime || !id || id > CO_NUM_NODES
            || reason != CO_NMT_EC_TIMEOUT)
        return;

    snapshot = (rt_uint32_t)rt_atomic_load(&runtime->remote_nmt_state[id]);
    last = (rt_uint8_t)(snapshot >> LELY_RTT_REMOTE_STATE_LAST_SHIFT);

    if (state == CO_NMT_EC_OCCURRED) {
        rt_atomic_store(&runtime->remote_nmt_state[id],
                lely_rtt_remote_state_pack(LELY_RTT_NMT_STATE_UNAVAILABLE,
                        last, RT_TRUE));
    } else if (state == CO_NMT_EC_RESOLVED
            && (snapshot & LELY_RTT_REMOTE_STATE_TIMEOUT)) {
        rt_atomic_store(&runtime->remote_nmt_state[id],
                lely_rtt_remote_state_pack(last, last, RT_FALSE));
    }
}

#if !LELY_NO_CO_NMT_BOOT
/**
 * @brief Publish the completed NMT boot result for a remote slave.
 * @param nmt Local Master NMT service.
 * @param id Remote Node-ID.
 * @param st State reported by the completed boot process.
 * @param es Lely boot error status, or 0 on success.
 * @param data Runtime instance owning the NMT service.
 */
static void
lely_rtt_master_boot_ind(co_nmt_t *nmt, co_unsigned8_t id,
        co_unsigned8_t st, char es, void *data)
{
    struct lely_rtt_runtime *runtime = data;
    rt_uint32_t result;

    (void)nmt;
    if (!runtime || !id || id > CO_NUM_NODES)
        return;

#if defined(PKG_LELY_USING_MASTER_SDO)
    lely_rtt_master_sdo_on_boot_complete(runtime, id,
            st & ~CO_NMT_ST_TOGGLE);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

    result = LELY_RTT_REMOTE_BOOT_VALID
            | ((rt_uint32_t)(rt_uint8_t)es << LELY_RTT_REMOTE_BOOT_ERROR_SHIFT)
            | (rt_uint32_t)(st & ~CO_NMT_ST_TOGGLE);
    rt_atomic_store(&runtime->remote_boot_result[id], (rt_atomic_t)result);
}
#endif /* !LELY_NO_CO_NMT_BOOT */

/**
 * @brief Create the optional static CANopen Master inside the owner thread.
 *
 * The target has LELY_NO_CO_DCF=1, so the local Master object dictionary must
 * already be a static struct co_sdev generated on the host. Remote Node1 DCF
 * data belongs to that host generation chain and is never instantiated as the
 * MCU's local CANopen device.
 *
 * After the local reset, co_nmt_is_master() is a hard role guard. A wrong OD
 * must fail startup rather than silently running the MCU as a CANopen slave.
 *
 * @param runtime Runtime instance with a started io_can_net.
 * @return RT_EOK when no Master is requested or creation succeeds; otherwise
 *         an RT-Thread error used to abort runtime startup.
 */
static rt_err_t
lely_rtt_master_init(struct lely_rtt_runtime *runtime)
{
    if (!runtime || !runtime->master_sdev)
        return RT_EOK;

    lely_rtt_master_snapshots_reset(runtime);

    runtime->master_dev = co_dev_create_from_sdev(runtime->master_sdev);
    if (!runtime->master_dev) {
        LELY_RTT_LOG_E("CANopen Master static device creation failed");
        return -RT_ERROR;
    }

    runtime->master_nmt = co_nmt_create(
            io_can_net_get_net(runtime->can_net), runtime->master_dev);
    if (!runtime->master_nmt) {
        LELY_RTT_LOG_E("CANopen Master NMT creation failed");
        return -RT_ERROR;
    }

    co_nmt_set_st_ind(runtime->master_nmt,
            &lely_rtt_master_state_ind, runtime);
    co_nmt_set_hb_ind(runtime->master_nmt,
            &lely_rtt_master_hb_ind, runtime);
#if !LELY_NO_CO_NMT_BOOT
    co_nmt_set_boot_ind(runtime->master_nmt,
            &lely_rtt_master_boot_ind, runtime);
#endif /* !LELY_NO_CO_NMT_BOOT */

    if (co_nmt_cs_ind(runtime->master_nmt, CO_NMT_CS_RESET_NODE) == -1) {
        LELY_RTT_LOG_E("CANopen Master local reset-node failed");
        return -RT_ERROR;
    }
    if (!co_nmt_is_master(runtime->master_nmt)) {
        LELY_RTT_LOG_E("configured local CANopen device is not an NMT Master");
        return -RT_ERROR;
    }

    rt_atomic_store(&runtime->local_node_id, co_nmt_get_id(runtime->master_nmt));
    rt_atomic_store(&runtime->local_nmt_state,
            co_nmt_get_st(runtime->master_nmt) & ~CO_NMT_ST_TOGGLE);
    LELY_RTT_LOG_I("CANopen Master ready: id=%u state=0x%02x",
            (unsigned int)co_nmt_get_id(runtime->master_nmt),
            (unsigned int)co_nmt_get_st(runtime->master_nmt));
    return RT_EOK;
}

/**
 * @brief Destroy the optional local Master before its CAN network disappears.
 * @param runtime Runtime instance owned by the current owner thread.
 */
static void
lely_rtt_master_fini(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    if (runtime->master_nmt) {
        co_nmt_destroy(runtime->master_nmt);
        runtime->master_nmt = RT_NULL;
    }
    if (runtime->master_dev) {
        co_dev_destroy(runtime->master_dev);
        runtime->master_dev = RT_NULL;
    }

    lely_rtt_master_snapshots_reset(runtime);
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
 * Cross-thread command and external callback admission are closed before any
 * owner-owned CANopen object can disappear. Queued/active application SDO work
 * is canceled while CAN is still valid; CAN/timer producers are then detached
 * and already-pinned callbacks are drained before the Master is released.
 *
 * @param runtime Runtime instance owned by the current owner thread.
 */
static void
lely_rtt_owner_cleanup(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    /*
     * Close cross-thread producers before owner-owned CANopen objects disappear.
     * Command posters hold a lifetime pin around queue access, matching the
     * callback admission pattern used by CAN/timer callbacks.
     */
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    lely_rtt_master_command_quiesce_begin(runtime);
    lely_rtt_master_command_wait_idle(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
    lely_rtt_callbacks_quiesce_begin(runtime);

#if defined(PKG_LELY_USING_MASTER_COMMAND)
    /* Queued SDO requests are canceled and active Client-SDOs stop before CAN. */
    lely_rtt_master_command_fini(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

    lely_rtt_can_quiesce(runtime);
    if (runtime->deadline_timer_initialized)
        rt_timer_stop(&runtime->deadline_timer);

    /*
     * CAN unregister/timer stop do not prove an already-entered callback has
     * returned. The reference-count drain establishes that lifetime guarantee.
     */
    lely_rtt_callbacks_wait_idle(runtime);

    /* The Master owns CAN receivers/timers attached to can_net, so release it first. */
    lely_rtt_master_fini(runtime);
    lely_rtt_drain_loop(runtime);

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

    err = lely_rtt_master_init(runtime);
    if (err != RT_EOK)
        return err;

#if defined(PKG_LELY_USING_MASTER_COMMAND)
    err = lely_rtt_master_command_init(runtime);
    if (err != RT_EOK)
        return err;
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

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
 * RX, timer, CAN-status and STOP normally wake the owner explicitly. With
 * Master command ingress enabled, every owner iteration drains one bounded
 * command batch so unrelated traffic cannot starve an already-enqueued
 * command after a lost COMMAND wake. The low-rate timed wait only guarantees
 * progress when the runtime is otherwise completely idle. Lifecycle callers
 * still use bounded READY/EXIT waits.
 *
 * @param parameter Pointer to struct lely_rtt_runtime.
 */
static void
lely_rtt_owner_entry(void *parameter)
{
    struct lely_rtt_runtime *runtime = parameter;
    rt_uint32_t events;
    rt_err_t err;
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    const rt_int32_t command_poll_ticks =
            lely_rtt_timeout_ticks(LELY_RTT_COMMAND_SAFETY_POLL_MS);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

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
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    lely_rtt_master_command_admission_open(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
    rt_event_send(&runtime->event, LELY_RTT_EVENT_READY);

    for (;;) {
        events = 0;
#if defined(PKG_LELY_USING_MASTER_COMMAND)
        err = rt_event_recv(&runtime->event, LELY_RTT_EVENT_OWNER_MASK,
                RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                command_poll_ticks, &events);
        if (err == -RT_ETIMEOUT)
            err = RT_EOK;
#else
        err = rt_event_recv(&runtime->event, LELY_RTT_EVENT_OWNER_MASK,
                RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                RT_WAITING_FOREVER, &events);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
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

        /*
         * RX/status injection above only queues Lely executor work. Synchronize
         * the passive clock after those inputs are queued but before command
         * dispatch or ev_loop execution. Otherwise a wake after a long idle can
         * create a relative deadline from stale Lely time while the RT timer
         * bridge compares it with current uptime, collapsing the timeout.
         */
        lely_rtt_timer_advance(runtime);
#if defined(PKG_LELY_USING_MASTER_COMMAND)
        /*
         * Drain one bounded batch on every owner iteration. COMMAND still gives
         * immediate wakeup, while unrelated RX/timer/status traffic can no
         * longer starve a queued command whose wake event was lost.
         */
        lely_rtt_master_command_dispatch(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

        /*
         * BSPs without status indications are sampled opportunistically after
         * real owner wakeups. This avoids a second periodic polling thread.
         */
        if (!runtime->config.use_status_indication
                && (events & (LELY_RTT_EVENT_RX_READY | LELY_RTT_EVENT_TIMER_DUE)))
            lely_rtt_can_process_status(runtime);

        lely_rtt_drain_loop(runtime);
#if defined(PKG_LELY_USING_MASTER_SDO)
        lely_rtt_master_sdo_reap(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
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
    lely_rtt_master_snapshots_reset(runtime);

    LELY_RTT_LOG_D("runtime created: can=%s", runtime->config.can_name);
    return runtime;
}

/**
 * @brief Bind one static local Master device before owner startup.
 */
rt_err_t
lely_rtt_runtime_configure_master(lely_rtt_runtime_t *runtime,
        const struct co_sdev *master_sdev)
{
    if (!runtime || !runtime->event_initialized || runtime->owner_thread
            || !master_sdev)
        return -RT_EINVAL;
    if (runtime->master_sdev)
        return -RT_EBUSY;

    runtime->master_sdev = master_sdev;
    return RT_EOK;
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
    lely_rtt_master_snapshots_reset(runtime);
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    lely_rtt_master_command_prepare(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
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
#if defined(PKG_LELY_USING_MASTER_COMMAND)
        lely_rtt_master_command_quiesce_begin(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
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

#if defined(PKG_LELY_USING_MASTER_COMMAND)
    lely_rtt_master_command_quiesce_begin(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
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
 * @brief Read the owner-published local Master NMT state cache.
 */
rt_err_t
lely_rtt_runtime_get_local_nmt_state(lely_rtt_runtime_t *runtime,
        rt_uint8_t *state)
{
    rt_atomic_t value;

    if (!runtime || !state)
        return -RT_EINVAL;

    value = rt_atomic_load(&runtime->local_nmt_state);
    if (value == LELY_RTT_NMT_STATE_UNAVAILABLE)
        return -RT_EBUSY;

    *state = (rt_uint8_t)value;
    return RT_EOK;
}

/**
 * @brief Preserve the pre-B4 state getter as an alias to the local Master state.
 */
rt_err_t
lely_rtt_runtime_get_nmt_state(lely_rtt_runtime_t *runtime,
        rt_uint8_t *state)
{
    return lely_rtt_runtime_get_local_nmt_state(runtime, state);
}

/**
 * @brief Read one owner-published remote NMT state snapshot.
 */
rt_err_t
lely_rtt_runtime_get_remote_nmt_state(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint8_t *state)
{
    rt_uint32_t snapshot;
    rt_uint8_t current;

    if (!runtime || !state || !node_id || node_id > CO_NUM_NODES)
        return -RT_EINVAL;

    snapshot = (rt_uint32_t)rt_atomic_load(&runtime->remote_nmt_state[node_id]);
    current = (rt_uint8_t)(snapshot & LELY_RTT_REMOTE_STATE_CURRENT_MASK);
    if (current == LELY_RTT_NMT_STATE_UNAVAILABLE)
        return -RT_EBUSY;

    *state = current;
    return RT_EOK;
}

/**
 * @brief Read one owner-published completed NMT boot result snapshot.
 */
rt_err_t
lely_rtt_runtime_get_remote_boot_status(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint8_t *state, char *error_status)
{
    rt_uint32_t result;

    if (!runtime || !state || !error_status || !node_id
            || node_id > CO_NUM_NODES)
        return -RT_EINVAL;

    result = (rt_uint32_t)rt_atomic_load(&runtime->remote_boot_result[node_id]);
    if (!(result & LELY_RTT_REMOTE_BOOT_VALID))
        return -RT_EBUSY;

    *state = (rt_uint8_t)(result & LELY_RTT_REMOTE_STATE_CURRENT_MASK);
    *error_status = (char)(rt_uint8_t)(result >> LELY_RTT_REMOTE_BOOT_ERROR_SHIFT);
    return RT_EOK;
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

    runtime->master_sdev = RT_NULL;

    if (runtime->event_initialized) {
        rt_event_detach(&runtime->event);
        runtime->event_initialized = RT_FALSE;
    }

    LELY_RTT_LOG_D("runtime destroyed");
    rt_free(runtime);
}
