/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-06     wdfk-prog         first version
 */

/**
 * @file test_master_cfg.c
 * @brief Host-stub regression tests for the RT-Thread manual NMT CFG bridge.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PKG_LELY_USING_MASTER_NMT_CFG 1
#define LELY_NO_CO_NMT_BOOT 1
#define LELY_RTT_INTERNAL_H_ 1

#define CO_NUM_NODES 127
#define CO_NMT_ST_RESET_NODE 0
#define CO_NMT_ST_RESET_COMM 1
#define CO_NMT_ST_STOP 4
#define CO_NMT_ST_PREOP 127
#define CO_SDO_AC_ERROR 0x08000000u

#define RT_EOK 0
#define RT_ERROR 1
#define RT_EINVAL 22
#define RT_EBUSY 16
#define RT_ENOMEM 12
#define RT_ENOSYS 38
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_NULL NULL

#define LELY_RTT_LOG_E(...) ((void)0)
#define LELY_RTT_LOG_W(...) ((void)0)

#define rt_malloc malloc
#define rt_free free
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
typedef struct co_csdo co_csdo_t;
typedef struct co_dev co_dev_t;
typedef struct co_sub co_sub_t;

typedef void co_nmt_cfg_ind_t(co_nmt_t *nmt, co_unsigned8_t id,
        co_csdo_t *sdo, void *data);
typedef void co_nmt_cfg_con_t(co_nmt_t *nmt, co_unsigned8_t id,
        co_unsigned32_t ac, void *data);
typedef void co_csdo_dn_con_t(co_csdo_t *sdo, co_unsigned16_t idx,
        co_unsigned8_t subidx, co_unsigned32_t ac, void *data);

struct co_sub {
    rt_bool_t present;
    rt_size_t size;
    rt_uint8_t value_u8;
};

struct co_dev {
    rt_uint32_t assignment[CO_NUM_NODES + 1];
    struct co_sub dcf_1f22[CO_NUM_NODES + 1];
    struct co_sub restore_1f8a[CO_NUM_NODES + 1];
};

struct co_nmt {
    int state;
    co_nmt_cfg_ind_t *cfg_ind;
    void *cfg_ind_data;
    co_nmt_cfg_con_t *cfg_con;
    void *cfg_con_data;
    rt_uint8_t cfg_node_id;
    int cfg_res_calls;
    rt_uint32_t last_cfg_res_ac;
};

struct co_csdo {
    int unused;
};

struct lely_rtt_master_sync {
    rt_bool_t initialized;
    rt_bool_t completed;
    rt_err_t result;
};

struct lely_rtt_master_cfg_source;
struct lely_rtt_master_cfg_request;

enum lely_rtt_nmt_command {
    LELY_RTT_NMT_COMMAND_START = 0,
    LELY_RTT_NMT_COMMAND_STOP,
    LELY_RTT_NMT_COMMAND_PREOP,
    LELY_RTT_NMT_COMMAND_RESET_NODE,
    LELY_RTT_NMT_COMMAND_RESET_COMM,
};

enum lely_rtt_nmt_cfg_completion_status {
    LELY_RTT_NMT_CFG_COMPLETION_OK = 0,
    LELY_RTT_NMT_CFG_COMPLETION_ABORT,
    LELY_RTT_NMT_CFG_COMPLETION_CANCELED,
    LELY_RTT_NMT_CFG_COMPLETION_LOCAL_ERROR,
};

struct lely_rtt_nmt_cfg_result {
    rt_uint8_t node_id;
    enum lely_rtt_nmt_cfg_completion_status status;
    rt_err_t local_error;
    rt_uint32_t abort_code;
};

enum lely_rtt_nmt_cfg_stage {
    LELY_RTT_NMT_CFG_STAGE_QUEUED = 0,
    LELY_RTT_NMT_CFG_STAGE_OWNER_PRECHECK,
    LELY_RTT_NMT_CFG_STAGE_LELY_SEQUENCE,
    LELY_RTT_NMT_CFG_STAGE_APPLICATION_DCF,
    LELY_RTT_NMT_CFG_STAGE_COMPLETE,
};

#define LELY_RTT_NMT_CFG_SOURCE_OBJECT_1F22 (1u << 0)
#define LELY_RTT_NMT_CFG_SOURCE_APPLICATION_DCF (1u << 1)
#define LELY_RTT_NMT_CFG_SOURCE_EXTERNAL_CFG_IND (1u << 2)

struct lely_rtt_nmt_cfg_diagnostic {
    enum lely_rtt_nmt_cfg_stage stage;
    rt_uint8_t source_flags;
    rt_bool_t restore_requested;
    rt_uint32_t application_dcf_entries;
    rt_uint16_t last_index;
    rt_uint8_t last_subindex;
};

struct lely_rtt_runtime {
    rt_thread_t owner_thread;
    void *can_net;
    co_dev_t *master_dev;
    co_nmt_t *master_nmt;
    rt_atomic_t local_node_id;
    struct lely_rtt_master_cfg_source *cfg_sources[CO_NUM_NODES + 1];
    struct lely_rtt_master_cfg_request *cfg_active[CO_NUM_NODES + 1];
    rt_bool_t cfg_resume_pending[CO_NUM_NODES + 1];
    rt_uint32_t cfg_resume_abort_code[CO_NUM_NODES + 1];
    rt_bool_t cfg_tearing_down;
    rt_bool_t event_initialized;
};

typedef struct lely_rtt_runtime lely_rtt_runtime_t;

enum lely_rtt_master_command_type {
    LELY_RTT_MASTER_COMMAND_NMT_CFG = 0,
};

struct lely_rtt_master_command {
    rt_uint8_t type;
    union {
        struct {
            struct lely_rtt_master_cfg_request *request;
        } cfg;
    } data;
};

static rt_thread_t rt_thread_self(void);
static int rt_atomic_load(const rt_atomic_t *value);

void co_nmt_get_cfg_ind(co_nmt_t *nmt, co_nmt_cfg_ind_t **cfg_ind,
        void **cfg_data);
void co_nmt_set_cfg_ind(co_nmt_t *nmt, co_nmt_cfg_ind_t *cfg_ind,
        void *cfg_data);
int co_nmt_get_st(const co_nmt_t *nmt);
int co_nmt_cfg_res(co_nmt_t *nmt, co_unsigned8_t id, co_unsigned32_t ac);
int co_nmt_cfg_req(co_nmt_t *nmt, co_unsigned8_t id, int timeout,
        co_nmt_cfg_con_t *con, void *data);
int co_csdo_dn_dcf_req(co_csdo_t *sdo, const rt_uint8_t *begin,
        const rt_uint8_t *end, co_csdo_dn_con_t *con, void *data);
co_sub_t *co_dev_find_sub(co_dev_t *dev, co_unsigned16_t idx,
        co_unsigned8_t subidx);
rt_size_t co_sub_sizeof_val(const co_sub_t *sub);
co_unsigned32_t co_dev_get_val_u32(const co_dev_t *dev,
        co_unsigned16_t idx, co_unsigned8_t subidx);
co_unsigned8_t co_sub_get_val_u8(const co_sub_t *sub);

rt_err_t lely_rtt_master_sync_init(struct lely_rtt_master_sync *sync,
        const char *name);
void lely_rtt_master_sync_complete(struct lely_rtt_master_sync *sync,
        rt_err_t result);
rt_err_t lely_rtt_master_sync_wait(struct lely_rtt_master_sync *sync);
void lely_rtt_master_sync_fini(struct lely_rtt_master_sync *sync);
rt_err_t lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command);

#include "../../port/rtthread/src/master_cfg.c"

static const rt_uint8_t valid_dcf[] = {
    0x01, 0x00, 0x00, 0x00,
    0x17, 0x10, 0x00, 0x02, 0x00, 0x00, 0x00,
    0xe8, 0x03,
};

struct cfg_fixture {
    struct lely_rtt_runtime runtime;
    struct co_nmt nmt;
    struct co_dev dev;
    struct co_csdo sdo;
};

static rt_thread_t current_thread = (rt_thread_t)(uintptr_t)0x2222u;
static int dcf_req_calls;
static int dcf_req_result;
static rt_size_t dcf_req_size;
static rt_uint8_t dcf_req_bytes[64];
static co_csdo_dn_con_t *dcf_con;
static void *dcf_con_data;
static struct cfg_fixture *wait_fixture;
static rt_uint32_t wait_abort_code;
static rt_bool_t wait_driver_failed;

static rt_thread_t
rt_thread_self(void)
{
    return current_thread;
}

static int
rt_atomic_load(const rt_atomic_t *value)
{
    return *value;
}

void
co_nmt_get_cfg_ind(co_nmt_t *nmt, co_nmt_cfg_ind_t **cfg_ind, void **cfg_data)
{
    if (cfg_ind)
        *cfg_ind = nmt->cfg_ind;
    if (cfg_data)
        *cfg_data = nmt->cfg_ind_data;
}

void
co_nmt_set_cfg_ind(co_nmt_t *nmt, co_nmt_cfg_ind_t *cfg_ind, void *cfg_data)
{
    nmt->cfg_ind = cfg_ind;
    nmt->cfg_ind_data = cfg_data;
}

int
co_nmt_get_st(const co_nmt_t *nmt)
{
    return nmt->state;
}

int
co_nmt_cfg_res(co_nmt_t *nmt, co_unsigned8_t id, co_unsigned32_t ac)
{
    nmt->cfg_res_calls++;
    nmt->last_cfg_res_ac = ac;
    if (nmt->cfg_con && nmt->cfg_node_id == id) {
        co_nmt_cfg_con_t *con = nmt->cfg_con;
        void *data = nmt->cfg_con_data;

        /* Mirror Lely's synchronous cfg_res() -> cfg_con completion ordering. */
        nmt->cfg_con = RT_NULL;
        nmt->cfg_con_data = RT_NULL;
        con(nmt, id, ac, data);
    }
    return 0;
}

int
co_nmt_cfg_req(co_nmt_t *nmt, co_unsigned8_t id, int timeout,
        co_nmt_cfg_con_t *con, void *data)
{
    (void)timeout;
    nmt->cfg_node_id = id;
    nmt->cfg_con = con;
    nmt->cfg_con_data = data;
    return 0;
}

int
co_csdo_dn_dcf_req(co_csdo_t *sdo, const rt_uint8_t *begin,
        const rt_uint8_t *end, co_csdo_dn_con_t *con, void *data)
{
    rt_size_t size = (rt_size_t)(end - begin);

    (void)sdo;
    dcf_req_calls++;
    dcf_req_size = size;
    if (size <= sizeof(dcf_req_bytes))
        memcpy(dcf_req_bytes, begin, size);
    dcf_con = con;
    dcf_con_data = data;
    return dcf_req_result;
}

co_sub_t *
co_dev_find_sub(co_dev_t *dev, co_unsigned16_t idx, co_unsigned8_t subidx)
{
    struct co_sub *sub = RT_NULL;

    if (!dev || !subidx || subidx > CO_NUM_NODES)
        return RT_NULL;
    if (idx == 0x1f22)
        sub = &dev->dcf_1f22[subidx];
    else if (idx == 0x1f8a)
        sub = &dev->restore_1f8a[subidx];
    return sub && sub->present ? sub : RT_NULL;
}

rt_size_t
co_sub_sizeof_val(const co_sub_t *sub)
{
    return sub ? sub->size : 0;
}

co_unsigned32_t
co_dev_get_val_u32(const co_dev_t *dev, co_unsigned16_t idx,
        co_unsigned8_t subidx)
{
    if (!dev || idx != 0x1f81 || !subidx || subidx > CO_NUM_NODES)
        return 0;
    return dev->assignment[subidx];
}

co_unsigned8_t
co_sub_get_val_u8(const co_sub_t *sub)
{
    return sub ? sub->value_u8 : 0;
}

rt_err_t
lely_rtt_master_sync_init(struct lely_rtt_master_sync *sync, const char *name)
{
    (void)name;
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
    sync->completed = RT_TRUE;
    sync->result = result;
}

static void
drive_manual_dcf(void)
{
    struct cfg_fixture *fixture = wait_fixture;

    if (!fixture || !fixture->nmt.cfg_ind) {
        wait_driver_failed = RT_TRUE;
        return;
    }

    fixture->nmt.cfg_ind(&fixture->nmt, 1, &fixture->sdo,
            fixture->nmt.cfg_ind_data);
    if (!dcf_con) {
        wait_driver_failed = RT_TRUE;
        return;
    }

    /* The owner reaper runs only after the originating Lely callback unwinds. */
    dcf_con(&fixture->sdo, 0x1017, 0, wait_abort_code, dcf_con_data);
    lely_rtt_master_cfg_reap(&fixture->runtime);
}

rt_err_t
lely_rtt_master_sync_wait(struct lely_rtt_master_sync *sync)
{
    if (!sync->completed && wait_fixture)
        drive_manual_dcf();
    return sync->completed ? sync->result : -RT_ERROR;
}

void
lely_rtt_master_sync_fini(struct lely_rtt_master_sync *sync)
{
    sync->initialized = RT_FALSE;
}

rt_err_t
lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command)
{
    if (!runtime || !command || command->type != LELY_RTT_MASTER_COMMAND_NMT_CFG)
        return -RT_EINVAL;
    lely_rtt_master_cfg_dispatch(runtime, command->data.cfg.request);
    return RT_EOK;
}

static void
reset_stubs(void)
{
    dcf_req_calls = 0;
    dcf_req_result = 0;
    dcf_req_size = 0;
    memset(dcf_req_bytes, 0, sizeof(dcf_req_bytes));
    dcf_con = RT_NULL;
    dcf_con_data = RT_NULL;
    wait_fixture = RT_NULL;
    wait_abort_code = 0;
    wait_driver_failed = RT_FALSE;
    current_thread = (rt_thread_t)(uintptr_t)0x2222u;
}

static void
fixture_init(struct cfg_fixture *fixture)
{
    reset_stubs();
    memset(fixture, 0, sizeof(*fixture));
    fixture->runtime.event_initialized = RT_TRUE;
    fixture->runtime.can_net = (void *)(uintptr_t)0x1u;
    fixture->runtime.master_dev = &fixture->dev;
    fixture->runtime.master_nmt = &fixture->nmt;
    fixture->runtime.local_node_id = 127;
    fixture->nmt.state = CO_NMT_ST_PREOP;
    fixture->dev.assignment[1] = 0x01u;
}

static int
start_fixture(struct cfg_fixture *fixture)
{
    rt_err_t err;

    err = lely_rtt_runtime_configure_nmt_dcf(&fixture->runtime, 1,
            valid_dcf, sizeof(valid_dcf));
    if (err != RT_EOK)
        return 1;
    err = lely_rtt_master_cfg_bind(&fixture->runtime);
    if (err != RT_EOK)
        return 1;
    fixture->runtime.owner_thread = (rt_thread_t)(uintptr_t)0x1111u;
    return 0;
}

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int
test_registration_and_framing(void)
{
    struct cfg_fixture fixture;
    rt_uint8_t mutable_dcf[sizeof(valid_dcf)];
    static const rt_uint8_t zero_entries[] = {0x00, 0x00, 0x00, 0x00};
    static const rt_uint8_t truncated_header[] = {
        0x01, 0x00, 0x00, 0x00, 0x17, 0x10,
    };
    static const rt_uint8_t truncated_value[] = {
        0x01, 0x00, 0x00, 0x00,
        0x17, 0x10, 0x00, 0x02, 0x00, 0x00, 0x00,
        0xe8,
    };
    rt_uint8_t trailing[sizeof(valid_dcf) + 1];
    struct lely_rtt_master_cfg_source *source;

    fixture_init(&fixture);
    memcpy(mutable_dcf, valid_dcf, sizeof(valid_dcf));
    TEST_CHECK(lely_rtt_runtime_configure_nmt_dcf(&fixture.runtime, 1,
            mutable_dcf, sizeof(mutable_dcf)) == RT_EOK);
    source = fixture.runtime.cfg_sources[1];
    TEST_CHECK(source != RT_NULL);
    TEST_CHECK(source->entries == 1);
    TEST_CHECK(source->size == sizeof(valid_dcf));
    TEST_CHECK(memcmp(source->data, valid_dcf, sizeof(valid_dcf)) == 0);

    mutable_dcf[sizeof(mutable_dcf) - 1] ^= 0xffu;
    TEST_CHECK(memcmp(source->data, valid_dcf, sizeof(valid_dcf)) == 0);
    TEST_CHECK(lely_rtt_runtime_configure_nmt_dcf(&fixture.runtime, 1,
            valid_dcf, sizeof(valid_dcf)) == -RT_EBUSY);
    TEST_CHECK(fixture.runtime.cfg_sources[1] == source);
    TEST_CHECK(source->entries == 1);
    TEST_CHECK(source->size == sizeof(valid_dcf));
    TEST_CHECK(memcmp(source->data, valid_dcf, sizeof(valid_dcf)) == 0);

    TEST_CHECK(lely_rtt_runtime_configure_nmt_dcf(&fixture.runtime, 2,
            zero_entries, sizeof(zero_entries)) == -RT_EINVAL);
    TEST_CHECK(fixture.runtime.cfg_sources[2] == RT_NULL);
    TEST_CHECK(lely_rtt_runtime_configure_nmt_dcf(&fixture.runtime, 3,
            truncated_header, sizeof(truncated_header)) == -RT_EINVAL);
    TEST_CHECK(fixture.runtime.cfg_sources[3] == RT_NULL);
    TEST_CHECK(lely_rtt_runtime_configure_nmt_dcf(&fixture.runtime, 4,
            truncated_value, sizeof(truncated_value)) == -RT_EINVAL);
    TEST_CHECK(fixture.runtime.cfg_sources[4] == RT_NULL);

    memcpy(trailing, valid_dcf, sizeof(valid_dcf));
    trailing[sizeof(valid_dcf)] = 0xa5u;
    TEST_CHECK(lely_rtt_runtime_configure_nmt_dcf(&fixture.runtime, 5,
            trailing, sizeof(trailing)) == -RT_EINVAL);
    TEST_CHECK(fixture.runtime.cfg_sources[5] == RT_NULL);

    fixture.runtime.owner_thread = (rt_thread_t)(uintptr_t)0x1111u;
    TEST_CHECK(lely_rtt_runtime_configure_nmt_dcf(&fixture.runtime, 6,
            valid_dcf, sizeof(valid_dcf)) == -RT_EINVAL);
    TEST_CHECK(fixture.runtime.cfg_sources[6] == RT_NULL);

    lely_rtt_master_cfg_sources_fini(&fixture.runtime);
    TEST_CHECK(fixture.runtime.cfg_sources[1] == RT_NULL);
    return 0;
}

static int
test_manual_only_auto_cfg_does_not_consume_application_dcf(void)
{
    struct cfg_fixture fixture;

    fixture_init(&fixture);
    TEST_CHECK(start_fixture(&fixture) == 0);
    TEST_CHECK(fixture.nmt.cfg_ind != RT_NULL);

    fixture.nmt.cfg_ind(&fixture.nmt, 1, &fixture.sdo,
            fixture.nmt.cfg_ind_data);
    TEST_CHECK(dcf_req_calls == 0);
    TEST_CHECK(fixture.nmt.cfg_res_calls == 1);
    TEST_CHECK(fixture.nmt.last_cfg_res_ac == 0);
    TEST_CHECK(fixture.runtime.cfg_sources[1] != RT_NULL);
    TEST_CHECK(fixture.runtime.cfg_active[1] == RT_NULL);

    lely_rtt_master_cfg_sources_fini(&fixture.runtime);
    return 0;
}

static int
test_manual_cfg_success_diagnostics(void)
{
    struct cfg_fixture fixture;
    struct lely_rtt_nmt_cfg_result result;
    struct lely_rtt_nmt_cfg_diagnostic diagnostic;
    rt_err_t err;

    fixture_init(&fixture);
    TEST_CHECK(start_fixture(&fixture) == 0);
    wait_fixture = &fixture;

    err = lely_rtt_runtime_nmt_configure_ex(&fixture.runtime, 1, 1000,
            &result, &diagnostic);
    TEST_CHECK(err == RT_EOK);
    TEST_CHECK(!wait_driver_failed);
    TEST_CHECK(result.status == LELY_RTT_NMT_CFG_COMPLETION_OK);
    TEST_CHECK(result.abort_code == 0);
    TEST_CHECK(diagnostic.stage == LELY_RTT_NMT_CFG_STAGE_COMPLETE);
    TEST_CHECK(diagnostic.source_flags == LELY_RTT_NMT_CFG_SOURCE_APPLICATION_DCF);
    TEST_CHECK(diagnostic.application_dcf_entries == 1);
    TEST_CHECK(diagnostic.last_index == 0x1017);
    TEST_CHECK(diagnostic.last_subindex == 0);
    TEST_CHECK(dcf_req_calls == 1);
    TEST_CHECK(dcf_req_size == sizeof(valid_dcf));
    TEST_CHECK(memcmp(dcf_req_bytes, valid_dcf, sizeof(valid_dcf)) == 0);
    TEST_CHECK(fixture.runtime.cfg_active[1] == RT_NULL);

    lely_rtt_master_cfg_sources_fini(&fixture.runtime);
    return 0;
}

static int
test_manual_cfg_abort_diagnostics(void)
{
    struct cfg_fixture fixture;
    struct lely_rtt_nmt_cfg_result result;
    struct lely_rtt_nmt_cfg_diagnostic diagnostic;
    const rt_uint32_t abort_code = 0x06090030u;
    rt_err_t err;

    fixture_init(&fixture);
    TEST_CHECK(start_fixture(&fixture) == 0);
    wait_fixture = &fixture;
    wait_abort_code = abort_code;

    err = lely_rtt_runtime_nmt_configure_ex(&fixture.runtime, 1, 1000,
            &result, &diagnostic);
    TEST_CHECK(err == RT_EOK);
    TEST_CHECK(!wait_driver_failed);
    TEST_CHECK(result.status == LELY_RTT_NMT_CFG_COMPLETION_ABORT);
    TEST_CHECK(result.abort_code == abort_code);
    TEST_CHECK(diagnostic.stage == LELY_RTT_NMT_CFG_STAGE_APPLICATION_DCF);
    TEST_CHECK(diagnostic.last_index == 0x1017);
    TEST_CHECK(diagnostic.last_subindex == 0);
    TEST_CHECK(fixture.nmt.last_cfg_res_ac == abort_code);
    TEST_CHECK(fixture.runtime.cfg_active[1] == RT_NULL);

    lely_rtt_master_cfg_sources_fini(&fixture.runtime);
    return 0;
}

static int
test_local_reset_retires_staged_request(void)
{
    struct cfg_fixture fixture;
    struct lely_rtt_master_cfg_request request;

    fixture_init(&fixture);
    TEST_CHECK(start_fixture(&fixture) == 0);
    memset(&request, 0, sizeof(request));
    request.runtime = &fixture.runtime;
    request.result.node_id = 1;
    request.timeout_ms = 1000;
    TEST_CHECK(lely_rtt_master_sync_init(&request.sync, "cfgreset") == RT_EOK);

    lely_rtt_master_cfg_dispatch(&fixture.runtime, &request);
    TEST_CHECK(fixture.runtime.cfg_active[1] == &request);
    fixture.nmt.cfg_ind(&fixture.nmt, 1, &fixture.sdo,
            fixture.nmt.cfg_ind_data);
    TEST_CHECK(dcf_con != RT_NULL);
    dcf_con(&fixture.sdo, 0x1017, 0, 0, dcf_con_data);
    TEST_CHECK(fixture.runtime.cfg_resume_pending[1] == RT_TRUE);

    /* Reset state is a barrier only after upstream has destroyed slave CFG state. */
    lely_rtt_master_cfg_on_local_nmt_state(&fixture.runtime, 0);
    TEST_CHECK(request.result.status == LELY_RTT_NMT_CFG_COMPLETION_CANCELED);
    TEST_CHECK(request.sync.completed == RT_TRUE);
    TEST_CHECK(fixture.runtime.cfg_active[1] == RT_NULL);
    TEST_CHECK(fixture.runtime.cfg_resume_pending[1] == RT_FALSE);
    TEST_CHECK(fixture.nmt.cfg_res_calls == 0);

    lely_rtt_master_cfg_reap(&fixture.runtime);
    TEST_CHECK(fixture.nmt.cfg_res_calls == 0);
    lely_rtt_master_cfg_sources_fini(&fixture.runtime);
    return 0;
}

static int
test_nmt_destroy_drops_deferred_resume_then_cancels(void)
{
    struct cfg_fixture fixture;
    struct lely_rtt_master_cfg_request request;

    fixture_init(&fixture);
    TEST_CHECK(start_fixture(&fixture) == 0);
    memset(&request, 0, sizeof(request));
    request.runtime = &fixture.runtime;
    request.result.node_id = 1;
    request.timeout_ms = 1000;
    TEST_CHECK(lely_rtt_master_sync_init(&request.sync, "cfgdestroy") == RT_EOK);

    lely_rtt_master_cfg_dispatch(&fixture.runtime, &request);
    fixture.nmt.cfg_ind(&fixture.nmt, 1, &fixture.sdo,
            fixture.nmt.cfg_ind_data);
    TEST_CHECK(dcf_con != RT_NULL);
    dcf_con(&fixture.sdo, 0x1017, 0, 0, dcf_con_data);
    TEST_CHECK(fixture.runtime.cfg_resume_pending[1] == RT_TRUE);

    /* Teardown must discard the deferred resume before releasing the stack request. */
    lely_rtt_master_cfg_prepare_nmt_destroy(&fixture.runtime);
    TEST_CHECK(fixture.runtime.cfg_tearing_down == RT_TRUE);
    TEST_CHECK(request.cancel_requested == RT_TRUE);
    lely_rtt_master_cfg_reap(&fixture.runtime);
    TEST_CHECK(fixture.runtime.cfg_resume_pending[1] == RT_FALSE);
    TEST_CHECK(fixture.nmt.cfg_res_calls == 0);
    TEST_CHECK(fixture.runtime.cfg_active[1] == &request);

    lely_rtt_master_cfg_after_nmt_destroy(&fixture.runtime);
    TEST_CHECK(request.result.status == LELY_RTT_NMT_CFG_COMPLETION_CANCELED);
    TEST_CHECK(request.sync.completed == RT_TRUE);
    TEST_CHECK(fixture.runtime.cfg_active[1] == RT_NULL);
    TEST_CHECK(fixture.runtime.cfg_tearing_down == RT_FALSE);

    lely_rtt_master_cfg_sources_fini(&fixture.runtime);
    return 0;
}

static int
test_stopped_state_is_not_a_lifetime_barrier(void)
{
    struct cfg_fixture fixture;
    struct lely_rtt_master_cfg_request request;

    fixture_init(&fixture);
    TEST_CHECK(start_fixture(&fixture) == 0);
    memset(&request, 0, sizeof(request));
    request.runtime = &fixture.runtime;
    request.result.node_id = 1;
    request.timeout_ms = 1000;
    TEST_CHECK(lely_rtt_master_sync_init(&request.sync, "cfgstop") == RT_EOK);

    lely_rtt_master_cfg_dispatch(&fixture.runtime, &request);
    TEST_CHECK(fixture.runtime.cfg_active[1] == &request);
    lely_rtt_master_cfg_on_local_nmt_state(&fixture.runtime, CO_NMT_ST_STOP);
    TEST_CHECK(fixture.runtime.cfg_active[1] == &request);
    TEST_CHECK(request.sync.completed == RT_FALSE);

    lely_rtt_master_cfg_prepare_nmt_destroy(&fixture.runtime);
    lely_rtt_master_cfg_after_nmt_destroy(&fixture.runtime);
    lely_rtt_master_cfg_sources_fini(&fixture.runtime);
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
        {"registration-and-framing", &test_registration_and_framing},
        {"manual-only-auto-cfg", &test_manual_only_auto_cfg_does_not_consume_application_dcf},
        {"manual-success-diagnostic", &test_manual_cfg_success_diagnostics},
        {"manual-abort-diagnostic", &test_manual_cfg_abort_diagnostics},
        {"local-reset-barrier", &test_local_reset_retires_staged_request},
        {"nmt-destroy-barrier", &test_nmt_destroy_drops_deferred_resume_then_cancels},
        {"stopped-not-barrier", &test_stopped_state_is_not_a_lifetime_barrier},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].run() != 0)
            return 1;
        printf("PASS %s\n", cases[i].name);
    }
    printf("Passed %zu/%zu host CFG cases\n", i, i);
    return 0;
}
