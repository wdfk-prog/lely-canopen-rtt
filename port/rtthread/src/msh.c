/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 * 2026-09-05     wdfk-prog         expose configuration, local OD and TIME commands
 * 2026-09-06     wdfk-prog         clarify unsupported CFG restore diagnostic
 * 2026-09-06     wdfk-prog         expose TPDO trigger and EMCY diagnostics
 */

/**
 * @file msh.c
 * @brief RT-Thread MSH front-end for the default CANopen Master runtime.
 *
 * This file only parses shell arguments and calls public runtime APIs, which
 * may read snapshots, post asynchronous commands, or synchronously cross the
 * owner queue. It never dereferences owner-only Lely objects.
 *
 * @author wdfk-prog
 */

#include <lely/co/dev.h>
#include <lely/co/nmt.h>
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
#include <lely/co/pdo.h>
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#include <lely/rtthread/runtime.h>

#include <finsh.h>
#include <rtthread.h>

#include <limits.h>
#include <string.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
struct lely_rtt_msh_scalar_type {
    const char *name;
    rt_uint8_t size;
    rt_bool_t is_signed;
    rt_bool_t is_boolean;
};

static const struct lely_rtt_msh_scalar_type lely_rtt_msh_scalar_types[] = {
    { "bool", 1, RT_FALSE, RT_TRUE },
    { "u8", 1, RT_FALSE, RT_FALSE },
    { "u16", 2, RT_FALSE, RT_FALSE },
    { "u32", 4, RT_FALSE, RT_FALSE },
    { "i8", 1, RT_TRUE, RT_FALSE },
    { "i16", 2, RT_TRUE, RT_FALSE },
    { "i32", 4, RT_TRUE, RT_FALSE },
};
#endif /* defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD) */

/** @brief Convert an NMT state byte to the shell vocabulary. */
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

/**
 * @brief Parse one unsigned integer using decimal or 0x hexadecimal notation.
 *
 * Keep MSH parsing independent of libc strto* helpers so the optional shell
 * remains usable with the package's RT_USING_NANO source-selection path.
 */
static rt_bool_t
lely_rtt_msh_parse_u32(const char *text, rt_uint32_t max_value,
        rt_uint32_t *value)
{
    const char *cursor = text;
    rt_uint32_t parsed = 0;
    rt_uint32_t base = 10;
    rt_bool_t have_digit = RT_FALSE;

    if (!cursor || !cursor[0] || !value)
        return RT_FALSE;
    if (*cursor == '+')
        cursor++;
    else if (*cursor == '-')
        return RT_FALSE;
    if (!cursor[0])
        return RT_FALSE;

    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    }

    for (; *cursor; cursor++) {
        rt_uint32_t digit;

        if (*cursor >= '0' && *cursor <= '9')
            digit = (rt_uint32_t)(*cursor - '0');
        else if (base == 16 && *cursor >= 'a' && *cursor <= 'f')
            digit = (rt_uint32_t)(*cursor - 'a') + 10u;
        else if (base == 16 && *cursor >= 'A' && *cursor <= 'F')
            digit = (rt_uint32_t)(*cursor - 'A') + 10u;
        else
            return RT_FALSE;

        if (digit >= base || digit > max_value
                || parsed > (max_value - digit) / base)
            return RT_FALSE;
        parsed = parsed * base + digit;
        have_digit = RT_TRUE;
    }

    if (!have_digit)
        return RT_FALSE;
    *value = parsed;
    return RT_TRUE;
}

#if defined(PKG_LELY_USING_MASTER_TIME)
/** @brief Parse one unsigned 64-bit decimal/hex value without libc strtoull(). */
static rt_bool_t
lely_rtt_msh_parse_u64(const char *text, rt_uint64_t max_value,
        rt_uint64_t *value)
{
    const char *cursor = text;
    rt_uint64_t parsed = 0;
    rt_uint64_t base = 10;
    rt_bool_t have_digit = RT_FALSE;

    if (!text || !*text || !value)
        return RT_FALSE;
    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16;
        cursor += 2;
    }

    for (; *cursor; cursor++) {
        rt_uint64_t digit;

        if (*cursor >= '0' && *cursor <= '9')
            digit = (rt_uint64_t)(*cursor - '0');
        else if (base == 16 && *cursor >= 'a' && *cursor <= 'f')
            digit = (rt_uint64_t)(*cursor - 'a') + 10u;
        else if (base == 16 && *cursor >= 'A' && *cursor <= 'F')
            digit = (rt_uint64_t)(*cursor - 'A') + 10u;
        else
            return RT_FALSE;

        if (digit >= base || digit > max_value
                || parsed > (max_value - digit) / base)
            return RT_FALSE;
        parsed = parsed * base + digit;
        have_digit = RT_TRUE;
    }

    if (!have_digit)
        return RT_FALSE;
    *value = parsed;
    return RT_TRUE;
}
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */

/** @brief Parse one remote node-ID; zero is deliberately not accepted. */
static rt_bool_t
lely_rtt_msh_parse_node(const char *text, rt_uint8_t *node_id)
{
    rt_uint32_t value;

    if (!lely_rtt_msh_parse_u32(text, CO_NUM_NODES, &value) || !value)
        return RT_FALSE;
    *node_id = (rt_uint8_t)value;
    return RT_TRUE;
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

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
/** @brief Look up one scalar shell data type. */
static const struct lely_rtt_msh_scalar_type *
lely_rtt_msh_find_scalar_type(const char *name)
{
    rt_size_t i;

    if (!name)
        return RT_NULL;
    for (i = 0; i < sizeof(lely_rtt_msh_scalar_types)
            / sizeof(lely_rtt_msh_scalar_types[0]); i++) {
        if (!strcmp(name, lely_rtt_msh_scalar_types[i].name))
            return &lely_rtt_msh_scalar_types[i];
    }
    return RT_NULL;
}
#endif /* defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD) */

/** @brief Print the commands available in the current Kconfig profile. */
static void
lely_rtt_msh_help(void)
{
    rt_kprintf("co status\n");
    rt_kprintf("co node <node-id>\n");
    rt_kprintf("co boot <node-id>\n");
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    rt_kprintf("co nmt start|stop|preop|reset-node|reset-comm <node-id|all>\n");
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    rt_kprintf("co cfg <node-id> <timeout-ms>\n");
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
    rt_kprintf("co od status\n");
    rt_kprintf("co od read <index> <subindex> <type>\n");
    rt_kprintf("co od write <index> <subindex> <type> <value>\n");
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
    rt_kprintf("co tpdo event <pdo-number>\n");
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
    rt_kprintf("co emcy [node-id]\n");
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
    rt_kprintf("co time status\n");
    rt_kprintf("co time mode off|consumer|producer|both\n");
    rt_kprintf("co time send <unix-sec> <nanoseconds>\n");
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
#if defined(PKG_LELY_USING_MASTER_SDO)
    rt_kprintf("co sdo read <node> <index> <subindex> <type> <timeout-ms>\n");
    rt_kprintf("co sdo write <node> <index> <subindex> <type> <value> <timeout-ms>\n");
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
    rt_kprintf("  type: bool|u8|u16|u32|i8|i16|i32\n");
#endif /* defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD) */
}

/** @brief Get the auto-init runtime or print the common not-ready diagnostic. */
static lely_rtt_runtime_t *
lely_rtt_msh_runtime(void)
{
    lely_rtt_runtime_t *runtime = lely_rtt_runtime_get_default();

    if (!runtime)
        rt_kprintf("co: runtime not ready\n");
    return runtime;
}

static void
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

static void
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

static void
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

static void
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

#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
static void
lely_rtt_msh_cfg(const char *node_text, const char *timeout_text)
{
    struct lely_rtt_nmt_cfg_result result;
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

    err = lely_rtt_runtime_nmt_configure(runtime, node_id, timeout_ms, &result);
    if (err != RT_EOK) {
        rt_kprintf("co: configuration request failed (%d)\n", err);
        return;
    }

    if (result.status == LELY_RTT_NMT_CFG_COMPLETION_OK) {
        rt_kprintf("node %u: configuration ok\n", (unsigned int)node_id);
    } else if (result.status == LELY_RTT_NMT_CFG_COMPLETION_ABORT) {
        rt_kprintf("node %u: configuration abort=0x%08x\n",
                (unsigned int)node_id, (unsigned int)result.abort_code);
    } else if (result.status == LELY_RTT_NMT_CFG_COMPLETION_CANCELED) {
        rt_kprintf("node %u: configuration canceled", (unsigned int)node_id);
        if (result.abort_code)
            rt_kprintf(" abort=0x%08x", (unsigned int)result.abort_code);
        rt_kprintf("\n");
    } else {
        rt_kprintf("node %u: configuration local error=%d",
                (unsigned int)node_id, result.local_error);
        if (result.local_error == -RT_ENOSYS)
            rt_kprintf(" (no supported 0x1F22/cfg_ind work; 0x1F8A restore unsupported)");
        rt_kprintf("\n");
    }
}
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */

#if defined(PKG_LELY_USING_MASTER_TIME)
static rt_bool_t
lely_rtt_msh_time_roles(const char *text, rt_uint8_t *roles)
{
    if (!text || !roles)
        return RT_FALSE;
    if (!strcmp(text, "off"))
        *roles = 0;
    else if (!strcmp(text, "consumer"))
        *roles = LELY_RTT_TIME_ROLE_CONSUMER;
    else if (!strcmp(text, "producer"))
        *roles = LELY_RTT_TIME_ROLE_PRODUCER;
    else if (!strcmp(text, "both"))
        *roles = LELY_RTT_TIME_ROLE_CONSUMER | LELY_RTT_TIME_ROLE_PRODUCER;
    else
        return RT_FALSE;
    return RT_TRUE;
}

static void
lely_rtt_msh_time(int argc, char **argv)
{
    lely_rtt_runtime_t *runtime = lely_rtt_msh_runtime();
    rt_err_t err;

    if (!runtime)
        return;

    if (argc == 3 && !strcmp(argv[2], "status")) {
        struct lely_rtt_time_value value;

        err = lely_rtt_runtime_get_time(runtime, &value);
        if (err == -RT_EBUSY)
            rt_kprintf("time: no received TIME value\n");
        else if (err != RT_EOK)
            rt_kprintf("co: TIME snapshot failed (%d)\n", err);
        else
            rt_kprintf("time: unix=%lld.%09d seq=%u\n",
                    (long long)value.seconds, (int)value.nanoseconds,
                    (unsigned int)value.sequence);
        return;
    }

    if (argc == 4 && !strcmp(argv[2], "mode")) {
        rt_uint8_t roles;

        if (!lely_rtt_msh_time_roles(argv[3], &roles)) {
            rt_kprintf("co: TIME mode must be off|consumer|producer|both\n");
            return;
        }
        err = lely_rtt_runtime_time_configure(runtime, roles);
        if (err != RT_EOK)
            rt_kprintf("co: TIME mode update failed (%d)\n", err);
        else
            rt_kprintf("time: mode=%s\n", argv[3]);
        return;
    }

    if (argc == 5 && !strcmp(argv[2], "send")) {
        rt_uint64_t seconds;
        rt_uint32_t nanoseconds;

        if (!lely_rtt_msh_parse_u64(argv[3],
                    (rt_uint64_t)0x7fffffffffffffffULL, &seconds)
                || !lely_rtt_msh_parse_u32(argv[4], 999999999u, &nanoseconds)) {
            rt_kprintf("co: invalid TIME timestamp\n");
            return;
        }
        err = lely_rtt_runtime_time_send(runtime, (rt_int64_t)seconds,
                (rt_int32_t)nanoseconds);
        if (err != RT_EOK)
            rt_kprintf("co: TIME send failed (%d)\n", err);
        else
            rt_kprintf("time: sent\n");
        return;
    }

    lely_rtt_msh_help();
}
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
/** @brief Encode a shell scalar in CiA 301 little-endian transfer order. */
static rt_bool_t
lely_rtt_msh_encode_scalar(const struct lely_rtt_msh_scalar_type *type,
        const char *text, rt_uint8_t data[4])
{
    rt_uint32_t raw = 0;
    rt_uint32_t magnitude;
    rt_uint32_t max_value;
    rt_uint8_t i;

    if (!type || !text || !data)
        return RT_FALSE;

    if (type->is_boolean) {
        if (!lely_rtt_msh_parse_u32(text, 1, &raw))
            return RT_FALSE;
    } else if (type->is_signed) {
        const rt_bool_t negative = text[0] == '-';
        const char *magnitude_text = negative ? text + 1 : text;
        const rt_uint8_t bits = type->size * 8;

        if (!magnitude_text[0])
            return RT_FALSE;
        if (negative) {
            max_value = bits == 32 ? 0x80000000u
                    : ((rt_uint32_t)1u << (bits - 1));
        } else {
            max_value = bits == 32 ? 0x7fffffffu
                    : ((rt_uint32_t)1u << (bits - 1)) - 1u;
        }
        if (!lely_rtt_msh_parse_u32(magnitude_text, max_value, &magnitude))
            return RT_FALSE;
        raw = negative ? 0u - magnitude : magnitude;
    } else {
        max_value = type->size == 4 ? 0xffffffffu
                : ((rt_uint32_t)1u << (type->size * 8)) - 1u;
        if (!lely_rtt_msh_parse_u32(text, max_value, &raw))
            return RT_FALSE;
    }

    for (i = 0; i < type->size; i++)
        data[i] = (rt_uint8_t)(raw >> (i * 8));
    return RT_TRUE;
}

/** @brief Decode and print one supported scalar from little-endian SDO bytes. */
static void
lely_rtt_msh_print_scalar(const struct lely_rtt_msh_scalar_type *type,
        const void *data, rt_size_t size)
{
    const rt_uint8_t *bytes = data;
    rt_uint32_t raw = 0;
    rt_uint8_t i;

    if (!type || !data || size != type->size) {
        rt_kprintf("<length %u>", (unsigned int)size);
        return;
    }

    for (i = 0; i < type->size; i++)
        raw |= (rt_uint32_t)bytes[i] << (i * 8);

    if (type->is_boolean) {
        rt_kprintf("%s (", raw ? "true" : "false");
    } else if (type->is_signed) {
        rt_int32_t value;
        if (type->size == 1)
            value = (rt_int8_t)raw;
        else if (type->size == 2)
            value = (rt_int16_t)raw;
        else
            value = (rt_int32_t)raw;
        rt_kprintf("%d (", (int)value);
    } else {
        rt_kprintf("%u (", (unsigned int)raw);
    }

    if (type->size == 1)
        rt_kprintf("0x%02x)", (unsigned int)raw);
    else if (type->size == 2)
        rt_kprintf("0x%04x)", (unsigned int)raw);
    else
        rt_kprintf("0x%08x)", (unsigned int)raw);
}
#endif /* defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD) */

#if defined(PKG_LELY_USING_LOCAL_OD)
static void
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

#if defined(PKG_LELY_USING_MASTER_PDO_TX)
static void
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

#if defined(PKG_LELY_USING_MASTER_EMCY)
static void
lely_rtt_msh_emcy(int argc, char **argv)
{
    struct lely_rtt_emcy_event event;
    lely_rtt_runtime_t *runtime;
    rt_uint8_t node_id = 0;
    rt_err_t err;

    if (argc != 2 && argc != 3) {
        lely_rtt_msh_help();
        return;
    }
    if (argc == 3 && !lely_rtt_msh_parse_node(argv[2], &node_id)) {
        rt_kprintf("co: EMCY node-id must be 1..127\n");
        return;
    }

    runtime = lely_rtt_msh_runtime();
    if (!runtime)
        return;

    err = lely_rtt_runtime_get_emcy(runtime, node_id, &event);
    if (err == -RT_EBUSY) {
        if (node_id)
            rt_kprintf("node %u: no retained EMCY\n", (unsigned int)node_id);
        else
            rt_kprintf("emcy: no retained event\n");
        return;
    }
    if (err != RT_EOK) {
        rt_kprintf("co: EMCY query failed (%d)\n", err);
        return;
    }

    rt_kprintf("emcy node %u: eec=0x%04x er=0x%02x "
            "msef=%02x%02x%02x%02x%02x seq=%u\n",
            (unsigned int)event.node_id, (unsigned int)event.error_code,
            (unsigned int)event.error_register,
            (unsigned int)event.manufacturer[0],
            (unsigned int)event.manufacturer[1],
            (unsigned int)event.manufacturer[2],
            (unsigned int)event.manufacturer[3],
            (unsigned int)event.manufacturer[4],
            (unsigned int)event.sequence);
}
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */

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

static void
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

static int
co(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "help"))) {
        lely_rtt_msh_help();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "status")) {
        lely_rtt_msh_status();
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "node")) {
        lely_rtt_msh_node(argv[2]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "boot")) {
        lely_rtt_msh_boot(argv[2]);
        return 0;
    }
#if defined(PKG_LELY_USING_MASTER_COMMAND)
    if (argc == 4 && !strcmp(argv[1], "nmt")) {
        lely_rtt_msh_nmt(argv[2], argv[3]);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    if (argc == 4 && !strcmp(argv[1], "cfg")) {
        lely_rtt_msh_cfg(argv[2], argv[3]);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#if defined(PKG_LELY_USING_LOCAL_OD)
    if (argc >= 2 && !strcmp(argv[1], "od")) {
        lely_rtt_msh_od(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_LOCAL_OD) */
#if defined(PKG_LELY_USING_MASTER_PDO_TX)
    if (argc >= 2 && !strcmp(argv[1], "tpdo")) {
        lely_rtt_msh_tpdo(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_PDO_TX) */
#if defined(PKG_LELY_USING_MASTER_EMCY)
    if (argc >= 2 && !strcmp(argv[1], "emcy")) {
        lely_rtt_msh_emcy(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_EMCY) */
#if defined(PKG_LELY_USING_MASTER_TIME)
    if (argc >= 2 && !strcmp(argv[1], "time")) {
        lely_rtt_msh_time(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_TIME) */
#if defined(PKG_LELY_USING_MASTER_SDO)
    if (argc >= 2 && !strcmp(argv[1], "sdo")) {
        lely_rtt_msh_sdo(argc, argv);
        return 0;
    }
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

    rt_kprintf("co: invalid command\n");
    lely_rtt_msh_help();
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(co, CANopen Master controller and diagnostics);

#endif /* defined(PKG_LELY_USING_MSH) */
