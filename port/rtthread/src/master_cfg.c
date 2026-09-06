/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-06     wdfk-prog         fix CFG lifetime and reject restore arbitration
 * 2026-09-06     wdfk-prog         add application concise DCF and stage diagnostics
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

#include <lely/co/csdo.h>
#include <lely/co/dev.h>
#include <lely/co/obj.h>

#include <limits.h>

struct lely_rtt_master_cfg_source {
    struct lely_rtt_runtime *runtime;
    rt_uint8_t node_id;
    rt_uint32_t entries;
    rt_size_t size;
    rt_uint8_t data[];
};

struct lely_rtt_master_cfg_request {
    struct lely_rtt_master_sync sync;
    struct lely_rtt_runtime *runtime;
    struct lely_rtt_nmt_cfg_result result;
    struct lely_rtt_nmt_cfg_diagnostic diagnostic;
    rt_uint32_t timeout_ms;
    rt_bool_t cancel_requested;
};

static void lely_rtt_master_cfg_ind(co_nmt_t *nmt, co_unsigned8_t id,
        co_csdo_t *sdo, void *data);

/** @brief Load one little-endian 32-bit field from a concise DCF. */
static rt_uint32_t
lely_rtt_master_cfg_load_u32(const rt_uint8_t *data)
{
    return (rt_uint32_t)data[0]
            | ((rt_uint32_t)data[1] << 8)
            | ((rt_uint32_t)data[2] << 16)
            | ((rt_uint32_t)data[3] << 24);
}

/** @brief Validate concise-DCF framing before caller data is copied. */
static rt_bool_t
lely_rtt_master_cfg_validate_dcf(const void *data, rt_size_t size,
        rt_uint32_t *entries)
{
    const rt_uint8_t *bytes = data;
    rt_uint32_t count;
    rt_uint32_t i;
    rt_size_t offset = 4;

    if (!bytes || size < 4 || !entries)
        return RT_FALSE;

    count = lely_rtt_master_cfg_load_u32(bytes);
    if (!count)
        return RT_FALSE;

    for (i = 0; i < count; i++) {
        rt_uint32_t value_size;

        if (size - offset < 7)
            return RT_FALSE;
        value_size = lely_rtt_master_cfg_load_u32(bytes + offset + 3);
        offset += 7;
        if ((rt_size_t)value_size > size - offset)
            return RT_FALSE;
        offset += (rt_size_t)value_size;
    }

    if (offset != size)
        return RT_FALSE;

    *entries = count;
    return RT_TRUE;
}

rt_err_t
lely_rtt_runtime_configure_nmt_dcf(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, const void *data, rt_size_t size)
{
    struct lely_rtt_master_cfg_source *source;
    rt_uint32_t entries;

    if (!runtime || !runtime->event_initialized || runtime->owner_thread
            || !node_id || node_id > CO_NUM_NODES)
        return -RT_EINVAL;
    if (runtime->cfg_sources[node_id])
        return -RT_EBUSY;
    if (!lely_rtt_master_cfg_validate_dcf(data, size, &entries)
            || size > (rt_size_t)-1 - sizeof(*source))
        return -RT_EINVAL;

    source = rt_malloc(sizeof(*source) + size);
    if (!source)
        return -RT_ENOMEM;

    source->runtime = runtime;
    source->node_id = node_id;
    source->entries = entries;
    source->size = size;
    rt_memcpy(source->data, data, size);
    runtime->cfg_sources[node_id] = source;
    return RT_EOK;
}

void
lely_rtt_master_cfg_sources_fini(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        rt_free(runtime->cfg_sources[id]);
        runtime->cfg_sources[id] = RT_NULL;
    }
}

/** @brief Return whether at least one application source requires cfg_ind. */
static rt_bool_t
lely_rtt_master_cfg_has_application_source(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return RT_FALSE;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        if (runtime->cfg_sources[id])
            return RT_TRUE;
    }
    return RT_FALSE;
}

rt_err_t
lely_rtt_master_cfg_bind(struct lely_rtt_runtime *runtime)
{
    co_nmt_cfg_ind_t *cfg_ind = RT_NULL;
    void *cfg_data = RT_NULL;

    if (!runtime || !runtime->master_nmt)
        return -RT_EINVAL;

    runtime->cfg_tearing_down = RT_FALSE;
    if (!lely_rtt_master_cfg_has_application_source(runtime))
        return RT_EOK;

    co_nmt_get_cfg_ind(runtime->master_nmt, &cfg_ind, &cfg_data);
    if (cfg_ind && (cfg_ind != &lely_rtt_master_cfg_ind
            || cfg_data != runtime)) {
        LELY_RTT_LOG_E(
                "NMT cfg_ind is already owned by another application callback");
        return -RT_EBUSY;
    }

    co_nmt_set_cfg_ind(runtime->master_nmt, &lely_rtt_master_cfg_ind, runtime);
    return RT_EOK;
}

/** @brief Complete one manual configuration request and clear owner state. */
static void
lely_rtt_master_cfg_complete(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_cfg_request *request,
        enum lely_rtt_nmt_cfg_completion_status status, rt_err_t local_error,
        rt_uint32_t abort_code)
{
    if (!request)
        return;

    if (runtime && request->result.node_id <= CO_NUM_NODES) {
        const rt_uint8_t node_id = request->result.node_id;

        if (runtime->cfg_active[node_id] == request) {
            runtime->cfg_active[node_id] = RT_NULL;
            runtime->cfg_resume_pending[node_id] = RT_FALSE;
            runtime->cfg_resume_abort_code[node_id] = 0;
        }
    }

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
        request->diagnostic.stage = LELY_RTT_NMT_CFG_STAGE_COMPLETE;
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_OK, RT_EOK, 0);
    }
}

/** @brief Stage the managed application concise-DCF result for owner reaping. */
static void
lely_rtt_master_cfg_dcf_con(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, void *data)
{
    struct lely_rtt_master_cfg_source *source = data;
    struct lely_rtt_master_cfg_request *request;
    struct lely_rtt_runtime *runtime;

    (void)sdo;
    if (!source || !source->runtime)
        return;

    runtime = source->runtime;
    request = runtime->cfg_active[source->node_id];
    if (!request || runtime->cfg_tearing_down)
        return;

    request->diagnostic.last_index = idx;
    request->diagnostic.last_subindex = subidx;

    /*
     * co_csdo_stop() invokes this callback synchronously while a local NMT
     * reset can already be destroying the enclosing Lely CFG object. Calling
     * cfg_res here would recursively destroy that same object. Stage only
     * stable node/result data; the owner reaper resumes CFG after the current
     * Lely call stack has fully unwound, or drops the result if reset retired it.
     */
    runtime->cfg_resume_abort_code[source->node_id] = ac;
    runtime->cfg_resume_pending[source->node_id] = RT_TRUE;
}

/**
 * @brief Managed Lely application configuration callback.
 *
 * Lely also reaches cfg_ind from automatic configuration/boot paths. The
 * copied RT-Thread application DCF is intentionally manual-only, so it is
 * consumed only while cfg_active proves this bridge owns the node request.
 */
static void
lely_rtt_master_cfg_ind(co_nmt_t *nmt, co_unsigned8_t id,
        co_csdo_t *sdo, void *data)
{
    struct lely_rtt_runtime *runtime = data;
    struct lely_rtt_master_cfg_request *request;
    struct lely_rtt_master_cfg_source *source;

    if (!runtime || !nmt || !sdo || !id || id > CO_NUM_NODES)
        return;

    if (runtime->cfg_tearing_down
            || co_nmt_get_st(nmt) == CO_NMT_ST_RESET_NODE
            || co_nmt_get_st(nmt) == CO_NMT_ST_RESET_COMM)
        return;

    request = runtime->cfg_active[id];
    source = runtime->cfg_sources[id];
    if (!request || !source) {
        if (co_nmt_cfg_res(nmt, id, 0) == -1) {
            LELY_RTT_LOG_E(
                    "NMT CFG no-op application stage could not complete: node=%u",
                    (unsigned int)id);
        }
        return;
    }

    request->diagnostic.stage = LELY_RTT_NMT_CFG_STAGE_APPLICATION_DCF;
    request->diagnostic.application_dcf_entries = source->entries;

    if (co_csdo_dn_dcf_req(sdo, source->data, source->data + source->size,
            &lely_rtt_master_cfg_dcf_con, source) == -1) {
        LELY_RTT_LOG_E(
                "application concise DCF could not start: node=%u entries=%u",
                (unsigned int)id, (unsigned int)source->entries);
        runtime->cfg_resume_abort_code[id] = CO_SDO_AC_ERROR;
        runtime->cfg_resume_pending[id] = RT_TRUE;
    }
}

void
lely_rtt_master_cfg_reap(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime)
        return;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        rt_uint32_t abort_code;

        if (!runtime->cfg_resume_pending[id])
            continue;

        abort_code = runtime->cfg_resume_abort_code[id];
        runtime->cfg_resume_pending[id] = RT_FALSE;
        runtime->cfg_resume_abort_code[id] = 0;

        /*
         * A local reset may have destroyed the Lely CFG object after staging
         * this result. cfg_active is retired only from the post-destroy state
         * barrier, so its absence proves there is nothing left to resume.
         */
        if (runtime->cfg_tearing_down || !runtime->master_nmt
                || !runtime->cfg_active[id])
            continue;

        /*
         * cfg_res() may synchronously invoke cfg_con and wake the stack owner.
         * Do not dereference cfg_active[id] after a successful call.
         */
        if (co_nmt_cfg_res(runtime->master_nmt, (co_unsigned8_t)id,
                abort_code) == -1) {
            LELY_RTT_LOG_E(
                    "application concise DCF result could not resume NMT CFG: node=%u",
                    (unsigned int)id);
        }
    }
}

rt_bool_t
lely_rtt_master_cfg_node_busy(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    return runtime && node_id && node_id <= CO_NUM_NODES
            && runtime->cfg_active[node_id] != RT_NULL;
}

/** @brief Return the configuration data/callback sources available for a node. */
static rt_uint8_t
lely_rtt_master_cfg_source_flags(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    co_nmt_cfg_ind_t *cfg_ind = RT_NULL;
    void *cfg_data = RT_NULL;
    co_sub_t *sub_1f22;
    rt_uint8_t flags = 0;

    if (!runtime || !node_id || node_id > CO_NUM_NODES)
        return 0;

    if (runtime->master_dev) {
        sub_1f22 = co_dev_find_sub(runtime->master_dev, 0x1f22, node_id);
        if (sub_1f22 && co_sub_sizeof_val(sub_1f22))
            flags |= LELY_RTT_NMT_CFG_SOURCE_OBJECT_1F22;
    }
    if (runtime->cfg_sources[node_id])
        flags |= LELY_RTT_NMT_CFG_SOURCE_APPLICATION_DCF;

    if (runtime->master_nmt) {
        co_nmt_get_cfg_ind(runtime->master_nmt, &cfg_ind, &cfg_data);
        if (cfg_ind && (cfg_ind != &lely_rtt_master_cfg_ind
                || cfg_data != runtime)) {
            flags |= LELY_RTT_NMT_CFG_SOURCE_EXTERNAL_CFG_IND;
        }
    }
    return flags;
}

/**
 * @brief Check whether the Master can perform useful manual configuration.
 *
 * With LELY_NO_CO_DCF=1, plain 0x1F20 DCF text is unavailable. The bridge
 * accepts object 0x1F22, a copied per-node application concise DCF, or an
 * externally owner-installed cfg_ind. Unsupported/no-op requests fail closed.
 */
static rt_bool_t
lely_rtt_master_cfg_has_work(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    co_unsigned32_t assignment;

    if (!runtime || !runtime->master_nmt || !runtime->master_dev || !node_id
            || node_id > CO_NUM_NODES)
        return RT_FALSE;

    assignment = co_dev_get_val_u32(runtime->master_dev, 0x1f81, node_id);
    if (!(assignment & 0x01u))
        return RT_FALSE;

    return lely_rtt_master_cfg_source_flags(runtime, node_id) != 0;
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
    struct lely_rtt_master_cfg_source *source;

    if (!runtime || !request || !node_id || node_id > CO_NUM_NODES)
        return;

    request->diagnostic.stage = LELY_RTT_NMT_CFG_STAGE_OWNER_PRECHECK;
    request->diagnostic.source_flags =
            lely_rtt_master_cfg_source_flags(runtime, node_id);
    request->diagnostic.restore_requested =
            lely_rtt_master_cfg_restore_requested(runtime, node_id);
    source = runtime->cfg_sources[node_id];
    if (source)
        request->diagnostic.application_dcf_entries = source->entries;

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
    if (request->diagnostic.restore_requested) {
        LELY_RTT_LOG_W(
                "NMT configuration 0x1F8A restore path is unsupported for node %u",
                (unsigned int)node_id);
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_ENOSYS, 0);
        return;
    }
    if (source && (request->diagnostic.source_flags
            & LELY_RTT_NMT_CFG_SOURCE_EXTERNAL_CFG_IND)) {
        LELY_RTT_LOG_W(
                "application DCF lost cfg_ind ownership for node %u",
                (unsigned int)node_id);
        lely_rtt_master_cfg_complete(runtime, request,
                LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }
    if (!lely_rtt_master_cfg_has_work(runtime, node_id)) {
        LELY_RTT_LOG_W(
                "NMT configuration has no supported data/cfg_ind work for node %u",
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

    request->diagnostic.stage = LELY_RTT_NMT_CFG_STAGE_LELY_SEQUENCE;
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

    runtime->cfg_tearing_down = RT_TRUE;
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
    rt_uint16_t id;

    /* co_nmt_destroy() has removed every cfg_con/cfg_data owner by this point. */
    lely_rtt_master_cfg_retire_all(runtime);
    if (!runtime)
        return;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        runtime->cfg_resume_pending[id] = RT_FALSE;
        runtime->cfg_resume_abort_code[id] = 0;
    }
    runtime->cfg_tearing_down = RT_FALSE;
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

/** @brief Shared implementation for legacy and diagnostic manual CFG APIs. */
static rt_err_t
lely_rtt_runtime_nmt_configure_common(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint32_t timeout_ms,
        struct lely_rtt_nmt_cfg_result *result,
        struct lely_rtt_nmt_cfg_diagnostic *diagnostic)
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
    request.diagnostic.stage = LELY_RTT_NMT_CFG_STAGE_QUEUED;
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

    if (err == RT_EOK) {
        *result = request.result;
        if (diagnostic)
            *diagnostic = request.diagnostic;
    }
    lely_rtt_master_sync_fini(&request.sync);
    return err;
}

rt_err_t
lely_rtt_runtime_nmt_configure(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint32_t timeout_ms,
        struct lely_rtt_nmt_cfg_result *result)
{
    return lely_rtt_runtime_nmt_configure_common(runtime, node_id, timeout_ms,
            result, RT_NULL);
}

rt_err_t
lely_rtt_runtime_nmt_configure_ex(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint32_t timeout_ms,
        struct lely_rtt_nmt_cfg_result *result,
        struct lely_rtt_nmt_cfg_diagnostic *diagnostic)
{
    if (!diagnostic)
        return -RT_EINVAL;

    return lely_rtt_runtime_nmt_configure_common(runtime, node_id, timeout_ms,
            result, diagnostic);
}

#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
