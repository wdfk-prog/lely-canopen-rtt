/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-06     wdfk-prog         first version
 */

/**
 * @file test_master_sync_pdo.c
 * @brief Host-stub regression tests for the RT-Thread B9 SYNC/PDO bridge.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PKG_LELY_USING_MASTER_COMMAND 1
#define PKG_LELY_USING_MASTER_PDO_TX 1
#define PKG_LELY_USING_MASTER_SYNC_PDO 1
#define LELY_RTT_INTERNAL_H_ 1

#define CO_NUM_PDOS 512
#define CO_PDO_NUM_MAPS 64
#define CO_PDO_COBID_VALID 0x80000000u
#define CO_SYNC_COBID_PRODUCER 0x40000000u
#define CO_DEFTYPE_UNSIGNED8 0x0005u
#define CO_DEFTYPE_UNSIGNED32 0x0007u

#define RT_EOK 0
#define RT_ERROR 1
#define RT_EINVAL 22
#define RT_EBUSY 16
#define RT_ENOSYS 38
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_NULL NULL

#define LELY_RTT_LOG_E(...) ((void)0)
#define LELY_RTT_LOG_W(...) ((void)0)

#define rt_memset memset

typedef int rt_err_t;
typedef int rt_bool_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef int32_t rt_atomic_t;
typedef void *rt_thread_t;

typedef uint8_t co_unsigned8_t;
typedef uint16_t co_unsigned16_t;
typedef uint32_t co_unsigned32_t;

typedef struct co_nmt co_nmt_t;
typedef struct co_dev co_dev_t;
typedef struct co_sub co_sub_t;
typedef struct co_sync co_sync_t;
typedef struct co_tpdo co_tpdo_t;

typedef void co_nmt_sync_ind_t(co_nmt_t *nmt, co_unsigned8_t counter,
        void *data);

struct co_pdo_comm_par {
    co_unsigned32_t cobid;
    co_unsigned8_t trans;
};

struct co_pdo_map_par {
    co_unsigned8_t n;
};

struct co_sub {
    co_unsigned16_t idx;
    co_unsigned8_t subidx;
    co_unsigned16_t type;
    co_unsigned32_t value_u32;
    co_unsigned8_t value_u8;
    co_unsigned32_t abort_code;
};

struct co_dev {
    struct co_sub sync_cobid;
    struct co_sub sync_period;
    struct co_sub rpdo_type;
    struct co_sub tpdo_type;
};

struct co_sync {
    int active;
};

struct co_tpdo {
    struct co_pdo_comm_par comm;
    struct co_pdo_map_par map;
    int event_calls;
    int event_result;
};

struct co_nmt {
    co_nmt_sync_ind_t *sync_ind;
    void *sync_ind_data;
    struct co_sync sync;
    rt_bool_t sync_available;
    struct co_tpdo tpdo;
};

struct lely_rtt_master_sync {
    rt_bool_t initialized;
    rt_bool_t completed;
    rt_err_t result;
};

typedef struct lely_rtt_runtime lely_rtt_runtime_t;

/** Local PDO direction used by the B9 transmission-type control API. */
enum lely_rtt_pdo_direction {
    LELY_RTT_PDO_DIRECTION_RPDO = 0,
    LELY_RTT_PDO_DIRECTION_TPDO,
};

#define LELY_RTT_SYNC_ROLE_CONSUMER (1u << 0)
#define LELY_RTT_SYNC_ROLE_PRODUCER (1u << 1)

struct lely_rtt_sync_event {
    rt_uint32_t sequence;
    rt_uint32_t period_us;
    rt_uint8_t counter;
    rt_uint8_t role;
};

typedef void lely_rtt_sync_ind_t(lely_rtt_runtime_t *runtime,
        const struct lely_rtt_sync_event *event, void *data);

struct lely_rtt_master_pdo_request;
struct lely_rtt_master_sync_control_request;

enum lely_rtt_master_command_type {
    LELY_RTT_MASTER_COMMAND_PDO_TX = 0,
    LELY_RTT_MASTER_COMMAND_SYNC,
};

struct lely_rtt_master_command {
    rt_uint8_t type;
    union {
        struct {
            struct lely_rtt_master_pdo_request *request;
        } pdo;
        struct {
            struct lely_rtt_master_sync_control_request *request;
        } sync_control;
    } data;
};

struct lely_rtt_runtime {
    rt_thread_t owner_thread;
    co_dev_t *master_dev;
    co_nmt_t *master_nmt;
    rt_bool_t event_initialized;
    lely_rtt_sync_ind_t *sync_app_ind;
    void *sync_app_data;
    rt_atomic_t sync_snapshot_seq;
    rt_atomic_t sync_snapshot_period_us;
    rt_atomic_t sync_snapshot_counter;
    rt_atomic_t sync_snapshot_role;
};

static rt_thread_t rt_thread_self(void);
static rt_atomic_t rt_atomic_load(const rt_atomic_t *value);
static void rt_atomic_store(rt_atomic_t *value, rt_atomic_t desired);
static void rt_thread_mdelay(int milliseconds);

void co_nmt_get_sync_ind(co_nmt_t *nmt, co_nmt_sync_ind_t **ind,
        void **data);
void co_nmt_set_sync_ind(co_nmt_t *nmt, co_nmt_sync_ind_t *ind, void *data);
co_sync_t *co_nmt_get_sync(const co_nmt_t *nmt);
co_tpdo_t *co_nmt_get_tpdo(const co_nmt_t *nmt, co_unsigned16_t num);
co_sub_t *co_dev_find_sub(co_dev_t *dev, co_unsigned16_t idx,
        co_unsigned8_t subidx);
co_unsigned32_t co_dev_get_val_u32(const co_dev_t *dev,
        co_unsigned16_t idx, co_unsigned8_t subidx);
co_unsigned32_t co_sub_get_val_u32(const co_sub_t *sub);
co_unsigned8_t co_sub_get_val_u8(const co_sub_t *sub);
co_unsigned16_t co_sub_get_type(const co_sub_t *sub);
co_unsigned32_t co_sub_dn_ind_val(co_sub_t *sub, co_unsigned16_t type,
        const void *value);
const struct co_pdo_comm_par *co_tpdo_get_comm_par(const co_tpdo_t *tpdo);
const struct co_pdo_map_par *co_tpdo_get_map_par(const co_tpdo_t *tpdo);
int co_tpdo_event(co_tpdo_t *tpdo);

rt_err_t lely_rtt_master_sync_init(struct lely_rtt_master_sync *sync,
        const char *name);
void lely_rtt_master_sync_complete(struct lely_rtt_master_sync *sync,
        rt_err_t result);
rt_err_t lely_rtt_master_sync_wait(struct lely_rtt_master_sync *sync);
void lely_rtt_master_sync_fini(struct lely_rtt_master_sync *sync);
rt_err_t lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command);

#include "../../port/rtthread/src/master_sync.c"
#include "../../port/rtthread/src/master_pdo.c"

struct b9_fixture {
    struct lely_rtt_runtime runtime;
    struct co_dev dev;
    struct co_nmt nmt;
};

static rt_thread_t current_thread = (rt_thread_t)(uintptr_t)0x2222u;
static unsigned int delay_calls;
static unsigned int dn_calls;
static co_unsigned16_t last_dn_index;
static co_unsigned8_t last_dn_subindex;
static rt_bool_t callback_called;
static struct lely_rtt_sync_event callback_event;

static rt_thread_t
rt_thread_self(void)
{
    return current_thread;
}

static rt_atomic_t
rt_atomic_load(const rt_atomic_t *value)
{
    return *value;
}

static void
rt_atomic_store(rt_atomic_t *value, rt_atomic_t desired)
{
    *value = desired;
}

static void
rt_thread_mdelay(int milliseconds)
{
    (void)milliseconds;
    delay_calls++;
}

void
co_nmt_get_sync_ind(co_nmt_t *nmt, co_nmt_sync_ind_t **ind, void **data)
{
    if (ind)
        *ind = nmt->sync_ind;
    if (data)
        *data = nmt->sync_ind_data;
}

void
co_nmt_set_sync_ind(co_nmt_t *nmt, co_nmt_sync_ind_t *ind, void *data)
{
    nmt->sync_ind = ind;
    nmt->sync_ind_data = data;
}

co_sync_t *
co_nmt_get_sync(const co_nmt_t *nmt)
{
    return nmt && nmt->sync_available ? (co_sync_t *)&nmt->sync : RT_NULL;
}

co_tpdo_t *
co_nmt_get_tpdo(const co_nmt_t *nmt, co_unsigned16_t num)
{
    return nmt && num == 1 ? (co_tpdo_t *)&nmt->tpdo : RT_NULL;
}

co_sub_t *
co_dev_find_sub(co_dev_t *dev, co_unsigned16_t idx, co_unsigned8_t subidx)
{
    if (!dev || subidx != 0x00) {
        if (!dev || subidx != 0x02)
            return RT_NULL;
    }

    if (idx == 0x1005 && subidx == 0x00)
        return &dev->sync_cobid;
    if (idx == 0x1006 && subidx == 0x00)
        return &dev->sync_period;
    if (idx == 0x1400 && subidx == 0x02)
        return &dev->rpdo_type;
    if (idx == 0x1800 && subidx == 0x02)
        return &dev->tpdo_type;
    return RT_NULL;
}

co_unsigned32_t
co_dev_get_val_u32(const co_dev_t *dev, co_unsigned16_t idx,
        co_unsigned8_t subidx)
{
    co_sub_t *sub = co_dev_find_sub((co_dev_t *)dev, idx, subidx);

    return sub ? sub->value_u32 : 0;
}

co_unsigned32_t
co_sub_get_val_u32(const co_sub_t *sub)
{
    return sub ? sub->value_u32 : 0;
}

co_unsigned8_t
co_sub_get_val_u8(const co_sub_t *sub)
{
    return sub ? sub->value_u8 : 0;
}

co_unsigned16_t
co_sub_get_type(const co_sub_t *sub)
{
    return sub ? sub->type : 0;
}

co_unsigned32_t
co_sub_dn_ind_val(co_sub_t *sub, co_unsigned16_t type, const void *value)
{
    if (!sub || !value || type != sub->type)
        return 0x06070010u;
    if (sub->abort_code)
        return sub->abort_code;

    dn_calls++;
    last_dn_index = sub->idx;
    last_dn_subindex = sub->subidx;
    if (type == CO_DEFTYPE_UNSIGNED32) {
        sub->value_u32 = *(const co_unsigned32_t *)value;
    } else if (type == CO_DEFTYPE_UNSIGNED8) {
        sub->value_u8 = *(const co_unsigned8_t *)value;
    } else {
        return 0x06070010u;
    }
    return 0;
}

const struct co_pdo_comm_par *
co_tpdo_get_comm_par(const co_tpdo_t *tpdo)
{
    return tpdo ? &tpdo->comm : RT_NULL;
}

const struct co_pdo_map_par *
co_tpdo_get_map_par(const co_tpdo_t *tpdo)
{
    return tpdo ? &tpdo->map : RT_NULL;
}

int
co_tpdo_event(co_tpdo_t *tpdo)
{
    if (!tpdo)
        return -1;
    tpdo->event_calls++;
    return tpdo->event_result;
}

rt_err_t
lely_rtt_master_sync_init(struct lely_rtt_master_sync *sync, const char *name)
{
    (void)name;
    if (!sync)
        return -RT_EINVAL;
    memset(sync, 0, sizeof(*sync));
    sync->initialized = RT_TRUE;
    return RT_EOK;
}

void
lely_rtt_master_sync_complete(struct lely_rtt_master_sync *sync,
        rt_err_t result)
{
    if (!sync || sync->completed)
        return;
    sync->result = result;
    sync->completed = RT_TRUE;
}

rt_err_t
lely_rtt_master_sync_wait(struct lely_rtt_master_sync *sync)
{
    return sync && sync->completed ? sync->result : -RT_ERROR;
}

void
lely_rtt_master_sync_fini(struct lely_rtt_master_sync *sync)
{
    if (sync)
        sync->initialized = RT_FALSE;
}

rt_err_t
lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command)
{
    if (!runtime || !command)
        return -RT_EINVAL;

    if (command->type == LELY_RTT_MASTER_COMMAND_SYNC) {
        lely_rtt_master_sync_dispatch(runtime, command->data.sync_control.request);
    } else if (command->type == LELY_RTT_MASTER_COMMAND_PDO_TX) {
        lely_rtt_master_pdo_dispatch(runtime, command->data.pdo.request);
    } else {
        return -RT_EINVAL;
    }
    return RT_EOK;
}

static void
app_sync_ind(lely_rtt_runtime_t *runtime,
        const struct lely_rtt_sync_event *event, void *data)
{
    struct b9_fixture *fixture = data;

    if (runtime != &fixture->runtime)
        return;
    callback_called = RT_TRUE;
    callback_event = *event;
}

static void
fixture_init(struct b9_fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    delay_calls = 0;
    dn_calls = 0;
    last_dn_index = 0;
    last_dn_subindex = 0;
    callback_called = RT_FALSE;
    memset(&callback_event, 0, sizeof(callback_event));
    current_thread = (rt_thread_t)(uintptr_t)0x2222u;

    fixture->runtime.event_initialized = RT_TRUE;
    fixture->runtime.master_dev = &fixture->dev;
    fixture->runtime.master_nmt = &fixture->nmt;

    fixture->dev.sync_cobid.idx = 0x1005;
    fixture->dev.sync_cobid.type = CO_DEFTYPE_UNSIGNED32;
    fixture->dev.sync_cobid.value_u32 = CO_SYNC_COBID_PRODUCER | 0x80u;
    fixture->dev.sync_period.idx = 0x1006;
    fixture->dev.sync_period.type = CO_DEFTYPE_UNSIGNED32;
    fixture->dev.sync_period.value_u32 = 1000000u;
    fixture->dev.rpdo_type.idx = 0x1400;
    fixture->dev.rpdo_type.subidx = 0x02;
    fixture->dev.rpdo_type.type = CO_DEFTYPE_UNSIGNED8;
    fixture->dev.rpdo_type.value_u8 = 1;
    fixture->dev.tpdo_type.idx = 0x1800;
    fixture->dev.tpdo_type.subidx = 0x02;
    fixture->dev.tpdo_type.type = CO_DEFTYPE_UNSIGNED8;
    fixture->dev.tpdo_type.value_u8 = 1;

    fixture->nmt.sync_available = RT_TRUE;
    fixture->nmt.tpdo.comm.cobid = 0x201u;
    fixture->nmt.tpdo.comm.trans = 1;
    fixture->nmt.tpdo.map.n = 1;
}

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", \
                    __func__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int
test_sync_snapshot_and_callback(void)
{
    struct b9_fixture fixture;
    struct lely_rtt_sync_event event;

    fixture_init(&fixture);
    TEST_CHECK(lely_rtt_runtime_configure_sync_ind(&fixture.runtime,
            &app_sync_ind, &fixture) == RT_EOK);
    TEST_CHECK(lely_rtt_master_sync_bind(&fixture.runtime) == RT_EOK);
    TEST_CHECK(fixture.nmt.sync_ind != RT_NULL);

    fixture.runtime.owner_thread = (rt_thread_t)(uintptr_t)0x1111u;
    fixture.nmt.sync_ind(&fixture.nmt, 7, fixture.nmt.sync_ind_data);

    TEST_CHECK(callback_called);
    TEST_CHECK(callback_event.sequence == 1);
    TEST_CHECK(callback_event.counter == 7);
    TEST_CHECK(callback_event.period_us == 1000000u);
    TEST_CHECK(callback_event.role == LELY_RTT_SYNC_ROLE_PRODUCER);
    TEST_CHECK(lely_rtt_runtime_get_sync(&fixture.runtime, &event) == RT_EOK);
    TEST_CHECK(event.sequence == 1);
    TEST_CHECK(event.counter == 7);
    TEST_CHECK(event.period_us == 1000000u);
    TEST_CHECK(event.role == LELY_RTT_SYNC_ROLE_PRODUCER);
    TEST_CHECK(delay_calls == 0);

    fixture.dev.sync_cobid.value_u32 = 0x80u;
    callback_called = RT_FALSE;
    fixture.nmt.sync_ind(&fixture.nmt, 8, fixture.nmt.sync_ind_data);
    TEST_CHECK(callback_called);
    TEST_CHECK(callback_event.sequence == 2);
    TEST_CHECK(callback_event.counter == 8);
    TEST_CHECK(callback_event.period_us == 1000000u);
    TEST_CHECK(callback_event.role == LELY_RTT_SYNC_ROLE_CONSUMER);
    TEST_CHECK(lely_rtt_runtime_get_sync(&fixture.runtime, &event) == RT_EOK);
    TEST_CHECK(event.sequence == 2);
    TEST_CHECK(event.counter == 8);
    TEST_CHECK(event.period_us == 1000000u);
    TEST_CHECK(event.role == LELY_RTT_SYNC_ROLE_CONSUMER);

    lely_rtt_master_sync_unbind(&fixture.runtime);
    TEST_CHECK(fixture.nmt.sync_ind == RT_NULL);
    return 0;
}

static void
foreign_sync_ind(co_nmt_t *nmt, co_unsigned8_t counter, void *data)
{
    (void)nmt;
    (void)counter;
    (void)data;
}

static int
test_sync_bind_ownership_and_registration_state(void)
{
    struct b9_fixture fixture;

    fixture_init(&fixture);
    fixture.nmt.sync_ind = &foreign_sync_ind;
    fixture.nmt.sync_ind_data = &fixture;
    TEST_CHECK(lely_rtt_master_sync_bind(&fixture.runtime) == -RT_EBUSY);

    fixture.nmt.sync_ind = RT_NULL;
    fixture.nmt.sync_ind_data = RT_NULL;
    fixture.runtime.owner_thread = (rt_thread_t)(uintptr_t)0x1111u;
    TEST_CHECK(lely_rtt_runtime_configure_sync_ind(&fixture.runtime,
            &app_sync_ind, &fixture) == -RT_EINVAL);
    return 0;
}

static int
test_sync_period_control(void)
{
    struct b9_fixture fixture;

    fixture_init(&fixture);
    fixture.runtime.owner_thread = (rt_thread_t)(uintptr_t)0x1111u;
    TEST_CHECK(lely_rtt_runtime_sync_set_period(&fixture.runtime, 250000u)
            == RT_EOK);
    TEST_CHECK(fixture.dev.sync_period.value_u32 == 250000u);
    TEST_CHECK(dn_calls == 1);
    TEST_CHECK(last_dn_index == 0x1006 && last_dn_subindex == 0x00);

    fixture.dev.sync_cobid.value_u32 = 0x80u;
    TEST_CHECK(lely_rtt_runtime_sync_set_period(&fixture.runtime, 1000u)
            == -RT_EBUSY);
    TEST_CHECK(fixture.dev.sync_period.value_u32 == 250000u);
    TEST_CHECK(lely_rtt_runtime_sync_set_period(&fixture.runtime, 0)
            == RT_EOK);
    TEST_CHECK(fixture.dev.sync_period.value_u32 == 0);

    fixture.dev.sync_cobid.value_u32 = CO_SYNC_COBID_PRODUCER | 0x80u;
    fixture.dev.sync_period.abort_code = 0x06090030u;
    TEST_CHECK(lely_rtt_runtime_sync_set_period(&fixture.runtime, 500000u)
            == -RT_ERROR);
    TEST_CHECK(fixture.dev.sync_period.value_u32 == 0);
    return 0;
}

static int
test_pdo_transmission_control(void)
{
    struct b9_fixture fixture;
    rt_uint8_t value = 0;
    unsigned int calls;

    fixture_init(&fixture);
    fixture.runtime.owner_thread = (rt_thread_t)(uintptr_t)0x1111u;

    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1, 1) == RT_EOK);
    TEST_CHECK(fixture.dev.rpdo_type.value_u8 == 1);
    TEST_CHECK(last_dn_index == 0x1400 && last_dn_subindex == 0x02);
    TEST_CHECK(lely_rtt_runtime_pdo_get_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1, &value) == RT_EOK);
    TEST_CHECK(value == 1);

    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, 0) == RT_EOK);
    TEST_CHECK(fixture.dev.tpdo_type.value_u8 == 0);
    TEST_CHECK(last_dn_index == 0x1800 && last_dn_subindex == 0x02);

    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, 240) == RT_EOK);
    TEST_CHECK(fixture.dev.tpdo_type.value_u8 == 240);
    TEST_CHECK(lely_rtt_runtime_pdo_get_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, &value) == RT_EOK);
    TEST_CHECK(value == 240);

    calls = dn_calls;
    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, 241) == -RT_EINVAL);
    TEST_CHECK(dn_calls == calls);
    TEST_CHECK(fixture.dev.tpdo_type.value_u8 == 240);
    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, 252) == -RT_EINVAL);
    TEST_CHECK(dn_calls == calls);
    TEST_CHECK(fixture.dev.tpdo_type.value_u8 == 240);
    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, 254) == RT_EOK);
    TEST_CHECK(fixture.dev.tpdo_type.value_u8 == 254);
    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, 255) == RT_EOK);
    TEST_CHECK(fixture.dev.tpdo_type.value_u8 == 255);
    return 0;
}

static int
test_tpdo_event_modes(void)
{
    struct b9_fixture fixture;

    fixture_init(&fixture);
    fixture.runtime.owner_thread = (rt_thread_t)(uintptr_t)0x1111u;

    fixture.nmt.tpdo.comm.trans = 0;
    TEST_CHECK(lely_rtt_runtime_tpdo_event(&fixture.runtime, 1) == RT_EOK);
    TEST_CHECK(fixture.nmt.tpdo.event_calls == 1);

    fixture.nmt.sync_available = RT_FALSE;
    TEST_CHECK(lely_rtt_runtime_tpdo_event(&fixture.runtime, 1) == -RT_EBUSY);
    TEST_CHECK(fixture.nmt.tpdo.event_calls == 1);

    fixture.nmt.sync_available = RT_TRUE;
    fixture.nmt.tpdo.comm.trans = 1;
    TEST_CHECK(lely_rtt_runtime_tpdo_event(&fixture.runtime, 1) == -RT_EINVAL);
    TEST_CHECK(fixture.nmt.tpdo.event_calls == 1);

    fixture.nmt.tpdo.comm.trans = 254;
    TEST_CHECK(lely_rtt_runtime_tpdo_event(&fixture.runtime, 1) == RT_EOK);
    TEST_CHECK(fixture.nmt.tpdo.event_calls == 2);

    fixture.nmt.tpdo.map.n = 0;
    TEST_CHECK(lely_rtt_runtime_tpdo_event(&fixture.runtime, 1) == -RT_EINVAL);
    TEST_CHECK(fixture.nmt.tpdo.event_calls == 2);
    return 0;
}

static int
test_owner_thread_wait_rejected(void)
{
    struct b9_fixture fixture;

    fixture_init(&fixture);
    fixture.runtime.owner_thread = current_thread;
    TEST_CHECK(lely_rtt_runtime_sync_set_period(&fixture.runtime, 1000u)
            == -RT_EINVAL);
    TEST_CHECK(lely_rtt_runtime_pdo_set_transmission(&fixture.runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1, 1) == -RT_EINVAL);
    TEST_CHECK(lely_rtt_runtime_tpdo_event(&fixture.runtime, 1) == -RT_EINVAL);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int
main(void)
{
    static const struct test_case cases[] = {
        { "sync-snapshot-and-callback", &test_sync_snapshot_and_callback },
        { "sync-bind-ownership", &test_sync_bind_ownership_and_registration_state },
        { "sync-period-control", &test_sync_period_control },
        { "pdo-transmission-control", &test_pdo_transmission_control },
        { "tpdo-event-modes", &test_tpdo_event_modes },
        { "owner-thread-wait-rejected", &test_owner_thread_wait_rejected },
    };
    unsigned int passed = 0;
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].run()) {
            fprintf(stderr, "FAIL %s\n", cases[i].name);
            continue;
        }
        printf("PASS %s\n", cases[i].name);
        passed++;
    }

    printf("Passed %u/%u host B9 cases\n", passed,
            (unsigned int)(sizeof(cases) / sizeof(cases[0])));
    return passed == sizeof(cases) / sizeof(cases[0]) ? 0 : 1;
}
