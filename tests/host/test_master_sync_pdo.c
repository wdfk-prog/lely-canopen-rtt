/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-08     wdfk-prog         first version
 */

/**
 * @file test_master_sync_pdo.c
 * @brief Host-stub regression tests for the RT-Thread B9 SYNC/PDO bridge.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LELY_RTT_INTERNAL_H_
#define PKG_LELY_USING_MASTER_COMMAND 1
#define PKG_LELY_USING_MASTER_PDO_TX 1
#define PKG_LELY_USING_MASTER_SYNC_PDO 1

#define RT_NULL NULL
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_EOK 0
#define RT_ERROR 1
#define RT_EINVAL 22
#define RT_EBUSY 16
#define RT_ENOMEM 12
#define RT_ENOSYS 38
#define RT_ETIMEOUT 110
#define RT_WAITING_NO 0
#define RT_IPC_FLAG_FIFO 0
#define RT_EVENT_FLAG_OR (1u << 0)
#define RT_EVENT_FLAG_CLEAR (1u << 1)
#define LELY_RTT_EVENT_COMMAND (1u << 6)
#define LELY_RTT_MASTER_SYNC_DONE (1u << 0)
#define PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH 8u

typedef int rt_bool_t;
typedef int rt_err_t;
typedef int32_t rt_atomic_t;
typedef int32_t rt_int32_t;
typedef int rt_base_t;
typedef size_t rt_size_t;
typedef void *rt_thread_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;

typedef uint8_t co_unsigned8_t;
typedef uint16_t co_unsigned16_t;
typedef uint32_t co_unsigned32_t;

typedef struct fake_dev co_dev_t;
typedef struct fake_nmt co_nmt_t;
typedef struct fake_sub co_sub_t;
typedef struct fake_rpdo co_rpdo_t;
typedef struct fake_tpdo co_tpdo_t;
typedef void co_nmt_sync_ind_t(co_nmt_t *, co_unsigned8_t, void *);

#define CO_NUM_NODES 127u
#define CO_NUM_PDOS 512u
#define CO_PDO_NUM_MAPS 64u
#define CO_PDO_COBID_VALID 0x80000000u
#define CO_SYNC_COBID_PRODUCER 0x40000000u
#define CO_DEFTYPE_UNSIGNED8 0x0005u
#define CO_DEFTYPE_UNSIGNED32 0x0007u

#define LELY_RTT_SYNC_ROLE_CONSUMER (1u << 0)
#define LELY_RTT_SYNC_ROLE_PRODUCER (1u << 1)

enum lely_rtt_pdo_direction {
    LELY_RTT_PDO_DIRECTION_RPDO = 0,
    LELY_RTT_PDO_DIRECTION_TPDO,
};

struct lely_rtt_sync_event {
    rt_uint32_t sequence;
    rt_uint32_t period_us;
    rt_uint8_t counter;
    rt_uint8_t role;
};

struct lely_rtt_runtime;
typedef void lely_rtt_sync_ind_t(struct lely_rtt_runtime *,
        const struct lely_rtt_sync_event *, void *);

struct rt_event {
    rt_uint32_t pending;
    int initialized;
};

struct lely_rtt_master_sync {
    struct rt_event event;
    rt_atomic_t done;
    rt_atomic_t completion_refs;
    rt_err_t result;
    rt_bool_t initialized;
};

struct co_pdo_comm_par {
    co_unsigned32_t cobid;
    co_unsigned8_t trans;
};

struct co_pdo_map_par {
    co_unsigned8_t n;
};

struct fake_sub {
    co_unsigned16_t type;
    co_unsigned32_t u32;
    co_unsigned8_t u8;
    int dn_abort;
    co_rpdo_t *rpdo;
    co_tpdo_t *tpdo;
};

struct fake_dev {
    struct fake_sub sub_1005;
    struct fake_sub sub_1006;
    struct fake_sub rpdo_trans;
    struct fake_sub tpdo_trans;
    int has_1005;
    int has_1006;
    int has_rpdo;
    int has_tpdo;
};

struct fake_rpdo {
    int stopped;
    int stop_calls;
    int start_calls;
    int start_failures_remaining;
    int sync_pending;
    int pending_frame;
    struct fake_sub *trans_sub;
    struct co_pdo_comm_par comm;
};

struct fake_tpdo {
    int stopped;
    int stop_calls;
    int start_calls;
    int start_failures_remaining;
    int event_timer_active;
    int event_pending;
    int sync_count;
    int event_calls;
    struct fake_sub *trans_sub;
    struct co_pdo_comm_par comm;
    struct co_pdo_map_par map;
};

struct fake_nmt {
    co_nmt_sync_ind_t *sync_ind;
    void *sync_data;
    void *sync_service;
    struct fake_rpdo *rpdo;
    struct fake_tpdo *tpdo;
    int get_sync_calls;
};

struct lely_rtt_master_pdo_request;
struct lely_rtt_master_sync_control_request;

enum lely_rtt_nmt_command {
    LELY_RTT_NMT_COMMAND_START = 0,
    LELY_RTT_NMT_COMMAND_STOP,
    LELY_RTT_NMT_COMMAND_PREOP,
    LELY_RTT_NMT_COMMAND_RESET_NODE,
    LELY_RTT_NMT_COMMAND_RESET_COMM,
};

#define CO_NMT_CS_START 0x01u
#define CO_NMT_CS_STOP 0x02u
#define CO_NMT_CS_ENTER_PREOP 0x80u
#define CO_NMT_CS_RESET_NODE 0x81u
#define CO_NMT_CS_RESET_COMM 0x82u

enum lely_rtt_master_command_type {
    LELY_RTT_MASTER_COMMAND_NMT = 0,
    LELY_RTT_MASTER_COMMAND_PDO_TX,
    LELY_RTT_MASTER_COMMAND_SYNC,
};

struct lely_rtt_master_command {
    rt_uint8_t type;
    union {
        struct {
            rt_uint8_t node_id;
            rt_uint8_t command;
        } nmt;
        struct {
            struct lely_rtt_master_pdo_request *request;
        } pdo;
        struct {
            struct lely_rtt_master_sync_control_request *request;
        } sync_control;
    } data;
};

struct fake_mq {
    struct lely_rtt_master_command items[PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH];
    rt_size_t head;
    rt_size_t count;
};

struct lely_rtt_runtime {
    struct fake_dev *master_dev;
    struct fake_nmt *master_nmt;
    rt_thread_t owner_thread;
    struct fake_mq *command_mq;
    struct rt_event event;
    int event_initialized;
    rt_atomic_t command_refs;
    rt_atomic_t command_stop_latched;
    rt_atomic_t command_quiescing;
    rt_atomic_t local_node_id;
    lely_rtt_sync_ind_t *sync_app_ind;
    void *sync_app_data;
    rt_atomic_t sync_snapshot_seq;
    rt_atomic_t sync_snapshot_period_us;
    rt_atomic_t sync_snapshot_counter;
    rt_atomic_t sync_snapshot_role;
};

typedef struct lely_rtt_runtime lely_rtt_runtime_t;

void lely_rtt_master_command_dispatch(struct lely_rtt_runtime *runtime);
void lely_rtt_master_pdo_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_pdo_request *request);
void lely_rtt_master_pdo_cancel_queued(
        struct lely_rtt_master_pdo_request *request);
void lely_rtt_master_sync_dispatch(struct lely_rtt_runtime *runtime,
        struct lely_rtt_master_sync_control_request *request);
void lely_rtt_master_sync_cancel_queued(
        struct lely_rtt_master_sync_control_request *request);

static rt_thread_t fake_self = (rt_thread_t)(uintptr_t)0x1234u;
static struct fake_mq fake_queue;
static struct lely_rtt_runtime *dispatch_runtime;
static int command_posts;
static int dispatch_calls;
static int dn_calls;
static rt_uint8_t last_command_type;
static int callback_calls;
static struct lely_rtt_sync_event callback_event;

#define LELY_RTT_LOG_E(...) ((void)0)
#define LELY_RTT_LOG_W(...) ((void)0)

static rt_atomic_t
rt_atomic_load(const rt_atomic_t *value)
{
    return *value;
}

static void
rt_atomic_store(rt_atomic_t *target, rt_atomic_t value)
{
    *target = value;
}

static void
rt_atomic_add(rt_atomic_t *target, rt_atomic_t value)
{
    *target += value;
}

static void
rt_atomic_sub(rt_atomic_t *target, rt_atomic_t value)
{
    *target -= value;
}

static rt_thread_t
rt_thread_self(void)
{
    return fake_self;
}

static void
rt_thread_mdelay(int ms)
{
    (void)ms;
}

static rt_err_t
rt_event_init(struct rt_event *event, const char *name, rt_uint8_t flag)
{
    (void)name;
    (void)flag;
    if (!event)
        return -RT_EINVAL;
    event->pending = 0;
    event->initialized = 1;
    return RT_EOK;
}

static rt_err_t
rt_event_send(struct rt_event *event, rt_uint32_t set)
{
    if (!event)
        return -RT_EINVAL;
    event->pending |= set;
    return RT_EOK;
}

static rt_err_t
rt_event_recv(struct rt_event *event, rt_uint32_t set, rt_uint8_t option,
        rt_int32_t timeout, rt_uint32_t *received)
{
    rt_uint32_t matched;

    (void)timeout;
    if (!event || !received)
        return -RT_EINVAL;

    /*
     * Pump the real Master command dispatcher at the point where the caller
     * would block. This models the separate owner thread deterministically, so
     * public B9 APIs still traverse command post -> queue -> dispatch -> wait.
     */
    if (!(event->pending & set) && dispatch_runtime
            && dispatch_runtime->command_mq
            && dispatch_runtime->command_mq->count) {
        dispatch_calls++;
        lely_rtt_master_command_dispatch(dispatch_runtime);
    }

    matched = event->pending & set;
    if (!matched) {
        fprintf(stderr, "host command dispatch did not complete the synchronous request\n");
        exit(2);
    }

    *received = matched;
    if (option & RT_EVENT_FLAG_CLEAR)
        event->pending &= ~matched;
    return RT_EOK;
}

static rt_err_t
rt_event_detach(struct rt_event *event)
{
    if (!event)
        return -RT_EINVAL;
    event->initialized = 0;
    event->pending = 0;
    return RT_EOK;
}

static struct fake_mq *
rt_mq_create(const char *name, rt_size_t msg_size, rt_size_t max_msgs,
        rt_uint8_t flag)
{
    struct fake_mq *mq;

    (void)name;
    (void)flag;
    if (msg_size != sizeof(struct lely_rtt_master_command)
            || !max_msgs || max_msgs > PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH)
        return NULL;
    mq = calloc(1, sizeof(*mq));
    return mq;
}

static rt_err_t
rt_mq_send(struct fake_mq *mq, const void *message, rt_size_t size)
{
    rt_size_t tail;

    if (!mq || !message || size != sizeof(struct lely_rtt_master_command))
        return -RT_EINVAL;
    if (mq->count >= PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH)
        return -RT_EBUSY;

    tail = (mq->head + mq->count) % PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH;
    memcpy(&mq->items[tail], message, size);
    mq->count++;
    command_posts++;
    last_command_type = ((const struct lely_rtt_master_command *)message)->type;
    return RT_EOK;
}

static rt_base_t
rt_mq_recv(struct fake_mq *mq, void *message, rt_size_t size,
        rt_int32_t timeout)
{
    (void)timeout;
    if (!mq || !message || size != sizeof(struct lely_rtt_master_command))
        return -RT_EINVAL;
    if (!mq->count)
        return -RT_ETIMEOUT;

    memcpy(message, &mq->items[mq->head], size);
    mq->head = (mq->head + 1u) % PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH;
    mq->count--;
    return RT_EOK;
}

static rt_err_t
rt_mq_delete(struct fake_mq *mq)
{
    free(mq);
    return RT_EOK;
}

static rt_int32_t
lely_rtt_timeout_ticks(rt_uint32_t timeout_ms)
{
    (void)timeout_ms;
    return 1;
}

static int
co_nmt_cs_req(co_nmt_t *nmt, co_unsigned8_t cs, co_unsigned8_t id)
{
    (void)nmt;
    (void)cs;
    (void)id;
    return 0;
}

#define rt_memset memset

static co_sub_t *
co_dev_find_sub(co_dev_t *dev, co_unsigned16_t idx, co_unsigned8_t subidx)
{
    if (!dev || (subidx != 0x00u && subidx != 0x02u))
        return NULL;
    if (idx == 0x1005u && subidx == 0x00u)
        return dev->has_1005 ? &dev->sub_1005 : NULL;
    if (idx == 0x1006u && subidx == 0x00u)
        return dev->has_1006 ? &dev->sub_1006 : NULL;
    if (idx == 0x1400u && subidx == 0x02u)
        return dev->has_rpdo ? &dev->rpdo_trans : NULL;
    if (idx == 0x1800u && subidx == 0x02u)
        return dev->has_tpdo ? &dev->tpdo_trans : NULL;
    return NULL;
}

static co_unsigned32_t
co_dev_get_val_u32(co_dev_t *dev, co_unsigned16_t idx, co_unsigned8_t subidx)
{
    co_sub_t *sub = co_dev_find_sub(dev, idx, subidx);
    return sub ? sub->u32 : 0;
}

static co_unsigned16_t
co_sub_get_type(const co_sub_t *sub)
{
    return sub->type;
}

static co_unsigned32_t
co_sub_get_val_u32(const co_sub_t *sub)
{
    return sub->u32;
}

static co_unsigned8_t
co_sub_get_val_u8(const co_sub_t *sub)
{
    return sub->u8;
}

static co_unsigned8_t *
co_sub_set_val_u8(co_sub_t *sub, co_unsigned8_t value)
{
    if (!sub)
        return NULL;
    sub->u8 = value;
    return &sub->u8;
}

static co_unsigned32_t
co_sub_dn_ind_val(co_sub_t *sub, co_unsigned16_t type, const void *value)
{
    if (!sub || type != sub->type)
        return 0x06070010u;
    dn_calls++;
    if (sub->dn_abort)
        return (co_unsigned32_t)sub->dn_abort;
    if (type == CO_DEFTYPE_UNSIGNED32)
        sub->u32 = *(const co_unsigned32_t *)value;
    else if (type == CO_DEFTYPE_UNSIGNED8) {
        sub->u8 = *(const co_unsigned8_t *)value;
        /* Mirror Lely's active PDO cache update performed by the OD indication. */
        if (sub->rpdo)
            sub->rpdo->comm.trans = sub->u8;
        if (sub->tpdo)
            sub->tpdo->comm.trans = sub->u8;
    }
    return 0;
}

static void
co_nmt_get_sync_ind(const co_nmt_t *nmt, co_nmt_sync_ind_t **ind, void **data)
{
    if (ind)
        *ind = nmt->sync_ind;
    if (data)
        *data = nmt->sync_data;
}

static void
co_nmt_set_sync_ind(co_nmt_t *nmt, co_nmt_sync_ind_t *ind, void *data)
{
    nmt->sync_ind = ind;
    nmt->sync_data = data;
}

static void *
co_nmt_get_sync(co_nmt_t *nmt)
{
    nmt->get_sync_calls++;
    return nmt->sync_service;
}

static co_rpdo_t *
co_nmt_get_rpdo(co_nmt_t *nmt, co_unsigned16_t num)
{
    return num == 1u ? nmt->rpdo : NULL;
}

static co_tpdo_t *
co_nmt_get_tpdo(co_nmt_t *nmt, co_unsigned16_t num)
{
    return num == 1u ? nmt->tpdo : NULL;
}

static int
co_rpdo_is_stopped(const co_rpdo_t *pdo)
{
    return pdo->stopped;
}

static void
co_rpdo_stop(co_rpdo_t *pdo)
{
    pdo->stop_calls++;
    pdo->stopped = 1;
}

static int
co_rpdo_start(co_rpdo_t *pdo)
{
    pdo->start_calls++;
    if (pdo->start_failures_remaining) {
        pdo->start_failures_remaining--;
        pdo->stopped = 1;
        return -1;
    }
    pdo->comm.trans = pdo->trans_sub->u8;
    pdo->sync_pending = 0;
    pdo->pending_frame = 0;
    pdo->stopped = 0;
    return 0;
}

static int
co_tpdo_is_stopped(const co_tpdo_t *pdo)
{
    return pdo->stopped;
}

static void
co_tpdo_stop(co_tpdo_t *pdo)
{
    pdo->stop_calls++;
    pdo->stopped = 1;
}

static int
co_tpdo_start(co_tpdo_t *pdo)
{
    pdo->start_calls++;
    if (pdo->start_failures_remaining) {
        pdo->start_failures_remaining--;
        pdo->stopped = 1;
        return -1;
    }
    pdo->comm.trans = pdo->trans_sub->u8;
    pdo->event_timer_active = 0;
    pdo->event_pending = 0;
    pdo->sync_count = 0;
    pdo->stopped = 0;
    return 0;
}

static const struct co_pdo_comm_par *
co_tpdo_get_comm_par(const co_tpdo_t *pdo)
{
    return &pdo->comm;
}

static const struct co_pdo_map_par *
co_tpdo_get_map_par(const co_tpdo_t *pdo)
{
    return &pdo->map;
}

static int
co_tpdo_event(co_tpdo_t *pdo)
{
    pdo->event_calls++;
    return 0;
}

#include "../../port/rtthread/src/master_command.c"
#include "../../port/rtthread/src/master_sync.c"
#include "../../port/rtthread/src/master_pdo.c"

static void
sync_callback(lely_rtt_runtime_t *runtime,
        const struct lely_rtt_sync_event *event, void *data)
{
    (void)runtime;
    (void)data;
    callback_calls++;
    callback_event = *event;
}

static void
fail(const char *name, const char *expr, int line)
{
    fprintf(stderr, "FAIL %s line %d: %s\n", name, line, expr);
    exit(1);
}

#define CHECK(name, expr) do { if (!(expr)) fail((name), #expr, __LINE__); } while (0)

static void
init_fixture(struct lely_rtt_runtime *runtime, struct fake_dev *dev,
        struct fake_nmt *nmt, struct fake_rpdo *rpdo, struct fake_tpdo *tpdo)
{
    memset(runtime, 0, sizeof(*runtime));
    memset(dev, 0, sizeof(*dev));
    memset(nmt, 0, sizeof(*nmt));
    memset(rpdo, 0, sizeof(*rpdo));
    memset(tpdo, 0, sizeof(*tpdo));

    dev->has_1005 = 1;
    dev->has_1006 = 1;
    dev->has_rpdo = 1;
    dev->has_tpdo = 1;
    dev->sub_1005.type = CO_DEFTYPE_UNSIGNED32;
    dev->sub_1005.u32 = CO_SYNC_COBID_PRODUCER | 0x80u;
    dev->sub_1006.type = CO_DEFTYPE_UNSIGNED32;
    dev->rpdo_trans.type = CO_DEFTYPE_UNSIGNED8;
    dev->rpdo_trans.u8 = 0xffu;
    dev->tpdo_trans.type = CO_DEFTYPE_UNSIGNED8;
    dev->tpdo_trans.u8 = 0xffu;

    dev->rpdo_trans.rpdo = rpdo;
    dev->tpdo_trans.tpdo = tpdo;
    rpdo->trans_sub = &dev->rpdo_trans;
    rpdo->comm.trans = 0xffu;
    tpdo->trans_sub = &dev->tpdo_trans;
    tpdo->comm.trans = 0xffu;
    tpdo->map.n = 1u;

    nmt->rpdo = rpdo;
    nmt->tpdo = tpdo;
    runtime->master_dev = dev;
    runtime->master_nmt = nmt;
    runtime->event_initialized = 1;
    runtime->command_mq = &fake_queue;
    memset(&fake_queue, 0, sizeof(fake_queue));
    memset(&runtime->event, 0, sizeof(runtime->event));
    rt_atomic_store(&runtime->command_refs, 0);
    rt_atomic_store(&runtime->command_stop_latched, 0);
    rt_atomic_store(&runtime->command_quiescing, 0);
    dispatch_runtime = runtime;
    command_posts = 0;
    dispatch_calls = 0;
    dn_calls = 0;
    last_command_type = 0xffu;
}

static void
test_sync_bind_before_service_creation(void)
{
    const char *name = "sync-bind-before-service-creation";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    nmt.sync_service = NULL;
    CHECK(name, lely_rtt_master_sync_bind(&runtime) == RT_EOK);
    CHECK(name, nmt.sync_ind != NULL);
    CHECK(name, nmt.sync_data == &runtime);
    CHECK(name, nmt.get_sync_calls == 0);
    puts("PASS sync-bind-before-service-creation");
}

static void foreign_sync_ind(co_nmt_t *nmt, co_unsigned8_t counter, void *data)
{
    (void)nmt;
    (void)counter;
    (void)data;
}

static void
test_sync_bind_ownership(void)
{
    const char *name = "sync-bind-ownership";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    nmt.sync_ind = foreign_sync_ind;
    CHECK(name, lely_rtt_master_sync_bind(&runtime) == -RT_EBUSY);
    CHECK(name, nmt.sync_ind == foreign_sync_ind);
    puts("PASS sync-bind-ownership");
}

static void
test_sync_callback_registration_and_snapshot(void)
{
    const char *name = "sync-callback-registration-and-snapshot";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;
    struct lely_rtt_sync_event event;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    CHECK(name, lely_rtt_runtime_get_sync(&runtime, &event) == -RT_EBUSY);
    CHECK(name, lely_rtt_runtime_configure_sync_ind(
            &runtime, sync_callback, &runtime) == RT_EOK);
    CHECK(name, runtime.sync_app_ind == sync_callback);
    CHECK(name, runtime.sync_app_data == &runtime);
    CHECK(name, lely_rtt_master_sync_bind(&runtime) == RT_EOK);

    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    CHECK(name, lely_rtt_runtime_configure_sync_ind(
            &runtime, RT_NULL, RT_NULL) == -RT_EINVAL);
    CHECK(name, runtime.sync_app_ind == sync_callback);
    CHECK(name, runtime.sync_app_data == &runtime);

    dev.sub_1006.u32 = 1000u;
    callback_calls = 0;
    memset(&callback_event, 0, sizeof(callback_event));
    nmt.sync_ind(&nmt, 7u, nmt.sync_data);
    CHECK(name, callback_calls == 1);
    CHECK(name, callback_event.sequence == 1u);
    CHECK(name, callback_event.period_us == 1000u);
    CHECK(name, callback_event.counter == 7u);
    CHECK(name, callback_event.role == LELY_RTT_SYNC_ROLE_PRODUCER);
    CHECK(name, lely_rtt_runtime_get_sync(&runtime, &event) == RT_EOK);
    CHECK(name, event.sequence == 1u);
    CHECK(name, event.period_us == 1000u);
    CHECK(name, event.counter == 7u);
    CHECK(name, event.role == LELY_RTT_SYNC_ROLE_PRODUCER);

    dev.sub_1005.u32 = 0x80u;
    nmt.sync_ind(&nmt, 8u, nmt.sync_data);
    CHECK(name, callback_calls == 2);
    CHECK(name, lely_rtt_runtime_get_sync(&runtime, &event) == RT_EOK);
    CHECK(name, event.sequence == 2u);
    CHECK(name, event.counter == 8u);
    CHECK(name, event.role == LELY_RTT_SYNC_ROLE_CONSUMER);

    lely_rtt_master_sync_unbind(&runtime);
    CHECK(name, nmt.sync_ind == RT_NULL);
    CHECK(name, nmt.sync_data == RT_NULL);
    puts("PASS sync-callback-registration-and-snapshot");
}

static void
test_public_owner_command_wiring(void)
{
    const char *name = "public-owner-command-wiring";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;
    rt_uint8_t transmission_type = 0;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    nmt.sync_service = &nmt;

    CHECK(name, lely_rtt_runtime_sync_set_period(&runtime, 2500u) == RT_EOK);
    CHECK(name, command_posts == 1);
    CHECK(name, dispatch_calls == 1);
    CHECK(name, last_command_type == LELY_RTT_MASTER_COMMAND_SYNC);
    CHECK(name, dev.sub_1006.u32 == 2500u);

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == RT_EOK);
    CHECK(name, command_posts == 2);
    CHECK(name, dispatch_calls == 2);
    CHECK(name, last_command_type == LELY_RTT_MASTER_COMMAND_PDO_TX);
    CHECK(name, dev.tpdo_trans.u8 == 1u);
    CHECK(name, tpdo.comm.trans == 1u);

    CHECK(name, lely_rtt_runtime_pdo_get_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, &transmission_type) == RT_EOK);
    CHECK(name, transmission_type == 1u);
    CHECK(name, command_posts == 3);
    CHECK(name, dispatch_calls == 3);
    CHECK(name, last_command_type == LELY_RTT_MASTER_COMMAND_PDO_TX);

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 0u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == RT_EOK);
    CHECK(name, tpdo.event_calls == 1);
    CHECK(name, last_command_type == LELY_RTT_MASTER_COMMAND_PDO_TX);
    puts("PASS public-owner-command-wiring");
}

static void
test_sync_period_control(void)
{
    const char *name = "sync-period-control";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    nmt.sync_service = &nmt;

    CHECK(name, lely_rtt_runtime_sync_set_period(&runtime, 2500u) == RT_EOK);
    CHECK(name, dev.sub_1006.u32 == 2500u);
    CHECK(name, dn_calls == 1);

    dev.sub_1005.u32 = 0x80u;
    CHECK(name, lely_rtt_runtime_sync_set_period(&runtime, 1000u) == -RT_EBUSY);
    CHECK(name, dev.sub_1006.u32 == 2500u);
    CHECK(name, dn_calls == 1);
    CHECK(name, lely_rtt_runtime_sync_set_period(&runtime, 0u) == RT_EOK);
    CHECK(name, dev.sub_1006.u32 == 0u);
    CHECK(name, dn_calls == 2);

    dev.sub_1005.u32 = CO_SYNC_COBID_PRODUCER | 0x80u;
    dev.sub_1006.dn_abort = 0x06090030;
    CHECK(name, lely_rtt_runtime_sync_set_period(&runtime, 5000u) == -RT_ERROR);
    CHECK(name, dev.sub_1006.u32 == 0u);
    CHECK(name, dn_calls == 3);

    dev.sub_1006.dn_abort = 0;
    nmt.sync_service = RT_NULL;
    CHECK(name, lely_rtt_runtime_sync_set_period(&runtime, 1000u) == -RT_EBUSY);
    CHECK(name, dev.sub_1006.u32 == 0u);
    CHECK(name, dn_calls == 3);
    puts("PASS sync-period-control");
}

static void
test_pdo_transmission_control(void)
{
    const char *name = "pdo-transmission-control";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;
    rt_uint8_t transmission_type = 0;
    int stop_calls;
    int start_calls;
    int writes;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_pdo_get_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, &transmission_type) == RT_EOK);
    CHECK(name, transmission_type == 1u);
    CHECK(name, tpdo.stop_calls == 1);
    CHECK(name, tpdo.start_calls == 1);
    CHECK(name, tpdo.comm.trans == 1u);

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1u, 1u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_pdo_get_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1u, &transmission_type) == RT_EOK);
    CHECK(name, transmission_type == 1u);
    CHECK(name, rpdo.stop_calls == 1);
    CHECK(name, rpdo.start_calls == 1);
    CHECK(name, rpdo.comm.trans == 1u);

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 240u) == RT_EOK);
    CHECK(name, dev.tpdo_trans.u8 == 240u);
    CHECK(name, tpdo.comm.trans == 240u);

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 254u) == RT_EOK);
    CHECK(name, dev.tpdo_trans.u8 == 254u);
    CHECK(name, tpdo.comm.trans == 254u);

    stop_calls = tpdo.stop_calls;
    start_calls = tpdo.start_calls;
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 255u) == RT_EOK);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, tpdo.comm.trans == 255u);
    CHECK(name, tpdo.stop_calls == stop_calls);
    CHECK(name, tpdo.start_calls == start_calls);

    writes = dn_calls;
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 255u) == RT_EOK);
    CHECK(name, dn_calls == writes);
    CHECK(name, tpdo.stop_calls == stop_calls);
    CHECK(name, tpdo.start_calls == start_calls);
    puts("PASS pdo-transmission-control");
}

static void
test_pdo_rejected_values_preserve_state(void)
{
    const char *name = "pdo-rejected-values-preserve-state";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;
    int posts;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    posts = command_posts;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 241u) == -RT_EINVAL);
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 253u) == -RT_EINVAL);
    CHECK(name, command_posts == posts);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, dn_calls == 0);
    CHECK(name, tpdo.stop_calls == 0);
    CHECK(name, tpdo.start_calls == 0);

    dev.has_1005 = 0;
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == -RT_ENOSYS);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, dn_calls == 0);
    CHECK(name, tpdo.stop_calls == 0);
    CHECK(name, tpdo.start_calls == 0);

    dev.has_1005 = 1;
    dev.tpdo_trans.dn_abort = 0x06090030;
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == -RT_ERROR);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, dn_calls == 1);
    CHECK(name, tpdo.stop_calls == 0);
    CHECK(name, tpdo.start_calls == 0);
    puts("PASS pdo-rejected-values-preserve-state");
}

static void
test_tpdo_mode_transition_clears_transient_state(void)
{
    const char *name = "tpdo-mode-transition-clears-transient-state";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    tpdo.event_timer_active = 1;
    tpdo.event_pending = 1;
    tpdo.sync_count = 9;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 0u) == RT_EOK);
    CHECK(name, dev.tpdo_trans.u8 == 0u);
    CHECK(name, tpdo.comm.trans == 0u);
    CHECK(name, tpdo.event_timer_active == 0);
    CHECK(name, tpdo.event_pending == 0);
    CHECK(name, tpdo.sync_count == 0);
    CHECK(name, tpdo.stop_calls == 1);
    CHECK(name, tpdo.start_calls == 1);

    tpdo.event_timer_active = 1;
    tpdo.event_pending = 1;
    tpdo.sync_count = 5;
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 255u) == RT_EOK);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, tpdo.comm.trans == 255u);
    CHECK(name, tpdo.event_timer_active == 0);
    CHECK(name, tpdo.event_pending == 0);
    CHECK(name, tpdo.sync_count == 0);
    CHECK(name, tpdo.stop_calls == 2);
    CHECK(name, tpdo.start_calls == 2);
    puts("PASS tpdo-mode-transition-clears-transient-state");
}

static void
test_rpdo_mode_transition_drops_pending_frame(void)
{
    const char *name = "rpdo-mode-transition-drops-pending-frame";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    dev.rpdo_trans.u8 = 1u;
    rpdo.comm.trans = 1u;
    rpdo.sync_pending = 1;
    rpdo.pending_frame = 1;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1u, 255u) == RT_EOK);
    CHECK(name, dev.rpdo_trans.u8 == 255u);
    CHECK(name, rpdo.comm.trans == 255u);
    CHECK(name, rpdo.sync_pending == 0);
    CHECK(name, rpdo.pending_frame == 0);
    CHECK(name, rpdo.stop_calls == 1);
    CHECK(name, rpdo.start_calls == 1);
    puts("PASS rpdo-mode-transition-drops-pending-frame");
}

static void
test_tpdo_sync_to_sync_restart_clears_transient_state(void)
{
    const char *name = "tpdo-sync-to-sync-restart-clears-transient-state";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    dev.tpdo_trans.u8 = 1u;
    tpdo.comm.trans = 1u;
    tpdo.event_timer_active = 1;
    tpdo.event_pending = 1;
    tpdo.sync_count = 7;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 240u) == RT_EOK);
    CHECK(name, dev.tpdo_trans.u8 == 240u);
    CHECK(name, tpdo.comm.trans == 240u);
    CHECK(name, tpdo.event_timer_active == 0);
    CHECK(name, tpdo.event_pending == 0);
    CHECK(name, tpdo.sync_count == 0);
    CHECK(name, tpdo.stop_calls == 1);
    CHECK(name, tpdo.start_calls == 1);
    puts("PASS tpdo-sync-to-sync-restart-clears-transient-state");
}

static void
test_rpdo_sync_to_sync_restart_drops_pending_frame(void)
{
    const char *name = "rpdo-sync-to-sync-restart-drops-pending-frame";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    dev.rpdo_trans.u8 = 1u;
    rpdo.comm.trans = 1u;
    rpdo.sync_pending = 1;
    rpdo.pending_frame = 1;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1u, 240u) == RT_EOK);
    CHECK(name, dev.rpdo_trans.u8 == 240u);
    CHECK(name, rpdo.comm.trans == 240u);
    CHECK(name, rpdo.sync_pending == 0);
    CHECK(name, rpdo.pending_frame == 0);
    CHECK(name, rpdo.stop_calls == 1);
    CHECK(name, rpdo.start_calls == 1);
    puts("PASS rpdo-sync-to-sync-restart-drops-pending-frame");
}

static void
test_pdo_restart_failure_rolls_back(void)
{
    const char *name = "pdo-restart-failure-rolls-back";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    tpdo.start_failures_remaining = 1;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == -RT_ERROR);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, tpdo.start_calls == 2);
    CHECK(name, tpdo.stopped == 0);
    CHECK(name, tpdo.comm.trans == 255u);

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == RT_EOK);
    CHECK(name, dev.tpdo_trans.u8 == 1u);
    CHECK(name, tpdo.start_calls == 3);
    CHECK(name, tpdo.stop_calls == 2);
    CHECK(name, tpdo.stopped == 0);
    CHECK(name, tpdo.comm.trans == 1u);
    puts("PASS pdo-restart-failure-rolls-back");
}

static void
test_rpdo_restart_failure_rolls_back(void)
{
    const char *name = "rpdo-restart-failure-rolls-back";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    rpdo.start_failures_remaining = 1;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1u, 1u) == -RT_ERROR);
    CHECK(name, dev.rpdo_trans.u8 == 255u);
    CHECK(name, rpdo.start_calls == 2);
    CHECK(name, rpdo.stopped == 0);
    CHECK(name, rpdo.comm.trans == 255u);

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_RPDO, 1u, 1u) == RT_EOK);
    CHECK(name, dev.rpdo_trans.u8 == 1u);
    CHECK(name, rpdo.start_calls == 3);
    CHECK(name, rpdo.stop_calls == 2);
    CHECK(name, rpdo.stopped == 0);
    CHECK(name, rpdo.comm.trans == 1u);
    puts("PASS rpdo-restart-failure-rolls-back");
}

static void
test_pdo_rollback_restart_failure_fails_closed(void)
{
    const char *name = "pdo-rollback-restart-failure-fails-closed";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;
    int writes;
    int start_calls;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    tpdo.start_failures_remaining = 2;

    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == -RT_ERROR);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, tpdo.start_calls == 2);
    CHECK(name, tpdo.stopped == 1);

    writes = dn_calls;
    start_calls = tpdo.start_calls;
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == -RT_EBUSY);
    CHECK(name, dev.tpdo_trans.u8 == 255u);
    CHECK(name, dn_calls == writes);
    CHECK(name, tpdo.start_calls == start_calls);
    CHECK(name, tpdo.stopped == 1);
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == -RT_EBUSY);
    CHECK(name, tpdo.event_calls == 0);
    puts("PASS pdo-rollback-restart-failure-fails-closed");
}

static void
test_tpdo_event_modes(void)
{
    const char *name = "tpdo-event-modes";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x5678u;
    tpdo.comm.trans = 0u;
    nmt.sync_service = RT_NULL;
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == -RT_EBUSY);
    CHECK(name, tpdo.event_calls == 0);

    nmt.sync_service = &nmt;
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == RT_EOK);
    CHECK(name, tpdo.event_calls == 1);

    tpdo.comm.trans = 1u;
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == -RT_EINVAL);
    CHECK(name, tpdo.event_calls == 1);

    tpdo.comm.trans = 254u;
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == RT_EOK);
    CHECK(name, tpdo.event_calls == 2);
    tpdo.comm.trans = 255u;
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == RT_EOK);
    CHECK(name, tpdo.event_calls == 3);

    tpdo.map.n = 0u;
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == -RT_EINVAL);
    CHECK(name, tpdo.event_calls == 3);
    tpdo.map.n = 1u;
    tpdo.comm.cobid = CO_PDO_COBID_VALID | 0x201u;
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == -RT_EINVAL);
    CHECK(name, tpdo.event_calls == 3);
    puts("PASS tpdo-event-modes");
}

static void
test_owner_thread_wait_rejected(void)
{
    const char *name = "owner-thread-wait-rejected";
    struct lely_rtt_runtime runtime;
    struct fake_dev dev;
    struct fake_nmt nmt;
    struct fake_rpdo rpdo;
    struct fake_tpdo tpdo;
    rt_uint8_t transmission_type = 0xa5u;

    init_fixture(&runtime, &dev, &nmt, &rpdo, &tpdo);
    runtime.owner_thread = fake_self;
    CHECK(name, lely_rtt_runtime_sync_set_period(&runtime, 1000u) == -RT_EINVAL);
    CHECK(name, lely_rtt_runtime_pdo_set_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, 1u) == -RT_EINVAL);
    CHECK(name, lely_rtt_runtime_pdo_get_transmission(&runtime,
            LELY_RTT_PDO_DIRECTION_TPDO, 1u, &transmission_type) == -RT_EINVAL);
    CHECK(name, transmission_type == 0xa5u);
    CHECK(name, lely_rtt_runtime_tpdo_event(&runtime, 1u) == -RT_EINVAL);
    CHECK(name, command_posts == 0);
    CHECK(name, fake_queue.count == 0u);
    puts("PASS owner-thread-wait-rejected");
}

int
main(void)
{
    test_sync_bind_before_service_creation();
    test_sync_bind_ownership();
    test_sync_callback_registration_and_snapshot();
    test_public_owner_command_wiring();
    test_sync_period_control();
    test_pdo_transmission_control();
    test_pdo_rejected_values_preserve_state();
    test_tpdo_mode_transition_clears_transient_state();
    test_rpdo_mode_transition_drops_pending_frame();
    test_tpdo_sync_to_sync_restart_clears_transient_state();
    test_rpdo_sync_to_sync_restart_drops_pending_frame();
    test_pdo_restart_failure_rolls_back();
    test_rpdo_restart_failure_rolls_back();
    test_pdo_rollback_restart_failure_fails_closed();
    test_tpdo_event_modes();
    test_owner_thread_wait_rejected();
    puts("Passed 16/16 host B9 cases");
    return 0;
}
