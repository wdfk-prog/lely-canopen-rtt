/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 * 2026-09-11     wdfk-prog         make PDO header dependency match feature guards
 */

/**
 * @file msh_pdo.c
 * @brief TPDO, SYNC and synchronous PDO shell handlers.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#if defined(PKG_LELY_USING_MASTER_PDO_TX) || defined(PKG_LELY_USING_MASTER_SYNC_PDO)
#include <lely/co/pdo.h>
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) || defined(PKG_LELY_USING_MASTER_SYNC_PDO) */

#include <string.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_MASTER_PDO_TX)
void
lely_rtt_msh_tpdo(int argc, char **argv)
{
    lely_rtt_runtime_t *runtime;
    rt_uint32_t pdo_number;
    rt_err_t err;

    if (argc != 4 || strcmp(argv[2], "event")
            || !lely_rtt_msh_parse_u32(argv[3], CO_NUM_PDOS, &pdo_number)
            || !pdo_number) {
        rt_kprintf("co: TPDO command must be event with number 1..%u\n",
                (unsigned int)CO_NUM_PDOS);
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    err = lely_rtt_runtime_tpdo_event(runtime, (rt_uint16_t)pdo_number);
    if (err != RT_EOK)
        rt_kprintf("co: TPDO event failed (%d)\n", err);
    else
        rt_kprintf("tpdo %u: event accepted\n", (unsigned int)pdo_number);
}
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */

#if defined(PKG_LELY_USING_MASTER_SYNC_PDO)
void
lely_rtt_msh_sync(int argc, char **argv)
{
    struct lely_rtt_sync_event event;
    lely_rtt_runtime_t *runtime;
    rt_uint32_t period_us;
    rt_err_t err;

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    if (argc == 3 && !strcmp(argv[2], "status")) {
        err = lely_rtt_runtime_get_sync(runtime, &event);
        if (err == -RT_EBUSY) {
            rt_kprintf("sync: no processed event\n");
            return;
        }
        if (err != RT_EOK) {
            rt_kprintf("co: SYNC status failed (%d)\n", err);
            return;
        }

        rt_kprintf("sync: seq=%u counter=%u role=%s period-us=%u\n",
                (unsigned int)event.sequence, (unsigned int)event.counter,
                event.role == LELY_RTT_SYNC_ROLE_PRODUCER
                        ? "producer" : "consumer",
                (unsigned int)event.period_us);
        return;
    }

    if (argc == 4 && !strcmp(argv[2], "period")
            && lely_rtt_msh_parse_u32(argv[3], 0xffffffffu, &period_us)) {
        err = lely_rtt_runtime_sync_set_period(runtime, period_us);
        if (err != RT_EOK)
            rt_kprintf("co: SYNC period update failed (%d)\n", err);
        else
            rt_kprintf("sync: period-us=%u\n", (unsigned int)period_us);
        return;
    }

    rt_kprintf("co: SYNC command must be status or period <microseconds>\n");
}

static rt_bool_t
lely_rtt_msh_pdo_direction(const char *text,
        enum lely_rtt_pdo_direction *direction)
{
    if (!text || !direction)
        return RT_FALSE;
    if (!strcmp(text, "rx"))
        *direction = LELY_RTT_PDO_DIRECTION_RPDO;
    else if (!strcmp(text, "tx"))
        *direction = LELY_RTT_PDO_DIRECTION_TPDO;
    else
        return RT_FALSE;
    return RT_TRUE;
}

void
lely_rtt_msh_pdo(int argc, char **argv)
{
    enum lely_rtt_pdo_direction direction;
    lely_rtt_runtime_t *runtime;
    rt_uint32_t pdo_number;
    rt_uint32_t transmission_type;
    rt_uint8_t current;
    rt_err_t err;

    if ((argc != 5 && argc != 6) || strcmp(argv[2], "trans")
            || !lely_rtt_msh_pdo_direction(argv[3], &direction)
            || !lely_rtt_msh_parse_u32(argv[4], CO_NUM_PDOS, &pdo_number)
            || !pdo_number) {
        rt_kprintf("co: PDO command must be trans rx|tx <number> [type]\n");
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    if (argc == 5) {
        err = lely_rtt_runtime_pdo_get_transmission(runtime, direction,
                (rt_uint16_t)pdo_number, &current);
        if (err != RT_EOK)
            rt_kprintf("co: PDO transmission query failed (%d)\n", err);
        else
            rt_kprintf("pdo %s %u: type=%u\n",
                    direction == LELY_RTT_PDO_DIRECTION_RPDO ? "rx" : "tx",
                    (unsigned int)pdo_number, (unsigned int)current);
        return;
    }

    if (!lely_rtt_msh_parse_u32(argv[5], 0xffu, &transmission_type)
            || (transmission_type > 0xf0u && transmission_type != 0xfeu
                    && transmission_type != 0xffu)) {
        rt_kprintf("co: PDO type must be 0..240, 254 or 255\n");
        return;
    }

    err = lely_rtt_runtime_pdo_set_transmission(runtime, direction,
            (rt_uint16_t)pdo_number, (rt_uint8_t)transmission_type);
    if (err != RT_EOK)
        rt_kprintf("co: PDO transmission update failed (%d)\n", err);
    else
        rt_kprintf("pdo %s %u: type=%u\n",
                direction == LELY_RTT_PDO_DIRECTION_RPDO ? "rx" : "tx",
                (unsigned int)pdo_number, (unsigned int)transmission_type);
}
#endif /* defined(PKG_LELY_USING_MASTER_SYNC_PDO) */


#endif /* defined(PKG_LELY_USING_MSH) */
