/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-05     wdfk-prog         arbitrate application SDO with manual NMT configuration
 * 2026-09-06     wdfk-prog         add block transfer and race-safe application cancellation
 * 2026-09-11     wdfk-prog         add custom CSDO routing and per-node request FIFO
 * 2026-09-11     wdfk-prog         reschedule pending SDO after failed cancel enqueue
 * 2026-09-11     wdfk-prog         split request and Client-SDO transport helpers
 * 2026-09-12     wdfk-prog         drain cancel pins before SDO teardown
 */

/**
 * @file master_sdo.c
 * @brief Owner-thread scheduling and lifecycle coordination for Master Client-SDO.
 *
 * This translation unit owns per-node FIFO scheduling, dispatch/reap, NMT
 * transition arbitration and teardown. Request lifetime/public APIs live in
 * master_sdo_request.c; channel selection and transfer startup live in
 * master_sdo_client.c.
 *
 * @author wdfk-prog
 */

#include "master_sdo_internal.h"

#if defined(PKG_LELY_USING_MASTER_SDO)

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

/** @brief Claim a request from the global command queue or finish a queued cancel. */
static rt_bool_t
lely_rtt_master_sdo_claim(lely_rtt_sdo_request_t *request,
        rt_atomic_t desired_state)
{
    rt_atomic_t expected = LELY_RTT_SDO_REQUEST_QUEUED;

    if (rt_atomic_compare_exchange_strong(&request->state, &expected,
            desired_state))
        return RT_TRUE;

    if (expected == LELY_RTT_SDO_REQUEST_CANCEL_PENDING) {
        request->cancel_requested = RT_TRUE;
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_CANCELED, RT_EOK,
                CO_SDO_AC_NO_SDO);
    }
    return RT_FALSE;
}

/** @brief Return whether owner policy currently permits a new SDO for a node. */
static rt_bool_t
lely_rtt_master_sdo_node_available(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    if (!runtime || !runtime->master_nmt || !runtime->can_net
            || !node_id || node_id > CO_NUM_NODES
            || runtime->sdo_suspended[node_id])
        return RT_FALSE;

#if !LELY_NO_CO_NMT_BOOT
    if (co_nmt_is_booting(runtime->master_nmt, node_id))
        return RT_FALSE;
#endif /* !LELY_NO_CO_NMT_BOOT */

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    if (lely_rtt_master_cfg_node_busy(runtime, node_id))
        return RT_FALSE;
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */

    return RT_TRUE;
}

/** @brief Append one owner-claimed request to the bounded per-node FIFO. */
static rt_bool_t
lely_rtt_master_sdo_pending_append(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request)
{
    lely_rtt_sdo_request_t *cursor;
    lely_rtt_sdo_request_t *tail = RT_NULL;
    rt_uint16_t count = 0;

    cursor = runtime->sdo_pending[request->node_id];
    while (cursor) {
        if (++count >= PKG_LELY_MASTER_SDO_QUEUE_DEPTH)
            return RT_FALSE;
        tail = cursor;
        cursor = cursor->next;
    }

    if (!lely_rtt_master_sdo_claim(request, LELY_RTT_SDO_REQUEST_PENDING))
        return RT_TRUE;

    request->next = RT_NULL;
    if (tail)
        tail->next = request;
    else
        runtime->sdo_pending[request->node_id] = request;
    return RT_TRUE;
}

void
lely_rtt_master_sdo_dispatch(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request)
{
    const rt_uint8_t node_id = request ? request->node_id : 0;

    if (!runtime || !request || !node_id || node_id > CO_NUM_NODES)
        return;

    if (!lely_rtt_master_sdo_node_available(runtime, node_id)) {
        if (lely_rtt_master_sdo_claim(request, LELY_RTT_SDO_REQUEST_ACTIVE))
            lely_rtt_master_sdo_complete(request,
                    LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }

    /*
     * Never let a newly dispatched command bypass an older per-node request.
     * Only the owner mutates the linked FIFO; non-owner cancellation changes
     * request state and asks the owner to unlink by stable request identity.
     */
    if (runtime->sdo_active[node_id] || runtime->sdo_pending[node_id]) {
        if (!lely_rtt_master_sdo_pending_append(runtime, request)) {
            if (lely_rtt_master_sdo_claim(request,
                    LELY_RTT_SDO_REQUEST_ACTIVE)) {
                lely_rtt_master_sdo_complete(request,
                        LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
            }
        }
        return;
    }

    if (!lely_rtt_master_sdo_claim(request, LELY_RTT_SDO_REQUEST_ACTIVE))
        return;
    lely_rtt_master_sdo_start_active(runtime, request);
}

/** @brief Remove and cancel one pending request by stable identity. */
static rt_bool_t
lely_rtt_master_sdo_cancel_pending(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint32_t request_id)
{
    lely_rtt_sdo_request_t **link = &runtime->sdo_pending[node_id];

    while (*link) {
        lely_rtt_sdo_request_t *request = *link;

        if (request->request_id == request_id) {
            lely_rtt_sdo_request_t *next = request->next;

            *link = next;
            request->next = RT_NULL;
            request->cancel_requested = RT_TRUE;
            lely_rtt_master_sdo_complete(request,
                    LELY_RTT_SDO_COMPLETION_CANCELED, RT_EOK,
                    CO_SDO_AC_NO_SDO);
            return RT_TRUE;
        }
        link = &request->next;
    }
    return RT_FALSE;
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
    if (!request || request->request_id != request_id) {
        (void)lely_rtt_master_sdo_cancel_pending(runtime, node_id, request_id);
        return;
    }

    sdo = lely_rtt_master_sdo_get_client(runtime, node_id, RT_FALSE);
    request->cancel_requested = RT_TRUE;

    /*
     * Frozen Lely co_csdo_abort_req() invokes the transfer confirmation before
     * returning. The confirmation may wake the request owner, so do not
     * dereference request after abort_req() if sdo_active no longer owns it.
     */
    if (sdo && !co_csdo_is_idle(sdo) && !co_csdo_is_stopped(sdo))
        co_csdo_abort_req(sdo, CO_SDO_AC_NO_SDO);

    if (runtime->sdo_active[node_id] == request) {
        runtime->sdo_active[node_id] = RT_NULL;
        if (lely_rtt_master_sdo_uses_predefined(runtime, node_id))
            runtime->sdo_stop_pending[node_id] = RT_TRUE;
        lely_rtt_master_sdo_complete(request, LELY_RTT_SDO_COMPLETION_CANCELED,
                RT_EOK, CO_SDO_AC_NO_SDO);
    }
}

void
lely_rtt_master_sdo_cancel_queued(lely_rtt_sdo_request_t *request)
{
    rt_atomic_t state;
    rt_uint32_t abort_code;

    if (!request)
        return;

    state = rt_atomic_load(&request->state);
    for (;;) {
        rt_atomic_t expected;

        if (state == LELY_RTT_SDO_REQUEST_CANCEL_PENDING) {
            abort_code = CO_SDO_AC_NO_SDO;
            break;
        }
        if (state != LELY_RTT_SDO_REQUEST_QUEUED)
            return;

        /*
         * This CAS linearizes plain command-queue teardown against explicit
         * cancel. Per-node pending requests are canceled by sdo_fini() instead.
         */
        expected = LELY_RTT_SDO_REQUEST_QUEUED;
        if (rt_atomic_compare_exchange_strong(&request->state, &expected,
                LELY_RTT_SDO_REQUEST_TEARDOWN_PENDING)) {
            abort_code = 0;
            break;
        }
        state = expected;
    }

    request->cancel_requested = RT_TRUE;
    lely_rtt_master_sdo_complete(request, LELY_RTT_SDO_COMPLETION_CANCELED,
            RT_EOK, abort_code);
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
        co_csdo_t *sdo = lely_rtt_master_sdo_get_client(runtime,
                (rt_uint8_t)id, RT_FALSE);
        lely_rtt_sdo_request_t *pending;

        if (request) {
            request->cancel_requested = RT_TRUE;
            /*
             * abort_req() confirms synchronously in the frozen Lely revision.
             * Save all state needed below before the call; an awakened request
             * owner may destroy request as soon as the confirmation is sent.
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

        /*
         * Detach the complete FIFO before publishing any completion. This makes
         * the owner list independent of request lifetime once a waiter wakes.
         */
        pending = runtime->sdo_pending[id];
        runtime->sdo_pending[id] = RT_NULL;
        while (pending) {
            lely_rtt_sdo_request_t *next = pending->next;

            pending->next = RT_NULL;
            pending->cancel_requested = RT_TRUE;
            lely_rtt_master_sdo_complete(pending,
                    LELY_RTT_SDO_COMPLETION_CANCELED, RT_EOK,
                    CO_SDO_AC_NO_SDO);
            pending = next;
        }

        if (lely_rtt_master_sdo_uses_predefined(runtime, (rt_uint8_t)id)) {
            sdo = runtime->sdo_clients[id];
            if (sdo && !co_csdo_is_stopped(sdo))
                co_csdo_stop(sdo);
        }
        runtime->sdo_stop_pending[id] = RT_FALSE;
    }
}

/**
 * @brief Promote the FIFO head without allowing a successful pending cancel to race it.
 * @return RT_TRUE only when the detached head finished synchronously and the
 *         owner may immediately try the next FIFO entry; otherwise RT_FALSE.
 */
static rt_bool_t
lely_rtt_master_sdo_start_pending(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    lely_rtt_sdo_request_t *request;
    rt_atomic_t expected;

    if (!lely_rtt_master_sdo_node_available(runtime, node_id)
            || runtime->sdo_active[node_id])
        return RT_FALSE;

    request = runtime->sdo_pending[node_id];
    if (!request)
        return RT_FALSE;

    if (rt_atomic_load(&request->state) == LELY_RTT_SDO_REQUEST_CANCEL_PENDING) {
        lely_rtt_sdo_request_t *next = request->next;

        runtime->sdo_pending[node_id] = next;
        request->next = RT_NULL;
        request->cancel_requested = RT_TRUE;
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_CANCELED, RT_EOK,
                CO_SDO_AC_NO_SDO);
        return RT_FALSE;
    }

    expected = LELY_RTT_SDO_REQUEST_PENDING;
    if (!rt_atomic_compare_exchange_strong(&request->state, &expected,
            LELY_RTT_SDO_REQUEST_ACTIVE)) {
        /* A cancel caller may still hold the lifetime pin; retry next owner pass. */
        return RT_FALSE;
    }

    runtime->sdo_pending[node_id] = request->next;
    request->next = RT_NULL;
    lely_rtt_master_sdo_start_active(runtime, request);
    return runtime->sdo_active[node_id] == RT_NULL;
}

void
lely_rtt_master_sdo_reap(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        for (;;) {
            co_csdo_t *sdo;

            if (runtime->sdo_active[id])
                break;

            if (runtime->sdo_stop_pending[id]) {
                sdo = runtime->sdo_clients[id];
                if (!sdo) {
                    runtime->sdo_stop_pending[id] = RT_FALSE;
                } else {
                    if (!co_csdo_is_stopped(sdo) && co_csdo_is_idle(sdo))
                        co_csdo_stop(sdo);
                    if (co_csdo_is_stopped(sdo))
                        runtime->sdo_stop_pending[id] = RT_FALSE;
                }
            }

            if (runtime->sdo_stop_pending[id]
                    || !lely_rtt_master_sdo_start_pending(runtime,
                            (rt_uint8_t)id))
                break;

            /*
             * A promoted head can finish before start_active() returns. Retry
             * only for that outcome so its successor does not depend on a new
             * owner wake; every retry consumes one bounded FIFO entry.
             */
        }
    }
}

void
lely_rtt_master_sdo_fini(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    lely_rtt_master_sdo_cancel_node(runtime, 0);
    /*
     * cancel_node() can detach work still pinned by an external canceler.
     * Drain those runtime pins before clients and runtime state are torn down.
     */
    lely_rtt_master_sdo_cancel_wait_idle(runtime);
    for (id = 1; id <= CO_NUM_NODES; id++) {
        if (runtime->sdo_clients[id])
            co_csdo_destroy(runtime->sdo_clients[id]);
        runtime->sdo_clients[id] = RT_NULL;
        runtime->sdo_active[id] = RT_NULL;
        runtime->sdo_pending[id] = RT_NULL;
        runtime->sdo_suspended[id] = RT_FALSE;
        runtime->sdo_reset_pending[id] = RT_FALSE;
        runtime->sdo_stop_pending[id] = RT_FALSE;
        /* sdo_channel[] is startup configuration and persists across restart. */
    }
}

#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
