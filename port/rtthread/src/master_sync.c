/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-06     wdfk-prog         first version
 */

/**
 * @file master_sync.c
 * @brief Owner-safe CANopen SYNC bridge and producer-cycle control.
 *
 * Lely's NMT SYNC indication runs only after synchronous TPDO processing and
 * then synchronous RPDO processing. This bridge deliberately hooks that final
 * indication point, publishes a stable cross-thread snapshot, and only then
 * invokes the optional application callback. It never reorders PDO handling.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)

#include <lely/co/dev.h>
#include <lely/co/obj.h>
#include <lely/co/sync.h>

enum lely_rtt_master_sync_operation {
    LELY_RTT_MASTER_SYNC_SET_PERIOD = 0,
};

struct lely_rtt_master_sync_control_request {
    struct lely_rtt_master_sync sync;
    enum lely_rtt_master_sync_operation operation;
    rt_uint32_t period_us;
};

/** @brief Publish one post-PDO SYNC value through an atomic seqlock. */
static void
lely_rtt_master_sync_publish(struct lely_rtt_runtime *runtime,
        rt_uint8_t counter)
{
    struct lely_rtt_sync_event event;
    co_unsigned32_t cobid;
    rt_uint32_t seq;

    if (!runtime || !runtime->master_dev)
        return;

    cobid = co_dev_get_val_u32(runtime->master_dev, 0x1005, 0x00);
    event.period_us = co_dev_get_val_u32(runtime->master_dev, 0x1006, 0x00);
    event.counter = counter;
    event.role = (cobid & CO_SYNC_COBID_PRODUCER)
            ? LELY_RTT_SYNC_ROLE_PRODUCER : LELY_RTT_SYNC_ROLE_CONSUMER;

    seq = (rt_uint32_t)rt_atomic_load(&runtime->sync_snapshot_seq);
    if (seq & 1u)
        seq++;
    if (seq > 0xfffffffcu)
        seq = 0;

    rt_atomic_store(&runtime->sync_snapshot_seq, (rt_atomic_t)(seq + 1u));
    rt_atomic_store(&runtime->sync_snapshot_period_us,
            (rt_atomic_t)event.period_us);
    rt_atomic_store(&runtime->sync_snapshot_counter,
            (rt_atomic_t)event.counter);
    rt_atomic_store(&runtime->sync_snapshot_role, (rt_atomic_t)event.role);
    rt_atomic_store(&runtime->sync_snapshot_seq, (rt_atomic_t)(seq + 2u));

    event.sequence = (seq + 2u) / 2u;
    if (runtime->sync_app_ind)
        runtime->sync_app_ind(runtime, &event, runtime->sync_app_data);
}

/** @brief Managed NMT indication reached after Lely finishes synchronous PDOs. */
static void
lely_rtt_master_sync_ind(co_nmt_t *nmt, co_unsigned8_t counter, void *data)
{
    struct lely_rtt_runtime *runtime = data;

    if (!nmt || !runtime || runtime->master_nmt != nmt)
        return;

    /*
     * co_nmt_on_sync() has already processed TPDOs first and RPDOs second.
     * Publishing here makes sequence N an application-visible boundary after
     * all PDO work belonging to SYNC N has completed in the owner thread.
     */
    lely_rtt_master_sync_publish(runtime, (rt_uint8_t)counter);
}

void
lely_rtt_master_sync_reset(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    rt_atomic_store(&runtime->sync_snapshot_seq, 0);
    rt_atomic_store(&runtime->sync_snapshot_period_us, 0);
    rt_atomic_store(&runtime->sync_snapshot_counter, 0);
    rt_atomic_store(&runtime->sync_snapshot_role, 0);
}

rt_err_t
lely_rtt_master_sync_bind(struct lely_rtt_runtime *runtime)
{
    co_nmt_sync_ind_t *ind = RT_NULL;
    void *data = RT_NULL;

    if (!runtime || !runtime->master_nmt || !runtime->master_dev)
        return -RT_EINVAL;
    if (!co_dev_find_sub(runtime->master_dev, 0x1005, 0x00)) {
        LELY_RTT_LOG_E("SYNC bridge enabled but object 0x1005 is unavailable");
        return -RT_ERROR;
    }

    co_nmt_get_sync_ind(runtime->master_nmt, &ind, &data);
    if (ind && (ind != &lely_rtt_master_sync_ind || data != runtime)) {
        LELY_RTT_LOG_E("NMT SYNC indication is already owned by another callback");
        return -RT_EBUSY;
    }

    co_nmt_set_sync_ind(runtime->master_nmt, &lely_rtt_master_sync_ind, runtime);
    return RT_EOK;
}

void
lely_rtt_master_sync_unbind(struct lely_rtt_runtime *runtime)
{
    co_nmt_sync_ind_t *ind = RT_NULL;
    void *data = RT_NULL;

    if (!runtime || !runtime->master_nmt)
        return;

    co_nmt_get_sync_ind(runtime->master_nmt, &ind, &data);
    if (ind == &lely_rtt_master_sync_ind && data == runtime)
        co_nmt_set_sync_ind(runtime->master_nmt, RT_NULL, RT_NULL);
}

/** @brief Update object 0x1006 through the active Lely SYNC service. */
static rt_err_t
lely_rtt_master_sync_set_period_owner(struct lely_rtt_runtime *runtime,
        rt_uint32_t period_us)
{
    co_sub_t *sub_1005;
    co_sub_t *sub_1006;
    co_unsigned32_t cobid;
    co_unsigned32_t value = period_us;
    co_unsigned32_t ac;

    if (!runtime || !runtime->master_nmt || !runtime->master_dev)
        return -RT_EBUSY;
    if (!co_nmt_get_sync(runtime->master_nmt))
        return -RT_EBUSY;

    sub_1005 = co_dev_find_sub(runtime->master_dev, 0x1005, 0x00);
    sub_1006 = co_dev_find_sub(runtime->master_dev, 0x1006, 0x00);
    if (!sub_1005 || !sub_1006)
        return -RT_ERROR;

    cobid = co_sub_get_val_u32(sub_1005);
    if (period_us && !(cobid & CO_SYNC_COBID_PRODUCER))
        return -RT_EBUSY;

    /*
     * The active SYNC service caches 0x1006 and owns its periodic CAN timer.
     * Use the OD download indication so Lely updates both atomically in the
     * owner thread; a direct OD store would leave the running timer stale.
     */
    ac = co_sub_dn_ind_val(sub_1006, CO_DEFTYPE_UNSIGNED32, &value);
    if (ac) {
        LELY_RTT_LOG_W("SYNC period update rejected: abort=0x%08x",
                (unsigned int)ac);
        return -RT_ERROR;
    }
    return RT_EOK;
}

void
lely_rtt_master_sync_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_sync_control_request *request)
{
    rt_err_t err;

    if (!request)
        return;
    if (!runtime || !runtime->master_nmt || !runtime->master_dev) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

    if (request->operation == LELY_RTT_MASTER_SYNC_SET_PERIOD)
        err = lely_rtt_master_sync_set_period_owner(runtime, request->period_us);
    else
        err = -RT_EINVAL;

    lely_rtt_master_sync_complete(&request->sync, err);
}

void
lely_rtt_master_sync_cancel_queued(
        struct lely_rtt_master_sync_control_request *request)
{
    if (request)
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
}

/** @brief Submit one synchronous SYNC control operation to the owner thread. */
static rt_err_t
lely_rtt_master_sync_submit(lely_rtt_runtime_t *runtime,
        struct lely_rtt_master_sync_control_request *request)
{
    struct lely_rtt_master_command command;
    rt_err_t err;

    if (!runtime || !request || runtime->owner_thread == rt_thread_self())
        return -RT_EINVAL;

    err = lely_rtt_master_sync_init(&request->sync, "lelysync");
    if (err != RT_EOK)
        return err;

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_SYNC;
    command.data.sync_control.request = request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err == RT_EOK)
        err = lely_rtt_master_sync_wait(&request->sync);
    lely_rtt_master_sync_fini(&request->sync);
    return err;
}

rt_err_t
lely_rtt_runtime_configure_sync_ind(lely_rtt_runtime_t *runtime,
        lely_rtt_sync_ind_t *ind, void *data)
{
    if (!runtime || !runtime->event_initialized || runtime->owner_thread)
        return -RT_EINVAL;

    runtime->sync_app_ind = ind;
    runtime->sync_app_data = ind ? data : RT_NULL;
    return RT_EOK;
}

rt_err_t
lely_rtt_runtime_sync_set_period(lely_rtt_runtime_t *runtime,
        rt_uint32_t period_us)
{
    struct lely_rtt_master_sync_control_request request;

    if (!runtime)
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_SYNC_SET_PERIOD;
    request.period_us = period_us;
    return lely_rtt_master_sync_submit(runtime, &request);
}

rt_err_t
lely_rtt_runtime_get_sync(lely_rtt_runtime_t *runtime,
        struct lely_rtt_sync_event *event)
{
    rt_uint32_t begin;
    rt_uint32_t end;
    rt_uint32_t period_us;
    rt_uint8_t counter;
    rt_uint8_t role;

    if (!runtime || !event)
        return -RT_EINVAL;

    for (;;) {
        begin = (rt_uint32_t)rt_atomic_load(&runtime->sync_snapshot_seq);
        if (!begin)
            return -RT_EBUSY;
        if (begin & 1u) {
            /* Let the owner close the seqlock instead of priority busy-spinning. */
            rt_thread_mdelay(1);
            continue;
        }

        period_us = (rt_uint32_t)rt_atomic_load(
                &runtime->sync_snapshot_period_us);
        counter = (rt_uint8_t)rt_atomic_load(&runtime->sync_snapshot_counter);
        role = (rt_uint8_t)rt_atomic_load(&runtime->sync_snapshot_role);
        end = (rt_uint32_t)rt_atomic_load(&runtime->sync_snapshot_seq);
        if (begin == end && !(end & 1u))
            break;

        rt_thread_mdelay(1);
    }

    event->sequence = end / 2u;
    event->period_us = period_us;
    event->counter = counter;
    event->role = role;
    return RT_EOK;
}

#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
