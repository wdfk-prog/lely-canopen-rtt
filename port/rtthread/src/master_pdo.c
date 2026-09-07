/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-06     wdfk-prog         first version
 * 2026-09-06     wdfk-prog         add B9 synchronous PDO transmission control
 */

/**
 * @file master_pdo.c
 * @brief Owner-safe TPDO event and PDO transmission control for RT-Thread.
 *
 * Local mapped values are written through the existing owner-safe OD API. This
 * module never changes mapping/COB-ID. B9 only permits transmission-type
 * updates through the existing Lely object download indications.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_PDO_TX)

#include <lely/co/dev.h>
#include <lely/co/obj.h>
#include <lely/co/pdo.h>
#include <lely/co/tpdo.h>

struct lely_rtt_master_pdo_request {
    struct lely_rtt_master_sync sync;
    rt_uint8_t operation;
    rt_uint8_t direction;
    rt_uint8_t transmission_type;
    rt_uint16_t pdo_number;
};

enum lely_rtt_master_pdo_operation {
    LELY_RTT_MASTER_PDO_EVENT = 0,
#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
    LELY_RTT_MASTER_PDO_GET_TRANSMISSION,
    LELY_RTT_MASTER_PDO_SET_TRANSMISSION,
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
};

#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
/** @brief Return whether B9 exposes this non-RTR CiA 301 transmission type. */
static rt_bool_t
lely_rtt_master_pdo_transmission_supported(rt_uint8_t transmission_type)
{
    return transmission_type <= 0xf0u || transmission_type == 0xfeu
            || transmission_type == 0xffu;
}

/** @brief Read/write one local PDO communication parameter sub-index 2. */
static rt_err_t
lely_rtt_master_pdo_transmission_owner(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_pdo_request *request, rt_bool_t write)
{
    co_unsigned16_t index;
    co_sub_t *sub;
    co_unsigned32_t ac;

    if (!runtime || !runtime->master_dev || !request)
        return -RT_EBUSY;
    if (request->direction != LELY_RTT_PDO_DIRECTION_RPDO
            && request->direction != LELY_RTT_PDO_DIRECTION_TPDO)
        return -RT_EINVAL;

    index = (request->direction == LELY_RTT_PDO_DIRECTION_RPDO ? 0x1400u
            : 0x1800u) + request->pdo_number - 1u;
    sub = co_dev_find_sub(runtime->master_dev, index, 0x02);
    if (!sub || co_sub_get_type(sub) != CO_DEFTYPE_UNSIGNED8)
        return -RT_ERROR;

    if (!write) {
        request->transmission_type = co_sub_get_val_u8(sub);
        return RT_EOK;
    }
    if (!lely_rtt_master_pdo_transmission_supported(
            request->transmission_type))
        return -RT_EINVAL;
    if (request->transmission_type <= 0xf0u
            && !co_dev_find_sub(runtime->master_dev, 0x1005, 0x00))
        return -RT_ENOSYS;

    /*
     * Use the OD download indication instead of assigning the sub-object value
     * directly: an active RPDO/TPDO service caches this parameter and Lely's
     * indication keeps that live cache synchronized with object 0x1400/0x1800.
     */
    ac = co_sub_dn_ind_val(sub, CO_DEFTYPE_UNSIGNED8,
            &request->transmission_type);
    if (ac) {
        LELY_RTT_LOG_W("PDO transmission update rejected: dir=%s pdo=%u abort=0x%08x",
                request->direction == LELY_RTT_PDO_DIRECTION_RPDO ? "rx" : "tx",
                (unsigned int)request->pdo_number, (unsigned int)ac);
        return -RT_ERROR;
    }
    return RT_EOK;
}
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */

void
lely_rtt_master_pdo_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_pdo_request *request)
{
    const struct co_pdo_comm_par *comm;
    const struct co_pdo_map_par *map;
    co_tpdo_t *tpdo;
    rt_err_t err = RT_EOK;

    if (!request)
        return;
    if (!runtime || !runtime->master_nmt) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
    if (request->operation == LELY_RTT_MASTER_PDO_GET_TRANSMISSION
            || request->operation == LELY_RTT_MASTER_PDO_SET_TRANSMISSION) {
        err = lely_rtt_master_pdo_transmission_owner(runtime, request,
                request->operation == LELY_RTT_MASTER_PDO_SET_TRANSMISSION);
        lely_rtt_master_sync_complete(&request->sync, err);
        return;
    }
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
    if (request->operation != LELY_RTT_MASTER_PDO_EVENT) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EINVAL);
        return;
    }

    /* PDO services exist only while the local NMT state is Operational. */
    tpdo = co_nmt_get_tpdo(runtime->master_nmt, request->pdo_number);
    if (!tpdo) {
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
        return;
    }

    comm = co_tpdo_get_comm_par(tpdo);
    map = co_tpdo_get_map_par(tpdo);
    if (!comm || !map) {
        err = -RT_ERROR;
    } else if ((comm->cobid & CO_PDO_COBID_VALID)
#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
            || (comm->trans != 0x00u && comm->trans != 0xfeu
                    && comm->trans != 0xffu)
#else
            || (comm->trans != 0xfeu && comm->trans != 0xffu)
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
            || !map->n || map->n > CO_PDO_NUM_MAPS) {
        /*
         * co_tpdo_event() reports success for several no-op configurations.
         * Fail closed here so callers never interpret an invalid, unsupported
         * synchronous, MPDO or empty mapping as an accepted application event.
         */
        err = -RT_EINVAL;
#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
    } else if (comm->trans == 0x00u
            && !co_nmt_get_sync(runtime->master_nmt)) {
        err = -RT_EBUSY;
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */
    } else if (co_tpdo_event(tpdo) == -1) {
        err = -RT_ERROR;
    }

    lely_rtt_master_sync_complete(&request->sync, err);
}

void
lely_rtt_master_pdo_cancel_queued(struct lely_rtt_master_pdo_request *request)
{
    if (request)
        lely_rtt_master_sync_complete(&request->sync, -RT_EBUSY);
}

rt_err_t
lely_rtt_runtime_tpdo_event(lely_rtt_runtime_t *runtime, rt_uint16_t pdo_number)
{
    struct lely_rtt_master_pdo_request request;
    struct lely_rtt_master_command command;
    rt_err_t err;

    if (!runtime || !pdo_number || pdo_number > CO_NUM_PDOS
            || runtime->owner_thread == rt_thread_self())
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_PDO_EVENT;
    request.pdo_number = pdo_number;
    err = lely_rtt_master_sync_init(&request.sync, "lelytpdo");
    if (err != RT_EOK)
        return err;

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_PDO_TX;
    command.data.pdo.request = &request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err == RT_EOK)
        err = lely_rtt_master_sync_wait(&request.sync);
    lely_rtt_master_sync_fini(&request.sync);
    return err;
}

#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
/** @brief Submit one synchronous B9 PDO communication-parameter request. */
static rt_err_t
lely_rtt_master_pdo_submit(lely_rtt_runtime_t *runtime,
        struct lely_rtt_master_pdo_request *request)
{
    struct lely_rtt_master_command command;
    rt_err_t err;

    if (!runtime || !request || runtime->owner_thread == rt_thread_self())
        return -RT_EINVAL;

    err = lely_rtt_master_sync_init(&request->sync, "lelypdo");
    if (err != RT_EOK)
        return err;

    rt_memset(&command, 0, sizeof(command));
    command.type = LELY_RTT_MASTER_COMMAND_PDO_TX;
    command.data.pdo.request = request;
    err = lely_rtt_master_command_post(runtime, &command);
    if (err == RT_EOK)
        err = lely_rtt_master_sync_wait(&request->sync);
    lely_rtt_master_sync_fini(&request->sync);
    return err;
}

rt_err_t
lely_rtt_runtime_pdo_set_transmission(lely_rtt_runtime_t *runtime,
        enum lely_rtt_pdo_direction direction, rt_uint16_t pdo_number,
        rt_uint8_t transmission_type)
{
    struct lely_rtt_master_pdo_request request;

    if (!runtime || !pdo_number || pdo_number > CO_NUM_PDOS
            || (direction != LELY_RTT_PDO_DIRECTION_RPDO
                    && direction != LELY_RTT_PDO_DIRECTION_TPDO)
            || !lely_rtt_master_pdo_transmission_supported(transmission_type))
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_PDO_SET_TRANSMISSION;
    request.direction = (rt_uint8_t)direction;
    request.pdo_number = pdo_number;
    request.transmission_type = transmission_type;
    return lely_rtt_master_pdo_submit(runtime, &request);
}

rt_err_t
lely_rtt_runtime_pdo_get_transmission(lely_rtt_runtime_t *runtime,
        enum lely_rtt_pdo_direction direction, rt_uint16_t pdo_number,
        rt_uint8_t *transmission_type)
{
    struct lely_rtt_master_pdo_request request;
    rt_err_t err;

    if (!runtime || !transmission_type || !pdo_number
            || pdo_number > CO_NUM_PDOS
            || (direction != LELY_RTT_PDO_DIRECTION_RPDO
                    && direction != LELY_RTT_PDO_DIRECTION_TPDO))
        return -RT_EINVAL;

    rt_memset(&request, 0, sizeof(request));
    request.operation = LELY_RTT_MASTER_PDO_GET_TRANSMISSION;
    request.direction = (rt_uint8_t)direction;
    request.pdo_number = pdo_number;
    err = lely_rtt_master_pdo_submit(runtime, &request);
    if (err == RT_EOK)
        *transmission_type = request.transmission_type;
    return err;
}
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */

#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
