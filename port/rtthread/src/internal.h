/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 * 2026-09-04     wdfk-prog         add owner CANopen node and command queue state
 * 2026-09-05     wdfk-prog         replace local slave state with Master snapshots
 * 2026-09-05     wdfk-prog         add Master command and SDO transaction state
 * 2026-09-05     wdfk-prog         add synchronous CFG, local OD and TIME state
 * 2026-09-06     wdfk-prog         add local NMT CFG lifetime barrier hook
 * 2026-09-06     wdfk-prog         add B5.2 TPDO and B6 EMCY owner bridges
 */

/**
 * @file internal.h
 * @brief Internal RT-Thread state for the owner runtime, CANopen Master, timer and CAN adapters.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_INTERNAL_H_
#define LELY_RTT_INTERNAL_H_

#include <lely/co/nmt.h>
#if defined(PKG_LELY_USING_MASTER_SDO)
#include <lely/co/csdo.h>
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
#include <lely/co/sdev.h>
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

#if defined(PKG_LELY_USING_MASTER_COMMAND)
/** @brief Cross-thread Master command queue contains work for the owner. */
#define LELY_RTT_EVENT_COMMAND    (1u << 6)
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

/**
 * @brief Event bits consumed by the owner work loop.
 *
 * READY and EXIT are deliberately excluded because they synchronize the
 * lifecycle caller rather than dispatch owner work.
 */
#if defined(PKG_LELY_USING_MASTER_COMMAND)
#define LELY_RTT_EVENT_OWNER_MASK \
    (LELY_RTT_EVENT_RX_READY | LELY_RTT_EVENT_TIMER_DUE | \
     LELY_RTT_EVENT_CAN_STATUS | LELY_RTT_EVENT_STOP | LELY_RTT_EVENT_COMMAND)
#else
#define LELY_RTT_EVENT_OWNER_MASK \
    (LELY_RTT_EVENT_RX_READY | LELY_RTT_EVENT_TIMER_DUE | \
     LELY_RTT_EVENT_CAN_STATUS | LELY_RTT_EVENT_STOP)
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

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

#if defined(PKG_LELY_USING_MASTER_EMCY)
/** @brief One atomically published remote EMCY history slot. */
struct lely_rtt_emcy_slot {
    rt_atomic_t guard; /**< Odd while the owner updates the remaining fields. */
    rt_atomic_t sequence; /**< Non-zero event sequence stored in this slot. */
    rt_atomic_t header; /**< Packed Node-ID, error code and error register. */
    rt_atomic_t msef_lo; /**< Manufacturer-specific bytes 0..3. */
    rt_atomic_t msef_hi; /**< Manufacturer-specific byte 4. */
};
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */

#if defined(PKG_LELY_USING_MASTER_COMMAND)
/** @brief Completion event used by synchronous application-to-owner requests. */
#define LELY_RTT_MASTER_SYNC_DONE (1u << 0)

/**
 * @brief Shared completion primitive for synchronous owner-thread operations.
 *
 * The request object embedding this structure remains owned by the blocked
 * caller until done is published. There is intentionally no independent wait
 * timeout: abandoning a queued stack request would leave the owner with a
 * dangling pointer. All completion sites run in the owner thread; done is the
 * duplicate-completion guard rather than a cross-thread arbitration primitive.
 * completion_refs also prevents the caller from detaching the event if a
 * higher-priority wakeup preempts the owner inside rt_event_send().
 */
struct lely_rtt_master_sync {
    struct rt_event event;
    rt_atomic_t done;
    rt_atomic_t completion_refs;
    rt_err_t result;
    rt_bool_t initialized;
};

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
struct lely_rtt_master_cfg_request;
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
struct lely_rtt_local_od_request;
struct lely_rtt_local_od_hook;
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
struct lely_rtt_master_pdo_request;
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
struct lely_rtt_master_emcy_request;
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
struct lely_rtt_master_time_request;
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */

/** @brief Owner-dispatched Master command kind. */
enum lely_rtt_master_command_type {
    LELY_RTT_MASTER_COMMAND_NMT = 0,
#if defined(PKG_LELY_USING_MASTER_SDO)
    LELY_RTT_MASTER_COMMAND_SDO,
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    LELY_RTT_MASTER_COMMAND_NMT_CFG,
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
    LELY_RTT_MASTER_COMMAND_LOCAL_OD,
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
    LELY_RTT_MASTER_COMMAND_PDO_TX,
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
    LELY_RTT_MASTER_COMMAND_EMCY,
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
    LELY_RTT_MASTER_COMMAND_TIME,
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
};

/** @brief One copied command carried through the per-runtime RT message queue. */
struct lely_rtt_master_command {
    rt_uint8_t type;
    union {
        struct {
            rt_uint8_t node_id;
            rt_uint8_t command;
        } nmt;
#if defined(PKG_LELY_USING_MASTER_SDO)
        struct {
            lely_rtt_sdo_request_t *request;
        } sdo;
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
        struct {
            struct lely_rtt_master_cfg_request *request;
        } cfg;
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
        struct {
            struct lely_rtt_local_od_request *request;
        } od;
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
        struct {
            struct lely_rtt_master_pdo_request *request;
        } pdo;
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
        struct {
            struct lely_rtt_master_emcy_request *request;
        } emcy;
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
        struct {
            struct lely_rtt_master_time_request *request;
        } time;
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
    } data;
};
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

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
    /** Static local Master device description configured before owner startup. */
    const struct co_sdev *master_sdev;

    /** Only RT-Thread thread allowed to execute Lely APIs. */
    rt_thread_t owner_thread;
    /** Open RT-Thread CAN device associated with config.can_name. */
    rt_device_t can_dev;

    io_ctx_t *ctx;             /**< Owner-thread Lely I/O context. */
    ev_loop_t *loop;           /**< Owner-thread Lely event loop/executor. */
    io_timer_t *timer;         /**< Passive Lely user timer. */
    io_can_chan_t *can_chan;   /**< Passive Lely user CAN channel. */
    io_can_net_t *can_net;     /**< Lely CAN network joining CAN and timer. */
    co_dev_t *master_dev;      /**< Optional owner-thread local Master device. */
    co_nmt_t *master_nmt;      /**< Optional owner-thread local Master NMT service. */

    struct lely_rtt_time_state time; /**< Monotonic RT tick extension state. */
    struct rt_can_status last_can_status; /**< Last status accepted by Lely. */

    /** Number of CAN/timer callbacks currently holding a runtime lifetime pin. */
    rt_atomic_t callback_refs;
    /** Non-zero after cleanup closes admission for new external callbacks. */
    rt_atomic_t callback_quiescing;
    /** Cached local Master Node-ID; zero while no local Master is ready. */
    rt_atomic_t local_node_id;
    /** Cached local Master NMT state, or LELY_RTT_NMT_STATE_UNAVAILABLE. */
    rt_atomic_t local_nmt_state;
    /** Packed remote NMT state/heartbeat snapshots indexed by Node-ID. */
    rt_atomic_t remote_nmt_state[CO_NUM_NODES + 1];
    /** Packed completed NMT boot result snapshots indexed by Node-ID. */
    rt_atomic_t remote_boot_result[CO_NUM_NODES + 1];

#if defined(PKG_LELY_USING_MASTER_COMMAND)
    /** Per-runtime command queue crossing into the Lely owner thread. */
    rt_mq_t command_mq;
    /** Number of command posters holding the queue lifetime pin. */
    rt_atomic_t command_refs;
    /** Non-zero while new Master command posts are rejected. */
    rt_atomic_t command_quiescing;
    /** Non-zero once STOP/startup-cancel irreversibly closes the current run. */
    rt_atomic_t command_stop_latched;
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    /** Owner-only manual configuration request active for each remote node. */
    struct lely_rtt_master_cfg_request *cfg_active[CO_NUM_NODES + 1];
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */

#if defined(PKG_LELY_USING_LOCAL_OD)
    /** Owner-owned chain restoring pre-existing manufacturer OD download hooks. */
    struct lely_rtt_local_od_hook *local_od_hooks;
    /** Owner-only marker classifying writes issued by the public local OD API. */
    rt_bool_t local_od_api_write_active;
    /** Even non-zero value identifies a stable local OD change snapshot. */
    rt_atomic_t local_od_change_seq;
    rt_atomic_t local_od_change_index;
    rt_atomic_t local_od_change_subindex;
    rt_atomic_t local_od_change_source;
    rt_atomic_t local_od_change_size;
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */

#if defined(PKG_LELY_USING_MASTER_EMCY)
    /** Latest non-zero received EMCY sequence; owner is the only writer. */
    rt_atomic_t emcy_latest_sequence;
    /** Bounded remote EMCY history published through per-slot seqlocks. */
    struct lely_rtt_emcy_slot emcy_history[PKG_LELY_MASTER_EMCY_HISTORY_DEPTH];
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */

#if defined(PKG_LELY_USING_MASTER_TIME)
    /** Even non-zero value identifies a stable received TIME snapshot. */
    rt_atomic_t time_snapshot_seq;
    /** Low 32 bits of the last received absolute Unix seconds. */
    rt_atomic_t time_snapshot_sec_lo;
    /** High 32 bits of the last received absolute Unix seconds. */
    rt_atomic_t time_snapshot_sec_hi;
    /** Nanoseconds of the last received absolute TIME value. */
    rt_atomic_t time_snapshot_nsec;
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */

#if defined(PKG_LELY_USING_MASTER_SDO)
    /** Lazily created application Client-SDO selected per remote Node-ID. */
    co_csdo_t *sdo_clients[CO_NUM_NODES + 1];
    /** At most one application SDO request can own each Client-SDO. */
    lely_rtt_sdo_request_t *sdo_active[CO_NUM_NODES + 1];
    /** Owner-only gate blocking application SDO during NMT stop/reset/boot. */
    rt_bool_t sdo_suspended[CO_NUM_NODES + 1];
    /** Owner-only marker keeping reset suspension until boot completion/state. */
    rt_bool_t sdo_reset_pending[CO_NUM_NODES + 1];
    /** Client-SDO receiver must be stopped after the current callback unwinds. */
    rt_bool_t sdo_stop_pending[CO_NUM_NODES + 1];
    /** Opaque per-runtime request identifier sequence for SDO posts. */
    rt_atomic_t sdo_next_request_id;
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

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

#if defined(PKG_LELY_USING_MASTER_COMMAND)
/** @brief Reset command admission to the closed state before owner startup. */
void lely_rtt_master_command_prepare(struct lely_rtt_runtime *runtime);
/** @brief Open command admission only for a ready Master and uncanceled run. */
void lely_rtt_master_command_admission_open(struct lely_rtt_runtime *runtime);
/** @brief Irreversibly close command admission for the current run. */
void lely_rtt_master_command_quiesce_begin(struct lely_rtt_runtime *runtime);
/** @brief Wait until cross-thread command posters release queue lifetime pins. */
void lely_rtt_master_command_wait_idle(struct lely_rtt_runtime *runtime);
/** @brief Create the per-runtime Master command queue. */
rt_err_t lely_rtt_master_command_init(struct lely_rtt_runtime *runtime);
/** @brief Cancel queued work, release SDO clients and destroy the command queue. */
void lely_rtt_master_command_fini(struct lely_rtt_runtime *runtime);
/** @brief Dispatch one bounded command batch from the owner thread. */
void lely_rtt_master_command_dispatch(struct lely_rtt_runtime *runtime);
/** @brief Post one already-validated internal command to the owner queue. */
rt_err_t lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command);
/** @brief Initialize one caller-owned synchronous owner request. */
rt_err_t lely_rtt_master_sync_init(struct lely_rtt_master_sync *sync,
        const char *name);
/** @brief Publish one synchronous owner request result exactly once. */
void lely_rtt_master_sync_complete(struct lely_rtt_master_sync *sync,
        rt_err_t result);
/** @brief Wait until the owner or teardown completes a synchronous request. */
rt_err_t lely_rtt_master_sync_wait(struct lely_rtt_master_sync *sync);
/** @brief Detach a completed/unposted synchronous request event safely. */
void lely_rtt_master_sync_fini(struct lely_rtt_master_sync *sync);
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
/** @brief Dispatch one manual NMT configuration request in the owner thread. */
void lely_rtt_master_cfg_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_cfg_request *request);
/** @brief Complete a configuration request that never reached the owner. */
void lely_rtt_master_cfg_cancel_queued(
        struct lely_rtt_master_cfg_request *request);
/** @brief Mark active configuration requests canceled before NMT destruction. */
void lely_rtt_master_cfg_prepare_nmt_destroy(struct lely_rtt_runtime *runtime);
/** @brief Complete any requests retained by the destroyed NMT service. */
void lely_rtt_master_cfg_after_nmt_destroy(struct lely_rtt_runtime *runtime);
/** @brief Report whether a manual configuration owns one node's default SDO. */
rt_bool_t lely_rtt_master_cfg_node_busy(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id);
/** @brief Mark affected configuration requests canceled after an NMT command. */
void lely_rtt_master_cfg_on_nmt_command(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, enum lely_rtt_nmt_command command);
/**
 * @brief Retire CFG requests after Lely destroys local slave-management state.
 *
 * The runtime calls this from the local NMT state indication. Only reset
 * indications (`state == 0`) and PRE-OP are lifecycle barriers because frozen
 * upstream emits those indications after co_nmt_slaves_fini() has cleared
 * every cfg callback/data pointer. STOP is deliberately not a barrier.
 */
void lely_rtt_master_cfg_on_local_nmt_state(
        struct lely_rtt_runtime *runtime, rt_uint8_t state);
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */

#if defined(PKG_LELY_USING_LOCAL_OD)
/** @brief Reset application-visible local OD write metadata. */
void lely_rtt_local_od_reset(struct lely_rtt_runtime *runtime);
/** @brief Chain manufacturer OD download indications for write observation. */
rt_err_t lely_rtt_local_od_bind(struct lely_rtt_runtime *runtime);
/** @brief Restore original manufacturer OD indications and free hook storage. */
void lely_rtt_local_od_unbind(struct lely_rtt_runtime *runtime);
/** @brief Dispatch one owner-safe local OD operation. */
void lely_rtt_local_od_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_local_od_request *request);
/** @brief Complete a local OD request that never reached the owner. */
void lely_rtt_local_od_cancel_queued(struct lely_rtt_local_od_request *request);
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */

#if defined(PKG_LELY_USING_MASTER_PDO_TX)
/** @brief Dispatch one owner-safe event-driven TPDO trigger. */
void lely_rtt_master_pdo_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_pdo_request *request);
/** @brief Complete a TPDO request that never reached the owner. */
void lely_rtt_master_pdo_cancel_queued(
        struct lely_rtt_master_pdo_request *request);
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */

#if defined(PKG_LELY_USING_MASTER_EMCY)
/** @brief Reset the retained remote EMCY history for a new runtime run. */
void lely_rtt_master_emcy_reset(struct lely_rtt_runtime *runtime);
/** @brief Attach the remote EMCY history callback to the active Lely service. */
rt_err_t lely_rtt_master_emcy_bind(struct lely_rtt_runtime *runtime);
/** @brief Detach the runtime EMCY callback when its service still exists. */
void lely_rtt_master_emcy_unbind(struct lely_rtt_runtime *runtime);
/** @brief Dispatch one owner-safe local EMCY producer operation. */
void lely_rtt_master_emcy_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_emcy_request *request);
/** @brief Complete an EMCY request that never reached the owner. */
void lely_rtt_master_emcy_cancel_queued(
        struct lely_rtt_master_emcy_request *request);
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */

#if defined(PKG_LELY_USING_MASTER_TIME)
/** @brief Reset the application-visible received TIME snapshot. */
void lely_rtt_master_time_reset(struct lely_rtt_runtime *runtime);
/** @brief Attach the received TIME snapshot callback to Lely's TIME service. */
rt_err_t lely_rtt_master_time_bind(struct lely_rtt_runtime *runtime);
/** @brief Detach the application TIME callback before NMT destruction. */
void lely_rtt_master_time_unbind(struct lely_rtt_runtime *runtime);
/** @brief Dispatch one owner-safe TIME control operation. */
void lely_rtt_master_time_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_time_request *request);
/** @brief Complete a TIME request that never reached the owner. */
void lely_rtt_master_time_cancel_queued(
        struct lely_rtt_master_time_request *request);
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */

#if defined(PKG_LELY_USING_MASTER_SDO)
/** @brief Dispatch one queued SDO request in the owner thread. */
void lely_rtt_master_sdo_dispatch(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request);
/** @brief Stop completed application CSDO receivers after callbacks unwind. */
void lely_rtt_master_sdo_reap(struct lely_rtt_runtime *runtime);
/** @brief Complete an SDO request that never reached the owner dispatcher. */
void lely_rtt_master_sdo_cancel_queued(lely_rtt_sdo_request_t *request);
/** @brief Cancel active application SDO work for one node or all nodes (id 0). */
void lely_rtt_master_sdo_cancel_node(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id);
/** @brief Retire one application CSDO before Lely starts remote NMT boot. */
void lely_rtt_master_sdo_before_boot(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id);
/** @brief Update the application-SDO gate after an observed remote NMT state. */
void lely_rtt_master_sdo_on_nmt_state(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint8_t state);
/** @brief Update the application-SDO gate after an accepted local NMT command. */
void lely_rtt_master_sdo_on_nmt_command(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, enum lely_rtt_nmt_command command);
/** @brief Finalize the application-SDO reset/boot gate after NMT boot completes. */
void lely_rtt_master_sdo_on_boot_complete(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint8_t state);
/** @brief Cancel and destroy all lazily created application Client-SDOs. */
void lely_rtt_master_sdo_fini(struct lely_rtt_runtime *runtime);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

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
