/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_od.c
 * @brief Local object-dictionary shell handlers.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#include <string.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_LOCAL_OD)
void
lely_rtt_msh_od(int argc, char **argv)
{
    const struct lely_rtt_msh_scalar_type *type;
    lely_rtt_runtime_t *runtime;
    rt_uint32_t index;
    rt_uint32_t subindex;
    rt_uint8_t data[4] = { 0 };
    rt_bool_t write;
    rt_err_t err;

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    if (argc == 3 && !strcmp(argv[2], "status")) {
        struct lely_rtt_local_od_change change;
        const char *source;

        err = lely_rtt_runtime_get_local_od_change(runtime, &change);
        if (err == -RT_EBUSY) {
            rt_kprintf("od: no observed write\n");
            return;
        }
        if (err != RT_EOK) {
            rt_kprintf("co: OD status failed (%d)\n", err);
            return;
        }

        source = change.source == LELY_RTT_LOCAL_OD_CHANGE_LOCAL_API
                ? "local-api" : "protocol";
        rt_kprintf("od: last=0x%04x:%02x size=%u source=%s seq=%u\n",
                (unsigned int)change.index, (unsigned int)change.subindex,
                (unsigned int)change.size, source,
                (unsigned int)change.sequence);
        return;
    }

    if (argc != 6 && argc != 7) {
        lely_rtt_msh_help();
        return;
    }
    if (!strcmp(argv[2], "read"))
        write = RT_FALSE;
    else if (!strcmp(argv[2], "write"))
        write = RT_TRUE;
    else {
        rt_kprintf("co: OD command must be read or write\n");
        return;
    }
    if ((!write && argc != 6) || (write && argc != 7)
            || !lely_rtt_msh_parse_u32(argv[3], 0xffffu, &index)
            || !lely_rtt_msh_parse_u32(argv[4], 0xffu, &subindex)) {
        rt_kprintf("co: invalid OD index/subindex\n");
        return;
    }

    type = lely_rtt_msh_find_scalar_type(argv[5]);
    if (!type) {
        rt_kprintf("co: unsupported OD type\n");
        return;
    }
    if (write && !lely_rtt_msh_encode_scalar(type, argv[6], data)) {
        rt_kprintf("co: invalid OD value\n");
        return;
    }

    if (write) {
        err = lely_rtt_runtime_local_od_write(runtime, (rt_uint16_t)index,
                (rt_uint8_t)subindex, data, type->size);
        if (err != RT_EOK)
            rt_kprintf("co: OD write failed (%d)\n", err);
        else
            rt_kprintf("od 0x%04x:%02x: write ok\n",
                    (unsigned int)index, (unsigned int)subindex);
    } else {
        void *value = RT_NULL;
        rt_size_t size = 0;

        err = lely_rtt_runtime_local_od_read(runtime, (rt_uint16_t)index,
                (rt_uint8_t)subindex, &value, &size);
        if (err != RT_EOK) {
            rt_kprintf("co: OD read failed (%d)\n", err);
            return;
        }
        rt_kprintf("od 0x%04x:%02x: ",
                (unsigned int)index, (unsigned int)subindex);
        lely_rtt_msh_print_scalar(type, value, size);
        rt_kprintf("\n");
        lely_rtt_local_od_free(value);
    }
}
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */


#endif /* defined(PKG_LELY_USING_MSH) */
