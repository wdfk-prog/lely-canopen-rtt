/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 */

/**
 * @file msh_emcy.c
 * @brief EMCY history and local producer shell handlers.
 *
 * @author wdfk-prog
 */

#include "msh_internal.h"

#include <string.h>

#if defined(PKG_LELY_USING_MSH)

#if defined(PKG_LELY_USING_MASTER_EMCY)
/** @brief Convert one hexadecimal character to its numeric value. */
static rt_bool_t
lely_rtt_msh_hex_nibble(char ch, rt_uint8_t *value)
{
    if (!value)
        return RT_FALSE;
    if (ch >= '0' && ch <= '9')
        *value = (rt_uint8_t)(ch - '0');
    else if (ch >= 'a' && ch <= 'f')
        *value = (rt_uint8_t)(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F')
        *value = (rt_uint8_t)(ch - 'A' + 10);
    else
        return RT_FALSE;
    return RT_TRUE;
}

/** @brief Parse the optional five-byte EMCY manufacturer field. */
static rt_bool_t
lely_rtt_msh_parse_emcy_msef(const char *text,
        rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE])
{
    rt_size_t i;

    if (!text || !manufacturer)
        return RT_FALSE;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    if (strlen(text) != LELY_RTT_EMCY_MSEF_SIZE * 2u)
        return RT_FALSE;

    for (i = 0; i < LELY_RTT_EMCY_MSEF_SIZE; i++) {
        rt_uint8_t hi;
        rt_uint8_t lo;

        if (!lely_rtt_msh_hex_nibble(text[i * 2u], &hi)
                || !lely_rtt_msh_hex_nibble(text[i * 2u + 1u], &lo))
            return RT_FALSE;
        manufacturer[i] = (rt_uint8_t)((hi << 4) | lo);
    }
    return RT_TRUE;
}

void
lely_rtt_msh_emcy(int argc, char **argv)
{
    struct lely_rtt_emcy_event event;
    lely_rtt_runtime_t *runtime;
    rt_uint8_t node_id = 0;
    rt_err_t err;

    if (argc >= 3 && !strcmp(argv[2], "push")) {
        rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE] = { 0 };
        rt_uint32_t error_code;
        rt_uint32_t error_register;

        if ((argc != 5 && argc != 6)
                || !lely_rtt_msh_parse_u32(argv[3], 0xffffu, &error_code)
                || !error_code
                || !lely_rtt_msh_parse_u32(argv[4], 0xffu, &error_register)
                || (argc == 6 && !lely_rtt_msh_parse_emcy_msef(argv[5], manufacturer))) {
            rt_kprintf("co: usage: co emcy push <eec> <error-register> [msef-10hex]\n");
            return;
        }

        runtime = lely_rtt_msh_runtime();
        if (!runtime)
            return;
        err = lely_rtt_runtime_emcy_push(runtime, (rt_uint16_t)error_code,
                (rt_uint8_t)error_register, manufacturer);
        if (err != RT_EOK)
            rt_kprintf("co: EMCY push failed (%d)\n", err);
        else
            rt_kprintf("emcy: local push accepted\n");
        return;
    }

    if (argc == 3 && (!strcmp(argv[2], "pop") || !strcmp(argv[2], "clear"))) {
        runtime = lely_rtt_msh_runtime();
        if (!runtime)
            return;
        if (!strcmp(argv[2], "pop"))
            err = lely_rtt_runtime_emcy_pop(runtime);
        else
            err = lely_rtt_runtime_emcy_clear(runtime);
        if (err != RT_EOK)
            rt_kprintf("co: EMCY %s failed (%d)\n", argv[2], err);
        else
            rt_kprintf("emcy: local %s accepted\n", argv[2]);
        return;
    }

    if (argc != 2 && argc != 3) {
        lely_rtt_msh_help();
        return;
    }
    if (argc == 3 && !lely_rtt_msh_parse_node(argv[2], &node_id)) {
        rt_kprintf("co: EMCY command must be push|pop|clear or node-id 1..127\n");
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


#endif /* defined(PKG_LELY_USING_MSH) */
