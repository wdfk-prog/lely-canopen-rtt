/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-06     wdfk-prog         first version
 */

/**
 * @file master_pdo.c
 * @brief Owner-safe event-driven TPDO trigger bridge for RT-Thread applications.
 *
 * Local mapped values are written through the existing owner-safe OD API. This
 * module only triggers an already configured static TPDO; it deliberately does
 * not remap PDOs or change their communication parameters at runtime.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_PDO_TX)

#include <lely/co/pdo.h>
#include <lely/co/tpdo.h>

struct lely_rtt_master_pdo_request {
    struct lely_rtt_master_sync sync;
    rt_uint16_t pdo_number;
};

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
            || (comm->trans != 0xfeu && comm->trans != 0xffu)
            || !map->n || map->n > CO_PDO_NUM_MAPS) {
        /*
         * co_tpdo_event() reports success for several no-op configurations.
         * Fail closed here so callers never interpret an invalid, synchronous,
         * MPDO or empty static mapping as a transmitted application PDO.
         */
        err = -RT_EINVAL;
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

#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
