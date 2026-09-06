/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-05     wdfk-prog         arbitrate application SDO with manual NMT configuration
 * 2026-09-06     wdfk-prog         add block transfer and application cancellation
 */

/**
 * @file master_sdo.c
 * @brief Owner-thread Client-SDO transactions for remote CANopen nodes.
 *
 * The owner lazily creates one application-owned CiA 301 default Client-SDO
 * per remote Node-ID. At most one application transaction is active per node.
 * Lely NMT boot creates its own default CSDO, so the application CSDO is
 * retired before Boot-up is chained into the NMT boot state machine.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_SDO)

#include <lely/co/csdo.h>

#include <limits.h>

/** @brief Completion event bit private to one SDO request object. */
#define LELY_RTT_SDO_EVENT_DONE (1u << 0)

enum lely_rtt_sdo_request_state {
    LELY_RTT_SDO_REQUEST_NEW = 0,
    LELY_RTT_SDO_REQUEST_QUEUED,
    LELY_RTT_SDO_REQUEST_ACTIVE,
    LELY_RTT_SDO_REQUEST_DONE,
};

struct lely_rtt_sdo_request {
    struct rt_event completion;
    rt_atomic_t state;
    rt_atomic_t completion_refs;

    lely_rtt_runtime_t *runtime;
    rt_uint32_t request_id;
    enum lely_rtt_sdo_operation operation;
    rt_uint8_t node_id;
    rt_uint16_t index;
    rt_uint8_t subindex;
    rt_uint32_t timeout_ms;
    rt_bool_t block_transfer;
    rt_uint8_t block_pst;

    void *buffer;
    rt_size_t size;

    enum lely_rtt_sdo_completion_status completion_status;
    rt_err_t local_error;
    rt_uint32_t abort_code;
    rt_bool_t cancel_requested;
};

/** @brief Publish one terminal request result and wake waiters exactly once. */
static void
lely_rtt_master_sdo_complete(lely_rtt_sdo_request_t *request,
        enum lely_rtt_sdo_completion_status status, rt_err_t local_error,
        rt_uint32_t abort_code)
{
    rt_uint32_t request_id;

    if (!request || rt_atomic_load(&request->state) == LELY_RTT_SDO_REQUEST_DONE)
        return;

    request->completion_status = status;
    request->local_error = local_error;
    request->abort_code = abort_code;
    request_id = request->request_id;

    /*
     * Publish DONE before waking a potentially higher-priority waiter. The pin
     * keeps the embedded event/request alive until rt_event_send() has fully
     * returned, so an awakened caller may safely proceed to destroy().
     */
    rt_atomic_store(&request->completion_refs, 1);
    rt_atomic_store(&request->state, LELY_RTT_SDO_REQUEST_DONE);
    if (rt_event_send(&request->completion, LELY_RTT_SDO_EVENT_DONE) != RT_EOK)
        LELY_RTT_LOG_E("SDO request completion event send failed: id=%u",
                (unsigned int)request_id);
    rt_atomic_store(&request->completion_refs, 0);
}

/** @brief Complete a remote SDO callback with protocol abort or success. */
static void
lely_rtt_master_sdo_finish_protocol(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request, rt_uint32_t abort_code)
{
    if (!runtime || !request)
        return;

    if (request->node_id <= CO_NUM_NODES
            && runtime->sdo_active[request->node_id] == request) {
        runtime->sdo_active[request->node_id] = RT_NULL;
        runtime->sdo_stop_pending[request->node_id] = RT_TRUE;
    }

    if (request->cancel_requested) {
        lely_rtt_master_sdo_complete(request, LELY_RTT_SDO_COMPLETION_CANCELED,
                RT_EOK, abort_code);
    } else if (abort_code) {
        lely_rtt_master_sdo_complete(request, LELY_RTT_SDO_COMPLETION_ABORT,
                RT_EOK, abort_code);
    } else {
        lely_rtt_master_sdo_complete(request, LELY_RTT_SDO_COMPLETION_OK,
                RT_EOK, 0);
    }
}

/** @brief Client-SDO download confirmation executed by the owner thread. */
static void
lely_rtt_master_sdo_dn_con(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, void *data)
{
    lely_rtt_sdo_request_t *request = data;

    (void)sdo;
    (void)idx;
    (void)subidx;
    if (!request || !request->runtime)
        return;

    lely_rtt_master_sdo_finish_protocol(request->runtime, request, ac);
}

/** @brief Client-SDO upload confirmation executed by the owner thread. */
static void
lely_rtt_master_sdo_up_con(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, const void *ptr,
        size_t n, void *data)
{
    lely_rtt_sdo_request_t *request = data;
    void *copy = RT_NULL;

    (void)sdo;
    (void)idx;
    (void)subidx;
    if (!request || !request->runtime)
        return;

    if (!ac && n) {
        copy = rt_malloc(n);
        if (!copy) {
            if (request->node_id <= CO_NUM_NODES
                    && request->runtime->sdo_active[request->node_id] == request) {
                request->runtime->sdo_active[request->node_id] = RT_NULL;
                request->runtime->sdo_stop_pending[request->node_id] = RT_TRUE;
            }
            lely_rtt_master_sdo_complete(request,
                    LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_ENOMEM, 0);
            return;
        }
        rt_memcpy(copy, ptr, n);
    }

    if (!ac) {
        request->buffer = copy;
        request->size = n;
    }
    lely_rtt_master_sdo_finish_protocol(request->runtime, request, ac);
}

/**
 * @brief Resolve the application-owned Client-SDO for one remote node.
 *
 * The first implementation intentionally uses the CiA 301 predefined SDO
 * connection derived from Node-ID. It does not borrow the NMT boot Client-SDO,
 * so application timeouts and callbacks cannot mutate NMT boot configuration.
 */
static co_csdo_t *
lely_rtt_master_sdo_get_client(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    co_csdo_t *sdo;

    if (!runtime || !runtime->can_net || !runtime->master_nmt
            || !node_id || node_id > CO_NUM_NODES)
        return RT_NULL;

    sdo = runtime->sdo_clients[node_id];
    if (sdo)
        return sdo;

    sdo = co_csdo_create(io_can_net_get_net(runtime->can_net), RT_NULL,
            node_id);
    if (!sdo) {
        LELY_RTT_LOG_E("Client-SDO creation failed: node=%u",
                (unsigned int)node_id);
        return RT_NULL;
    }

    runtime->sdo_clients[node_id] = sdo;
    return sdo;
}

lely_rtt_sdo_request_t *
lely_rtt_sdo_request_create(void)
{
    lely_rtt_sdo_request_t *request = rt_calloc(1, sizeof(*request));

    if (!request)
        return RT_NULL;
    if (rt_event_init(&request->completion, "lelysdo", RT_IPC_FLAG_FIFO)
            != RT_EOK) {
        rt_free(request);
        return RT_NULL;
    }

    rt_atomic_store(&request->state, LELY_RTT_SDO_REQUEST_NEW);
    rt_atomic_store(&request->completion_refs, 0);
    return request;
}

rt_err_t
lely_rtt_sdo_request_destroy(lely_rtt_sdo_request_t *request)
{
    const rt_atomic_t state = request
            ? rt_atomic_load(&request->state) : LELY_RTT_SDO_REQUEST_NEW;

    if (!request)
        return RT_EOK;
    if (state == LELY_RTT_SDO_REQUEST_QUEUED
            || state == LELY_RTT_SDO_REQUEST_ACTIVE)
        return -RT_EBUSY;

    /*
     * A waiter can preempt the owner from inside rt_event_send(). Do not detach
     * the embedded event until the completion publisher has returned from it.
     */
    while (rt_atomic_load(&request->completion_refs) != 0)
        rt_thread_mdelay(1);

    rt_event_detach(&request->completion);
    rt_free(request->buffer);
    request->buffer = RT_NULL;
    rt_free(request);
    return RT_EOK;
}

rt_err_t
lely_rtt_sdo_request_get_id(const lely_rtt_sdo_request_t *request,
        rt_uint32_t *request_id)
{
    if (!request || !request_id
            || rt_atomic_load((rt_atomic_t *)&request->state)
                    == LELY_RTT_SDO_REQUEST_NEW)
        return -RT_EINVAL;

    *request_id = request->request_id;
    return RT_EOK;
}

rt_err_t
lely_rtt_sdo_request_wait(lely_rtt_sdo_request_t *request,
        rt_int32_t timeout_ms)
{
    rt_uint32_t events = 0;
    rt_int32_t ticks;

    if (!request)
        return -RT_EINVAL;
    if (rt_atomic_load(&request->state) == LELY_RTT_SDO_REQUEST_NEW)
        return -RT_EINVAL;
    if (rt_atomic_load(&request->state) == LELY_RTT_SDO_REQUEST_DONE)
        return RT_EOK;

    if (timeout_ms == RT_WAITING_FOREVER) {
        ticks = RT_WAITING_FOREVER;
    } else if (timeout_ms < 0) {
        return -RT_EINVAL;
    } else if (!timeout_ms) {
        ticks = RT_WAITING_NO;
    } else {
        ticks = lely_rtt_timeout_ticks((rt_uint32_t)timeout_ms);
    }

    {
        rt_err_t err = rt_event_recv(&request->completion,
                LELY_RTT_SDO_EVENT_DONE, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                ticks, &events);

        if (err != RT_EOK)
            return err;
        return rt_atomic_load(&request->state) == LELY_RTT_SDO_REQUEST_DONE
                ? RT_EOK : -RT_ERROR;
    }
}

rt_err_t
lely_rtt_sdo_request_get_result(const lely_rtt_sdo_request_t *request,
        struct lely_rtt_sdo_result *result)
{
    if (!request || !result)
        return -RT_EINVAL;
    if (rt_atomic_load((rt_atomic_t *)&request->state)
            != LELY_RTT_SDO_REQUEST_DONE)
        return -RT_EBUSY;

    result->request_id = request->request_id;
    result->operation = request->operation;
    result->node_id = request->node_id;
    result->index = request->index;
    result->subindex = request->subindex;
    result->status = request->completion_status;
    result->local_error = request->local_error;
    result->abort_code = request->abort_code;
    result->data = request->buffer;
    result->size = request->size;
    return RT_EOK;
}

/** @brief Common preflight and queue submission for one SDO request. */
static rt_err_t
lely_rtt_runtime_post_sdo(lely_rtt_runtime_t *runtime,
        lely_rtt_sdo_request_t *request, enum lely_rtt_sdo_operation operation,
        rt_uint8_t node_id, rt_uint16_t index, rt_uint8_t subindex,
        const void *data, rt_size_t size, rt_uint32_t timeout_ms,
        rt_bool_t block_transfer, rt_uint8_t block_pst)
{
    struct lely_rtt_master_command command;
    void *copy = RT_NULL;
    rt_err_t err;

    if (!runtime || !request || !node_id || node_id > CO_NUM_NODES
            || !timeout_ms || timeout_ms > INT_MAX)
        return -RT_EINVAL;
    if ((rt_uint8_t)rt_atomic_load(&runtime->local_node_id) == node_id)
        return -RT_EINVAL;
    if (rt_atomic_load(&request->state) != LELY_RTT_SDO_REQUEST_NEW)
        return -RT_EBUSY;
    if (operation == LELY_RTT_SDO_DOWNLOAD && (!data || !size))
        return -RT_EINVAL;

    if (operation == LELY_RTT_SDO_DOWNLOAD) {
        copy = rt_malloc(size);
        if (!copy)
            return -RT_ENOMEM;
        rt_memcpy(copy, data, size);
    }

    request->runtime = runtime;
    request->request_id = (rt_uint32_t)rt_atomic_add(
            &runtime->sdo_next_request_id, 1) + 1u;
    request->operation = operation;
    request->node_id = node_id;
    request->index = index;
    request->subindex = subindex;
    request->timeout_ms = timeout_ms;
    request->block_transfer = block_transfer;
    request->block_pst = block_pst;
    request->buffer = copy;
    request->size = operation == LELY_RTT_SDO_DOWNLOAD ? size : 0;
    request->completion_status = LELY_RTT_SDO_COMPLETION_LOCAL_ERROR;
    request->local_error = RT_EOK;
    request->abort_code = 0;
    request->cancel_requested = RT_FALSE;
    rt_atomic_store(&request->state, LELY_RTT_SDO_REQUEST_QUEUED);

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_SDO;
    command.data.sdo.request = request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err != RT_EOK) {
        rt_atomic_store(&request->state, LELY_RTT_SDO_REQUEST_NEW);
        request->runtime = RT_NULL;
        request->buffer = RT_NULL;
        request->size = 0;
        rt_free(copy);
    }

    return err;
}

rt_err_t
lely_rtt_runtime_post_sdo_upload(lely_rtt_runtime_t *runtime,
        lely_rtt_sdo_request_t *request, rt_uint8_t node_id,
        rt_uint16_t index, rt_uint8_t subindex, rt_uint32_t timeout_ms)
{
    return lely_rtt_runtime_post_sdo(runtime, request, LELY_RTT_SDO_UPLOAD,
            node_id, index, subindex, RT_NULL, 0, timeout_ms, RT_FALSE, 0);
}

rt_err_t
lely_rtt_runtime_post_sdo_download(lely_rtt_runtime_t *runtime,
        lely_rtt_sdo_request_t *request, rt_uint8_t node_id,
        rt_uint16_t index, rt_uint8_t subindex, const void *data,
        rt_size_t size, rt_uint32_t timeout_ms)
{
    return lely_rtt_runtime_post_sdo(runtime, request, LELY_RTT_SDO_DOWNLOAD,
            node_id, index, subindex, data, size, timeout_ms, RT_FALSE, 0);
}

rt_err_t
lely_rtt_runtime_post_sdo_block_upload(lely_rtt_runtime_t *runtime,
        lely_rtt_sdo_request_t *request, rt_uint8_t node_id,
        rt_uint16_t index, rt_uint8_t subindex, rt_uint8_t pst,
        rt_uint32_t timeout_ms)
{
    return lely_rtt_runtime_post_sdo(runtime, request, LELY_RTT_SDO_UPLOAD,
            node_id, index, subindex, RT_NULL, 0, timeout_ms, RT_TRUE, pst);
}

rt_err_t
lely_rtt_runtime_post_sdo_block_download(lely_rtt_runtime_t *runtime,
        lely_rtt_sdo_request_t *request, rt_uint8_t node_id,
        rt_uint16_t index, rt_uint8_t subindex, const void *data,
        rt_size_t size, rt_uint32_t timeout_ms)
{
    return lely_rtt_runtime_post_sdo(runtime, request, LELY_RTT_SDO_DOWNLOAD,
            node_id, index, subindex, data, size, timeout_ms, RT_TRUE, 0);
}

rt_err_t
lely_rtt_sdo_request_cancel(lely_rtt_sdo_request_t *request)
{
    struct lely_rtt_master_command command;
    const rt_atomic_t state = request
            ? rt_atomic_load(&request->state) : LELY_RTT_SDO_REQUEST_NEW;

    if (!request || state == LELY_RTT_SDO_REQUEST_NEW || !request->runtime)
        return -RT_EINVAL;
    if (state == LELY_RTT_SDO_REQUEST_DONE)
        return -RT_EBUSY;

    /*
     * The queued cancel carries only stable identity values, not request*. If
     * the transfer wins the race and the caller destroys the completed request
     * before owner dispatch reaches this command, no stale request pointer is
     * left in the command queue.
     */
    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_SDO_CANCEL;
    command.data.sdo_cancel.node_id = request->node_id;
    command.data.sdo_cancel.request_id = request->request_id;
    return lely_rtt_master_command_post(request->runtime, &command);
}

/**
 * @brief Apply one owner-only SDO suspension state to one node or all nodes.
 *
 * A successful stop/reset command can be followed immediately by another
 * queued shell/application request before heartbeat/Boot-up catches up. The
 * explicit gate prevents that later SDO from racing the transition merely
 * because co_nmt_is_booting() has not become true yet.
 */
static void
lely_rtt_master_sdo_set_suspended(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_bool_t suspended, rt_bool_t reset_pending)
{
    rt_uint16_t first = node_id ? node_id : 1;
    rt_uint16_t last = node_id ? node_id : CO_NUM_NODES;
    rt_uint16_t id;

    if (!runtime || node_id > CO_NUM_NODES)
        return;

    for (id = first; id <= last; id++) {
        runtime->sdo_suspended[id] = suspended;
        runtime->sdo_reset_pending[id] = reset_pending;
    }
}

void
lely_rtt_master_sdo_before_boot(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    if (!runtime || !node_id || node_id > CO_NUM_NODES)
        return;

    lely_rtt_master_sdo_cancel_node(runtime, node_id);

    /*
     * NMT boot creates its own default Client-SDO. The application CSDO must
     * be destroyed, not merely left idle, because both would otherwise own a
     * receiver for 0x580 + node_id while boot is in progress.
     */
    if (runtime->sdo_clients[node_id]) {
        co_csdo_destroy(runtime->sdo_clients[node_id]);
        runtime->sdo_clients[node_id] = RT_NULL;
    }
    lely_rtt_master_sdo_set_suspended(runtime, node_id, RT_TRUE, RT_TRUE);
}

void
lely_rtt_master_sdo_on_nmt_state(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint8_t state)
{
    if (!runtime || !node_id || node_id > CO_NUM_NODES)
        return;

    switch (state) {
    case CO_NMT_ST_BOOTUP:
#if !LELY_NO_CO_NMT_BOOT
        if (runtime->master_nmt
                && co_nmt_is_booting(runtime->master_nmt, node_id)) {
            lely_rtt_master_sdo_set_suspended(runtime, node_id,
                    RT_TRUE, RT_TRUE);
        } else {
            lely_rtt_master_sdo_set_suspended(runtime, node_id,
                    RT_FALSE, RT_FALSE);
        }
#else
        /* Boot-up announces that reset communication completed into Pre-op. */
        lely_rtt_master_sdo_set_suspended(runtime, node_id, RT_FALSE, RT_FALSE);
#endif /* !LELY_NO_CO_NMT_BOOT */
        break;
    case CO_NMT_ST_STOP:
        lely_rtt_master_sdo_cancel_node(runtime, node_id);
        /*
         * STOP is not evidence that an earlier reset/boot sequence completed.
         * Strengthen suspension without clearing an existing reset hold.
         */
        runtime->sdo_suspended[node_id] = RT_TRUE;
        break;
    case CO_NMT_ST_PREOP:
    case CO_NMT_ST_START:
        /*
         * PREOP/START may be stale or arrive before the reset boot sequence
         * completes. Only Boot-up/boot completion may release a reset hold;
         * ordinary STOP suspension can still be cleared by a usable state.
         */
        if (!runtime->sdo_reset_pending[node_id])
            lely_rtt_master_sdo_set_suspended(runtime, node_id,
                    RT_FALSE, RT_FALSE);
        break;
    default:
        break;
    }
}

void
lely_rtt_master_sdo_on_nmt_command(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, enum lely_rtt_nmt_command command)
{
    rt_uint16_t first = node_id ? node_id : 1;
    rt_uint16_t last = node_id ? node_id : CO_NUM_NODES;
    rt_uint16_t id;

    if (!runtime || node_id > CO_NUM_NODES)
        return;

    switch (command) {
    case LELY_RTT_NMT_COMMAND_STOP:
        /*
         * A STOP command must not erase a reset hold for the same target.
         * Boot-up/boot completion remains the authority that clears it.
         */
        for (id = first; id <= last; id++)
            runtime->sdo_suspended[id] = RT_TRUE;
        break;
    case LELY_RTT_NMT_COMMAND_RESET_NODE:
    case LELY_RTT_NMT_COMMAND_RESET_COMM:
        lely_rtt_master_sdo_set_suspended(runtime, node_id, RT_TRUE, RT_TRUE);
        break;
    case LELY_RTT_NMT_COMMAND_START:
    case LELY_RTT_NMT_COMMAND_PREOP:
        /*
         * Do not let an immediate START/PREOP erase a reset hold. Reset stays
         * blocked until the remote Boot-up/boot result or a usable state proves
         * that communication initialization has completed.
         */
        for (id = first; id <= last; id++) {
            if (!runtime->sdo_reset_pending[id])
                runtime->sdo_suspended[id] = RT_FALSE;
        }
        break;
    default:
        break;
    }
}

void
lely_rtt_master_sdo_on_boot_complete(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint8_t state)
{
    rt_bool_t suspended;

    if (!runtime || !node_id || node_id > CO_NUM_NODES)
        return;

    /*
     * Boot process completion and usable SDO state are separate facts. Keep
     * Stopped/unknown nodes closed; a later PREOP/Operational state indication
     * can reopen the application path without reviving the reset hold.
     */
    suspended = state != CO_NMT_ST_PREOP && state != CO_NMT_ST_START;
    lely_rtt_master_sdo_set_suspended(runtime, node_id, suspended, RT_FALSE);
}

void
lely_rtt_master_sdo_dispatch(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request)
{
    co_csdo_t *sdo;
    int result;

    if (!runtime || !request
            || rt_atomic_load(&request->state) != LELY_RTT_SDO_REQUEST_QUEUED)
        return;
    if (!runtime->master_nmt || !runtime->can_net) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }
    if (runtime->sdo_suspended[request->node_id]) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }

#if !LELY_NO_CO_NMT_BOOT
    if (co_nmt_is_booting(runtime->master_nmt, request->node_id)) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }
#endif /* !LELY_NO_CO_NMT_BOOT */

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    if (lely_rtt_master_cfg_node_busy(runtime, request->node_id)) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */

    if (runtime->sdo_active[request->node_id]) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }

    sdo = lely_rtt_master_sdo_get_client(runtime, request->node_id);
    if (!sdo) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_ERROR, 0);
        return;
    }
    if (co_csdo_is_stopped(sdo) && co_csdo_start(sdo) == -1) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_ERROR, 0);
        return;
    }
    if (!co_csdo_is_idle(sdo)) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }

    co_csdo_set_timeout(sdo, (int)request->timeout_ms);
    runtime->sdo_active[request->node_id] = request;
    rt_atomic_store(&request->state, LELY_RTT_SDO_REQUEST_ACTIVE);

    if (request->operation == LELY_RTT_SDO_UPLOAD) {
        if (request->block_transfer) {
            result = co_csdo_blk_up_req(sdo, request->index, request->subindex,
                    request->block_pst, &lely_rtt_master_sdo_up_con, request);
        } else {
            result = co_csdo_up_req(sdo, request->index, request->subindex,
                    &lely_rtt_master_sdo_up_con, request);
        }
    } else if (request->block_transfer) {
        result = co_csdo_blk_dn_req(sdo, request->index, request->subindex,
                request->buffer, request->size, &lely_rtt_master_sdo_dn_con,
                request);
    } else {
        result = co_csdo_dn_req(sdo, request->index, request->subindex,
                request->buffer, request->size, &lely_rtt_master_sdo_dn_con,
                request);
    }

    if (result == -1 && runtime->sdo_active[request->node_id] == request) {
        runtime->sdo_active[request->node_id] = RT_NULL;
        runtime->sdo_stop_pending[request->node_id] = RT_TRUE;
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_ERROR, 0);
    }
}

void
lely_rtt_master_sdo_cancel_dispatch(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint32_t request_id)
{
    lely_rtt_sdo_request_t *request;
    co_csdo_t *sdo;

    if (!runtime || !node_id || node_id > CO_NUM_NODES)
        return;

    request = runtime->sdo_active[node_id];
    if (!request || request->request_id != request_id)
        return;

    sdo = runtime->sdo_clients[node_id];
    request->cancel_requested = RT_TRUE;

    /*
     * Frozen Lely co_csdo_abort_req() invokes the transfer confirmation before
     * returning. The confirmation clears sdo_active and marks stop_pending, so
     * request must not be dereferenced after this call if it woke its owner.
     */
    if (sdo && !co_csdo_is_idle(sdo) && !co_csdo_is_stopped(sdo))
        co_csdo_abort_req(sdo, CO_SDO_AC_NO_SDO);

    if (runtime->sdo_active[node_id] == request) {
        runtime->sdo_active[node_id] = RT_NULL;
        runtime->sdo_stop_pending[node_id] = RT_TRUE;
        lely_rtt_master_sdo_complete(request, LELY_RTT_SDO_COMPLETION_CANCELED,
                RT_EOK, CO_SDO_AC_NO_SDO);
    }
}

void
lely_rtt_master_sdo_cancel_queued(lely_rtt_sdo_request_t *request)
{
    if (!request)
        return;

    request->cancel_requested = RT_TRUE;
    lely_rtt_master_sdo_complete(request, LELY_RTT_SDO_COMPLETION_CANCELED,
            RT_EOK, 0);
}

void
lely_rtt_master_sdo_cancel_node(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    rt_uint16_t first = node_id ? node_id : 1;
    rt_uint16_t last = node_id ? node_id : CO_NUM_NODES;
    rt_uint16_t id;

    if (!runtime || node_id > CO_NUM_NODES)
        return;

    for (id = first; id <= last; id++) {
        lely_rtt_sdo_request_t *request = runtime->sdo_active[id];
        co_csdo_t *sdo = runtime->sdo_clients[id];

        if (request) {
            request->cancel_requested = RT_TRUE;
            /*
             * In the frozen Lely CSDO implementation abort_req() runs the
             * abort state transition and confirmation callback synchronously.
             * That callback may wake the request owner, so after this call only
             * compare the saved pointer value; do not dereference request.
             */
            if (sdo && !co_csdo_is_idle(sdo) && !co_csdo_is_stopped(sdo))
                co_csdo_abort_req(sdo, CO_SDO_AC_NO_SDO);

            if (runtime->sdo_active[id] == request) {
                runtime->sdo_active[id] = RT_NULL;
                lely_rtt_master_sdo_complete(request,
                        LELY_RTT_SDO_COMPLETION_CANCELED, RT_EOK,
                        CO_SDO_AC_NO_SDO);
            }
        }

        if (sdo && !co_csdo_is_stopped(sdo))
            co_csdo_stop(sdo);
        runtime->sdo_stop_pending[id] = RT_FALSE;
    }
}

void
lely_rtt_master_sdo_reap(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        co_csdo_t *sdo;

        if (!runtime->sdo_stop_pending[id] || runtime->sdo_active[id])
            continue;

        sdo = runtime->sdo_clients[id];
        if (!sdo) {
            runtime->sdo_stop_pending[id] = RT_FALSE;
            continue;
        }
        if (!co_csdo_is_stopped(sdo) && co_csdo_is_idle(sdo))
            co_csdo_stop(sdo);
        if (co_csdo_is_stopped(sdo))
            runtime->sdo_stop_pending[id] = RT_FALSE;
    }
}

void
lely_rtt_master_sdo_fini(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    lely_rtt_master_sdo_cancel_node(runtime, 0);
    for (id = 1; id <= CO_NUM_NODES; id++) {
        if (runtime->sdo_clients[id])
            co_csdo_destroy(runtime->sdo_clients[id]);
        runtime->sdo_clients[id] = RT_NULL;
        runtime->sdo_active[id] = RT_NULL;
        runtime->sdo_suspended[id] = RT_FALSE;
        runtime->sdo_reset_pending[id] = RT_FALSE;
        runtime->sdo_stop_pending[id] = RT_FALSE;
    }
}

#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
