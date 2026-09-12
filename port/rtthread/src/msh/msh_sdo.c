/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_sdo.c
 * @brief Client-SDO upload/download shell handlers.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#include <limits.h>
#include <string.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_MASTER_SDO)
static void
lely_rtt_msh_sdo_result(lely_rtt_sdo_request_t *request,
        const struct lely_rtt_msh_scalar_type *type)
{
    struct lely_rtt_sdo_result result;
    rt_err_t err = lely_rtt_sdo_request_get_result(request, &result);

    if (err != RT_EOK) {
        rt_kprintf("co: SDO result unavailable (%d)\n", err);
        return;
    }

    if (result.status == LELY_RTT_SDO_COMPLETION_ABORT) {
        rt_kprintf("sdo #%u: abort=0x%08x\n",
                (unsigned int)result.request_id,
                (unsigned int)result.abort_code);
        return;
    }
    if (result.status == LELY_RTT_SDO_COMPLETION_CANCELED) {
        rt_kprintf("sdo #%u: canceled", (unsigned int)result.request_id);
        if (result.abort_code)
            rt_kprintf(" abort=0x%08x", (unsigned int)result.abort_code);
        rt_kprintf("\n");
        return;
    }
    if (result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR) {
        rt_kprintf("sdo #%u: local error=%d\n",
                (unsigned int)result.request_id, result.local_error);
        return;
    }

    rt_kprintf("sdo #%u: ok", (unsigned int)result.request_id);
    if (result.operation == LELY_RTT_SDO_UPLOAD) {
        rt_kprintf(", value=");
        lely_rtt_msh_print_scalar(type, result.data, result.size);
    }
    rt_kprintf("\n");
}

void
lely_rtt_msh_sdo(int argc, char **argv)
{
    const struct lely_rtt_msh_scalar_type *type;
    lely_rtt_sdo_request_t *request;
    lely_rtt_runtime_t *runtime;
    rt_uint8_t node_id;
    rt_uint32_t index;
    rt_uint32_t subindex;
    rt_uint32_t timeout_ms;
    rt_uint8_t data[4] = { 0 };
    rt_bool_t write;
    rt_err_t err;

    if (argc != 8 && argc != 9) {
        lely_rtt_msh_help();
        return;
    }

    if (!strcmp(argv[2], "read"))
        write = RT_FALSE;
    else if (!strcmp(argv[2], "write"))
        write = RT_TRUE;
    else {
        rt_kprintf("co: SDO command must be read or write\n");
        return;
    }

    if ((!write && argc != 8) || (write && argc != 9)) {
        lely_rtt_msh_help();
        return;
    }
    if (!lely_rtt_msh_parse_node(argv[3], &node_id)
            || !lely_rtt_msh_parse_u32(argv[4], 0xffffu, &index)
            || !lely_rtt_msh_parse_u32(argv[5], 0xffu, &subindex)) {
        rt_kprintf("co: invalid SDO node/index/subindex\n");
        return;
    }

    type = lely_rtt_msh_find_scalar_type(argv[6]);
    if (!type) {
        rt_kprintf("co: unsupported SDO type\n");
        return;
    }

    if (write) {
        if (!lely_rtt_msh_encode_scalar(type, argv[7], data)
                || !lely_rtt_msh_parse_u32(argv[8], INT_MAX, &timeout_ms)
                || !timeout_ms) {
            rt_kprintf("co: invalid SDO value or timeout\n");
            return;
        }
    } else if (!lely_rtt_msh_parse_u32(argv[7], INT_MAX, &timeout_ms)
            || !timeout_ms) {
        rt_kprintf("co: invalid SDO timeout\n");
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    request = lely_rtt_sdo_request_create();
    if (!request) {
        rt_kprintf("co: SDO request allocation failed\n");
        return;
    }

    if (write) {
        err = lely_rtt_runtime_post_sdo_download(runtime, request, node_id,
                (rt_uint16_t)index, (rt_uint8_t)subindex, data, type->size,
                timeout_ms);
    } else {
        err = lely_rtt_runtime_post_sdo_upload(runtime, request, node_id,
                (rt_uint16_t)index, (rt_uint8_t)subindex, timeout_ms);
    }

    if (err != RT_EOK) {
        rt_kprintf("co: SDO queue failed (%d)\n", err);
        (void)lely_rtt_sdo_request_destroy(request);
        return;
    }

    err = lely_rtt_sdo_request_wait(request, RT_WAITING_FOREVER);
    if (err != RT_EOK) {
        rt_kprintf("co: SDO wait failed (%d)\n", err);
    } else {
        lely_rtt_msh_sdo_result(request, type);
    }

    err = lely_rtt_sdo_request_destroy(request);
    if (err != RT_EOK)
        rt_kprintf("co: SDO request release failed (%d)\n", err);
}
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */


#endif /* defined(PKG_LELY_USING_MSH) */
