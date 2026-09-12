/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 * 2026-09-12     wdfk-prog         pin runtime across staged SDO cancellation
 */

/**
 * @file master_sdo_request.c
 * @brief Application-facing Client-SDO request lifetime, submission and cancellation.
 *
 * @author wdfk-prog
 */

#include "master_sdo_internal.h"

#if defined(PKG_LELY_USING_MASTER_SDO)

#include <limits.h>

/** @brief Completion event bit private to one SDO request object. */
#define LELY_RTT_SDO_EVENT_DONE (1u << 0)

/** @brief Publish one terminal request result and wake waiters exactly once. */
void
lely_rtt_master_sdo_complete(lely_rtt_sdo_request_t *request,
        enum lely_rtt_sdo_completion_status status, rt_err_t local_error,
        rt_uint32_t abort_code)
{
    rt_atomic_t state;
    rt_uint32_t request_id;

    if (!request)
        return;

    state = rt_atomic_load(&request->state);
    if (state == LELY_RTT_SDO_REQUEST_DONE
            || state == LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_DONE
            || state == LELY_RTT_SDO_REQUEST_PENDING_CANCEL_DONE)
        return;

    request->completion_status = status;
    request->local_error = local_error;
    request->abort_code = abort_code;
    request_id = request->request_id;

    for (;;) {
        rt_atomic_t expected;
        rt_atomic_t staged;

        if (state == LELY_RTT_SDO_REQUEST_DONE
                || state == LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_DONE
                || state == LELY_RTT_SDO_REQUEST_PENDING_CANCEL_DONE)
            return;

        if (state == LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_PINNED
                || state == LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED) {
            lely_rtt_runtime_t *runtime = request->runtime;

            staged = state == LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_PINNED
                    ? LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_DONE
                    : LELY_RTT_SDO_REQUEST_PENDING_CANCEL_DONE;
            /*
             * Completion can detach this request while cancel() still owns
             * CANCEL_PINNED. Pin the runtime before publishing CANCEL_DONE
             * so owner teardown cannot invalidate request->runtime.
             */
            if (runtime)
                rt_atomic_add(&runtime->sdo_cancel_refs, 1);
            expected = state;
            if (rt_atomic_compare_exchange_strong(&request->state, &expected,
                    staged))
                return;
            if (runtime)
                rt_atomic_sub(&runtime->sdo_cancel_refs, 1);
            state = expected;
            continue;
        }

        /*
         * Claim DONE atomically against an active/pending cancel trying to pin
         * the request. completion_refs protects the event until send() returns.
         */
        rt_atomic_store(&request->completion_refs, 1);
        expected = state;
        if (rt_atomic_compare_exchange_strong(&request->state, &expected,
                LELY_RTT_SDO_REQUEST_DONE)) {
            if (rt_event_send(&request->completion, LELY_RTT_SDO_EVENT_DONE)
                    != RT_EOK) {
                LELY_RTT_LOG_E("SDO request completion event send failed: id=%u",
                        (unsigned int)request_id);
            }
            rt_atomic_store(&request->completion_refs, 0);
            return;
        }
        rt_atomic_store(&request->completion_refs, 0);
        state = expected;
    }
}

/**
 * @brief Release a cancel lifetime pin and publish any staged completion.
 * @return RT_TRUE when failed pending-cancel admission restored PENDING and
 *         owner progress must be rescheduled; otherwise RT_FALSE.
 */
static rt_bool_t
lely_rtt_sdo_request_cancel_unpin(lely_rtt_sdo_request_t *request,
        rt_atomic_t pinned_state, rt_bool_t cancel_admitted)
{
    const rt_atomic_t base_state =
            pinned_state == LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED
            ? LELY_RTT_SDO_REQUEST_PENDING : LELY_RTT_SDO_REQUEST_ACTIVE;
    const rt_atomic_t staged_state =
            pinned_state == LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED
            ? LELY_RTT_SDO_REQUEST_PENDING_CANCEL_DONE
            : LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_DONE;
    const rt_atomic_t released_state =
            cancel_admitted && base_state == LELY_RTT_SDO_REQUEST_PENDING
            ? LELY_RTT_SDO_REQUEST_CANCEL_PENDING : base_state;
    rt_atomic_t expected = pinned_state;

    if (rt_atomic_compare_exchange_strong(&request->state, &expected,
            released_state)) {
        return !cancel_admitted
                && base_state == LELY_RTT_SDO_REQUEST_PENDING;
    }

    if (expected == staged_state) {
        lely_rtt_runtime_t *runtime = request->runtime;
        const rt_uint32_t request_id = request->request_id;

        rt_atomic_store(&request->completion_refs, 1);
        expected = staged_state;
        if (rt_atomic_compare_exchange_strong(&request->state, &expected,
                LELY_RTT_SDO_REQUEST_DONE)) {
            if (rt_event_send(&request->completion, LELY_RTT_SDO_EVENT_DONE)
                    != RT_EOK) {
                LELY_RTT_LOG_E("SDO request completion event send failed: id=%u",
                        (unsigned int)request_id);
            }
        }
        rt_atomic_store(&request->completion_refs, 0);
        /*
         * Release the staged runtime pin only after completion publication;
         * the canceling caller performs no request/runtime access afterwards.
         */
        if (runtime)
            rt_atomic_sub(&runtime->sdo_cancel_refs, 1);
    }

    return RT_FALSE;
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

/**
 * @brief Drain runtime pins retained by completed cancel-pinned requests.
 *
 * Owner teardown must cross this barrier before releasing SDO/runtime storage
 * that an external canceling caller can still reach through request->runtime.
 */
void
lely_rtt_master_sdo_cancel_wait_idle(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    while (rt_atomic_load(&runtime->sdo_cancel_refs) != 0)
        rt_thread_mdelay(1);
}

rt_err_t
lely_rtt_sdo_request_destroy(lely_rtt_sdo_request_t *request)
{
    const rt_atomic_t state = request
            ? rt_atomic_load(&request->state) : LELY_RTT_SDO_REQUEST_NEW;

    if (!request)
        return RT_EOK;
    if (state == LELY_RTT_SDO_REQUEST_QUEUED
            || state == LELY_RTT_SDO_REQUEST_PENDING
            || state == LELY_RTT_SDO_REQUEST_CANCEL_PENDING
            || state == LELY_RTT_SDO_REQUEST_TEARDOWN_PENDING
            || state == LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED
            || state == LELY_RTT_SDO_REQUEST_PENDING_CANCEL_DONE
            || state == LELY_RTT_SDO_REQUEST_ACTIVE
            || state == LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_PINNED
            || state == LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_DONE)
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
    request->next = RT_NULL;
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
        request->next = RT_NULL;
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
    lely_rtt_runtime_t *runtime;
    rt_uint32_t request_id;
    rt_uint8_t node_id;
    rt_bool_t runtime_pin;
    rt_bool_t reschedule_pending;
    rt_err_t err;
    rt_atomic_t state;

    if (!request)
        return -RT_EINVAL;

    state = rt_atomic_load(&request->state);
    for (;;) {
        rt_atomic_t expected;
        rt_atomic_t pinned_state;

        if (state == LELY_RTT_SDO_REQUEST_NEW)
            return -RT_EINVAL;
        if (state == LELY_RTT_SDO_REQUEST_DONE)
            return -RT_EBUSY;
        if (state == LELY_RTT_SDO_REQUEST_CANCEL_PENDING)
            return RT_EOK;

        if (state == LELY_RTT_SDO_REQUEST_PENDING
                || state == LELY_RTT_SDO_REQUEST_ACTIVE) {
            /*
             * Pin the request before copying immutable identity. Pending uses
             * a separate pin state so the owner cannot promote it to ACTIVE
             * until command admission either succeeds or fails.
             */
            pinned_state = state == LELY_RTT_SDO_REQUEST_PENDING
                    ? LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED
                    : LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_PINNED;
            expected = state;
            if (!rt_atomic_compare_exchange_strong(&request->state, &expected,
                    pinned_state)) {
                state = expected;
                continue;
            }

            runtime = request->runtime;
            node_id = request->node_id;
            request_id = request->request_id;
            runtime_pin = RT_FALSE;

            if (state == LELY_RTT_SDO_REQUEST_PENDING && runtime) {
                /*
                 * Keep the runtime event alive through unpin + recovery wake.
                 * Cleanup closes callback admission only after command ingress,
                 * so failure here means owner teardown already provides progress.
                 */
                if (!lely_rtt_callback_acquire(runtime)) {
                    (void)lely_rtt_sdo_request_cancel_unpin(request,
                            pinned_state, RT_FALSE);
                    return -RT_EBUSY;
                }
                runtime_pin = RT_TRUE;
            }

            rt_memset(&command, 0, sizeof(command));
            command.type = LELY_RTT_MASTER_COMMAND_SDO_CANCEL;
            command.data.sdo_cancel.node_id = node_id;
            command.data.sdo_cancel.request_id = request_id;
            err = lely_rtt_master_command_post(runtime, &command);
            reschedule_pending = lely_rtt_sdo_request_cancel_unpin(request,
                    pinned_state, err == RT_EOK);

            if (reschedule_pending && runtime_pin) {
                /*
                 * Owner reap may already have observed the temporary pin and
                 * consumed the active-completion wake. Failed cancel admission
                 * adds no command wake, so restoring PENDING must wake owner
                 * again. request may be completed/freed after this send.
                 */
                if (rt_event_send(&runtime->event, LELY_RTT_EVENT_COMMAND)
                        != RT_EOK) {
                    LELY_RTT_LOG_E(
                            "SDO pending cancel recovery wake failed: id=%u",
                            (unsigned int)request_id);
                }
            }
            if (runtime_pin)
                lely_rtt_callback_release(runtime);
            return err;
        }

        if (state != LELY_RTT_SDO_REQUEST_QUEUED)
            return -RT_EBUSY;

        /*
         * This CAS is the global-command queued-cancel linearization point. If
         * it wins, the original SDO command remains responsible for terminal
         * publication and no remote transfer may start.
         */
        expected = LELY_RTT_SDO_REQUEST_QUEUED;
        if (rt_atomic_compare_exchange_strong(&request->state, &expected,
                LELY_RTT_SDO_REQUEST_CANCEL_PENDING))
            return RT_EOK;
        state = expected;
    }
}

#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
