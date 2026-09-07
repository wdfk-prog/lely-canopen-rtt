/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-06     wdfk-prog         avoid snapshot retry priority livelock
 */

/**
 * @file master_od.c
 * @brief Owner-safe local application object-dictionary access for RT-Thread.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_LOCAL_OD)

#include <lely/co/dev.h>
#include <lely/co/obj.h>
#include <lely/co/sdo.h>

/** Manufacturer-specific CANopen object range exposed to application threads. */
#define LELY_RTT_LOCAL_OD_INDEX_MIN 0x2000u
#define LELY_RTT_LOCAL_OD_INDEX_MAX 0x5fffu

/**
 * @brief One owner-owned wrapper around an existing OD download indication.
 *
 * The wrapper never replaces application/protocol behavior. It first invokes
 * the indication that was present when the Master became ready and publishes
 * metadata only after that indication accepts a non-empty final transfer
 * segment. Zero-length/preflight indications are deliberately not reported as
 * application-visible value changes.
 */
struct lely_rtt_local_od_hook {
    struct lely_rtt_local_od_hook *next;
    struct lely_rtt_runtime *runtime;
    co_sub_t *sub;
    co_sub_dn_ind_t *previous;
    void *previous_data;
};

enum lely_rtt_local_od_operation {
    LELY_RTT_LOCAL_OD_READ = 0,
    LELY_RTT_LOCAL_OD_WRITE,
};

struct lely_rtt_local_od_request {
    struct lely_rtt_master_sync sync;
    enum lely_rtt_local_od_operation operation;
    rt_uint16_t index;
    rt_uint8_t subindex;
    const void *input;
    rt_size_t input_size;
    void *output;
    rt_size_t output_size;
    rt_uint32_t abort_code;
};

/** @brief Publish one successful manufacturer OD write through an atomic seqlock. */
static void
lely_rtt_local_od_publish(struct lely_rtt_runtime *runtime, co_sub_t *sub,
        const struct co_sdo_req *req)
{
    co_obj_t *obj;
    rt_uint32_t seq;
    rt_uint32_t size;
    rt_uint32_t source;

    if (!runtime || !sub || !req)
        return;

    obj = co_sub_get_obj(sub);
    if (!obj)
        return;

    size = req->size > (size_t)0xffffffffu
            ? 0xffffffffu : (rt_uint32_t)req->size;
    source = runtime->local_od_api_write_active
            ? LELY_RTT_LOCAL_OD_CHANGE_LOCAL_API
            : LELY_RTT_LOCAL_OD_CHANGE_PROTOCOL;

    seq = (rt_uint32_t)rt_atomic_load(&runtime->local_od_change_seq);
    if (seq & 1u)
        seq++;
    if (seq > 0xfffffffcu)
        seq = 0;

    rt_atomic_store(&runtime->local_od_change_seq, (rt_atomic_t)(seq + 1u));
    rt_atomic_store(&runtime->local_od_change_index,
            (rt_atomic_t)co_obj_get_idx(obj));
    rt_atomic_store(&runtime->local_od_change_subindex,
            (rt_atomic_t)co_sub_get_subidx(sub));
    rt_atomic_store(&runtime->local_od_change_source, (rt_atomic_t)source);
    rt_atomic_store(&runtime->local_od_change_size, (rt_atomic_t)size);
    rt_atomic_store(&runtime->local_od_change_seq, (rt_atomic_t)(seq + 2u));
}

/** @brief Chain one manufacturer OD download and publish successful final writes. */
static co_unsigned32_t
lely_rtt_local_od_dn_ind(co_sub_t *sub, struct co_sdo_req *req, void *data)
{
    struct lely_rtt_local_od_hook *hook = data;
    co_unsigned32_t ac;

    if (!hook || !hook->runtime || !hook->previous)
        return CO_SDO_AC_ERROR;

    ac = hook->previous(sub, req, hook->previous_data);
    if (!ac && req && req->nbyte && co_sdo_req_last(req))
        lely_rtt_local_od_publish(hook->runtime, sub, req);
    return ac;
}

void
lely_rtt_local_od_reset(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    rt_atomic_store(&runtime->local_od_change_seq, 0);
    rt_atomic_store(&runtime->local_od_change_index, 0);
    rt_atomic_store(&runtime->local_od_change_subindex, 0);
    rt_atomic_store(&runtime->local_od_change_source, 0);
    rt_atomic_store(&runtime->local_od_change_size, 0);
    runtime->local_od_api_write_active = RT_FALSE;
}

void
lely_rtt_local_od_unbind(struct lely_rtt_runtime *runtime)
{
    struct lely_rtt_local_od_hook *hook;

    if (!runtime)
        return;

    hook = runtime->local_od_hooks;
    runtime->local_od_hooks = RT_NULL;
    while (hook) {
        struct lely_rtt_local_od_hook *next = hook->next;
        co_sub_dn_ind_t *current = RT_NULL;
        void *current_data = RT_NULL;

        if (hook->sub) {
            co_sub_get_dn_ind(hook->sub, &current, &current_data);
            if (current == &lely_rtt_local_od_dn_ind && current_data == hook)
                co_sub_set_dn_ind(hook->sub, hook->previous,
                        hook->previous_data);
        }
        rt_free(hook);
        hook = next;
    }
    runtime->local_od_api_write_active = RT_FALSE;
}

rt_err_t
lely_rtt_local_od_bind(struct lely_rtt_runtime *runtime)
{
    co_obj_t *obj;

    if (!runtime || !runtime->master_dev || runtime->local_od_hooks)
        return -RT_EINVAL;

    lely_rtt_local_od_reset(runtime);
    for (obj = co_dev_first_obj(runtime->master_dev); obj;
            obj = co_obj_next(obj)) {
        co_unsigned16_t index = co_obj_get_idx(obj);
        co_sub_t *sub;

        if (index < LELY_RTT_LOCAL_OD_INDEX_MIN
                || index > LELY_RTT_LOCAL_OD_INDEX_MAX)
            continue;

        for (sub = co_obj_first_sub(obj); sub; sub = co_sub_next(sub)) {
            struct lely_rtt_local_od_hook *hook;

            if (!(co_sub_get_access(sub) & CO_ACCESS_WRITE))
                continue;

            hook = rt_calloc(1, sizeof(*hook));
            if (!hook) {
                lely_rtt_local_od_unbind(runtime);
                return -RT_ENOMEM;
            }

            hook->runtime = runtime;
            hook->sub = sub;
            co_sub_get_dn_ind(sub, &hook->previous, &hook->previous_data);
            if (!hook->previous) {
                rt_free(hook);
                lely_rtt_local_od_unbind(runtime);
                return -RT_ERROR;
            }

            hook->next = runtime->local_od_hooks;
            runtime->local_od_hooks = hook;
            co_sub_set_dn_ind(sub, &lely_rtt_local_od_dn_ind, hook);
        }
    }

    return RT_EOK;
}

/**
 * @brief Resolve one OD entry with the same ARRAY-active-element rule as SSDO.
 *
 * co_ssdo checks sub-index 0 before invoking co_sub_up_ind()/co_sub_dn_ind().
 * The local owner bridge must preserve that protocol-visible boundary instead
 * of exposing statically allocated but currently inactive ARRAY elements.
 */
static co_sub_t *
lely_rtt_local_od_find_sub(struct lely_rtt_runtime *runtime,
        struct lely_rtt_local_od_request *request)
{
    co_obj_t *obj;
    co_sub_t *sub;

    obj = co_dev_find_obj(runtime->master_dev, request->index);
    if (!obj) {
        request->abort_code = CO_SDO_AC_NO_OBJ;
        return RT_NULL;
    }
    sub = co_obj_find_sub(obj, request->subindex);
    if (!sub) {
        request->abort_code = CO_SDO_AC_NO_SUB;
        return RT_NULL;
    }
    if (co_obj_get_code(obj) == CO_OBJECT_ARRAY && request->subindex
            && request->subindex > co_obj_get_val_u8(obj, 0x00)) {
        request->abort_code = CO_SDO_AC_NO_DATA;
        return RT_NULL;
    }
    return sub;
}

/** @brief Execute one local OD SDO-style upload after entering the owner. */
static rt_err_t
lely_rtt_local_od_read_owner(struct lely_rtt_runtime *runtime,
        struct lely_rtt_local_od_request *request)
{
    struct co_sdo_req sdo_req;
    co_sub_t *sub;
    co_unsigned32_t ac;
    void *copy = RT_NULL;
    size_t copied = 0;
    size_t total = 0;

    sub = lely_rtt_local_od_find_sub(runtime, request);
    if (!sub)
        return -RT_ERROR;

    co_sdo_req_init(&sdo_req);
    for (;;) {
        ac = co_sub_up_ind(sub, &sdo_req);
        if (ac) {
            request->abort_code = ac;
            co_sdo_req_fini(&sdo_req);
            rt_free(copy);
            return -RT_ERROR;
        }

        if (!copy && sdo_req.size) {
            total = sdo_req.size;
            copy = rt_malloc(total);
            if (!copy) {
                co_sdo_req_fini(&sdo_req);
                return -RT_ENOMEM;
            }
        } else if (copy && sdo_req.size != total) {
            co_sdo_req_fini(&sdo_req);
            rt_free(copy);
            return -RT_ERROR;
        }

        if (sdo_req.offset != copied || copied > total
                || sdo_req.nbyte > total - copied
                || (sdo_req.nbyte && !sdo_req.buf)) {
            co_sdo_req_fini(&sdo_req);
            rt_free(copy);
            return -RT_ERROR;
        }
        if (sdo_req.nbyte) {
            rt_memcpy((rt_uint8_t *)copy + copied, sdo_req.buf,
                    sdo_req.nbyte);
            copied += sdo_req.nbyte;
        }
        if (co_sdo_req_last(&sdo_req))
            break;
        if (!sdo_req.nbyte) {
            co_sdo_req_fini(&sdo_req);
            rt_free(copy);
            return -RT_ENOSYS;
        }
    }

    if (copied != total) {
        co_sdo_req_fini(&sdo_req);
        rt_free(copy);
        return -RT_ERROR;
    }

    request->output = copy;
    request->output_size = (rt_size_t)copied;
    co_sdo_req_fini(&sdo_req);
    return RT_EOK;
}

/** @brief Execute one local OD SDO-style download after entering the owner. */
static rt_err_t
lely_rtt_local_od_write_owner(struct lely_rtt_runtime *runtime,
        struct lely_rtt_local_od_request *request)
{
    struct co_sdo_req sdo_req;
    co_sub_t *sub;
    co_unsigned32_t ac;

    sub = lely_rtt_local_od_find_sub(runtime, request);
    if (!sub)
        return -RT_ERROR;

    co_sdo_req_init(&sdo_req);
    sdo_req.size = request->input_size;
    sdo_req.buf = request->input;
    sdo_req.nbyte = request->input_size;
    sdo_req.offset = 0;

    runtime->local_od_api_write_active = RT_TRUE;
    ac = co_sub_dn_ind(sub, &sdo_req);
    runtime->local_od_api_write_active = RT_FALSE;

    request->abort_code = ac;
    co_sdo_req_fini(&sdo_req);
    return ac ? -RT_ERROR : RT_EOK;
}

void
lely_rtt_local_od_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_local_od_request *request)
{
    rt_err_t err;

    if (!runtime || !request)
        return;
    if (!runtime->master_dev) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

    if (request->operation == LELY_RTT_LOCAL_OD_READ)
        err = lely_rtt_local_od_read_owner(runtime, request);
    else if (request->operation == LELY_RTT_LOCAL_OD_WRITE)
        err = lely_rtt_local_od_write_owner(runtime, request);
    else
        err = -RT_EINVAL;

    lely_rtt_master_sync_complete(&request->sync, err);
}

void
lely_rtt_local_od_cancel_queued(struct lely_rtt_local_od_request *request)
{
    if (request)
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
}

/** @brief Submit one synchronous local OD request to the owner thread. */
static rt_err_t
lely_rtt_local_od_submit(lely_rtt_runtime_t *runtime,
        struct lely_rtt_local_od_request *request)
{
    struct lely_rtt_master_command command;
    rt_err_t err;

    if (!runtime || !request || runtime->owner_thread == rt_thread_self())
        return -RT_EINVAL;

    err = lely_rtt_master_sync_init(&request->sync, "lelyod");
    if (err != RT_EOK)
        return err;

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_LOCAL_OD;
    command.data.od.request = request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err == RT_EOK)
        err = lely_rtt_master_sync_wait(&request->sync);
    lely_rtt_master_sync_fini(&request->sync);
    return err;
}

rt_err_t
lely_rtt_runtime_local_od_read(lely_rtt_runtime_t *runtime,
        rt_uint16_t index, rt_uint8_t subindex, void **data, rt_size_t *size)
{
    struct lely_rtt_local_od_request request;
    rt_err_t err;

    if (!runtime || !data || !size || index < LELY_RTT_LOCAL_OD_INDEX_MIN
            || index > LELY_RTT_LOCAL_OD_INDEX_MAX)
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_LOCAL_OD_READ;
    request.index = index;
    request.subindex = subindex;
    err = lely_rtt_local_od_submit(runtime, &request);
    if (err != RT_EOK)
        return err;

    *data = request.output;
    *size = request.output_size;
    return RT_EOK;
}

rt_err_t
lely_rtt_runtime_local_od_write(lely_rtt_runtime_t *runtime,
        rt_uint16_t index, rt_uint8_t subindex, const void *data, rt_size_t size)
{
    struct lely_rtt_local_od_request request;

    if (!runtime || !data || !size || index < LELY_RTT_LOCAL_OD_INDEX_MIN
            || index > LELY_RTT_LOCAL_OD_INDEX_MAX)
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_LOCAL_OD_WRITE;
    request.index = index;
    request.subindex = subindex;
    request.input = data;
    request.input_size = size;
    return lely_rtt_local_od_submit(runtime, &request);
}

void
lely_rtt_local_od_free(void *data)
{
    rt_free(data);
}

rt_err_t
lely_rtt_runtime_get_local_od_change(lely_rtt_runtime_t *runtime,
        struct lely_rtt_local_od_change *change)
{
    rt_uint32_t begin;
    rt_uint32_t end;
    rt_uint32_t index;
    rt_uint32_t subindex;
    rt_uint32_t source;
    rt_uint32_t size;

    if (!runtime || !change)
        return -RT_EINVAL;

    for (;;) {
        begin = (rt_uint32_t)rt_atomic_load(&runtime->local_od_change_seq);
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

        index = (rt_uint32_t)rt_atomic_load(&runtime->local_od_change_index);
        subindex = (rt_uint32_t)rt_atomic_load(
                &runtime->local_od_change_subindex);
        source = (rt_uint32_t)rt_atomic_load(&runtime->local_od_change_source);
        size = (rt_uint32_t)rt_atomic_load(&runtime->local_od_change_size);
        end = (rt_uint32_t)rt_atomic_load(&runtime->local_od_change_seq);
        if (begin == end && !(end & 1u))
            break;

        /* Do not turn a publication collision into a priority busy-loop. */
        rt_thread_mdelay(1);
    }

    change->index = (rt_uint16_t)index;
    change->subindex = (rt_uint8_t)subindex;
    change->source = (enum lely_rtt_local_od_change_source)source;
    change->size = size;
    change->sequence = end / 2u;
    return RT_EOK;
}

#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
