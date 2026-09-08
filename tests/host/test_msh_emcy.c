/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-08     wdfk-prog         first version
 */

/**
 * @file test_msh_emcy.c
 * @brief Host-stub regression tests for EMCY MSH argument parsing and routing.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PKG_LELY_USING_MSH 1
#define PKG_LELY_USING_MASTER_EMCY 1

#define CO_NUM_NODES 127u
#define CO_NMT_ST_BOOTUP 0u
#define CO_NMT_ST_STOP 4u
#define CO_NMT_ST_START 5u
#define CO_NMT_ST_RESET_NODE 6u
#define CO_NMT_ST_RESET_COMM 7u
#define CO_NMT_ST_PREOP 127u

#define LELY_RTT_NMT_STATE_UNAVAILABLE 0xffu
#define LELY_RTT_EMCY_MSEF_SIZE 5u

#define RT_EOK 0
#define RT_ERROR 1
#define RT_EBUSY 16
#define RT_EINVAL 22
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_NULL NULL

#define MSH_CMD_EXPORT(name, desc)

typedef int rt_err_t;
typedef int rt_bool_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef uint64_t rt_uint64_t;
typedef int32_t rt_int32_t;
typedef int64_t rt_int64_t;
typedef size_t rt_size_t;

typedef struct lely_rtt_runtime {
    int unused;
} lely_rtt_runtime_t;

struct lely_rtt_emcy_event {
    rt_uint8_t node_id;
    rt_uint16_t error_code;
    rt_uint8_t error_register;
    rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE];
    rt_uint32_t sequence;
};

static lely_rtt_runtime_t runtime_instance;
static int push_calls;
static int pop_calls;
static int clear_calls;
static int query_calls;
static rt_uint16_t last_error_code;
static rt_uint8_t last_error_register;
static rt_uint8_t last_manufacturer[LELY_RTT_EMCY_MSEF_SIZE];
static rt_uint8_t last_query_node;

static int
rt_kprintf(const char *format, ...)
{
    (void)format;
    return 0;
}

static lely_rtt_runtime_t *
lely_rtt_runtime_get_default(void)
{
    return &runtime_instance;
}

static rt_err_t
lely_rtt_runtime_get_local_nmt_state(lely_rtt_runtime_t *runtime,
        rt_uint8_t *state)
{
    (void)runtime;
    *state = CO_NMT_ST_PREOP;
    return RT_EOK;
}

static rt_err_t
lely_rtt_runtime_get_remote_nmt_state(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint8_t *state)
{
    (void)runtime;
    (void)node_id;
    *state = CO_NMT_ST_PREOP;
    return RT_EOK;
}

static rt_err_t
lely_rtt_runtime_get_remote_boot_status(lely_rtt_runtime_t *runtime,
        rt_uint8_t node_id, rt_uint8_t *state, char *error_status)
{
    (void)runtime;
    (void)node_id;
    *state = CO_NMT_ST_PREOP;
    *error_status = 0;
    return RT_EOK;
}

static rt_err_t
lely_rtt_runtime_emcy_push(lely_rtt_runtime_t *runtime,
        rt_uint16_t error_code, rt_uint8_t error_register,
        const rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE])
{
    (void)runtime;
    push_calls++;
    last_error_code = error_code;
    last_error_register = error_register;
    memcpy(last_manufacturer, manufacturer, sizeof(last_manufacturer));
    return RT_EOK;
}

static rt_err_t
lely_rtt_runtime_emcy_pop(lely_rtt_runtime_t *runtime)
{
    (void)runtime;
    pop_calls++;
    return RT_EOK;
}

static rt_err_t
lely_rtt_runtime_emcy_clear(lely_rtt_runtime_t *runtime)
{
    (void)runtime;
    clear_calls++;
    return RT_EOK;
}

static rt_err_t
lely_rtt_runtime_get_emcy(lely_rtt_runtime_t *runtime, rt_uint8_t node_id,
        struct lely_rtt_emcy_event *event)
{
    (void)runtime;
    query_calls++;
    last_query_node = node_id;
    memset(event, 0, sizeof(*event));
    event->node_id = node_id;
    return RT_EOK;
}

#include "../../port/rtthread/src/msh.c"

#define CHECK(name, condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", name, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static void
reset_calls(void)
{
    push_calls = 0;
    pop_calls = 0;
    clear_calls = 0;
    query_calls = 0;
    last_error_code = 0;
    last_error_register = 0;
    last_query_node = 0;
    memset(last_manufacturer, 0, sizeof(last_manufacturer));
}

static int
run_co(int argc, const char *const args[])
{
    char *argv[8];
    int i;

    for (i = 0; i < argc; i++)
        argv[i] = (char *)args[i];
    return co(argc, argv);
}

static int
test_push_hex_forms_and_boundaries(void)
{
    const char *name = "push-hex-forms-and-boundaries";
    const char *lower[] = { "co", "emcy", "push", "0xffff", "0xff", "abcdef0123" };
    const char *upper[] = { "co", "emcy", "push", "65535", "255", "0XABCDEF0123" };
    const char *without_msef[] = { "co", "emcy", "push", "1", "0" };
    const rt_uint8_t expected[] = { 0xab, 0xcd, 0xef, 0x01, 0x23 };
    const rt_uint8_t zero_msef[LELY_RTT_EMCY_MSEF_SIZE] = { 0 };

    reset_calls();
    CHECK(name, run_co(6, lower) == 0);
    CHECK(name, push_calls == 1);
    CHECK(name, last_error_code == 0xffffu);
    CHECK(name, last_error_register == 0xffu);
    CHECK(name, memcmp(last_manufacturer, expected, sizeof(expected)) == 0);

    reset_calls();
    CHECK(name, run_co(6, upper) == 0);
    CHECK(name, push_calls == 1);
    CHECK(name, memcmp(last_manufacturer, expected, sizeof(expected)) == 0);

    reset_calls();
    CHECK(name, run_co(5, without_msef) == 0);
    CHECK(name, push_calls == 1);
    CHECK(name, last_error_code == 1u);
    CHECK(name, last_error_register == 0u);
    CHECK(name, memcmp(last_manufacturer, zero_msef, sizeof(zero_msef)) == 0);
    puts("PASS push-hex-forms-and-boundaries");
    return 0;
}

static int
test_push_rejects_invalid_fields(void)
{
    const char *name = "push-rejects-invalid-fields";
    const char *zero_eec[] = { "co", "emcy", "push", "0", "0" };
    const char *wide_eec[] = { "co", "emcy", "push", "0x10000", "0" };
    const char *wide_er[] = { "co", "emcy", "push", "1", "0x100" };
    const char *short_msef[] = { "co", "emcy", "push", "1", "0", "123456789" };
    const char *long_msef[] = { "co", "emcy", "push", "1", "0", "12345678901" };
    const char *bad_msef[] = { "co", "emcy", "push", "1", "0", "12345678g0" };
    const char *const *cases[] = {
        zero_eec, wide_eec, wide_er, short_msef, long_msef, bad_msef,
    };
    const int argc[] = { 5, 5, 5, 6, 6, 6 };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset_calls();
        CHECK(name, run_co(argc[i], cases[i]) == 0);
        CHECK(name, push_calls == 0);
        CHECK(name, query_calls == 0 && pop_calls == 0 && clear_calls == 0);
    }
    puts("PASS push-rejects-invalid-fields");
    return 0;
}

static int
test_pop_clear_and_query_routing(void)
{
    const char *name = "pop-clear-and-query-routing";
    const char *pop[] = { "co", "emcy", "pop" };
    const char *clear[] = { "co", "emcy", "clear" };
    const char *query[] = { "co", "emcy", "127" };
    const char *latest[] = { "co", "emcy" };
    const char *bad_route[] = { "co", "emcy", "pushx" };

    reset_calls();
    CHECK(name, run_co(3, pop) == 0);
    CHECK(name, pop_calls == 1 && clear_calls == 0 && query_calls == 0);

    reset_calls();
    CHECK(name, run_co(3, clear) == 0);
    CHECK(name, pop_calls == 0 && clear_calls == 1 && query_calls == 0);

    reset_calls();
    CHECK(name, run_co(3, query) == 0);
    CHECK(name, query_calls == 1 && last_query_node == 127u);
    CHECK(name, push_calls == 0 && pop_calls == 0 && clear_calls == 0);

    reset_calls();
    CHECK(name, run_co(2, latest) == 0);
    CHECK(name, query_calls == 1 && last_query_node == 0u);
    CHECK(name, push_calls == 0 && pop_calls == 0 && clear_calls == 0);

    reset_calls();
    CHECK(name, run_co(3, bad_route) == 0);
    CHECK(name, query_calls == 0 && push_calls == 0 && pop_calls == 0 && clear_calls == 0);
    puts("PASS pop-clear-and-query-routing");
    return 0;
}

int
main(void)
{
    if (test_push_hex_forms_and_boundaries())
        return 1;
    if (test_push_rejects_invalid_fields())
        return 1;
    if (test_pop_clear_and_query_routing())
        return 1;
    puts("Passed 3/3 host EMCY MSH cases");
    return 0;
}
