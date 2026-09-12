/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_nmt.c
 * @brief NMT status, boot and command shell handlers.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#include <lely/co/nmt.h>

#include <string.h>

#if defined(PKG_LELY_USING_MSH)

static const char *
lely_rtt_msh_nmt_state_name(rt_uint8_t state)
{
    switch (state) {
    case CO_NMT_ST_BOOTUP:
        return "boot-up";
    case CO_NMT_ST_STOP:
        return "stopped";
    case CO_NMT_ST_START:
        return "operational";
    case CO_NMT_ST_RESET_NODE:
        return "reset-node";
    case CO_NMT_ST_RESET_COMM:
        return "reset-communication";
    case CO_NMT_ST_PREOP:
        return "pre-operational";
    case LELY_RTT_NMT_STATE_UNAVAILABLE:
        return "unavailable";
    default:
        return "unknown";
    }
}

/** @brief Parse one NMT target, mapping the explicit shell token "all" to 0. */
static rt_bool_t
lely_rtt_msh_parse_nmt_target(const char *text, rt_uint8_t *node_id)
{
    if (!text || !node_id)
        return RT_FALSE;
    if (!strcmp(text, "all")) {
        *node_id = 0;
        return RT_TRUE;
    }
    return lely_rtt_msh_parse_node(text, node_id);
}

void
lely_rtt_msh_status(void)
{
    lely_rtt_runtime_t *runtime = lely_rtt_msh_runtime();
    rt_uint8_t state;
    rt_err_t err;

    if (!runtime)
        return;

    err = lely_rtt_runtime_get_local_nmt_state(runtime, &state);
    if (err != RT_EOK) {
        rt_kprintf("co: master state unavailable (%d)\n", err);
        return;
    }

    rt_kprintf("master: %s (0x%02x)\n",
            lely_rtt_msh_nmt_state_name(state), state);
}

void
lely_rtt_msh_node(const char *node_text)
{
    lely_rtt_runtime_t *runtime;
    rt_uint8_t node_id;
    rt_uint8_t state;
    rt_err_t err;

    if (!lely_rtt_msh_parse_node(node_text, &node_id)) {
        rt_kprintf("co: node-id must be 1..127\n");
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    err = lely_rtt_runtime_get_remote_nmt_state(runtime, node_id, &state);
    if (err == -RT_EBUSY) {
        rt_kprintf("node %u: unavailable\n", (unsigned int)node_id);
        return;
    }
    if (err != RT_EOK) {
        rt_kprintf("co: node query failed (%d)\n", err);
        return;
    }

    rt_kprintf("node %u: %s (0x%02x)\n", (unsigned int)node_id,
            lely_rtt_msh_nmt_state_name(state), state);
}

void
lely_rtt_msh_boot(const char *node_text)
{
    lely_rtt_runtime_t *runtime;
    rt_uint8_t node_id;
    rt_uint8_t state;
    char error_status;
    rt_err_t err;

    if (!lely_rtt_msh_parse_node(node_text, &node_id)) {
        rt_kprintf("co: node-id must be 1..127\n");
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    err = lely_rtt_runtime_get_remote_boot_status(runtime, node_id,
            &state, &error_status);
    if (err == -RT_EBUSY) {
        rt_kprintf("node %u: boot unavailable\n", (unsigned int)node_id);
        return;
    }
    if (err != RT_EOK) {
        rt_kprintf("co: boot query failed (%d)\n", err);
        return;
    }

    if (error_status) {
        rt_kprintf("node %u: boot done, state=%s, error=%c\n",
                (unsigned int)node_id, lely_rtt_msh_nmt_state_name(state),
                error_status);
    } else {
        rt_kprintf("node %u: boot done, state=%s, error=0\n",
                (unsigned int)node_id, lely_rtt_msh_nmt_state_name(state));
    }
}

#if defined(PKG_LELY_USING_MASTER_COMMAND)
static rt_bool_t
lely_rtt_msh_parse_nmt_command(const char *text,
        enum lely_rtt_nmt_command *command)
{
    if (!text || !command)
        return RT_FALSE;
    if (!strcmp(text, "start"))
        *command = LELY_RTT_NMT_COMMAND_START;
    else if (!strcmp(text, "stop"))
        *command = LELY_RTT_NMT_COMMAND_STOP;
    else if (!strcmp(text, "preop"))
        *command = LELY_RTT_NMT_COMMAND_PREOP;
    else if (!strcmp(text, "reset-node"))
        *command = LELY_RTT_NMT_COMMAND_RESET_NODE;
    else if (!strcmp(text, "reset-comm"))
        *command = LELY_RTT_NMT_COMMAND_RESET_COMM;
    else
        return RT_FALSE;
    return RT_TRUE;
}

void
lely_rtt_msh_nmt(const char *command_text, const char *target_text)
{
    lely_rtt_runtime_t *runtime;
    enum lely_rtt_nmt_command command;
    rt_uint8_t node_id;
    rt_err_t err;

    if (!lely_rtt_msh_parse_nmt_command(command_text, &command)) {
        rt_kprintf("co: invalid NMT command\n");
        return;
    }
    if (!lely_rtt_msh_parse_nmt_target(target_text, &node_id)) {
        rt_kprintf("co: NMT target must be 1..127 or all\n");
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    err = lely_rtt_runtime_post_nmt(runtime, command, node_id);
    if (err != RT_EOK) {
        rt_kprintf("co: NMT command queue failed (%d)\n", err);
        return;
    }

    if (node_id) {
        rt_kprintf("queued: nmt %s node %u\n", command_text,
                (unsigned int)node_id);
    } else {
        rt_kprintf("queued: nmt %s all nodes\n", command_text);
    }
}
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */


#endif /* defined(PKG_LELY_USING_MSH) */
