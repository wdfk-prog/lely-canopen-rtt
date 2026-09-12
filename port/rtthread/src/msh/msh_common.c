/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_common.c
 * @brief Shared parsing and scalar helpers for CANopen MSH commands.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#include <lely/co/dev.h>

#include <string.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
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

rt_bool_t
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

rt_bool_t
lely_rtt_msh_parse_node(const char *text, rt_uint8_t *node_id)
{
    rt_uint32_t value;

    if (!lely_rtt_msh_parse_u32(text, CO_NUM_NODES, &value) || !value)
        return RT_FALSE;
    *node_id = (rt_uint8_t)value;
    return RT_TRUE;
}

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
/** @brief Look up one scalar shell data type. */
const struct lely_rtt_msh_scalar_type *
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

lely_rtt_runtime_t *
lely_rtt_msh_runtime(void)
{
    lely_rtt_runtime_t *runtime = lely_rtt_runtime_get_default();

    if (!runtime)
        rt_kprintf("co: runtime not ready\n");
    return runtime;
}

#if defined(PKG_LELY_USING_MASTER_SDO) || defined(PKG_LELY_USING_LOCAL_OD)
/** @brief Encode a shell scalar in CiA 301 little-endian transfer order. */
rt_bool_t
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
void
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


#endif /* defined(PKG_LELY_USING_MSH) */
