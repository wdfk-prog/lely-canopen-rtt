/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 */

/**
 * @file internal.h
 * @brief Internal RT-Thread state shared by the B3 runtime, timer and CAN adapters.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_INTERNAL_H_
#define LELY_RTT_INTERNAL_H_

#include <lely/ev/loop.h>
#include <lely/io2/can_net.h>
#include <lely/io2/ctx.h>
#include <lely/io2/user/can.h>
#include <lely/io2/user/timer.h>
#include <lely/rtthread/runtime.h>

#include "log.h"

#include <rtatomic.h>
#include <rtdevice.h>
#include <rtthread.h>

/** @brief CAN RX data is available in the RT-Thread CAN software FIFO. */
#define LELY_RTT_EVENT_RX_READY   (1u << 0)
/** @brief The RT one-shot deadline expired; owner must advance io_user_timer. */
#define LELY_RTT_EVENT_TIMER_DUE  (1u << 1)
/** @brief CAN controller status changed; owner must query it. */
#define LELY_RTT_EVENT_CAN_STATUS (1u << 2)
/** @brief Request orderly shutdown of the owner thread. */
#define LELY_RTT_EVENT_STOP       (1u << 3)

/**
 * @brief Owner initialization acknowledgement consumed by start().
 *
 * READY is not part of the owner work mask. It prevents
 * lely_rtt_runtime_start() from reporting success before the Lely context,
 * timer, CAN bridge and io_can_net have either initialized or failed.
 */
#define LELY_RTT_EVENT_READY      (1u << 4)

/**
 * @brief Owner cleanup acknowledgement consumed by start()/stop().
 *
 * EXIT is sent only after external callback admission is closed and the
 * owner has completed Lely/CAN/timer cleanup. The caller therefore never frees
 * runtime storage merely because STOP was requested.
 */
#define LELY_RTT_EVENT_EXIT       (1u << 5)

/**
 * @brief Event bits consumed by the owner work loop.
 *
 * READY and EXIT are deliberately excluded because they synchronize the
 * lifecycle caller rather than dispatch owner work.
 */
#define LELY_RTT_EVENT_OWNER_MASK \
    (LELY_RTT_EVENT_RX_READY | LELY_RTT_EVENT_TIMER_DUE | \
     LELY_RTT_EVENT_CAN_STATUS | LELY_RTT_EVENT_STOP)

/**
 * @brief Monotonic extension state for the wrapping RT-Thread tick counter.
 *
 * Only the Lely owner thread mutates this structure, so it deliberately has no
 * internal lock. The accumulated 64-bit value is independent of wall-clock or
 * RTC time and exists only to drive Lely's passive timer clock.
 */
struct lely_rtt_time_state {
    rt_tick_t last_tick;       /**< Last raw rt_tick_get() sample. */
    rt_uint64_t ticks;         /**< Accumulated monotonic tick count. */
    rt_bool_t initialized;     /**< RT_TRUE after the first valid tick sample. */
};

/**
 * @brief Complete mutable state owned by one RT-Thread Lely runtime instance.
 *
 * Lely objects in this structure are owner-thread-only. Cross-context CAN/timer
 * callbacks may only acquire/release the lifetime pin and send event bits; they
 * never dereference Lely EV/IO2/CANopen objects directly.
 */
struct lely_rtt_runtime {
    struct lely_rtt_runtime_config config; /**< Immutable copied configuration. */

    /** Link in the global CAN-device-to-runtime callback registry. */
    struct lely_rtt_runtime *registry_next;
    /** Shared owner-work and READY/EXIT lifecycle event object. */
    struct rt_event event;
    /** RT-Thread one-shot timer backing Lely io_user_timer deadlines. */
    struct rt_timer deadline_timer;

    /** Only RT-Thread thread allowed to execute Lely APIs. */
    rt_thread_t owner_thread;
    /** Open RT-Thread CAN device associated with config.can_name. */
    rt_device_t can_dev;

    io_ctx_t *ctx;             /**< Owner-thread Lely I/O context. */
    ev_loop_t *loop;           /**< Owner-thread Lely event loop/executor. */
    io_timer_t *timer;         /**< Passive Lely user timer. */
    io_can_chan_t *can_chan;   /**< Passive Lely user CAN channel. */
    io_can_net_t *can_net;     /**< Lely CAN network joining CAN and timer. */

    struct lely_rtt_time_state time; /**< Monotonic RT tick extension state. */
    struct rt_can_status last_can_status; /**< Last status accepted by Lely. */

    /** Number of CAN/timer callbacks currently holding a runtime lifetime pin. */
    rt_atomic_t callback_refs;
    /** Non-zero after cleanup closes admission for new external callbacks. */
    rt_atomic_t callback_quiescing;

    rt_err_t init_result;      /**< Owner initialization result published by READY. */
    rt_err_t runtime_error;    /**< First asynchronous runtime/shutdown error. */

    rt_bool_t event_initialized;          /**< event is initialized. */
    rt_bool_t deadline_timer_initialized; /**< deadline_timer is initialized. */
    rt_bool_t can_opened;                 /**< CAN device open succeeded. */
    rt_bool_t status_registered;          /**< CAN status indication registered. */
    rt_bool_t registry_registered;        /**< Runtime is in CAN callback registry. */
    rt_bool_t controller_started;         /**< RT_CAN_CMD_START succeeded. */
    rt_bool_t status_valid;               /**< last_can_status is valid. */
    rt_bool_t running;                    /**< Owner work loop is active. */
};

/**
 * @brief Reset external-callback lifetime state before producers are published.
 * @param runtime Runtime instance.
 */
void lely_rtt_callbacks_init(struct lely_rtt_runtime *runtime);

/**
 * @brief Try to pin runtime storage for one CAN/timer callback.
 * @param runtime Runtime instance.
 * @return RT_TRUE when the lifetime pin was acquired; RT_FALSE after quiesce.
 */
rt_bool_t lely_rtt_callback_acquire(struct lely_rtt_runtime *runtime);

/**
 * @brief Release one external-callback lifetime pin.
 * @param runtime Runtime instance.
 */
void lely_rtt_callback_release(struct lely_rtt_runtime *runtime);

/**
 * @brief Prevent new external callbacks from acquiring lifetime pins.
 * @param runtime Runtime instance.
 */
void lely_rtt_callbacks_quiesce_begin(struct lely_rtt_runtime *runtime);

/**
 * @brief Wait cooperatively until all already-pinned callbacks have returned.
 *
 * The owner sleeps for 1 ms between refcount checks. This avoids busy-spinning
 * while preserving the rule that runtime storage cannot be released until all
 * callbacks admitted before quiesce have exited.
 *
 * @param runtime Runtime instance.
 */
void lely_rtt_callbacks_wait_idle(struct lely_rtt_runtime *runtime);

/**
 * @brief Convert a bounded lifecycle timeout from milliseconds to RT ticks.
 * @param timeout_ms Positive timeout in milliseconds.
 * @return Positive RT-Thread tick count clamped below the half-range limit.
 */
rt_int32_t lely_rtt_timeout_ticks(rt_uint32_t timeout_ms);

/**
 * @brief Read the monotonic RT-Thread uptime used by Lely's passive clock.
 * @param runtime Runtime instance containing the wrap-extension state.
 * @param tp Output normalized monotonic time.
 * @return RT_EOK on success or -RT_EINVAL for invalid arguments.
 */
rt_err_t lely_rtt_time_now(struct lely_rtt_runtime *runtime,
        struct timespec *tp);

/**
 * @brief Create the passive Lely timer and RT one-shot timer bridge.
 * @param runtime Runtime instance owned by the caller thread.
 * @return RT_EOK on success or an RT-Thread/Lely initialization error.
 */
rt_err_t lely_rtt_timer_init(struct lely_rtt_runtime *runtime);

/**
 * @brief Stop/detach and destroy timer bridge resources.
 * @param runtime Runtime instance.
 */
void lely_rtt_timer_fini(struct lely_rtt_runtime *runtime);

/**
 * @brief Advance the passive Lely timer clock from the current RT tick sample.
 * @param runtime Runtime instance; this function is owner-thread-only.
 */
void lely_rtt_timer_advance(struct lely_rtt_runtime *runtime);

/**
 * @brief Open/configure RT CAN and create the passive Lely user CAN channel.
 * @param runtime Runtime instance; this function is owner-thread-only.
 * @return RT_EOK on success or an RT-Thread/Lely initialization error.
 */
rt_err_t lely_rtt_can_init(struct lely_rtt_runtime *runtime);

/**
 * @brief Detach CAN callback producers and remove the callback registry entry.
 * @param runtime Runtime instance.
 */
void lely_rtt_can_quiesce(struct lely_rtt_runtime *runtime);

/**
 * @brief Destroy the passive CAN channel and close the RT CAN device.
 * @param runtime Runtime instance.
 */
void lely_rtt_can_fini(struct lely_rtt_runtime *runtime);

/**
 * @brief Drain one bounded RX batch and inject valid frames into io_user_can.
 * @param runtime Runtime instance; this function is owner-thread-only.
 */
void lely_rtt_can_drain_rx(struct lely_rtt_runtime *runtime);

/**
 * @brief Query/map RT CAN status and inject accepted state/error transitions.
 * @param runtime Runtime instance; this function is owner-thread-only.
 */
void lely_rtt_can_process_status(struct lely_rtt_runtime *runtime);

#endif /* LELY_RTT_INTERNAL_H_ */
