/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 * 2026-09-12     wdfk-prog         reject conflicts across enabled NMT custom CSDOs
 */

/**
 * @file master_sdo_client.c
 * @brief Client-SDO channel selection, validation, protocol callbacks and transfer start.
 *
 * @author wdfk-prog
 */

#include "master_sdo_internal.h"

#if defined(PKG_LELY_USING_MASTER_SDO)

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
        if (lely_rtt_master_sdo_uses_predefined(runtime, request->node_id))
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
                if (lely_rtt_master_sdo_uses_predefined(
                            request->runtime, request->node_id))
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

/** @brief Return whether a node uses the independent predefined application CSDO. */
rt_bool_t
lely_rtt_master_sdo_uses_predefined(const struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id)
{
    return runtime && node_id && node_id <= CO_NUM_NODES
            && runtime->sdo_channel[node_id] == LELY_RTT_SDO_CHANNEL_PREDEFINED;
}

/** @brief Lazily create the application-owned predefined Client-SDO for one node. */
static co_csdo_t *
lely_rtt_master_sdo_get_predefined_client(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_bool_t create)
{
    co_csdo_t *sdo;

    if (!runtime || !runtime->can_net || !runtime->master_nmt
            || !node_id || node_id > CO_NUM_NODES)
        return RT_NULL;

    sdo = runtime->sdo_clients[node_id];
    if (sdo || !create)
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

/** @brief Resolve the currently selected Client-SDO without changing ownership. */
co_csdo_t *
lely_rtt_master_sdo_get_client(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_bool_t create_predefined)
{
    rt_uint8_t sdo_number;

    if (!runtime || !runtime->master_nmt || !node_id || node_id > CO_NUM_NODES)
        return RT_NULL;

    sdo_number = runtime->sdo_channel[node_id];
    if (sdo_number == LELY_RTT_SDO_CHANNEL_PREDEFINED)
        return lely_rtt_master_sdo_get_predefined_client(runtime, node_id,
                create_predefined);

    /* Custom 0x1280 services belong to the NMT service manager. */
    return co_nmt_get_csdo(runtime->master_nmt, sdo_number);
}

rt_err_t
lely_rtt_runtime_configure_sdo_channel(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint8_t sdo_number)
{
    if (!runtime || !runtime->event_initialized || runtime->owner_thread
            || !node_id || node_id > CO_NUM_NODES || sdo_number > CO_NUM_SDOS)
        return -RT_EINVAL;

    runtime->sdo_channel[node_id] = sdo_number;
    return RT_EOK;
}

/** @brief Check whether a standard 11-bit COB-ID overlaps a predefined SDO range. */
static rt_bool_t
lely_rtt_master_sdo_is_predefined_cobid(rt_uint32_t cobid,
        rt_uint32_t first, rt_uint32_t last)
{
    const rt_uint32_t id = cobid & 0x7ffu;

    return !(cobid & CO_SDO_COBID_FRAME) && id >= first && id <= last;
}

/** @brief Compare two enabled COB-IDs by CAN frame format and identifier. */
static rt_bool_t
lely_rtt_master_sdo_same_can_identity(rt_uint32_t lhs, rt_uint32_t rhs)
{
    const rt_bool_t lhs_extended = (lhs & CO_SDO_COBID_FRAME) != 0;
    const rt_bool_t rhs_extended = (rhs & CO_SDO_COBID_FRAME) != 0;
    const rt_uint32_t mask = lhs_extended ? 0x1fffffffu : 0x7ffu;

    return lhs_extended == rhs_extended && (lhs & mask) == (rhs & mask);
}

/** @brief Return whether two custom Client-SDO parameter sets share a CAN ID. */
static rt_bool_t
lely_rtt_master_sdo_custom_channels_conflict(const struct co_sdo_par *lhs,
        const struct co_sdo_par *rhs)
{
    return lely_rtt_master_sdo_same_can_identity(
                lhs->cobid_req, rhs->cobid_req)
            || lely_rtt_master_sdo_same_can_identity(
                lhs->cobid_req, rhs->cobid_res)
            || lely_rtt_master_sdo_same_can_identity(
                lhs->cobid_res, rhs->cobid_req)
            || lely_rtt_master_sdo_same_can_identity(
                lhs->cobid_res, rhs->cobid_res);
}

/** @brief Validate one selected NMT-owned custom Client-SDO mapping. */
rt_err_t
lely_rtt_master_sdo_validate_custom_channel(
        struct lely_rtt_runtime *runtime, rt_uint8_t node_id,
        rt_uint8_t sdo_number, co_csdo_t **client)
{
    const struct co_sdo_par *par;
    co_csdo_t *sdo;

    if (!runtime || !runtime->master_nmt || !node_id
            || node_id > CO_NUM_NODES || !sdo_number
            || sdo_number > CO_NUM_SDOS)
        return -RT_EINVAL;

    if (node_id == co_nmt_get_id(runtime->master_nmt)) {
        LELY_RTT_LOG_E("custom Client-SDO targets local Master node: %u",
                (unsigned int)node_id);
        return -RT_EINVAL;
    }

    sdo = co_nmt_get_csdo(runtime->master_nmt, sdo_number);
    if (!sdo) {
        LELY_RTT_LOG_E("custom Client-SDO missing: node=%u sdo=%u",
                (unsigned int)node_id, (unsigned int)sdo_number);
        return -RT_EINVAL;
    }

    par = co_csdo_get_par(sdo);
    if (!par || par->n < 3u || par->id != (co_unsigned8_t)node_id
            || (par->cobid_req & CO_SDO_COBID_VALID)
            || (par->cobid_res & CO_SDO_COBID_VALID)) {
        LELY_RTT_LOG_E(
                "custom Client-SDO invalid: node=%u sdo=%u target=%u",
                (unsigned int)node_id, (unsigned int)sdo_number,
                par ? (unsigned int)par->id : 0u);
        return -RT_EINVAL;
    }

    /*
     * NMT boot always creates its own CiA 301 predefined Client-SDO. A
     * selected custom channel must stay outside both predefined standard
     * COB-ID ranges; otherwise the always-live NMT-owned custom receiver
     * can race a boot receiver even when no application request is active.
     */
    if (lely_rtt_master_sdo_is_predefined_cobid(
                par->cobid_req, 0x581u, 0x5ffu)
            || lely_rtt_master_sdo_is_predefined_cobid(
                par->cobid_req, 0x601u, 0x67fu)
            || lely_rtt_master_sdo_is_predefined_cobid(
                par->cobid_res, 0x581u, 0x5ffu)
            || lely_rtt_master_sdo_is_predefined_cobid(
                par->cobid_res, 0x601u, 0x67fu)) {
        LELY_RTT_LOG_E(
                "custom Client-SDO overlaps predefined COB-ID range: node=%u sdo=%u",
                (unsigned int)node_id, (unsigned int)sdo_number);
        return -RT_EINVAL;
    }

    if (client)
        *client = sdo;
    return RT_EOK;
}

/**
 * @brief Validate one custom channel against every other enabled NMT custom CSDO.
 *
 * NMT-owned custom CSDOs can remain live even when this runtime did not select
 * them for a node. Every enabled service therefore shares one request/response
 * CAN identity namespace; duplicates can route traffic to the wrong service.
 */
static rt_err_t
lely_rtt_master_sdo_validate_custom_channel_set(
        struct lely_rtt_runtime *runtime, rt_uint8_t node_id,
        rt_uint8_t sdo_number, co_csdo_t **client)
{
    const struct co_sdo_par *par;
    co_csdo_t *sdo;
    rt_uint16_t other_sdo_number;
    rt_err_t err;

    err = lely_rtt_master_sdo_validate_custom_channel(runtime, node_id,
            sdo_number, &sdo);
    if (err != RT_EOK)
        return err;
    par = co_csdo_get_par(sdo);

    for (other_sdo_number = 1; other_sdo_number <= CO_NUM_SDOS;
            other_sdo_number++) {
        const struct co_sdo_par *other_par;
        co_csdo_t *other_sdo = co_nmt_get_csdo(runtime->master_nmt,
                (rt_uint8_t)other_sdo_number);

        if (!other_sdo || other_sdo == sdo || !co_csdo_is_valid(other_sdo))
            continue;
        other_par = co_csdo_get_par(other_sdo);

        if (lely_rtt_master_sdo_custom_channels_conflict(par, other_par)) {
            LELY_RTT_LOG_E(
                    "custom Client-SDO COB-ID conflict: node=%u sdo=%u other_sdo=%u",
                    (unsigned int)node_id, (unsigned int)sdo_number,
                    (unsigned int)other_sdo_number);
            return -RT_EINVAL;
        }
    }

    if (client)
        *client = sdo;
    return RT_EOK;
}

rt_err_t
lely_rtt_master_sdo_validate_channels(struct lely_rtt_runtime *runtime)
{
    rt_uint16_t id;

    if (!runtime || !runtime->master_nmt)
        return -RT_EINVAL;

    for (id = 1; id <= CO_NUM_NODES; id++) {
        const rt_uint8_t sdo_number = runtime->sdo_channel[id];
        rt_err_t err;

        if (sdo_number == LELY_RTT_SDO_CHANNEL_PREDEFINED)
            continue;

        err = lely_rtt_master_sdo_validate_custom_channel_set(runtime,
                (rt_uint8_t)id, sdo_number, RT_NULL);
        if (err != RT_EOK)
            return err;
    }

    return RT_EOK;
}

/** @brief Start one request whose state is already ACTIVE. */
void
lely_rtt_master_sdo_start_active(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request)
{
    const rt_uint8_t node_id = request->node_id;
    const rt_bool_t predefined =
            lely_rtt_master_sdo_uses_predefined(runtime, node_id);
    co_csdo_t *sdo;
    rt_err_t err;
    int result;

    if (predefined) {
        sdo = lely_rtt_master_sdo_get_client(runtime, node_id, RT_TRUE);
        if (!sdo) {
            lely_rtt_master_sdo_complete(request,
                    LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_ERROR, 0);
            return;
        }
    } else {
        /*
         * Lely permits 0x1280 parameters to change while the service exists.
         * Revalidate the selected channel and all enabled NMT custom peers at the
         * owner activation boundary so drift cannot create a cross-channel CAN-ID
         * collision after startup validation.
         */
        err = lely_rtt_master_sdo_validate_custom_channel_set(runtime, node_id,
                runtime->sdo_channel[node_id], &sdo);
        if (err != RT_EOK) {
            lely_rtt_master_sdo_complete(request,
                    LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, err, 0);
            return;
        }
    }

    if (predefined) {
        if (co_csdo_is_stopped(sdo) && co_csdo_start(sdo) == -1) {
            lely_rtt_master_sdo_complete(request,
                    LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_ERROR, 0);
            return;
        }
        runtime->sdo_stop_pending[node_id] = RT_FALSE;
    } else if (co_csdo_is_stopped(sdo)) {
        /* NMT owns custom service lifecycle; the application must not revive it. */
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }

    if (!co_csdo_is_idle(sdo)) {
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_EBUSY, 0);
        return;
    }

    co_csdo_set_timeout(sdo, (int)request->timeout_ms);
    runtime->sdo_active[node_id] = request;

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

    if (result == -1 && runtime->sdo_active[node_id] == request) {
        runtime->sdo_active[node_id] = RT_NULL;
        if (predefined)
            runtime->sdo_stop_pending[node_id] = RT_TRUE;
        lely_rtt_master_sdo_complete(request,
                LELY_RTT_SDO_COMPLETION_LOCAL_ERROR, -RT_ERROR, 0);
    }
}

#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
