/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_cfg.c
 * @brief Manual NMT configuration shell handler.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#include <limits.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
static const char *
lely_rtt_msh_cfg_stage_name(enum lely_rtt_nmt_cfg_stage stage)
{
    switch (stage) {
    case LELY_RTT_NMT_CFG_STAGE_QUEUED:
        return "queued";
    case LELY_RTT_NMT_CFG_STAGE_OWNER_PRECHECK:
        return "precheck";
    case LELY_RTT_NMT_CFG_STAGE_LELY_SEQUENCE:
        return "lely";
    case LELY_RTT_NMT_CFG_STAGE_APPLICATION_DCF:
        return "app-dcf";
    case LELY_RTT_NMT_CFG_STAGE_COMPLETE:
        return "complete";
    default:
        return "unknown";
    }
}

static void
lely_rtt_msh_cfg_print_diag(const struct lely_rtt_nmt_cfg_diagnostic *diag)
{
    rt_bool_t printed = RT_FALSE;

    rt_kprintf(" stage=%s source=", lely_rtt_msh_cfg_stage_name(diag->stage));
    if (diag->source_flags & LELY_RTT_NMT_CFG_SOURCE_OBJECT_1F22) {
        rt_kprintf("1f22");
        printed = RT_TRUE;
    }
    if (diag->source_flags & LELY_RTT_NMT_CFG_SOURCE_APPLICATION_DCF) {
        rt_kprintf("%sapp-dcf", printed ? "+" : "");
        printed = RT_TRUE;
    }
    if (diag->source_flags & LELY_RTT_NMT_CFG_SOURCE_EXTERNAL_CFG_IND) {
        rt_kprintf("%sexternal-cfg-ind", printed ? "+" : "");
        printed = RT_TRUE;
    }
    if (!printed)
        rt_kprintf("none");
    if (diag->restore_requested)
        rt_kprintf(" restore=1f8a");
    if (diag->application_dcf_entries)
        rt_kprintf(" entries=%u", (unsigned int)diag->application_dcf_entries);
    if (diag->last_index) {
        rt_kprintf(" last=%04x:%02x", (unsigned int)diag->last_index,
                (unsigned int)diag->last_subindex);
    }
    rt_kprintf("\n");
}

void
lely_rtt_msh_cfg(const char *node_text, const char *timeout_text)
{
    struct lely_rtt_nmt_cfg_result result;
    struct lely_rtt_nmt_cfg_diagnostic diagnostic;
    lely_rtt_runtime_t *runtime;
    rt_uint32_t timeout_ms;
    rt_uint8_t node_id;
    rt_err_t err;

    if (!lely_rtt_msh_parse_node(node_text, &node_id)
            || !lely_rtt_msh_parse_u32(timeout_text, INT_MAX, &timeout_ms)
            || !timeout_ms) {
        rt_kprintf("co: invalid configuration node or timeout\n");
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    err = lely_rtt_runtime_nmt_configure_ex(runtime, node_id, timeout_ms,
            &result, &diagnostic);
    if (err != RT_EOK) {
        rt_kprintf("co: configuration request failed (%d)\n", err);
        return;
    }

    if (result.status == LELY_RTT_NMT_CFG_COMPLETION_OK) {
        rt_kprintf("node %u: configuration ok", (unsigned int)node_id);
    } else if (result.status == LELY_RTT_NMT_CFG_COMPLETION_ABORT) {
        rt_kprintf("node %u: configuration abort=0x%08x",
                (unsigned int)node_id, (unsigned int)result.abort_code);
    } else if (result.status == LELY_RTT_NMT_CFG_COMPLETION_CANCELED) {
        rt_kprintf("node %u: configuration canceled", (unsigned int)node_id);
        if (result.abort_code)
            rt_kprintf(" abort=0x%08x", (unsigned int)result.abort_code);
    } else {
        rt_kprintf("node %u: configuration local error=%d",
                (unsigned int)node_id, result.local_error);
        if (result.local_error == -RT_ENOSYS) {
            if (diagnostic.restore_requested)
                rt_kprintf(" (0x1F8A restore unsupported)");
            else
                rt_kprintf(" (no 0x1F22/app-dcf/external cfg_ind work)");
        }
    }
    lely_rtt_msh_cfg_print_diag(&diagnostic);
}
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */


#endif /* defined(PKG_LELY_USING_MSH) */
