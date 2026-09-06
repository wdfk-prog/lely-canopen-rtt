/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-06     wdfk-prog         fix CFG lifetime and reject restore arbitration
 */

/**
 * @file master_cfg.c
 * @brief Owner-thread manual NMT configuration requests for remote CANopen nodes.
 *
 * The caller keeps each request on its stack while blocked. An active Lely
 * configuration callback may therefore retain the request pointer until Lely
 * destroys the per-node configuration service. Runtime teardown uses
 * co_nmt_destroy() as that lifetime barrier; local reset/PRE-OP indications are
 * equivalent barriers only after upstream co_nmt_slaves_fini() has run.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)

#include <lely/co/dev.h>
#include <lely/co/obj.h>

#include <limits.h>

struct lely_rtt_master_cfg_request {
    struct lely_rtt_master_sync sync;
    struct lely_rtt_runtime *runtime;
    struct lely_rtt_nmt_cfg_result result;
    rt_uint32_t timeout_ms;
    rt_bool_t cancel_requested;
};

/** @brief Complete one manual configuration request and clear owner state. */
static void
lely_rtt_master_cfg_complete(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_cfg_request *request,
        enum lely_rtt_nmt_cfg_completion_status status, rt_err_t local_error,
        rt_uint32_t abort_code)
{
    if (!request)
        return;

    if (runtime && request->result.node_id <= CO_NUM_NODES
            && runtime->cfg_active[request->result.node_id] == request)
        runtime->cfg_active[request->result.node_id] = RT_NULL;

    request->result.status = status;
    request->result.local_error = local_error;
    request->result.abort_code = abort_code;
    lely_rtt_master_sync_complete(&request->sync, RT_EOK);
}

/** @brief Lely NMT configuration completion callback executed by the owner. */
static void
lely_rtt_master_cfg_con(co_nmt_t *nmt, co_unsigned8_t id,
        co_unsigned32_t ac, void *data)
{
    struct lely_rtt_master_cfg_request *request = data;
    struct lely_rtt_runtime *runtime;

    (void)nmt;
    if (!request || request->result.node_id != id)
        return;

    runtime = request->runtime;
    if (request->cancel_requested) {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_CANCELED, RT_EOK, ac);
    } else if (ac) {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_ABORT, RT_EOK, ac);
    } else {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_OK, RT_EOK, 0);
    }
}

rt_bool_t
lely_rtt_master_cfg_node_busy(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    return runtime && node_id && node_id <= CO_NUM_NODES
            && runtime->cfg_active[node_id] != RT_NULL;
}

/**
 * @brief Check whether the static Master OD can perform useful configuration.
 *
 * With LELY_NO_CO_DCF=1, this bridge accepts an embedded concise DCF
 * (0x1F22) or an explicitly installed cfg_ind callback. The 0x1F8A restore
 * path is rejected separately because its reset/Boot-up handshake conflicts
 * with the current Master's automatic NMT-boot ownership. Without supported
 * work, upstream Lely can complete co_nmt_cfg_req() successfully without
 * writing anything, so the bridge fails closed instead.
 */
static rt_bool_t
lely_rtt_master_cfg_has_work(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    co_nmt_cfg_ind_t *cfg_ind = RT_NULL;
    co_unsigned32_t assignment;
    co_sub_t *sub_1f22;

    if (!runtime || !runtime->master_nmt || !runtime->master_dev || !node_id
            || node_id > CO_NUM_NODES)
        return RT_FALSE;

    assignment = co_dev_get_val_u32(runtime->master_dev, 0x1f81, node_id);
    if (!(assignment & 0x01u))
        return RT_FALSE;

    sub_1f22 = co_dev_find_sub(runtime->master_dev, 0x1f22, node_id);
    if (sub_1f22 && co_sub_sizeof_val(sub_1f22))
        return RT_TRUE;
    co_nmt_get_cfg_ind(runtime->master_nmt, &cfg_ind, RT_NULL);
    return cfg_ind != RT_NULL;
}

/**
 * @brief Return whether Lely would enter the unsupported restore/reset path.
 *
 * That path temporarily owns the node's Boot-up handshake and predefined CSDO.
 * The current Master also auto-consumes Boot-up and may start NMT boot, so the
 * bridge rejects restore rather than allowing two protocol owners to race.
 */
static rt_bool_t
lely_rtt_master_cfg_restore_requested(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    co_unsigned32_t assignment;
    co_sub_t *sub_1f8a;

    if (!runtime || !runtime->master_dev || !node_id
            || node_id > CO_NUM_NODES)
        return RT_FALSE;

    assignment = co_dev_get_val_u32(runtime->master_dev, 0x1f81, node_id);
    if (!(assignment & 0x80u))
        return RT_FALSE;

    sub_1f8a = co_dev_find_sub(runtime->master_dev, 0x1f8a, node_id);
    return sub_1f8a && co_sub_get_val_u8(sub_1f8a) != 0;
}

void
lely_rtt_master_cfg_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_cfg_request *request)
{
    const rt_uint8_t node_id = request ? request->result.node_id : 0;

    if (!runtime || !request || !node_id || node_id > CO_NUM_NODES)
        return;
    if (!runtime->master_nmt || !runtime->can_net) {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }
    if (runtime->cfg_active[node_id]) {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }
    if (!(co_dev_get_val_u32(runtime->master_dev, 0x1f81, node_id) & 0x01u)) {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_EINVAL, 0);
        return;
    }
    if (lely_rtt_master_cfg_restore_requested(runtime, node_id)) {
        LELY_RTT_LOG_W(
                "NMT configuration 0x1F8A restore path is unsupported for node %u",
                (unsigned int)node_id);
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_ENOSYS, 0);
        return;
    }
    if (!lely_rtt_master_cfg_has_work(runtime, node_id)) {
        LELY_RTT_LOG_W(
                "NMT configuration has no supported 0x1F22/cfg_ind work for node %u",
                (unsigned int)node_id);
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_ENOSYS, 0);
        return;
    }

#if !LELY_NO_CO_NMT_BOOT
    if (co_nmt_is_booting(runtime->master_nmt, node_id)) {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }
#endif /* !LELY_NO_CO_NMT_BOOT */

#if defined(PKG_LELY_USING_MASTER_SDO)
    /* The manual NMT configuration service needs the same predefined SDO pair. */
    lely_rtt_master_sdo_cancel_node(runtime, node_id);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

    runtime->cfg_active[node_id] = request;
    if (co_nmt_cfg_req(runtime->master_nmt, node_id,
            (int)request->timeout_ms, &lely_rtt_master_cfg_con, request) == -1
            && runtime->cfg_active[node_id] == request) {
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_ERROR, 0);
    }
}

void
lely_rtt_master_cfg_cancel_queued(struct lely_rtt_master_cfg_request *request)
{
    if (!request)
        return;

    request->cancel_requested = RT_TRUE;
    lely_rtt_master_cfg_complete(RT_NULL, request,
            LELY_RTT_NMT_CFG_COMPLETION_CANCELED, RT_EOK, 0);
}

void
lely_rtt_master_cfg_prepare_nmt_destroy(struct lely_rtt_runtime *runtime)
{
    /*
     * Do not complete active requests here. Lely can still call their cfg_con
     * until co_nmt_destroy() has released every per-node configuration service.
     */
    rt_uint16_t id;

    if (!runtime)
        return;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        if (runtime->cfg_active[id])
            runtime->cfg_active[id]->cancel_requested = RT_TRUE;
    }
}

/** Complete all stack-backed CFG requests after a proven callback barrier. */
static void
lely_rtt_master_cfg_retire_all(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        struct lely_rtt_master_cfg_request *request = runtime->cfg_active[id];

        if (!request)
            continue;
        request->cancel_requested = RT_TRUE;
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_CANCELED, RT_EOK, 0);
    }
}

void
lely_rtt_master_cfg_after_nmt_destroy(struct lely_rtt_runtime *runtime)
{
    /* co_nmt_destroy() has removed every cfg_con/cfg_data owner by this point. */
    lely_rtt_master_cfg_retire_all(runtime);
}

void
lely_rtt_master_cfg_on_nmt_command(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, enum lely_rtt_nmt_command command)
{
    rt_uint16_t first;
    rt_uint16_t last;
    rt_uint16_t id;

    if (!runtime || node_id > CO_NUM_NODES)
        return;
    if (command != LELY_RTT_NMT_COMMAND_STOP
            && command != LELY_RTT_NMT_COMMAND_RESET_NODE
            && command != LELY_RTT_NMT_COMMAND_RESET_COMM)
        return;

    first = node_id ? node_id : 1;
    last = node_id ? node_id : CO_NUM_NODES;
    for (id = first; id <= last; id++) {
        if (runtime->cfg_active[id])
            runtime->cfg_active[id]->cancel_requested = RT_TRUE;
    }
}

void
lely_rtt_master_cfg_on_local_nmt_state(struct lely_rtt_runtime *runtime,
        rt_uint8_t state)
{
    if (!runtime || (state != 0 && state != CO_NMT_ST_PREOP))
        return;

    /*
     * Frozen upstream emits reset state 0 and PRE-OP only after
     * co_nmt_slaves_fini() has destroyed slave->cfg and cleared cfg callback
     * data. STOP does not run that teardown and must not release caller stacks.
     */
    lely_rtt_master_cfg_retire_all(runtime);
}

rt_err_t
lely_rtt_runtime_nmt_configure(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint32_t timeout_ms,
        struct lely_rtt_nmt_cfg_result *result)
{
    struct lely_rtt_master_cfg_request request;
    struct lely_rtt_master_command command;
    rt_err_t err;

    if (!runtime || !result || !node_id || node_id > CO_NUM_NODES
            || !timeout_ms || timeout_ms > INT_MAX)
        return -RT_EINVAL;
    if (runtime->owner_thread == rt_thread_self())
        return -RT_EINVAL;
    if ((rt_uint8_t)rt_atomic_load(&runtime->local_node_id) == node_id)
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.runtime = runtime;
    request.result.node_id = node_id;
    request.result.status = LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR;
    request.result.local_error = -RT_EBUSY;
    request.timeout_ms = timeout_ms;

    err = lely_rtt_master_sync_init(&request.sync, "lelycfg");
    if (err != RT_EOK)
        return err;

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_NMT_CFG;
    command.data.cfg.request = &request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err == RT_EOK)
        err = lely_rtt_master_sync_wait(&request.sync);

    if (err == RT_EOK)
        *result = request.result;
    lely_rtt_master_sync_fini(&request.sync);
    return err;
}

#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
