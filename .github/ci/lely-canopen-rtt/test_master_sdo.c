/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-08     wdfk-prog         first version
 * 2026-09-11     wdfk-prog         cover custom CSDO routing and per-node FIFO
 * 2026-09-11     wdfk-prog         cover FIFO progress after synchronous start failure
 * 2026-09-11     wdfk-prog         cover failed pending-cancel owner reschedule
 * 2026-09-12     wdfk-prog         cover custom CSDO COB-ID collision rejection
 * 2026-09-12     wdfk-prog         cover cancel teardown pins and unselected CSDOs
 */

/**
 * @file test_master_sdo.c
 * @brief Host-stub regression tests for SDO routing, FIFO and cancellation.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PKG_LELY_USING_MASTER_SDO 1
#define PKG_LELY_MASTER_SDO_QUEUE_DEPTH 2
#define LELY_NO_CO_NMT_BOOT 1
#define LELY_RTT_INTERNAL_H_ 1

#define CO_NUM_NODES 127u
#define CO_NUM_SDOS 128u
#define CO_SDO_COBID_VALID 0x80000000u
#define CO_SDO_COBID_FRAME 0x20000000u
#define LELY_RTT_SDO_CHANNEL_PREDEFINED 0u
#define CO_NMT_ST_BOOTUP 0u
#define CO_NMT_ST_STOP 4u
#define CO_NMT_ST_START 5u
#define CO_NMT_ST_PREOP 127u
#define CO_SDO_AC_NO_SDO 0x060a0023u

#define RT_EOK 0
#define RT_ERROR 1
#define RT_ETIMEOUT 110
#define RT_EINVAL 22
#define RT_EBUSY 16
#define RT_ENOMEM 12
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_NULL NULL
#define RT_IPC_FLAG_FIFO 0
#define RT_EVENT_FLAG_OR 1u
#define RT_EVENT_FLAG_CLEAR 2u
#define LELY_RTT_EVENT_COMMAND (1u << 6)
#define RT_WAITING_FOREVER (-1)
#define RT_WAITING_NO 0

#define LELY_RTT_LOG_E(...) lely_test_log(__VA_ARGS__)

#define rt_calloc calloc
#define rt_malloc malloc
#define rt_free free
#define rt_memcpy memcpy
#define rt_memset memset

typedef int rt_err_t;
typedef int rt_bool_t;
typedef int32_t rt_int32_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;
typedef size_t rt_size_t;
typedef int rt_atomic_t;

typedef uint8_t co_unsigned8_t;
typedef uint16_t co_unsigned16_t;
typedef uint32_t co_unsigned32_t;

typedef struct co_nmt co_nmt_t;
typedef struct co_csdo co_csdo_t;
typedef struct io_can_net io_can_net_t;

struct co_sdo_par {
    co_unsigned8_t n;
    co_unsigned32_t cobid_req;
    co_unsigned32_t cobid_res;
    co_unsigned8_t id;
};

typedef void co_csdo_dn_con_t(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, void *data);
typedef void co_csdo_up_con_t(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, const void *ptr,
        size_t n, void *data);

struct rt_event {
    int signaled;
};

struct co_nmt {
    co_unsigned8_t id;
    co_csdo_t *csdos[CO_NUM_SDOS + 1u];
};

struct io_can_net {
    int unused;
};

struct co_csdo {
    int stopped;
    int idle;
    int start_fail;
    int request_fail;
    int abort_calls;
    int stop_calls;
    int block_up_calls;
    int block_dn_calls;
    int normal_up_calls;
    int normal_dn_calls;
    int timeout;
    rt_uint8_t pst;
    const void *request_data;
    size_t request_size;
    co_csdo_dn_con_t *dn_con;
    co_csdo_up_con_t *up_con;
    void *con_data;
    struct co_sdo_par par;
};

struct lely_rtt_sdo_request;
typedef struct lely_rtt_sdo_request lely_rtt_sdo_request_t;
typedef struct lely_rtt_runtime lely_rtt_runtime_t;

enum lely_rtt_sdo_operation {
    LELY_RTT_SDO_UPLOAD = 0,
    LELY_RTT_SDO_DOWNLOAD,
};

enum lely_rtt_sdo_completion_status {
    LELY_RTT_SDO_COMPLETION_OK = 0,
    LELY_RTT_SDO_COMPLETION_ABORT,
    LELY_RTT_SDO_COMPLETION_CANCELED,
    LELY_RTT_SDO_COMPLETION_LOCAL_ERROR,
};

struct lely_rtt_sdo_result {
    rt_uint32_t request_id;
    enum lely_rtt_sdo_operation operation;
    rt_uint8_t node_id;
    rt_uint16_t index;
    rt_uint8_t subindex;
    enum lely_rtt_sdo_completion_status status;
    rt_err_t local_error;
    rt_uint32_t abort_code;
    const void *data;
    rt_size_t size;
};

enum lely_rtt_nmt_command {
    LELY_RTT_NMT_COMMAND_START = 0,
    LELY_RTT_NMT_COMMAND_STOP,
    LELY_RTT_NMT_COMMAND_PREOP,
    LELY_RTT_NMT_COMMAND_RESET_NODE,
    LELY_RTT_NMT_COMMAND_RESET_COMM,
};

enum lely_rtt_master_command_type {
    LELY_RTT_MASTER_COMMAND_SDO = 0,
    LELY_RTT_MASTER_COMMAND_SDO_CANCEL,
};

struct lely_rtt_master_command {
    enum lely_rtt_master_command_type type;
    union {
        struct {
            lely_rtt_sdo_request_t *request;
        } sdo;
        struct {
            rt_uint8_t node_id;
            rt_uint32_t request_id;
        } sdo_cancel;
    } data;
};

struct lely_rtt_runtime {
    void *can_net;
    co_nmt_t *master_nmt;
    void *owner_thread;
    struct rt_event event;
    rt_bool_t event_initialized;
    rt_atomic_t local_node_id;
    rt_atomic_t sdo_next_request_id;
    rt_uint8_t sdo_channel[CO_NUM_NODES + 1u];
    co_csdo_t *sdo_clients[CO_NUM_NODES + 1u];
    lely_rtt_sdo_request_t *sdo_active[CO_NUM_NODES + 1u];
    lely_rtt_sdo_request_t *sdo_pending[CO_NUM_NODES + 1u];
    rt_bool_t sdo_suspended[CO_NUM_NODES + 1u];
    rt_bool_t sdo_reset_pending[CO_NUM_NODES + 1u];
    rt_bool_t sdo_stop_pending[CO_NUM_NODES + 1u];
    rt_atomic_t sdo_cancel_refs;
};

static struct lely_rtt_master_command queued_command;
static int queued_command_valid;
static int command_auto_dispatch;
static int cancel_auto_dispatch;
static int cancel_race_remote_completion;
static int cancel_race_pending_promotion;
static rt_err_t command_post_error;
static int request_fail_once;
static int event_send_calls;
static int owner_event_send_calls;
static int owner_event_auto_reap;
static int runtime_lifetime_pins;
static int owner_wake_without_lifetime_pin;
static int cancel_wait_delay_calls;
static lely_rtt_sdo_request_t *cancel_wait_release_request;
static rt_atomic_t cancel_wait_release_state;
static struct lely_rtt_runtime *event_runtime;
static const rt_uint8_t race_payload[] = { 0x91u, 0x92u, 0x93u };

static void
lely_test_log(const char *format, ...)
{
    (void)format;
}

void lely_rtt_master_sdo_dispatch(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request);
void lely_rtt_master_sdo_cancel_dispatch(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_uint32_t request_id);
void lely_rtt_master_sdo_cancel_node(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id);
void lely_rtt_master_sdo_reap(struct lely_rtt_runtime *runtime);
static rt_bool_t lely_rtt_sdo_request_cancel_unpin(
        lely_rtt_sdo_request_t *request, rt_atomic_t pinned_state,
        rt_bool_t cancel_admitted);

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

static rt_atomic_t
rt_atomic_add(rt_atomic_t *target, rt_atomic_t value)
{
    const rt_atomic_t old = *target;
    *target += value;
    return old;
}

static rt_atomic_t
rt_atomic_sub(rt_atomic_t *target, rt_atomic_t value)
{
    const rt_atomic_t old = *target;
    *target -= value;
    return old;
}

static int
rt_atomic_compare_exchange_strong(rt_atomic_t *target, rt_atomic_t *expected,
        rt_atomic_t desired)
{
    if (*target == *expected) {
        *target = desired;
        return 1;
    }
    *expected = *target;
    return 0;
}

static rt_err_t
rt_event_init(struct rt_event *event, const char *name, int flag)
{
    (void)name;
    (void)flag;
    event->signaled = 0;
    return RT_EOK;
}

static rt_err_t
rt_event_detach(struct rt_event *event)
{
    event->signaled = 0;
    return RT_EOK;
}

static rt_err_t
rt_event_send(struct rt_event *event, rt_uint32_t events)
{
    event->signaled = 1;
    event_send_calls++;
    if (event_runtime && event == &event_runtime->event
            && (events & LELY_RTT_EVENT_COMMAND)) {
        owner_event_send_calls++;
        if (runtime_lifetime_pins <= 0)
            owner_wake_without_lifetime_pin++;
        if (owner_event_auto_reap)
            lely_rtt_master_sdo_reap(event_runtime);
    }
    return RT_EOK;
}

static rt_bool_t
lely_rtt_callback_acquire(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return RT_FALSE;
    runtime_lifetime_pins++;
    return RT_TRUE;
}

static void
lely_rtt_callback_release(struct lely_rtt_runtime *runtime)
{
    (void)runtime;
    runtime_lifetime_pins--;
}

static rt_err_t
rt_event_recv(struct rt_event *event, rt_uint32_t set, rt_uint32_t option,
        rt_int32_t timeout, rt_uint32_t *events)
{
    (void)set;
    (void)option;
    (void)timeout;
    if (!event->signaled)
        return -RT_ETIMEOUT;
    if (events)
        *events = 1u;
    event->signaled = 0;
    return RT_EOK;
}

static void
rt_thread_mdelay(int delay)
{
    lely_rtt_sdo_request_t *request;
    rt_atomic_t pinned_state;

    (void)delay;
    if (!cancel_wait_release_request)
        return;

    request = cancel_wait_release_request;
    pinned_state = cancel_wait_release_state;
    cancel_wait_release_request = NULL;
    cancel_wait_delay_calls++;
    (void)lely_rtt_sdo_request_cancel_unpin(request, pinned_state, RT_FALSE);
}

static rt_int32_t
lely_rtt_timeout_ticks(rt_uint32_t timeout_ms)
{
    return (rt_int32_t)timeout_ms;
}

static void *
io_can_net_get_net(void *can_net)
{
    return can_net;
}

static co_csdo_t *
co_csdo_create(void *net, void *dev, co_unsigned8_t id)
{
    co_csdo_t *sdo;

    (void)net;
    (void)dev;
    (void)id;
    sdo = calloc(1, sizeof(*sdo));
    if (sdo)
        sdo->idle = 1;
    return sdo;
}

static void
co_csdo_destroy(co_csdo_t *sdo)
{
    free(sdo);
}

static int
co_csdo_is_stopped(const co_csdo_t *sdo)
{
    return sdo->stopped;
}

static int
co_csdo_is_idle(const co_csdo_t *sdo)
{
    return sdo->idle;
}

static int
co_csdo_is_valid(const co_csdo_t *sdo)
{
    return !(sdo->par.cobid_req & CO_SDO_COBID_VALID)
            && !(sdo->par.cobid_res & CO_SDO_COBID_VALID);
}

static int
co_csdo_start(co_csdo_t *sdo)
{
    if (sdo->start_fail)
        return -1;
    sdo->stopped = 0;
    sdo->idle = 1;
    return 0;
}

static void
co_csdo_stop(co_csdo_t *sdo)
{
    sdo->stop_calls++;
    sdo->stopped = 1;
    sdo->idle = 1;
}

static void
co_csdo_set_timeout(co_csdo_t *sdo, int timeout)
{
    sdo->timeout = timeout;
}

static int
fake_request_should_fail(co_csdo_t *sdo)
{
    if (request_fail_once) {
        request_fail_once = 0;
        return 1;
    }
    return sdo->request_fail;
}

static const struct co_sdo_par *
co_csdo_get_par(const co_csdo_t *sdo)
{
    return &sdo->par;
}

static co_csdo_t *
co_nmt_get_csdo(const co_nmt_t *nmt, co_unsigned8_t n)
{
    if (!nmt || !n || n > CO_NUM_SDOS)
        return NULL;
    return nmt->csdos[n];
}

static co_unsigned8_t
co_nmt_get_id(const co_nmt_t *nmt)
{
    return nmt ? nmt->id : 0u;
}

static int
co_csdo_up_req(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_csdo_up_con_t *con, void *data)
{
    (void)idx;
    (void)subidx;
    sdo->normal_up_calls++;
    sdo->up_con = con;
    sdo->dn_con = NULL;
    sdo->con_data = data;
    if (fake_request_should_fail(sdo))
        return -1;
    sdo->idle = 0;
    return 0;
}

static int
co_csdo_dn_req(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, const void *ptr, size_t n,
        co_csdo_dn_con_t *con, void *data)
{
    (void)idx;
    (void)subidx;
    sdo->normal_dn_calls++;
    sdo->request_data = ptr;
    sdo->request_size = n;
    sdo->dn_con = con;
    sdo->up_con = NULL;
    sdo->con_data = data;
    if (fake_request_should_fail(sdo))
        return -1;
    sdo->idle = 0;
    return 0;
}

static int
co_csdo_blk_up_req(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned8_t pst,
        co_csdo_up_con_t *con, void *data)
{
    (void)idx;
    (void)subidx;
    sdo->block_up_calls++;
    sdo->pst = pst;
    sdo->up_con = con;
    sdo->dn_con = NULL;
    sdo->con_data = data;
    if (fake_request_should_fail(sdo))
        return -1;
    sdo->idle = 0;
    return 0;
}

static int
co_csdo_blk_dn_req(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, const void *ptr, size_t n,
        co_csdo_dn_con_t *con, void *data)
{
    (void)idx;
    (void)subidx;
    sdo->block_dn_calls++;
    sdo->request_data = ptr;
    sdo->request_size = n;
    sdo->dn_con = con;
    sdo->up_con = NULL;
    sdo->con_data = data;
    if (fake_request_should_fail(sdo))
        return -1;
    sdo->idle = 0;
    return 0;
}

static int
co_csdo_abort_req(co_csdo_t *sdo, co_unsigned32_t ac)
{
    sdo->abort_calls++;
    sdo->idle = 1;
    if (sdo->dn_con)
        sdo->dn_con(sdo, 0, 0, ac, sdo->con_data);
    else if (sdo->up_con)
        sdo->up_con(sdo, 0, 0, ac, NULL, 0, sdo->con_data);
    return 0;
}

static int
co_nmt_is_booting(const co_nmt_t *nmt, co_unsigned8_t id)
{
    (void)nmt;
    (void)id;
    return 0;
}

static void
fake_complete_download(co_csdo_t *sdo, co_unsigned32_t ac)
{
    co_csdo_dn_con_t *con = sdo->dn_con;
    void *data = sdo->con_data;

    sdo->idle = 1;
    sdo->dn_con = NULL;
    sdo->con_data = NULL;
    con(sdo, 0x2000u, 0u, ac, data);
}

static void
fake_complete_upload(co_csdo_t *sdo, co_unsigned32_t ac,
        const void *payload, size_t size)
{
    co_csdo_up_con_t *con = sdo->up_con;
    void *data = sdo->con_data;

    sdo->idle = 1;
    sdo->up_con = NULL;
    sdo->con_data = NULL;
    con(sdo, 0x2000u, 0u, ac, payload, size, data);
}

static rt_err_t
lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command)
{
    if (command_post_error != RT_EOK) {
        if (command->type == LELY_RTT_MASTER_COMMAND_SDO_CANCEL
                && cancel_race_pending_promotion) {
            co_csdo_t *sdo = runtime->sdo_clients[command->data.sdo_cancel.node_id];

            /* Exercise owner promotion while the pending cancel pin is held. */
            cancel_race_pending_promotion = 0;
            fake_complete_upload(sdo, 0, race_payload, sizeof(race_payload));
            lely_rtt_master_sdo_reap(runtime);
        }
        return command_post_error;
    }

    queued_command = *command;
    queued_command_valid = 1;

    if (command->type == LELY_RTT_MASTER_COMMAND_SDO && command_auto_dispatch) {
        queued_command_valid = 0;
        lely_rtt_master_sdo_dispatch(runtime, command->data.sdo.request);
    } else if (command->type == LELY_RTT_MASTER_COMMAND_SDO_CANCEL) {
        if (cancel_race_remote_completion) {
            co_csdo_t *sdo = runtime->sdo_clients[command->data.sdo_cancel.node_id];

            cancel_race_remote_completion = 0;
            fake_complete_upload(sdo, 0, race_payload, sizeof(race_payload));
        } else if (cancel_race_pending_promotion) {
            co_csdo_t *sdo = runtime->sdo_clients[command->data.sdo_cancel.node_id];

            /*
             * Finish the active request while cancel() still pins the FIFO
             * head, then let the owner reaper attempt promotion in that window.
             */
            cancel_race_pending_promotion = 0;
            fake_complete_upload(sdo, 0, race_payload, sizeof(race_payload));
            lely_rtt_master_sdo_reap(runtime);
        } else if (cancel_auto_dispatch) {
            queued_command_valid = 0;
            lely_rtt_master_sdo_cancel_dispatch(runtime,
                    command->data.sdo_cancel.node_id,
                    command->data.sdo_cancel.request_id);
        }
    }
    return RT_EOK;
}

#include "../../../port/rtthread/src/master_sdo_request.c"
#include "../../../port/rtthread/src/master_sdo_client.c"
#include "../../../port/rtthread/src/master_sdo.c"

#define CHECK(name, condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", name, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static void
reset_runtime(struct lely_rtt_runtime *runtime, struct co_nmt *nmt,
        struct io_can_net *can_net)
{
    memset(runtime, 0, sizeof(*runtime));
    memset(nmt, 0, sizeof(*nmt));
    memset(can_net, 0, sizeof(*can_net));
    runtime->master_nmt = nmt;
    runtime->can_net = can_net;
    runtime->event_initialized = RT_TRUE;
    event_runtime = runtime;
    runtime->local_node_id = 127;
    nmt->id = 127u;
    queued_command_valid = 0;
    command_auto_dispatch = 0;
    cancel_auto_dispatch = 0;
    cancel_race_remote_completion = 0;
    cancel_race_pending_promotion = 0;
    command_post_error = RT_EOK;
    request_fail_once = 0;
    event_send_calls = 0;
    owner_event_send_calls = 0;
    owner_event_auto_reap = 0;
    runtime_lifetime_pins = 0;
    owner_wake_without_lifetime_pin = 0;
    cancel_wait_delay_calls = 0;
    cancel_wait_release_request = NULL;
    cancel_wait_release_state = 0;
}

static int
test_block_transfer_success_and_ownership(void)
{
    const char *name = "block-transfer-success-and-ownership";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *request;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;
    rt_uint8_t source[] = { 1u, 2u, 3u, 4u };
    rt_uint8_t upload[] = { 0xa1u, 0xb2u, 0xc3u };

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    request = lely_rtt_sdo_request_create();
    CHECK(name, request != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_block_download(&runtime, request,
            1u, 0x2000u, 0u, source, sizeof(source), 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    CHECK(name, sdo != NULL && sdo->block_dn_calls == 1);
    CHECK(name, sdo->request_data != source);
    source[0] = 0xeeu;
    CHECK(name, ((const rt_uint8_t *)sdo->request_data)[0] == 1u);
    fake_complete_download(sdo, 0);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, result.size == 4u && ((const rt_uint8_t *)result.data)[0] == 1u);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);

    request = lely_rtt_sdo_request_create();
    CHECK(name, request != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_block_upload(&runtime, request,
            1u, 0x2001u, 0u, 7u, 1000u) == RT_EOK);
    CHECK(name, sdo->block_up_calls == 1 && sdo->pst == 7u);
    fake_complete_upload(sdo, 0, upload, sizeof(upload));
    upload[0] = 0xffu;
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, result.size == 3u && ((const rt_uint8_t *)result.data)[0] == 0xa1u);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS block-transfer-success-and-ownership");
    return 0;
}

static int
test_block_abort_and_start_failures(void)
{
    const char *name = "block-abort-and-start-failures";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *request;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;
    const rt_uint8_t payload[] = { 1u, 2u };

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    request = lely_rtt_sdo_request_create();
    CHECK(name, lely_rtt_runtime_post_sdo_block_upload(&runtime, request,
            1u, 0x2000u, 0u, 0u, 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    fake_complete_upload(sdo, 0x05040000u, NULL, 0);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_ABORT);
    CHECK(name, result.abort_code == 0x05040000u);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);

    request = lely_rtt_sdo_request_create();
    sdo->request_fail = 1;
    CHECK(name, lely_rtt_runtime_post_sdo_block_download(&runtime, request,
            1u, 0x2000u, 0u, payload, sizeof(payload), 1000u) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_ERROR);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    sdo->request_fail = 0;

    request = lely_rtt_sdo_request_create();
    sdo->stopped = 1;
    sdo->start_fail = 1;
    CHECK(name, lely_rtt_runtime_post_sdo_block_upload(&runtime, request,
            1u, 0x2000u, 0u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_ERROR);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    sdo->start_fail = 0;
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS block-abort-and-start-failures");
    return 0;
}

static int
test_queued_cancel_and_teardown_arbitration(void)
{
    const char *name = "queued-cancel-and-teardown-arbitration";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *request;
    struct lely_rtt_sdo_result result;

    reset_runtime(&runtime, &nmt, &can_net);
    request = lely_rtt_sdo_request_create();
    CHECK(name, lely_rtt_runtime_post_sdo_block_upload(&runtime, request,
            1u, 0x2000u, 0u, 0u, 1000u) == RT_EOK);
    CHECK(name, queued_command_valid);
    CHECK(name, lely_rtt_sdo_request_cancel(request) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_cancel(request) == RT_EOK);
    lely_rtt_master_sdo_dispatch(&runtime, queued_command.data.sdo.request);
    CHECK(name, runtime.sdo_clients[1] == NULL);
    CHECK(name, event_send_calls == 1);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, result.abort_code == CO_SDO_AC_NO_SDO);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);

    request = lely_rtt_sdo_request_create();
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x2000u, 0u, 1000u) == RT_EOK);
    lely_rtt_master_sdo_cancel_queued(request);
    CHECK(name, lely_rtt_sdo_request_cancel(request) == -RT_EBUSY);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, result.abort_code == 0u);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    puts("PASS queued-cancel-and-teardown-arbitration");
    return 0;
}

static int
test_active_cancel_and_completion_pin(void)
{
    const char *name = "active-cancel-and-completion-pin";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *request;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;
    rt_uint32_t request_id;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    cancel_auto_dispatch = 1;
    request = lely_rtt_sdo_request_create();
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x2000u, 0u, 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    CHECK(name, lely_rtt_sdo_request_get_id(request, &request_id) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_cancel(request) == RT_EOK);
    CHECK(name, sdo->abort_calls == 1 && event_send_calls == 1);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, result.abort_code == CO_SDO_AC_NO_SDO);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);

    request = lely_rtt_sdo_request_create();
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x2001u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_id(request, &request_id) == RT_EOK);
    cancel_auto_dispatch = 0;
    cancel_race_remote_completion = 1;
    event_send_calls = 0;
    CHECK(name, lely_rtt_sdo_request_cancel(request) == RT_EOK);
    CHECK(name, event_send_calls == 1);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, result.size == sizeof(race_payload));
    CHECK(name, memcmp(result.data, race_payload, sizeof(race_payload)) == 0);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    lely_rtt_master_sdo_cancel_dispatch(&runtime, 1u, request_id);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS active-cancel-and-completion-pin");
    return 0;
}


static int
test_cancel_pin_blocks_sdo_teardown(void)
{
    const char *name = "cancel-pin-blocks-sdo-teardown";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *active;
    lely_rtt_sdo_request_t *pending;
    struct lely_rtt_sdo_result result;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    active = lely_rtt_sdo_request_create();
    CHECK(name, active != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, active,
            1u, 0x2002u, 0u, 1000u) == RT_EOK);
    CHECK(name, runtime.sdo_active[1] == active);

    rt_atomic_store(&active->state, LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_PINNED);
    cancel_wait_release_request = active;
    cancel_wait_release_state = LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_PINNED;
    lely_rtt_master_sdo_fini(&runtime);
    CHECK(name, cancel_wait_delay_calls == 1);
    CHECK(name, runtime.sdo_cancel_refs == 0);
    CHECK(name, lely_rtt_sdo_request_get_result(active, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, lely_rtt_sdo_request_destroy(active) == RT_EOK);

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    active = lely_rtt_sdo_request_create();
    pending = lely_rtt_sdo_request_create();
    CHECK(name, active && pending);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, active,
            1u, 0x2003u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, pending,
            1u, 0x2004u, 0u, 1000u) == RT_EOK);
    CHECK(name, runtime.sdo_pending[1] == pending);

    rt_atomic_store(&pending->state, LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED);
    cancel_wait_release_request = pending;
    cancel_wait_release_state = LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED;
    lely_rtt_master_sdo_fini(&runtime);
    CHECK(name, cancel_wait_delay_calls == 1);
    CHECK(name, runtime.sdo_cancel_refs == 0);
    CHECK(name, lely_rtt_sdo_request_get_result(active, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, lely_rtt_sdo_request_get_result(pending, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, lely_rtt_sdo_request_destroy(active) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(pending) == RT_EOK);

    puts("PASS cancel-pin-blocks-sdo-teardown");
    return 0;
}

static int
test_stale_cancel_identity_is_ignored(void)
{
    const char *name = "stale-cancel-identity-is-ignored";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *request;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;
    rt_uint32_t request_id;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    request = lely_rtt_sdo_request_create();
    CHECK(name, lely_rtt_runtime_post_sdo_download(&runtime, request,
            1u, 0x2000u, 0u, "x", 1u, 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    CHECK(name, lely_rtt_sdo_request_get_id(request, &request_id) == RT_EOK);
    lely_rtt_master_sdo_cancel_dispatch(&runtime, 1u, request_id + 1u);
    CHECK(name, sdo->abort_calls == 0);
    CHECK(name, runtime.sdo_active[1] == request);
    fake_complete_download(sdo, 0);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS stale-cancel-identity-is-ignored");
    return 0;
}

static void
init_custom_sdo(struct co_nmt *nmt, struct co_csdo *sdo,
        rt_uint8_t sdo_number, rt_uint8_t node_id,
        rt_uint32_t request_cobid, rt_uint32_t response_cobid)
{
    memset(sdo, 0, sizeof(*sdo));
    sdo->idle = 1;
    sdo->par.n = 3u;
    sdo->par.id = node_id;
    sdo->par.cobid_req = request_cobid;
    sdo->par.cobid_res = response_cobid;
    nmt->csdos[sdo_number] = sdo;
}

static int
test_custom_channel_validation_and_borrowed_lifecycle(void)
{
    const char *name = "custom-channel-validation-and-borrowed-lifecycle";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    struct co_csdo custom;
    lely_rtt_sdo_request_t *request;
    struct lely_rtt_sdo_result result;

    reset_runtime(&runtime, &nmt, &can_net);
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 0u, 1u)
            == -RT_EINVAL);
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 1u, 129u)
            == -RT_EINVAL);
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 1u, 128u)
            == RT_EOK);
    CHECK(name, runtime.sdo_channel[1] == 128u);
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 1u,
            LELY_RTT_SDO_CHANNEL_PREDEFINED) == RT_EOK);
    runtime.owner_thread = &runtime;
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 1u, 2u)
            == -RT_EINVAL);
    runtime.owner_thread = NULL;

    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 1u, 2u)
            == RT_EOK);
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    init_custom_sdo(&nmt, &custom, 2u, 2u, 0x700u, 0x680u);
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    custom.par.id = 1u;
    custom.par.n = 2u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    custom.par.n = 3u;
    custom.par.cobid_req = 0x601u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    custom.par.cobid_req = 0x581u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    custom.par.cobid_req = 0x700u;
    custom.par.cobid_res = 0x581u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    custom.par.cobid_res = 0x601u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    custom.par.cobid_res = CO_SDO_COBID_VALID | 0x680u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    custom.par.cobid_res = 0x680u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == RT_EOK);

    command_auto_dispatch = 1;
    custom.stopped = 1;
    request = lely_rtt_sdo_request_create();
    CHECK(name, request != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x20feu, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_EBUSY);
    CHECK(name, custom.normal_up_calls == 0);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);

    custom.stopped = 0;
    custom.idle = 0;
    request = lely_rtt_sdo_request_create();
    CHECK(name, request != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x20ffu, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_EBUSY);
    CHECK(name, custom.normal_up_calls == 0);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    custom.idle = 1;

    request = lely_rtt_sdo_request_create();
    CHECK(name, request != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x2100u, 0u, 1000u) == RT_EOK);
    CHECK(name, runtime.sdo_clients[1] == NULL);
    CHECK(name, runtime.sdo_active[1] == request);
    CHECK(name, custom.normal_up_calls == 1);
    fake_complete_upload(&custom, 0, "z", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, custom.stop_calls == 0);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);

    /* Runtime 0x1280 drift must fail closed before another transfer starts. */
    custom.par.id = 2u;
    request = lely_rtt_sdo_request_create();
    CHECK(name, request != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x2101u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_EINVAL);
    CHECK(name, custom.normal_up_calls == 1);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);
    custom.par.id = 1u;

    request = lely_rtt_sdo_request_create();
    CHECK(name, request != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, request,
            1u, 0x2101u, 0u, 1000u) == RT_EOK);
    CHECK(name, runtime.sdo_active[1] == request);
    lely_rtt_master_sdo_cancel_node(&runtime, 1u);
    CHECK(name, custom.abort_calls == 1);
    CHECK(name, custom.stop_calls == 0);
    CHECK(name, lely_rtt_sdo_request_get_result(request, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, lely_rtt_sdo_request_destroy(request) == RT_EOK);

    lely_rtt_master_sdo_fini(&runtime);
    CHECK(name, custom.stop_calls == 0);
    CHECK(name, runtime.sdo_channel[1] == 2u);
    puts("PASS custom-channel-validation-and-borrowed-lifecycle");
    return 0;
}

static int
test_custom_channel_cobid_conflicts_and_runtime_drift(void)
{
    const char *name = "custom-channel-cobid-conflicts-and-runtime-drift";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    struct co_csdo custom1;
    struct co_csdo custom2;
    struct co_csdo unselected;
    lely_rtt_sdo_request_t *node1;
    lely_rtt_sdo_request_t *node2;
    lely_rtt_sdo_request_t *drifted;
    struct lely_rtt_sdo_result result;

    reset_runtime(&runtime, &nmt, &can_net);
    init_custom_sdo(&nmt, &custom1, 2u, 1u, 0x700u, 0x680u);
    init_custom_sdo(&nmt, &custom2, 3u, 2u, 0x710u, 0x690u);
    init_custom_sdo(&nmt, &unselected, 4u, 3u, 0x720u, 0x6a0u);
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 1u, 2u)
            == RT_EOK);
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 2u, 3u)
            == RT_EOK);
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == RT_EOK);

    custom2.par.cobid_req = custom1.par.cobid_req;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);
    custom2.par.cobid_req = 0x710u;

    custom2.par.cobid_res = custom1.par.cobid_res;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);
    custom2.par.cobid_res = 0x690u;

    custom2.par.cobid_req = custom1.par.cobid_res;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);

    /* Standard and extended frames with the same low 11 bits are distinct. */
    custom2.par.cobid_req = CO_SDO_COBID_FRAME | custom1.par.cobid_req;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == RT_EOK);
    custom2.par.cobid_req = 0x710u;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == RT_EOK);

    unselected.par.cobid_req = custom1.par.cobid_req;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);
    unselected.par.cobid_req = 0x720u;
    unselected.par.cobid_res = custom1.par.cobid_res;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);
    unselected.par.cobid_res = 0x6a0u;
    unselected.par.cobid_req = custom1.par.cobid_res;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);
    unselected.par.cobid_req = 0x720u;
    unselected.par.cobid_res = custom1.par.cobid_req;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == -RT_EINVAL);
    unselected.par.cobid_res = 0x6a0u;

    unselected.par.cobid_req = CO_SDO_COBID_FRAME | custom1.par.cobid_req;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == RT_EOK);
    unselected.par.cobid_req = CO_SDO_COBID_VALID | custom1.par.cobid_req;
    CHECK(name, lely_rtt_master_sdo_validate_channels(&runtime) == RT_EOK);
    unselected.par.cobid_req = 0x720u;

    command_auto_dispatch = 1;
    node1 = lely_rtt_sdo_request_create();
    node2 = lely_rtt_sdo_request_create();
    CHECK(name, node1 && node2);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, node1,
            1u, 0x2501u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, node2,
            2u, 0x2502u, 0u, 1000u) == RT_EOK);
    CHECK(name, runtime.sdo_active[1] == node1);
    CHECK(name, runtime.sdo_active[2] == node2);
    CHECK(name, custom1.normal_up_calls == 1);
    CHECK(name, custom2.normal_up_calls == 1);

    fake_complete_upload(&custom1, 0, "1", 1u);
    fake_complete_upload(&custom2, 0, "2", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, lely_rtt_sdo_request_get_result(node1, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, lely_rtt_sdo_request_get_result(node2, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, lely_rtt_sdo_request_destroy(node1) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(node2) == RT_EOK);

    /* Runtime parameter drift in an unselected live peer must also fail closed. */
    unselected.par.cobid_res = custom1.par.cobid_res;
    drifted = lely_rtt_sdo_request_create();
    CHECK(name, drifted != NULL);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, drifted,
            1u, 0x2503u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_result(drifted, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_EINVAL);
    CHECK(name, custom1.normal_up_calls == 1);
    CHECK(name, lely_rtt_sdo_request_destroy(drifted) == RT_EOK);

    lely_rtt_master_sdo_fini(&runtime);
    CHECK(name, custom1.stop_calls == 0 && custom2.stop_calls == 0);
    CHECK(name, unselected.stop_calls == 0);
    puts("PASS custom-channel-cobid-conflicts-and-runtime-drift");
    return 0;
}

static int
test_per_node_fifo_order_and_capacity(void)
{
    const char *name = "per-node-fifo-order-and-capacity";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *r1;
    lely_rtt_sdo_request_t *r2;
    lely_rtt_sdo_request_t *r3;
    lely_rtt_sdo_request_t *r4;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    r1 = lely_rtt_sdo_request_create();
    r2 = lely_rtt_sdo_request_create();
    r3 = lely_rtt_sdo_request_create();
    r4 = lely_rtt_sdo_request_create();
    CHECK(name, r1 && r2 && r3 && r4);

    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, r1,
            1u, 0x2001u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, r2,
            1u, 0x2002u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, r3,
            1u, 0x2003u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, r4,
            1u, 0x2004u, 0u, 1000u) == RT_EOK);

    sdo = runtime.sdo_clients[1];
    CHECK(name, sdo != NULL && runtime.sdo_active[1] == r1);
    CHECK(name, runtime.sdo_pending[1] == r2);
    CHECK(name, runtime.sdo_pending[1]->next == r3);
    CHECK(name, sdo->normal_up_calls == 1);
    CHECK(name, lely_rtt_sdo_request_get_result(r4, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_EBUSY);

    fake_complete_upload(sdo, 0, "1", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, runtime.sdo_active[1] == r2);
    CHECK(name, runtime.sdo_pending[1] == r3);
    CHECK(name, sdo->normal_up_calls == 2);

    fake_complete_upload(sdo, 0, "2", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, runtime.sdo_active[1] == r3);
    CHECK(name, runtime.sdo_pending[1] == NULL);
    CHECK(name, sdo->normal_up_calls == 3);

    fake_complete_upload(sdo, 0, "3", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, runtime.sdo_active[1] == NULL);
    CHECK(name, sdo->stop_calls >= 1);

    CHECK(name, lely_rtt_sdo_request_destroy(r1) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(r2) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(r3) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(r4) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS per-node-fifo-order-and-capacity");
    return 0;
}

static int
test_pending_custom_busy_continues_fifo(void)
{
    const char *name = "pending-custom-busy-continues-fifo";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    struct co_csdo custom;
    lely_rtt_sdo_request_t *active;
    lely_rtt_sdo_request_t *pending1;
    lely_rtt_sdo_request_t *pending2;
    struct lely_rtt_sdo_result result;

    reset_runtime(&runtime, &nmt, &can_net);
    init_custom_sdo(&nmt, &custom, 2u, 1u, 0x700u, 0x680u);
    CHECK(name, lely_rtt_runtime_configure_sdo_channel(&runtime, 1u, 2u)
            == RT_EOK);
    command_auto_dispatch = 1;
    active = lely_rtt_sdo_request_create();
    pending1 = lely_rtt_sdo_request_create();
    pending2 = lely_rtt_sdo_request_create();
    CHECK(name, active && pending1 && pending2);

    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, active,
            1u, 0x2301u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, pending1,
            1u, 0x2302u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, pending2,
            1u, 0x2303u, 0u, 1000u) == RT_EOK);
    CHECK(name, runtime.sdo_active[1] == active);
    CHECK(name, runtime.sdo_pending[1] == pending1);

    fake_complete_upload(&custom, 0, "a", 1u);
    /* Model the borrowed NMT-owned CSDO becoming busy before promotion. */
    custom.idle = 0;
    lely_rtt_master_sdo_reap(&runtime);

    CHECK(name, lely_rtt_sdo_request_get_result(pending1, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_EBUSY);
    CHECK(name, lely_rtt_sdo_request_get_result(pending2, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_EBUSY);
    CHECK(name, runtime.sdo_active[1] == NULL);
    CHECK(name, runtime.sdo_pending[1] == NULL);
    CHECK(name, custom.normal_up_calls == 1);

    custom.idle = 1;
    CHECK(name, lely_rtt_sdo_request_destroy(active) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(pending1) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(pending2) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    CHECK(name, custom.stop_calls == 0);
    puts("PASS pending-custom-busy-continues-fifo");
    return 0;
}

static int
test_pending_request_failure_continues_fifo(void)
{
    const char *name = "pending-request-failure-continues-fifo";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *active;
    lely_rtt_sdo_request_t *failing;
    lely_rtt_sdo_request_t *next;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    active = lely_rtt_sdo_request_create();
    failing = lely_rtt_sdo_request_create();
    next = lely_rtt_sdo_request_create();
    CHECK(name, active && failing && next);

    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, active,
            1u, 0x2301u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, failing,
            1u, 0x2302u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, next,
            1u, 0x2303u, 0u, 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    CHECK(name, sdo && runtime.sdo_active[1] == active);
    CHECK(name, runtime.sdo_pending[1] == failing);
    CHECK(name, failing->next == next);

    fake_complete_upload(sdo, 0, "a", 1u);
    /* Fail only the first promotion; its successor must start in this reap. */
    request_fail_once = 1;
    lely_rtt_master_sdo_reap(&runtime);

    CHECK(name, lely_rtt_sdo_request_get_result(failing, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_LOCAL_ERROR);
    CHECK(name, result.local_error == -RT_ERROR);
    CHECK(name, runtime.sdo_active[1] == next);
    CHECK(name, runtime.sdo_pending[1] == NULL);
    CHECK(name, sdo->normal_up_calls == 3);

    fake_complete_upload(sdo, 0, "n", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, lely_rtt_sdo_request_get_result(next, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);

    CHECK(name, lely_rtt_sdo_request_destroy(active) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(failing) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(next) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS pending-request-failure-continues-fifo");
    return 0;
}

static int
test_pending_cancel_and_teardown(void)
{
    const char *name = "pending-cancel-and-teardown";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *active;
    lely_rtt_sdo_request_t *pending1;
    lely_rtt_sdo_request_t *pending2;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    cancel_auto_dispatch = 1;
    active = lely_rtt_sdo_request_create();
    pending1 = lely_rtt_sdo_request_create();
    pending2 = lely_rtt_sdo_request_create();
    CHECK(name, active && pending1 && pending2);

    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, active,
            1u, 0x2001u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, pending1,
            1u, 0x2002u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, pending2,
            1u, 0x2003u, 0u, 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    CHECK(name, sdo->normal_up_calls == 1);

    command_post_error = -RT_EBUSY;
    CHECK(name, lely_rtt_sdo_request_cancel(pending1) == -RT_EBUSY);
    CHECK(name, runtime.sdo_pending[1] == pending1);
    CHECK(name, lely_rtt_sdo_request_get_result(pending1, &result) == -RT_EBUSY);
    CHECK(name, sdo->normal_up_calls == 1);
    command_post_error = RT_EOK;

    CHECK(name, lely_rtt_sdo_request_cancel(pending1) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_get_result(pending1, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, runtime.sdo_pending[1] == pending2);
    CHECK(name, sdo->normal_up_calls == 1);

    lely_rtt_master_sdo_cancel_node(&runtime, 1u);
    CHECK(name, runtime.sdo_active[1] == NULL);
    CHECK(name, runtime.sdo_pending[1] == NULL);
    CHECK(name, lely_rtt_sdo_request_get_result(active, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, lely_rtt_sdo_request_get_result(pending2, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, sdo->stop_calls >= 1);

    CHECK(name, lely_rtt_sdo_request_destroy(active) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(pending1) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(pending2) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS pending-cancel-and-teardown");
    return 0;
}

static int
test_pending_cancel_race_with_promotion(void)
{
    const char *name = "pending-cancel-race-with-promotion";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *active;
    lely_rtt_sdo_request_t *pending;
    lely_rtt_sdo_request_t *next;
    struct lely_rtt_sdo_result result;
    struct lely_rtt_master_command stale_cancel;
    co_csdo_t *sdo;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    active = lely_rtt_sdo_request_create();
    pending = lely_rtt_sdo_request_create();
    next = lely_rtt_sdo_request_create();
    CHECK(name, active && pending && next);

    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, active,
            1u, 0x2201u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, pending,
            1u, 0x2202u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, next,
            1u, 0x2203u, 0u, 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    CHECK(name, sdo && sdo->normal_up_calls == 1);
    CHECK(name, runtime.sdo_pending[1] == pending);

    cancel_race_pending_promotion = 1;
    CHECK(name, lely_rtt_sdo_request_cancel(pending) == RT_EOK);
    CHECK(name, runtime.sdo_active[1] == NULL);
    CHECK(name, runtime.sdo_pending[1] == pending);
    CHECK(name, sdo->normal_up_calls == 1);
    CHECK(name, queued_command_valid == 1);
    stale_cancel = queued_command;

    CHECK(name, lely_rtt_sdo_request_get_result(active, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);

    /* CANCEL_PENDING is removed before a later FIFO request can become active. */
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, lely_rtt_sdo_request_get_result(pending, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_CANCELED);
    CHECK(name, runtime.sdo_pending[1] == next);
    CHECK(name, sdo->normal_up_calls == 1);

    /* The delayed owner cancel identity must not cancel the next request. */
    queued_command_valid = 0;
    lely_rtt_master_sdo_cancel_dispatch(&runtime,
            stale_cancel.data.sdo_cancel.node_id,
            stale_cancel.data.sdo_cancel.request_id);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, runtime.sdo_active[1] == next);
    CHECK(name, sdo->normal_up_calls == 2);

    fake_complete_upload(sdo, 0, "n", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, lely_rtt_sdo_request_get_result(next, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);

    CHECK(name, lely_rtt_sdo_request_destroy(active) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(pending) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(next) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS pending-cancel-race-with-promotion");
    return 0;
}

static int
test_pending_cancel_post_failure_reschedules_promotion(void)
{
    const char *name = "pending-cancel-post-failure-reschedules-promotion";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *active;
    lely_rtt_sdo_request_t *pending;
    lely_rtt_sdo_request_t *next;
    struct lely_rtt_sdo_result result;
    co_csdo_t *sdo;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    owner_event_auto_reap = 1;
    active = lely_rtt_sdo_request_create();
    pending = lely_rtt_sdo_request_create();
    next = lely_rtt_sdo_request_create();
    CHECK(name, active && pending && next);

    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, active,
            1u, 0x2401u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, pending,
            1u, 0x2402u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, next,
            1u, 0x2403u, 0u, 1000u) == RT_EOK);
    sdo = runtime.sdo_clients[1];
    CHECK(name, sdo && runtime.sdo_active[1] == active);
    CHECK(name, runtime.sdo_pending[1] == pending);
    CHECK(name, pending->next == next);

    /* Consume the active-completion owner pass while pending is cancel-pinned. */
    command_post_error = -RT_EBUSY;
    cancel_race_pending_promotion = 1;
    CHECK(name, lely_rtt_sdo_request_cancel(pending) == -RT_EBUSY);
    command_post_error = RT_EOK;

    CHECK(name, lely_rtt_sdo_request_get_result(active, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);
    CHECK(name, owner_event_send_calls == 1);
    CHECK(name, owner_wake_without_lifetime_pin == 0);
    CHECK(name, runtime_lifetime_pins == 0);
    CHECK(name, runtime.sdo_active[1] == pending);
    CHECK(name, runtime.sdo_pending[1] == next);
    CHECK(name, lely_rtt_sdo_request_get_result(pending, &result) == -RT_EBUSY);

    fake_complete_upload(sdo, 0, "p", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, runtime.sdo_active[1] == next);
    CHECK(name, runtime.sdo_pending[1] == NULL);

    fake_complete_upload(sdo, 0, "n", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, lely_rtt_sdo_request_get_result(next, &result) == RT_EOK);
    CHECK(name, result.status == LELY_RTT_SDO_COMPLETION_OK);

    CHECK(name, lely_rtt_sdo_request_destroy(active) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(pending) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(next) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS pending-cancel-post-failure-reschedules-promotion");
    return 0;
}

static int
test_cross_node_requests_remain_parallel(void)
{
    const char *name = "cross-node-requests-remain-parallel";
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct io_can_net can_net;
    lely_rtt_sdo_request_t *node1;
    lely_rtt_sdo_request_t *node2;
    co_csdo_t *sdo1;
    co_csdo_t *sdo2;

    reset_runtime(&runtime, &nmt, &can_net);
    command_auto_dispatch = 1;
    node1 = lely_rtt_sdo_request_create();
    node2 = lely_rtt_sdo_request_create();
    CHECK(name, node1 && node2);

    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, node1,
            1u, 0x2001u, 0u, 1000u) == RT_EOK);
    CHECK(name, lely_rtt_runtime_post_sdo_upload(&runtime, node2,
            2u, 0x2001u, 0u, 1000u) == RT_EOK);
    sdo1 = runtime.sdo_clients[1];
    sdo2 = runtime.sdo_clients[2];
    CHECK(name, sdo1 && sdo2 && sdo1 != sdo2);
    CHECK(name, runtime.sdo_active[1] == node1);
    CHECK(name, runtime.sdo_active[2] == node2);
    CHECK(name, runtime.sdo_pending[1] == NULL && runtime.sdo_pending[2] == NULL);

    fake_complete_upload(sdo1, 0, "a", 1u);
    CHECK(name, runtime.sdo_active[2] == node2);
    fake_complete_upload(sdo2, 0, "b", 1u);
    lely_rtt_master_sdo_reap(&runtime);
    CHECK(name, runtime.sdo_active[1] == NULL && runtime.sdo_active[2] == NULL);

    CHECK(name, lely_rtt_sdo_request_destroy(node1) == RT_EOK);
    CHECK(name, lely_rtt_sdo_request_destroy(node2) == RT_EOK);
    lely_rtt_master_sdo_fini(&runtime);
    puts("PASS cross-node-requests-remain-parallel");
    return 0;
}

int
main(void)
{
    int failed = 0;

    failed += test_block_transfer_success_and_ownership();
    failed += test_block_abort_and_start_failures();
    failed += test_queued_cancel_and_teardown_arbitration();
    failed += test_active_cancel_and_completion_pin();
    failed += test_cancel_pin_blocks_sdo_teardown();
    failed += test_stale_cancel_identity_is_ignored();
    failed += test_custom_channel_validation_and_borrowed_lifecycle();
    failed += test_custom_channel_cobid_conflicts_and_runtime_drift();
    failed += test_per_node_fifo_order_and_capacity();
    failed += test_pending_custom_busy_continues_fifo();
    failed += test_pending_request_failure_continues_fifo();
    failed += test_pending_cancel_and_teardown();
    failed += test_pending_cancel_race_with_promotion();
    failed += test_pending_cancel_post_failure_reschedules_promotion();
    failed += test_cross_node_requests_remain_parallel();
    if (failed)
        return 1;

    puts("Passed 15/15 host SDO cases");
    return 0;
}
