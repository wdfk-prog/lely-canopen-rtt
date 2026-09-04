/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 */

/**
 * @file runtime.h
 * @brief RT-Thread single-owner runtime for the vendored Lely CANopen stack.
 *
 * The runtime owns one RT-Thread thread. All Lely EV/IO2/CANopen work created
 * by this port executes in that owner thread. CAN/timer callbacks only acquire
 * a short lifetime pin and wake the owner; they never call Lely directly.
 *
 * Lifecycle calls for one runtime are a single-caller control plane. The
 * application must serialize create/start/stop/destroy operations for the same
 * runtime; READY/EXIT are one-caller handshakes, not broadcast synchronization.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_RUNTIME_H_
#define LELY_RTT_RUNTIME_H_

#include <lely/io2/can/err.h>

#include <rtdevice.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque RT-Thread runtime handle. */
typedef struct lely_rtt_runtime lely_rtt_runtime_t;

/** @brief RT-Thread CAN FD length-field convention used by the selected BSP driver. */
enum lely_rtt_canfd_len_mode {
    /** rt_can_msg.len contains the actual payload byte count. */
    LELY_RTT_CANFD_LEN_BYTES = 0,
    /** rt_can_msg.len contains the raw CAN FD DLC value (0..15). */
    LELY_RTT_CANFD_LEN_DLC = 1
};

/**
 * @brief Optional startup hook for RT-Thread hardware CAN acceptance filters.
 *
 * The Lely software receive tree remains the correctness filter. This hook is
 * only a BSP/application optimization to reject irrelevant frames before they
 * consume RT-Thread RX FIFO and owner-thread CPU time. When the hook is NULL,
 * the port installs no restrictive hardware filter and therefore requires the
 * BSP's normal open state to accept all frames needed by the CANopen network.
 *
 * A restrictive filter must cover every currently valid CANopen COB-ID. If the
 * application allows dynamic COB-ID changes, it must either keep hardware
 * acceptance broad enough for all allowed values or provide its own safe
 * reconfiguration policy outside this startup-only hook.
 *
 * @param dev Open RT-Thread CAN device. The bitrate/CAN-FD mode has already
 *            been configured, but RX/status callbacks are not registered yet.
 * @param arg User value from lely_rtt_runtime_config.filter_setup_arg.
 *
 * The hook runs synchronously in the Lely owner thread during startup. It may
 * configure the RT-Thread CAN device, but must not call Lely APIs or re-enter
 * lely_rtt_runtime_start()/stop(). If it returns an error after changing filter
 * state, the hook owns rollback: it must restore its changes or leave the CAN
 * device in a state that can be safely closed and retried. The generic runtime
 * cannot reconstruct BSP-specific filter state.
 *
 * @return RT_EOK on success; any other RT-Thread error aborts runtime startup.
 */
typedef rt_err_t lely_rtt_can_filter_setup_t(rt_device_t dev, void *arg);

/**
 * @brief Optional BSP-specific CAN status mapper executed only by the Lely owner.
 *
 * Use this when the BSP gives rt_can_status.errcode/lasterrtype semantics that
 * are more precise than the conservative generic REC/TEC mapping.
 *
 * @param status RT-Thread CAN status returned by RT_CAN_CMD_GET_STATUS.
 * @param previous Previous accepted status, or RT_NULL on the first sample.
 * @param err Output Lely CAN state/error description.
 * @param arg User value from lely_rtt_runtime_config.status_mapper_arg.
 * The mapper must only translate the supplied status; it must not re-enter
 * Lely or runtime lifecycle APIs.
 *
 * @return Non-zero when an error/state event should be injected, zero to only
 *         accept the status as the new baseline.
 */
typedef int lely_rtt_can_status_mapper_t(const struct rt_can_status *status,
        const struct rt_can_status *previous, struct can_err *err, void *arg);

/**
 * @brief Runtime configuration supplied by the BSP/application.
 *
 * The core runtime never guesses missing values. Manual callers must fill every
 * required resource, timeout and CAN field explicitly; the optional auto-init
 * layer builds the same structure from its Kconfig defaults.
 */
struct lely_rtt_runtime_config {
    /** Registered RT-Thread CAN device name, for example "can1". */
    const char *can_name;
    /** Arbitration-phase CAN bitrate accepted by RT_CAN_CMD_SET_BAUD. */
    rt_uint32_t can_bitrate;
    /** Number of frames drained per RX batch before other event classes run. */
    rt_size_t rx_batch;

    /** Owner thread stack size in bytes. */
    rt_uint32_t thread_stack_size;
    /** Owner thread priority. */
    rt_uint8_t thread_priority;
    /** Owner thread time slice in RT-Thread ticks. */
    rt_uint32_t thread_timeslice;

    /** Maximum wait for owner initialization acknowledgement, in milliseconds. */
    rt_uint32_t start_timeout_ms;
    /** Maximum wait for one owner shutdown acknowledgement attempt, in milliseconds. */
    rt_uint32_t stop_timeout_ms;

    /** Non-zero when the BSP requires RT_CAN_CMD_START during start/stop. */
    rt_bool_t start_controller;
    /** Non-zero to register RT_CAN_CMD_SET_STATUS_IND. */
    rt_bool_t use_status_indication;
    /** Non-zero to enable CAN FD mapping; requires PKG_LELY_USING_CANFD. */
    rt_bool_t can_fd;
    /** Non-zero to permit CAN FD bit-rate switching. Requires can_fd. */
    rt_bool_t can_brs;
    /** BSP convention for rt_can_msg.len when can_fd is enabled. */
    enum lely_rtt_canfd_len_mode can_fd_len_mode;

    /** Optional startup-only hardware acceptance-filter setup callback. */
    lely_rtt_can_filter_setup_t *filter_setup;
    /** User value forwarded to filter_setup. */
    void *filter_setup_arg;

    /** Optional BSP-specific status mapper; RT_NULL uses conservative mapping. */
    lely_rtt_can_status_mapper_t *status_mapper;
    /** User value forwarded to status_mapper. */
    void *status_mapper_arg;
};

/**
 * @brief Allocate a stopped runtime and copy the supplied configuration.
 *
 * This function does not create Lely objects and does not open the CAN device.
 * The caller may invoke it from a normal RT-Thread thread.
 *
 * @param config Runtime configuration. Strings referenced by the configuration
 *               must remain valid until lely_rtt_runtime_destroy().
 * @return A runtime handle, or RT_NULL when allocation or validation fails.
 */
lely_rtt_runtime_t *lely_rtt_runtime_create(
        const struct lely_rtt_runtime_config *config);

/**
 * @brief Start the owner thread and wait for bounded initialization acknowledgement.
 *
 * All Lely objects and the RT-Thread CAN bridge are initialized by the owner
 * before this function returns RT_EOK. If the bounded wait expires, the
 * function requests owner shutdown and returns -RT_ETIMEOUT; the runtime must
 * still be stopped successfully before it can be destroyed.
 *
 * Calls to start/stop/destroy for this runtime must be serialized by the
 * application; this API does not arbitrate multiple lifecycle callers.
 *
 * @param runtime Runtime handle.
 * @return RT_EOK on success, -RT_ETIMEOUT when start_timeout_ms expires, or an
 *         RT-Thread error code describing the failed initialization step.
 */
rt_err_t lely_rtt_runtime_start(lely_rtt_runtime_t *runtime);

/**
 * @brief Request orderly shutdown and wait for bounded owner exit acknowledgement.
 *
 * The stop request is safe from a normal non-owner RT-Thread thread. Calling it
 * from the owner thread is invalid because the function waits for owner exit.
 * A timeout does not free runtime storage; the caller may call stop again until
 * the owner has completed cleanup and acknowledged EXIT. Calls to
 * start/stop/destroy for this runtime must remain externally serialized.
 *
 * @param runtime Runtime handle.
 * @return RT_EOK on success, -RT_EINVAL for an invalid call, -RT_ETIMEOUT when
 *         stop_timeout_ms expires, or a latched owner/runtime error.
 */
rt_err_t lely_rtt_runtime_stop(lely_rtt_runtime_t *runtime);

/**
 * @brief Release a stopped runtime handle.
 *
 * The runtime must not be running. Call lely_rtt_runtime_stop() until it has
 * completed before destroy. A runtime whose owner_thread is still present is
 * intentionally left allocated. The application must serialize destroy against
 * start/stop and any other lifecycle call for the same runtime.
 *
 * @param runtime Runtime handle; RT_NULL is accepted.
 */
void lely_rtt_runtime_destroy(lely_rtt_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif /* LELY_RTT_RUNTIME_H_ */
