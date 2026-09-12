/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_time.c
 * @brief CANopen TIME shell handlers and timestamp parsing.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#include <string.h>

#if defined(PKG_LELY_USING_MSH)

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

void
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
        else {
            const rt_uint64_t seconds = (rt_uint64_t)value.seconds;
            const unsigned int seconds_hi =
                    (unsigned int)(seconds / 1000000000u);
            const unsigned int seconds_lo =
                    (unsigned int)(seconds % 1000000000u);

            /* Avoid %ll: some RT-Thread BSPs omit long-long formatter support. */
            if (seconds_hi)
                rt_kprintf("time: unix=%u%09u.%09u seq=%u\n",
                        seconds_hi, seconds_lo,
                        (unsigned int)value.nanoseconds,
                        (unsigned int)value.sequence);
            else
                rt_kprintf("time: unix=%u.%09u seq=%u\n", seconds_lo,
                        (unsigned int)value.nanoseconds,
                        (unsigned int)value.sequence);
        }
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


#endif /* defined(PKG_LELY_USING_MSH) */
