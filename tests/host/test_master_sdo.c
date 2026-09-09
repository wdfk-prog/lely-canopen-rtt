/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-08     wdfk-prog         first version
 */

/**
 * @file test_master_sdo.c
 * @brief Host-stub regression tests for block SDO and cancellation ordering.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PKG_LELY_USING_MASTER_SDO 1
#define LELY_NO_CO_NMT_BOOT 1
#define LELY_RTT_INTERNAL_H_ 1

#define CO_NUM_NODES 127u
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

typedef void co_csdo_dn_con_t(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, void *data);
typedef void co_csdo_up_con_t(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, const void *ptr,
        size_t n, void *data);

struct rt_event {
    int signaled;
};

struct co_nmt {
    int unused;
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
    rt_atomic_t local_node_id;
    rt_atomic_t sdo_next_request_id;
    co_csdo_t *sdo_clients[CO_NUM_NODES + 1u];
    lely_rtt_sdo_request_t *sdo_active[CO_NUM_NODES + 1u];
    rt_bool_t sdo_suspended[CO_NUM_NODES + 1u];
    rt_bool_t sdo_reset_pending[CO_NUM_NODES + 1u];
    rt_bool_t sdo_stop_pending[CO_NUM_NODES + 1u];
};

static struct lely_rtt_master_command queued_command;
static int queued_command_valid;
static int command_auto_dispatch;
static int cancel_auto_dispatch;
static int cancel_race_remote_completion;
static int event_send_calls;
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
    (void)events;
    event->signaled = 1;
    event_send_calls++;
    return RT_EOK;
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
    (void)delay;
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
co_csdo_up_req(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_csdo_up_con_t *con, void *data)
{
    (void)idx;
    (void)subidx;
    sdo->normal_up_calls++;
    sdo->up_con = con;
    sdo->dn_con = NULL;
    sdo->con_data = data;
    if (sdo->request_fail)
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
    if (sdo->request_fail)
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
    if (sdo->request_fail)
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
    if (sdo->request_fail)
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
        } else if (cancel_auto_dispatch) {
            queued_command_valid = 0;
            lely_rtt_master_sdo_cancel_dispatch(runtime,
                    command->data.sdo_cancel.node_id,
                    command->data.sdo_cancel.request_id);
        }
    }
    return RT_EOK;
}

#include "../../port/rtthread/src/master_sdo.c"

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
    runtime->local_node_id = 127;
    queued_command_valid = 0;
    command_auto_dispatch = 0;
    cancel_auto_dispatch = 0;
    cancel_race_remote_completion = 0;
    event_send_calls = 0;
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

int
main(void)
{
    int failed = 0;

    failed += test_block_transfer_success_and_ownership();
    failed += test_block_abort_and_start_failures();
    failed += test_queued_cancel_and_teardown_arbitration();
    failed += test_active_cancel_and_completion_pin();
    failed += test_stale_cancel_identity_is_ignored();
    if (failed)
        return 1;

    puts("Passed 5/5 host SDO cases");
    return 0;
}
