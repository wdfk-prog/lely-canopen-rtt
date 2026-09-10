/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-08     wdfk-prog         first version
 */

/**
 * @file test_master_emcy_contract.c
 * @brief Host-stub contract test for the frozen Lely EMCY indication typedef.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PKG_LELY_USING_MASTER_EMCY 1
#define LELY_RTT_INTERNAL_H_ 1

#define CO_NUM_NODES 127u
#define CO_EMCY_COBID_VALID 0x80000000u
#define LELY_RTT_EMCY_MSEF_SIZE 5u
#define PKG_LELY_MASTER_EMCY_HISTORY_DEPTH 4u

#define RT_EOK 0
#define RT_ERROR 1
#define RT_EINVAL 22
#define RT_EBUSY 16
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_NULL NULL

#define LELY_RTT_LOG_E(...) lely_test_log(__VA_ARGS__)
#define rt_memcpy memcpy
#define rt_memset memset

typedef int rt_err_t;
typedef int rt_bool_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef size_t rt_size_t;
typedef int rt_atomic_t;
typedef void *rt_thread_t;

typedef uint8_t co_unsigned8_t;
typedef uint16_t co_unsigned16_t;
typedef uint32_t co_unsigned32_t;

typedef struct co_nmt co_nmt_t;
typedef struct co_emcy co_emcy_t;
typedef struct co_dev co_dev_t;
typedef struct co_obj co_obj_t;

/* Keep this typedef byte-for-byte compatible with the frozen Lely callback API. */
typedef void co_emcy_ind_t(co_emcy_t *emcy, co_unsigned8_t id,
        co_unsigned16_t eec, co_unsigned8_t er,
        co_unsigned8_t msef[LELY_RTT_EMCY_MSEF_SIZE], void *data);

struct co_obj {
    co_unsigned32_t cobid;
};

struct co_dev {
    struct co_obj object_1014;
};

struct co_emcy {
    co_dev_t *dev;
    co_emcy_ind_t *ind;
    void *ind_data;
};

struct co_nmt {
    co_emcy_t *emcy;
};

struct lely_rtt_master_sync {
    rt_err_t result;
};

struct lely_rtt_emcy_slot {
    rt_atomic_t guard;
    rt_atomic_t sequence;
    rt_atomic_t header;
    rt_atomic_t msef_lo;
    rt_atomic_t msef_hi;
};

struct lely_rtt_emcy_event {
    rt_uint8_t node_id;
    rt_uint16_t error_code;
    rt_uint8_t error_register;
    rt_uint8_t manufacturer[LELY_RTT_EMCY_MSEF_SIZE];
    rt_uint32_t sequence;
};

struct lely_rtt_master_emcy_request;

enum lely_rtt_master_command_type {
    LELY_RTT_MASTER_COMMAND_EMCY = 0,
};

struct lely_rtt_master_command {
    enum lely_rtt_master_command_type type;
    union {
        struct {
            struct lely_rtt_master_emcy_request *request;
        } emcy;
    } data;
};

struct lely_rtt_runtime {
    co_nmt_t *master_nmt;
    rt_thread_t owner_thread;
    rt_atomic_t emcy_latest_sequence;
    struct lely_rtt_emcy_slot emcy_history[PKG_LELY_MASTER_EMCY_HISTORY_DEPTH];
};

typedef struct lely_rtt_runtime lely_rtt_runtime_t;

static void
lely_test_log(const char *format, ...)
{
    (void)format;
}

static int
rt_atomic_load(const rt_atomic_t *value)
{
    return *value;
}

static void
rt_atomic_store(rt_atomic_t *target, rt_atomic_t value)
{
    *target = value;
}

static rt_thread_t
rt_thread_self(void)
{
    return RT_NULL;
}

static void
rt_thread_mdelay(int milliseconds)
{
    (void)milliseconds;
}

co_emcy_t *
co_nmt_get_emcy(co_nmt_t *nmt)
{
    return nmt ? nmt->emcy : RT_NULL;
}

co_dev_t *
co_emcy_get_dev(co_emcy_t *emcy)
{
    return emcy ? emcy->dev : RT_NULL;
}

co_obj_t *
co_dev_find_obj(co_dev_t *dev, co_unsigned16_t idx)
{
    return dev && idx == 0x1014u ? &dev->object_1014 : RT_NULL;
}

co_unsigned32_t
co_obj_get_val_u32(const co_obj_t *obj, co_unsigned8_t subidx)
{
    (void)subidx;
    return obj ? obj->cobid : CO_EMCY_COBID_VALID;
}

void
co_emcy_get_ind(co_emcy_t *emcy, co_emcy_ind_t **ind, void **data)
{
    if (ind)
        *ind = emcy ? emcy->ind : RT_NULL;
    if (data)
        *data = emcy ? emcy->ind_data : RT_NULL;
}

void
co_emcy_set_ind(co_emcy_t *emcy, co_emcy_ind_t *ind, void *data)
{
    if (!emcy)
        return;
    emcy->ind = ind;
    emcy->ind_data = data;
}

int
co_emcy_push(co_emcy_t *emcy, co_unsigned16_t eec, co_unsigned8_t er,
        const co_unsigned8_t msef[LELY_RTT_EMCY_MSEF_SIZE])
{
    (void)emcy;
    (void)eec;
    (void)er;
    (void)msef;
    return 0;
}

int
co_emcy_pop(co_emcy_t *emcy, co_unsigned16_t *eec, co_unsigned8_t *er)
{
    (void)emcy;
    (void)eec;
    (void)er;
    return 0;
}

int
co_emcy_clear(co_emcy_t *emcy)
{
    (void)emcy;
    return 0;
}

rt_err_t
lely_rtt_master_sync_init(struct lely_rtt_master_sync *sync, const char *name)
{
    (void)name;
    sync->result = RT_EOK;
    return RT_EOK;
}

void
lely_rtt_master_sync_complete(struct lely_rtt_master_sync *sync, rt_err_t result)
{
    sync->result = result;
}

rt_err_t
lely_rtt_master_sync_wait(struct lely_rtt_master_sync *sync)
{
    return sync->result;
}

void
lely_rtt_master_sync_fini(struct lely_rtt_master_sync *sync)
{
    (void)sync;
}

rt_err_t
lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command)
{
    (void)runtime;
    (void)command;
    return -RT_EBUSY;
}

#include "../../../port/rtthread/src/master_emcy.c"

static int
expect_true(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int
main(void)
{
    struct lely_rtt_runtime runtime;
    struct lely_rtt_emcy_event event;
    struct co_nmt nmt;
    struct co_emcy emcy;
    struct co_dev dev;
    co_unsigned8_t msef[LELY_RTT_EMCY_MSEF_SIZE] = { 1u, 2u, 3u, 4u, 5u };
    int failures = 0;

    memset(&runtime, 0, sizeof(runtime));
    memset(&event, 0, sizeof(event));
    memset(&nmt, 0, sizeof(nmt));
    memset(&emcy, 0, sizeof(emcy));
    memset(&dev, 0, sizeof(dev));

    runtime.master_nmt = &nmt;
    nmt.emcy = &emcy;
    emcy.dev = &dev;
    dev.object_1014.cobid = 0x81u;

    failures += expect_true(lely_rtt_master_emcy_bind(&runtime) == RT_EOK,
            "EMCY bind should accept the exact frozen callback typedef");
    failures += expect_true(emcy.ind != RT_NULL,
            "EMCY bind should install the consumer indication");

    if (emcy.ind)
        emcy.ind(&emcy, 1u, 0x1234u, 0x5au, msef, emcy.ind_data);

    failures += expect_true(lely_rtt_runtime_get_emcy(&runtime, 1u, &event) == RT_EOK,
            "registered indication should publish one history entry");
    failures += expect_true(event.node_id == 1u && event.error_code == 0x1234u
            && event.error_register == 0x5au,
            "published EMCY header should match callback values");
    failures += expect_true(memcmp(event.manufacturer, msef, sizeof(msef)) == 0,
            "published EMCY manufacturer bytes should match callback values");

    lely_rtt_master_emcy_unbind(&runtime);
    failures += expect_true(emcy.ind == RT_NULL,
            "EMCY unbind should remove only the bridge-owned indication");

    if (failures)
        return 1;
    puts("MASTER_EMCY_CONTRACT_PASS");
    return 0;
}
