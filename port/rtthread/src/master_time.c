/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-06     wdfk-prog         guard TIME control by service lifetime
 * 2026-09-06     wdfk-prog         avoid snapshot retry priority livelock
 */

/**
 * @file master_time.c
 * @brief Owner-safe CANopen TIME consumer snapshot and explicit producer bridge.
 *
 * The RT-Thread port drives Lely protocol timers from monotonic uptime. That
 * clock must never be reinterpreted as UTC or shifted to received wall time.
 * Consumer timestamps are therefore copied into a separate snapshot, while
 * producer frames are one-shot values encoded from caller-supplied Unix time.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_TIME)

#include <lely/can/msg.h>
#include <lely/can/net.h>
#include <lely/co/dev.h>
#include <lely/co/obj.h>
#include <lely/co/time.h>

/** Unix seconds at 1984-01-01 00:00:00, the CANopen TIME epoch. */
#define LELY_RTT_CANOPEN_TIME_EPOCH ((rt_int64_t)441763200)
/** Number of seconds in one CANopen TIME day. */
#define LELY_RTT_SECONDS_PER_DAY ((rt_int64_t)86400)
/** Number of representable days in the 16-bit CANopen TIME day field. */
#define LELY_RTT_CANOPEN_TIME_DAY_COUNT ((rt_int64_t)65536)

enum lely_rtt_master_time_operation {
    LELY_RTT_MASTER_TIME_CONFIGURE = 0,
    LELY_RTT_MASTER_TIME_SEND,
};

struct lely_rtt_master_time_request {
    struct lely_rtt_master_sync sync;
    enum lely_rtt_master_time_operation operation;
    rt_uint8_t roles;
    rt_int64_t seconds;
    rt_int32_t nanoseconds;
};

/** @brief Publish a TIME consumer callback value through an atomic seqlock. */
static void
lely_rtt_master_time_publish(struct lely_rtt_runtime *runtime,
        const struct timespec *tp)
{
    rt_uint64_t seconds;
    rt_uint32_t seq;

    if (!runtime || !tp)
        return;

    seconds = (rt_uint64_t)(rt_int64_t)tp->tv_sec;
    seq = (rt_uint32_t)rt_atomic_load(&runtime->time_snapshot_seq);
    if (seq & 1u)
        seq++;
    if (seq > 0xfffffffcu)
        seq = 0;

    rt_atomic_store(&runtime->time_snapshot_seq, (rt_atomic_t)(seq + 1u));
    rt_atomic_store(&runtime->time_snapshot_sec_lo,
            (rt_atomic_t)(rt_uint32_t)seconds);
    rt_atomic_store(&runtime->time_snapshot_sec_hi,
            (rt_atomic_t)(rt_uint32_t)(seconds >> 32));
    rt_atomic_store(&runtime->time_snapshot_nsec, (rt_atomic_t)tp->tv_nsec);
    rt_atomic_store(&runtime->time_snapshot_seq, (rt_atomic_t)(seq + 2u));
}

/** @brief TIME consumer indication executed by the Lely owner thread. */
static void
lely_rtt_master_time_ind(co_time_t *time, const struct timespec *tp, void *data)
{
    (void)time;
    lely_rtt_master_time_publish(data, tp);
}

void
lely_rtt_master_time_reset(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    rt_atomic_store(&runtime->time_snapshot_seq, 0);
    rt_atomic_store(&runtime->time_snapshot_sec_lo, 0);
    rt_atomic_store(&runtime->time_snapshot_sec_hi, 0);
    rt_atomic_store(&runtime->time_snapshot_nsec, 0);
}

rt_err_t
lely_rtt_master_time_bind(struct lely_rtt_runtime *runtime)
{
    co_time_t *time;

    if (!runtime || !runtime->master_nmt)
        return -RT_EINVAL;

    time = co_nmt_get_time(runtime->master_nmt);
    if (!time) {
        LELY_RTT_LOG_E("TIME bridge enabled but object 0x1012/service is unavailable");
        return -RT_ERROR;
    }

    co_time_set_ind(time, &lely_rtt_master_time_ind, runtime);
    return RT_EOK;
}

void
lely_rtt_master_time_unbind(struct lely_rtt_runtime *runtime)
{
    co_time_t *time;

    if (!runtime || !runtime->master_nmt)
        return;

    time = co_nmt_get_time(runtime->master_nmt);
    if (time)
        co_time_set_ind(time, RT_NULL, RT_NULL);
}

/** @brief Configure the consumer/producer role bits without changing CAN-ID. */
static rt_err_t
lely_rtt_master_time_configure_owner(struct lely_rtt_runtime *runtime,
        rt_uint8_t roles)
{
    co_sub_t *sub;
    co_unsigned32_t cobid;
    co_unsigned32_t ac;

    if (roles & ~(LELY_RTT_TIME_ROLE_CONSUMER | LELY_RTT_TIME_ROLE_PRODUCER))
        return -RT_EINVAL;
    /*
     * NMT owns the TIME service lifetime. Do not invoke object 0x1012's
     * indication after STOP/reset has destroyed that service; the frozen
     * upstream stop-state bug can otherwise leave stale callback data there.
     */
    if (!co_nmt_get_time(runtime->master_nmt))
        return -RT_EBUSY;

    sub = co_dev_find_sub(runtime->master_dev, 0x1012, 0x00);
    if (!sub)
        return -RT_ERROR;

    cobid = co_sub_get_val_u32(sub);
    cobid &= ~(CO_TIME_COBID_CONSUMER | CO_TIME_COBID_PRODUCER);
    if (roles & LELY_RTT_TIME_ROLE_CONSUMER)
        cobid |= CO_TIME_COBID_CONSUMER;
    if (roles & LELY_RTT_TIME_ROLE_PRODUCER)
        cobid |= CO_TIME_COBID_PRODUCER;

    ac = co_sub_dn_ind_val(sub, CO_DEFTYPE_UNSIGNED32, &cobid);
    if (ac) {
        LELY_RTT_LOG_W("TIME role update rejected: abort=0x%08x",
                (unsigned int)ac);
        return -RT_ERROR;
    }
    return RT_EOK;
}

/**
 * @brief Send one caller-supplied absolute CANopen TIME value immediately.
 *
 * This deliberately bypasses co_time_start_prod(): Lely's producer timer uses
 * the CAN network's monotonic time base, whereas the wire value is wall time.
 */
static rt_err_t
lely_rtt_master_time_send_owner(struct lely_rtt_runtime *runtime,
        rt_int64_t seconds, rt_int32_t nanoseconds)
{
    rt_int64_t since_epoch;
    co_sub_t *sub;
    co_unsigned32_t cobid;
    co_unsigned32_t ms;
    co_unsigned16_t days;
    struct can_msg msg = CAN_MSG_INIT;

    if (seconds < LELY_RTT_CANOPEN_TIME_EPOCH || nanoseconds < 0
            || nanoseconds >= 1000000000)
        return -RT_EINVAL;

    since_epoch = seconds - LELY_RTT_CANOPEN_TIME_EPOCH;
    if (since_epoch >= LELY_RTT_CANOPEN_TIME_DAY_COUNT
            * LELY_RTT_SECONDS_PER_DAY)
        return -RT_EINVAL;

    /* TIME is an active NMT service only in Pre-op/Operational. */
    if (!co_nmt_get_time(runtime->master_nmt))
        return -RT_EBUSY;

    sub = co_dev_find_sub(runtime->master_dev, 0x1012, 0x00);
    if (!sub)
        return -RT_ERROR;
    cobid = co_sub_get_val_u32(sub);
    if (!(cobid & CO_TIME_COBID_PRODUCER))
        return -RT_EBUSY;

    days = (co_unsigned16_t)(since_epoch / LELY_RTT_SECONDS_PER_DAY);
    ms = (co_unsigned32_t)((since_epoch % LELY_RTT_SECONDS_PER_DAY) * 1000
            + nanoseconds / 1000000);

    if (cobid & CO_TIME_COBID_FRAME) {
        msg.id = cobid & CAN_MASK_EID;
        msg.flags |= CAN_FLAG_IDE;
    } else {
        msg.id = cobid & CAN_MASK_BID;
    }
    msg.len = 6;
    msg.data[0] = (rt_uint8_t)ms;
    msg.data[1] = (rt_uint8_t)(ms >> 8);
    msg.data[2] = (rt_uint8_t)(ms >> 16);
    msg.data[3] = (rt_uint8_t)(ms >> 24);
    msg.data[4] = (rt_uint8_t)days;
    msg.data[5] = (rt_uint8_t)(days >> 8);

    return can_net_send(co_nmt_get_net(runtime->master_nmt), &msg) == -1
            ? -RT_ERROR : RT_EOK;
}

void
lely_rtt_master_time_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_time_request *request)
{
    rt_err_t err;

    if (!runtime || !request)
        return;
    if (!runtime->master_nmt || !runtime->master_dev) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

    if (request->operation == LELY_RTT_MASTER_TIME_CONFIGURE) {
        err = lely_rtt_master_time_configure_owner(runtime, request->roles);
    } else if (request->operation == LELY_RTT_MASTER_TIME_SEND) {
        err = lely_rtt_master_time_send_owner(runtime,
                request->seconds, request->nanoseconds);
    } else {
        err = -RT_EINVAL;
    }

    lely_rtt_master_sync_complete(&request->sync, err);
}

void
lely_rtt_master_time_cancel_queued(struct lely_rtt_master_time_request *request)
{
    if (request)
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
}

/** @brief Submit one synchronous TIME operation to the owner thread. */
static rt_err_t
lely_rtt_master_time_submit(lely_rtt_runtime_t *runtime,
        struct lely_rtt_master_time_request *request)
{
    struct lely_rtt_master_command command;
    rt_err_t err;

    if (!runtime || !request || runtime->owner_thread == rt_thread_self())
        return -RT_EINVAL;

    err = lely_rtt_master_sync_init(&request->sync, "lelytime");
    if (err != RT_EOK)
        return err;

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_TIME;
    command.data.time.request = request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err == RT_EOK)
        err = lely_rtt_master_sync_wait(&request->sync);
    lely_rtt_master_sync_fini(&request->sync);
    return err;
}

rt_err_t
lely_rtt_runtime_time_configure(lely_rtt_runtime_t *runtime, rt_uint8_t roles)
{
    struct lely_rtt_master_time_request request;

    if (!runtime
            || (roles & ~(LELY_RTT_TIME_ROLE_CONSUMER
                    | LELY_RTT_TIME_ROLE_PRODUCER)))
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_TIME_CONFIGURE;
    request.roles = roles;
    return lely_rtt_master_time_submit(runtime, &request);
}

rt_err_t
lely_rtt_runtime_time_send(lely_rtt_runtime_t *runtime,
        rt_int64_t seconds, rt_int32_t nanoseconds)
{
    struct lely_rtt_master_time_request request;
    rt_int64_t since_epoch;

    if (!runtime || seconds < LELY_RTT_CANOPEN_TIME_EPOCH || nanoseconds < 0
            || nanoseconds >= 1000000000)
        return -RT_EINVAL;

    since_epoch = seconds - LELY_RTT_CANOPEN_TIME_EPOCH;
    if (since_epoch >= LELY_RTT_CANOPEN_TIME_DAY_COUNT
            * LELY_RTT_SECONDS_PER_DAY)
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_TIME_SEND;
    request.seconds = seconds;
    request.nanoseconds = nanoseconds;
    return lely_rtt_master_time_submit(runtime, &request);
}

rt_err_t
lely_rtt_runtime_get_time(lely_rtt_runtime_t *runtime,
        struct lely_rtt_time_value *value)
{
    rt_uint32_t begin;
    rt_uint32_t end;
    rt_uint32_t lo;
    rt_uint32_t hi;
    rt_uint32_t nsec;

    if (!runtime || !value)
        return -RT_EINVAL;

    for (;;) {
        begin = (rt_uint32_t)rt_atomic_load(&runtime->time_snapshot_seq);
        if (!begin)
            return -RT_EBUSY;
        if (begin & 1u) {
            /*
             * The owner publishes the odd marker before the snapshot fields.
             * A higher-priority reader can preempt it at that point; sleeping
             * here, rather than yielding/spinning, lets the lower-priority
             * owner resume and close the seqlock with the even marker.
             */
            rt_thread_mdelay(1);
            continue;
        }

        lo = (rt_uint32_t)rt_atomic_load(&runtime->time_snapshot_sec_lo);
        hi = (rt_uint32_t)rt_atomic_load(&runtime->time_snapshot_sec_hi);
        nsec = (rt_uint32_t)rt_atomic_load(&runtime->time_snapshot_nsec);
        end = (rt_uint32_t)rt_atomic_load(&runtime->time_snapshot_seq);
        if (begin == end && !(end & 1u))
            break;

        /* Do not turn a publication collision into a priority busy-loop. */
        rt_thread_mdelay(1);
    }

    value->seconds = (rt_int64_t)(((rt_uint64_t)hi << 32) | lo);
    value->nanoseconds = (rt_int32_t)nsec;
    value->sequence = end / 2u;
    return RT_EOK;
}

#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
