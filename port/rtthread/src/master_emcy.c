/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-06     wdfk-prog         first version
 * 2026-09-06     wdfk-prog         make history sequence wrap an epoch boundary
 */

/**
 * @file master_emcy.c
 * @brief Owner-safe CANopen EMCY consumer history and local producer bridge.
 *
 * Lely owns the EMCY service and recreates it when the local NMT state returns
 * from Stopped/reset to Pre-operational. The runtime therefore rebinds this
 * indication only from the owner thread. Received frames are copied into a
 * bounded atomic history so application threads never touch co_emcy directly.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_EMCY)

#include <lely/co/dev.h>
#include <lely/co/emcy.h>
#include <lely/co/obj.h>

/** @brief Pack Node-ID, EEC and error register into one atomic word. */
static rt_uint32_t
lely_rtt_master_emcy_pack(rt_uint8_t node_id, rt_uint16_t error_code,
        rt_uint8_t error_register)
{
    return (rt_uint32_t)node_id
            | ((rt_uint32_t)error_code << 8)
            | ((rt_uint32_t)error_register << 24);
}

/**
 * @brief Return the previous event sequence without crossing a wrap epoch.
 *
 * Sequence zero is reserved. Returning zero for sequence 1 terminates a
 * history scan at the current epoch boundary instead of jumping back to
 * UINT32_MAX, whose modulo slot mapping is discontinuous after zero is skipped.
 */
static rt_uint32_t
lely_rtt_master_emcy_prev_sequence(rt_uint32_t sequence)
{
    return sequence > 1u ? sequence - 1u : 0u;
}

/** @brief Check whether the local EMCY producer has a usable COB-ID. */
static rt_bool_t
lely_rtt_master_emcy_producer_ready(co_emcy_t *emcy)
{
    co_dev_t *dev;
    co_obj_t *obj_1014;

    if (!emcy)
        return RT_FALSE;

    dev = co_emcy_get_dev(emcy);
    obj_1014 = dev ? co_dev_find_obj(dev, 0x1014) : RT_NULL;
    if (!obj_1014)
        return RT_FALSE;

    return !(co_obj_get_val_u32(obj_1014, 0x00) & CO_EMCY_COBID_VALID);
}

/** @brief Publish one received remote EMCY into the bounded history ring. */
static void
lely_rtt_master_emcy_publish(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint16_t error_code, rt_uint8_t error_register,
        const rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE])
{
    struct lely_rtt_emcy_slot *slot;
    rt_uint32_t sequence;
    rt_uint32_t guard;
    rt_uint32_t msef_lo = 0;
    rt_uint32_t msef_hi = 0;

    if (!runtime || !node_id || node_id > CO_NUM_NODES)
        return;

    sequence = (rt_uint32_t)rt_atomic_load(&runtime->emcy_latest_sequence) + 1u;
    if (!sequence)
        sequence = 1u;
    slot = &runtime->emcy_history[(sequence - 1u)
            % PKG_LELY_MASTER_EMCY_HISTORY_DEPTH];

    if (manufacturer) {
        msef_lo = (rt_uint32_t)manufacturer[0]
                | ((rt_uint32_t)manufacturer[1] << 8)
                | ((rt_uint32_t)manufacturer[2] << 16)
                | ((rt_uint32_t)manufacturer[3] << 24);
        msef_hi = manufacturer[4];
    }

    guard = (rt_uint32_t)rt_atomic_load(&slot->guard);
    if (guard & 1u)
        guard++;
    if (guard > 0xfffffffcu)
        guard = 0;

    /*
     * The owner is the single writer. Publish an odd guard first and the even
     * guard last so preempting readers can detect a torn slot and sleep until
     * the owner completes publication instead of spinning at higher priority.
     */
    rt_atomic_store(&slot->guard, (rt_atomic_t)(guard + 1u));
    rt_atomic_store(&slot->sequence, (rt_atomic_t)sequence);
    rt_atomic_store(&slot->header, (rt_atomic_t)lely_rtt_master_emcy_pack(
            node_id, error_code, error_register));
    rt_atomic_store(&slot->msef_lo, (rt_atomic_t)msef_lo);
    rt_atomic_store(&slot->msef_hi, (rt_atomic_t)msef_hi);
    rt_atomic_store(&slot->guard, (rt_atomic_t)(guard + 2u));
    rt_atomic_store(&runtime->emcy_latest_sequence, (rt_atomic_t)sequence);
}

/** @brief EMCY consumer indication executed in the Lely owner thread. */
static void
lely_rtt_master_emcy_ind(co_emcy_t *emcy, co_unsigned8_t id,
        co_unsigned16_t eec, co_unsigned8_t er,
        const co_unsigned8_t msef[LELY_RTT_EMCY_MSEF_SIZE], void *data)
{
    (void)emcy;
    lely_rtt_master_emcy_publish(data, id, eec, er, msef);
}

void
lely_rtt_master_emcy_reset(struct lely_rtt_runtime *runtime)
{
    rt_size_t i;

    if (!runtime)
        return;

    rt_atomic_store(&runtime->emcy_latest_sequence, 0);
    for (i = 0; i < PKG_LELY_MASTER_EMCY_HISTORY_DEPTH; i++) {
        rt_atomic_store(&runtime->emcy_history[i].guard, 0);
        rt_atomic_store(&runtime->emcy_history[i].sequence, 0);
        rt_atomic_store(&runtime->emcy_history[i].header, 0);
        rt_atomic_store(&runtime->emcy_history[i].msef_lo, 0);
        rt_atomic_store(&runtime->emcy_history[i].msef_hi, 0);
    }
}

rt_err_t
lely_rtt_master_emcy_bind(struct lely_rtt_runtime *runtime)
{
    co_emcy_ind_t *ind = RT_NULL;
    void *data = RT_NULL;
    co_emcy_t *emcy;

    if (!runtime || !runtime->master_nmt)
        return -RT_EINVAL;

    emcy = co_nmt_get_emcy(runtime->master_nmt);
    if (!emcy)
        return -RT_EBUSY;

    co_emcy_get_ind(emcy, &ind, &data);
    if (ind && (ind != &lely_rtt_master_emcy_ind || data != runtime)) {
        LELY_RTT_LOG_E("EMCY bridge cannot replace an existing consumer indication");
        return -RT_EBUSY;
    }

    co_emcy_set_ind(emcy, &lely_rtt_master_emcy_ind, runtime);
    return RT_EOK;
}

void
lely_rtt_master_emcy_unbind(struct lely_rtt_runtime *runtime)
{
    co_emcy_ind_t *ind = RT_NULL;
    void *data = RT_NULL;
    co_emcy_t *emcy;

    if (!runtime || !runtime->master_nmt)
        return;

    emcy = co_nmt_get_emcy(runtime->master_nmt);
    if (!emcy)
        return;

    co_emcy_get_ind(emcy, &ind, &data);
    if (ind == &lely_rtt_master_emcy_ind && data == runtime)
        co_emcy_set_ind(emcy, RT_NULL, RT_NULL);
}

enum lely_rtt_master_emcy_operation {
    LELY_RTT_MASTER_EMCY_PUSH = 0,
    LELY_RTT_MASTER_EMCY_POP,
    LELY_RTT_MASTER_EMCY_CLEAR,
};

struct lely_rtt_master_emcy_request {
    struct lely_rtt_master_sync sync;
    enum lely_rtt_master_emcy_operation operation;
    rt_uint16_t error_code;
    rt_uint8_t error_register;
    rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE];
};

void
lely_rtt_master_emcy_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_emcy_request *request)
{
    co_emcy_t *emcy;
    rt_err_t err = RT_EOK;

    if (!request)
        return;
    if (!runtime || !runtime->master_nmt) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

    /* EMCY exists only in local NMT Pre-op/Operational states. */
    emcy = co_nmt_get_emcy(runtime->master_nmt);
    if (!emcy) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

    /*
     * Upstream producer operations return success when 0x1014 is absent or
     * disabled, but no frame is emitted. Fail closed so RT_EOK always means
     * this bridge had an enabled local EMCY producer to submit through.
     */
    if (!lely_rtt_master_emcy_producer_ready(emcy)) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

    switch (request->operation) {
    case LELY_RTT_MASTER_EMCY_PUSH:
        if (!request->error_code) {
            err = -RT_EINVAL;
        } else if (co_emcy_push(emcy, request->error_code,
                request->error_register, request->manufacturer) == -1) {
            err = -RT_ERROR;
        }
        break;
    case LELY_RTT_MASTER_EMCY_POP:
        if (co_emcy_pop(emcy, RT_NULL, RT_NULL) == -1)
            err = -RT_ERROR;
        break;
    case LELY_RTT_MASTER_EMCY_CLEAR:
        if (co_emcy_clear(emcy) == -1)
            err = -RT_ERROR;
        break;
    default:
        err = -RT_EINVAL;
        break;
    }

    lely_rtt_master_sync_complete(&request->sync, err);
}

void
lely_rtt_master_emcy_cancel_queued(struct lely_rtt_master_emcy_request *request)
{
    if (request)
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
}

/** @brief Submit one synchronous local EMCY producer operation. */
static rt_err_t
lely_rtt_master_emcy_submit(lely_rtt_runtime_t *runtime,
        struct lely_rtt_master_emcy_request *request)
{
    struct lely_rtt_master_command command;
    rt_err_t err;

    if (!runtime || !request || runtime->owner_thread == rt_thread_self())
        return -RT_EINVAL;

    err = lely_rtt_master_sync_init(&request->sync, "lelyemcy");
    if (err != RT_EOK)
        return err;

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_EMCY;
    command.data.emcy.request = request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err == RT_EOK)
        err = lely_rtt_master_sync_wait(&request->sync);
    lely_rtt_master_sync_fini(&request->sync);
    return err;
}

rt_err_t
lely_rtt_runtime_emcy_push(lely_rtt_runtime_t *runtime,
        rt_uint16_t error_code, rt_uint8_t error_register,
        const rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE])
{
    struct lely_rtt_master_emcy_request request;

    if (!runtime || !error_code)
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_EMCY_PUSH;
    request.error_code = error_code;
    request.error_register = error_register;
    if (manufacturer)
        rt_memcpy(request.manufacturer, manufacturer, sizeof(request.manufacturer));
    return lely_rtt_master_emcy_submit(runtime, &request);
}

rt_err_t
lely_rtt_runtime_emcy_pop(lely_rtt_runtime_t *runtime)
{
    struct lely_rtt_master_emcy_request request;

    if (!runtime)
        return -RT_EINVAL;
    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_EMCY_POP;
    return lely_rtt_master_emcy_submit(runtime, &request);
}

rt_err_t
lely_rtt_runtime_emcy_clear(lely_rtt_runtime_t *runtime)
{
    struct lely_rtt_master_emcy_request request;

    if (!runtime)
        return -RT_EINVAL;
    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_EMCY_CLEAR;
    return lely_rtt_master_emcy_submit(runtime, &request);
}

rt_err_t
lely_rtt_runtime_get_emcy(lely_rtt_runtime_t *runtime, rt_uint8_t node_id,
        struct lely_rtt_emcy_event *event)
{
    rt_uint32_t latest;
    rt_uint32_t candidate;
    rt_size_t remaining;

    if (!runtime || !event || node_id > CO_NUM_NODES)
        return -RT_EINVAL;

    for (;;) {
        rt_bool_t retry = RT_FALSE;

        latest = (rt_uint32_t)rt_atomic_load(&runtime->emcy_latest_sequence);
        if (!latest)
            return -RT_EBUSY;

        candidate = latest;
        remaining = PKG_LELY_MASTER_EMCY_HISTORY_DEPTH;
        while (remaining-- && candidate) {
            struct lely_rtt_emcy_slot *slot;
            rt_uint32_t begin;
            rt_uint32_t end;
            rt_uint32_t sequence;
            rt_uint32_t header;
            rt_uint32_t msef_lo;
            rt_uint32_t msef_hi;
            rt_uint8_t candidate_node;

            slot = &runtime->emcy_history[(candidate - 1u)
                    % PKG_LELY_MASTER_EMCY_HISTORY_DEPTH];
            begin = (rt_uint32_t)rt_atomic_load(&slot->guard);
            if (!begin)
                break;
            if (begin & 1u) {
                retry = RT_TRUE;
                break;
            }

            sequence = (rt_uint32_t)rt_atomic_load(&slot->sequence);
            header = (rt_uint32_t)rt_atomic_load(&slot->header);
            msef_lo = (rt_uint32_t)rt_atomic_load(&slot->msef_lo);
            msef_hi = (rt_uint32_t)rt_atomic_load(&slot->msef_hi);
            end = (rt_uint32_t)rt_atomic_load(&slot->guard);
            if (begin != end || (end & 1u)) {
                retry = RT_TRUE;
                break;
            }

            /*
             * The scan never crosses sequence 1 into the previous wrap epoch.
             * A stable mismatch therefore means the owner overwrote this slot
             * after our latest snapshot. Restart after sleeping so the owner
             * can publish the corresponding new latest sequence.
             */
            if (sequence != candidate) {
                retry = RT_TRUE;
                break;
            }

            candidate_node = (rt_uint8_t)header;
            if (!node_id || candidate_node == node_id) {
                event->node_id = candidate_node;
                event->error_code = (rt_uint16_t)(header >> 8);
                event->error_register = (rt_uint8_t)(header >> 24);
                event->manufacturer[0] = (rt_uint8_t)msef_lo;
                event->manufacturer[1] = (rt_uint8_t)(msef_lo >> 8);
                event->manufacturer[2] = (rt_uint8_t)(msef_lo >> 16);
                event->manufacturer[3] = (rt_uint8_t)(msef_lo >> 24);
                event->manufacturer[4] = (rt_uint8_t)msef_hi;
                event->sequence = sequence;
                return RT_EOK;
            }

            candidate = lely_rtt_master_emcy_prev_sequence(candidate);
        }

        if (!retry)
            return -RT_EBUSY;
        rt_thread_mdelay(1);
    }
}

#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
