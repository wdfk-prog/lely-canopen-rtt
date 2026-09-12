/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-12     wdfk-prog         first version
 */

/**
 * @file test_master_od_hooks.c
 * @brief Host-stub regression tests for application OD hooks and notification.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LELY_RTT_INTERNAL_H_
#define PKG_LELY_USING_LOCAL_OD 1
#define PKG_LELY_USING_MASTER_OD_HOOKS 1
#define PKG_LELY_USING_MASTER_COMMAND 1

#define RT_NULL NULL
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_EOK 0
#define RT_ERROR 1
#define RT_EINVAL 22
#define RT_EBUSY 16
#define RT_ENOMEM 12
#define RT_ENOSYS 38

#define CO_ACCESS_READ 0x01u
#define CO_ACCESS_WRITE 0x02u
#define CO_ACCESS_RW (CO_ACCESS_READ | CO_ACCESS_WRITE)
#define CO_OBJECT_ARRAY 0x08u
#define CO_SDO_AC_ERROR 0x08000000u
#define CO_SDO_AC_NO_OBJ 0x06020000u
#define CO_SDO_AC_NO_SUB 0x06090011u
#define CO_SDO_AC_NO_DATA 0x08000024u
#define CO_SDO_AC_NO_READ 0x06010001u
#define CO_SDO_AC_NO_WRITE 0x06010002u
#define CO_SDO_AC_PARAM_VAL 0x06090030u

#define LELY_RTT_LOG_E(...) lely_test_log(__VA_ARGS__)
#define rt_calloc calloc
#define rt_malloc malloc
#define rt_free free
#define rt_memcpy memcpy
#define rt_memset memset

typedef int rt_err_t;
typedef int rt_bool_t;
typedef int rt_atomic_t;
typedef size_t rt_size_t;
typedef void *rt_thread_t;
typedef uint8_t rt_uint8_t;
typedef uint16_t rt_uint16_t;
typedef uint32_t rt_uint32_t;

typedef uint8_t co_unsigned8_t;
typedef uint16_t co_unsigned16_t;
typedef uint32_t co_unsigned32_t;

typedef struct fake_dev co_dev_t;
typedef struct fake_obj co_obj_t;
typedef struct fake_sub co_sub_t;

struct co_sdo_req {
    size_t size;
    const void *buf;
    size_t nbyte;
    size_t offset;
};

typedef co_unsigned32_t co_sub_dn_ind_t(
        co_sub_t *sub, struct co_sdo_req *req, void *data);
typedef co_unsigned32_t co_sub_up_ind_t(
        const co_sub_t *sub, struct co_sdo_req *req, void *data);

int co_sdo_req_up(struct co_sdo_req *req, const void *ptr, size_t size,
        co_unsigned32_t *ac);

struct fake_sub {
    struct fake_sub *next;
    co_obj_t *obj;
    co_unsigned8_t subidx;
    co_unsigned8_t access;
    co_sub_dn_ind_t *dn_ind;
    void *dn_data;
    co_sub_up_ind_t *up_ind;
    void *up_data;
    uint8_t value[16];
    size_t value_size;
    co_unsigned32_t dn_abort;
};

struct fake_obj {
    struct fake_obj *next;
    co_dev_t *dev;
    co_sub_t *first_sub;
    co_unsigned16_t idx;
    co_unsigned8_t code;
    co_unsigned8_t array_count;
};

struct fake_dev {
    co_obj_t *first_obj;
};

struct lely_rtt_runtime;
typedef struct lely_rtt_runtime lely_rtt_runtime_t;

/** @brief Origin of the most recent successful manufacturer OD write. */
enum lely_rtt_local_od_change_source {
    LELY_RTT_LOCAL_OD_CHANGE_LOCAL_API = 0,
    LELY_RTT_LOCAL_OD_CHANGE_PROTOCOL,
};

struct lely_rtt_local_od_change {
    rt_uint16_t index;
    rt_uint8_t subindex;
    enum lely_rtt_local_od_change_source source;
    rt_uint32_t size;
    rt_uint32_t sequence;
};

typedef rt_uint32_t lely_rtt_local_od_upload_ind_t(lely_rtt_runtime_t *runtime,
        rt_uint16_t index, rt_uint8_t subindex, const void **data,
        rt_size_t *size, void *user);
typedef void lely_rtt_local_od_change_ind_t(lely_rtt_runtime_t *runtime,
        const struct lely_rtt_local_od_change *change, void *user);

struct lely_rtt_master_sync {
    rt_err_t result;
};

struct lely_rtt_local_od_request;
struct lely_rtt_local_od_hook;
struct lely_rtt_local_od_app_hook;

struct lely_rtt_runtime {
    rt_bool_t event_initialized;
    rt_thread_t owner_thread;
    co_dev_t *master_dev;
    struct lely_rtt_local_od_hook *local_od_hooks;
    struct lely_rtt_local_od_app_hook *local_od_app_hooks;
    lely_rtt_local_od_change_ind_t *local_od_change_ind;
    void *local_od_change_data;
    rt_bool_t local_od_api_write_active;
    rt_atomic_t local_od_change_seq;
    rt_atomic_t local_od_change_index;
    rt_atomic_t local_od_change_subindex;
    rt_atomic_t local_od_change_source;
    rt_atomic_t local_od_change_size;
};

enum lely_rtt_master_command_type {
    LELY_RTT_MASTER_COMMAND_LOCAL_OD = 0,
};

struct lely_rtt_master_command {
    enum lely_rtt_master_command_type type;
    union {
        struct {
            struct lely_rtt_local_od_request *request;
        } od;
    } data;
};

static void lely_test_log(const char *format, ...)
{
    (void)format;
}

static int rt_atomic_load(const rt_atomic_t *value)
{
    return *value;
}

static void rt_atomic_store(rt_atomic_t *target, rt_atomic_t value)
{
    *target = value;
}

static rt_thread_t rt_thread_self(void)
{
    return (rt_thread_t)(uintptr_t)0x22u;
}

static void rt_thread_mdelay(int milliseconds)
{
    (void)milliseconds;
}

static co_unsigned32_t
fake_default_dn(co_sub_t *sub, struct co_sdo_req *req, void *data)
{
    (void)data;
    if (!sub || !req)
        return CO_SDO_AC_ERROR;
    if (sub->dn_abort)
        return sub->dn_abort;
    if (!req->nbyte || req->offset + req->nbyte != req->size
            || req->size > sizeof(sub->value))
        return CO_SDO_AC_ERROR;
    memcpy(sub->value, req->buf, req->size);
    sub->value_size = req->size;
    return 0;
}

static co_unsigned32_t
fake_default_up(const co_sub_t *sub, struct co_sdo_req *req, void *data)
{
    co_unsigned32_t ac = 0;

    (void)data;
    if (!sub || !req)
        return CO_SDO_AC_ERROR;
    if (co_sdo_req_up(req, sub->value, sub->value_size, &ac) == -1)
        return ac ? ac : CO_SDO_AC_ERROR;
    return 0;
}

static void
fake_od_init(co_dev_t *dev, co_obj_t *obj, co_sub_t *sub,
        co_unsigned16_t index, co_unsigned8_t subindex,
        co_unsigned8_t access)
{
    memset(dev, 0, sizeof(*dev));
    memset(obj, 0, sizeof(*obj));
    memset(sub, 0, sizeof(*sub));
    dev->first_obj = obj;
    obj->dev = dev;
    obj->first_sub = sub;
    obj->idx = index;
    sub->obj = obj;
    sub->subidx = subindex;
    sub->access = access;
    sub->dn_ind = &fake_default_dn;
    sub->up_ind = &fake_default_up;
}

co_obj_t *co_dev_first_obj(co_dev_t *dev)
{
    return dev ? dev->first_obj : RT_NULL;
}

co_obj_t *co_obj_next(co_obj_t *obj)
{
    return obj ? obj->next : RT_NULL;
}

co_sub_t *co_obj_first_sub(co_obj_t *obj)
{
    return obj ? obj->first_sub : RT_NULL;
}

co_sub_t *co_sub_next(co_sub_t *sub)
{
    return sub ? sub->next : RT_NULL;
}

co_obj_t *co_sub_get_obj(co_sub_t *sub)
{
    return sub ? sub->obj : RT_NULL;
}

co_unsigned16_t co_obj_get_idx(const co_obj_t *obj)
{
    return obj ? obj->idx : 0;
}

co_unsigned8_t co_sub_get_subidx(const co_sub_t *sub)
{
    return sub ? sub->subidx : 0;
}

co_unsigned8_t co_sub_get_access(const co_sub_t *sub)
{
    return sub ? sub->access : 0;
}

co_unsigned8_t co_obj_get_code(const co_obj_t *obj)
{
    return obj ? obj->code : 0;
}

co_unsigned8_t co_obj_get_val_u8(const co_obj_t *obj, co_unsigned8_t subidx)
{
    (void)subidx;
    return obj ? obj->array_count : 0;
}

co_obj_t *co_dev_find_obj(co_dev_t *dev, co_unsigned16_t index)
{
    co_obj_t *obj;

    for (obj = co_dev_first_obj(dev); obj; obj = co_obj_next(obj)) {
        if (obj->idx == index)
            return obj;
    }
    return RT_NULL;
}

co_sub_t *co_obj_find_sub(co_obj_t *obj, co_unsigned8_t subindex)
{
    co_sub_t *sub;

    for (sub = co_obj_first_sub(obj); sub; sub = co_sub_next(sub)) {
        if (sub->subidx == subindex)
            return sub;
    }
    return RT_NULL;
}

co_sub_t *co_dev_find_sub(co_dev_t *dev, co_unsigned16_t index,
        co_unsigned8_t subindex)
{
    return co_obj_find_sub(co_dev_find_obj(dev, index), subindex);
}

void co_sub_get_dn_ind(const co_sub_t *sub, co_sub_dn_ind_t **ind, void **data)
{
    if (ind)
        *ind = sub ? sub->dn_ind : RT_NULL;
    if (data)
        *data = sub ? sub->dn_data : RT_NULL;
}

void co_sub_set_dn_ind(co_sub_t *sub, co_sub_dn_ind_t *ind, void *data)
{
    if (!sub)
        return;
    sub->dn_ind = ind ? ind : &fake_default_dn;
    sub->dn_data = ind ? data : RT_NULL;
}

void co_sub_get_up_ind(const co_sub_t *sub, co_sub_up_ind_t **ind, void **data)
{
    if (ind)
        *ind = sub ? sub->up_ind : RT_NULL;
    if (data)
        *data = sub ? sub->up_data : RT_NULL;
}

void co_sub_set_up_ind(co_sub_t *sub, co_sub_up_ind_t *ind, void *data)
{
    if (!sub)
        return;
    sub->up_ind = ind ? ind : &fake_default_up;
    sub->up_data = ind ? data : RT_NULL;
}

co_unsigned32_t co_sub_dn_ind(co_sub_t *sub, struct co_sdo_req *req)
{
    if (!sub)
        return CO_SDO_AC_NO_SUB;
    if (!(sub->access & CO_ACCESS_WRITE))
        return CO_SDO_AC_NO_WRITE;
    return sub->dn_ind ? sub->dn_ind(sub, req, sub->dn_data) : CO_SDO_AC_ERROR;
}

co_unsigned32_t co_sub_up_ind(const co_sub_t *sub, struct co_sdo_req *req)
{
    if (!sub)
        return CO_SDO_AC_NO_SUB;
    if (!(sub->access & CO_ACCESS_READ))
        return CO_SDO_AC_NO_READ;
    return sub->up_ind ? sub->up_ind(sub, req, sub->up_data) : CO_SDO_AC_ERROR;
}

void co_sdo_req_init(struct co_sdo_req *req)
{
    memset(req, 0, sizeof(*req));
}

void co_sdo_req_fini(struct co_sdo_req *req)
{
    (void)req;
}

int co_sdo_req_first(const struct co_sdo_req *req)
{
    return req && req->offset == 0;
}

int co_sdo_req_last(const struct co_sdo_req *req)
{
    return req && req->offset + req->nbyte >= req->size;
}

int co_sdo_req_up(struct co_sdo_req *req, const void *ptr, size_t size,
        co_unsigned32_t *ac)
{
    if (!req || (size && !ptr)) {
        if (ac)
            *ac = CO_SDO_AC_ERROR;
        return -1;
    }
    req->size = size;
    req->buf = ptr;
    req->nbyte = size;
    req->offset = 0;
    return 0;
}

rt_err_t lely_rtt_master_sync_init(struct lely_rtt_master_sync *sync,
        const char *name)
{
    (void)name;
    sync->result = RT_EOK;
    return RT_EOK;
}

void lely_rtt_master_sync_complete(struct lely_rtt_master_sync *sync,
        rt_err_t result)
{
    sync->result = result;
}

rt_err_t lely_rtt_master_sync_wait(struct lely_rtt_master_sync *sync)
{
    return sync->result;
}

void lely_rtt_master_sync_fini(struct lely_rtt_master_sync *sync)
{
    (void)sync;
}

rt_err_t lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command);

#include "../../../port/rtthread/src/master_od.c"

rt_err_t
lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command)
{
    if (!runtime || !command
            || command->type != LELY_RTT_MASTER_COMMAND_LOCAL_OD)
        return -RT_EINVAL;
    lely_rtt_local_od_dispatch(runtime, command->data.od.request);
    return RT_EOK;
}

struct test_app_state {
    uint8_t upload[8];
    size_t upload_size;
    rt_uint32_t upload_abort;
    unsigned upload_calls;
    unsigned change_calls;
    struct lely_rtt_local_od_change last_change;
};

static rt_uint32_t
test_upload(lely_rtt_runtime_t *runtime, rt_uint16_t index,
        rt_uint8_t subindex, const void **data, rt_size_t *size, void *user)
{
    struct test_app_state *state = user;

    (void)runtime;
    if (index != 0x2000u || subindex != 0x01u || !state || !data || !size)
        return CO_SDO_AC_ERROR;
    state->upload_calls++;
    if (state->upload_abort)
        return state->upload_abort;
    *data = state->upload;
    *size = state->upload_size;
    return 0;
}

static void
test_change(lely_rtt_runtime_t *runtime,
        const struct lely_rtt_local_od_change *change, void *user)
{
    struct test_app_state *state = user;

    (void)runtime;
    if (!state || !change)
        return;
    state->change_calls++;
    state->last_change = *change;
}

static int expect(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    struct lely_rtt_runtime runtime;
    struct test_app_state app;
    co_dev_t dev;
    co_obj_t obj;
    co_sub_t sub;
    co_sub_dn_ind_t *original_dn;
    co_sub_up_ind_t *original_up;
    struct co_sdo_req req;
    struct lely_rtt_local_od_change snapshot;
    uint8_t local_value[] = {0x11u, 0x22u, 0x33u, 0x44u};
    uint8_t protocol_value[] = {0xa1u, 0xb2u};
    void *copy = RT_NULL;
    rt_size_t copy_size = 0;
    unsigned failures = 0;

    memset(&runtime, 0, sizeof(runtime));
    memset(&app, 0, sizeof(app));
    fake_od_init(&dev, &obj, &sub, 0x2000u, 0x01u, CO_ACCESS_RW);
    original_dn = sub.dn_ind;
    original_up = sub.up_ind;
    runtime.event_initialized = RT_TRUE;
    runtime.master_dev = &dev;
    app.upload[0] = 0x78u;
    app.upload[1] = 0x56u;
    app.upload[2] = 0x34u;
    app.upload[3] = 0x12u;
    app.upload_size = 4;

    failures += expect(lely_rtt_runtime_configure_local_od_upload_ind(
            &runtime, 0x1fffu, 0x01u, &test_upload, &app) == -RT_EINVAL,
            "out-of-range upload registration must fail");
    failures += expect(lely_rtt_runtime_configure_local_od_upload_ind(
            &runtime, 0x2000u, 0x01u, &test_upload, &app) == RT_EOK,
            "valid upload registration must succeed");
    failures += expect(lely_rtt_runtime_configure_local_od_change_ind(
            &runtime, &test_change, &app) == RT_EOK,
            "write notification registration must succeed");

    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x11u;
    failures += expect(lely_rtt_runtime_configure_local_od_change_ind(
            &runtime, RT_NULL, RT_NULL) == -RT_EINVAL,
            "registration must be rejected while owner is active");
    failures += expect(lely_rtt_local_od_bind(&runtime) == RT_EOK,
            "binding registered application OD hooks must succeed");
    failures += expect(sub.dn_ind != original_dn,
            "download indication must be wrapped for notification");
    failures += expect(sub.up_ind != original_up,
            "registered upload indication must be replaced while bound");

    failures += expect(lely_rtt_runtime_local_od_read(&runtime, 0x2000u,
            0x01u, &copy, &copy_size) == RT_EOK,
            "owner-safe read must use the application upload hook");
    failures += expect(copy_size == app.upload_size
            && copy && !memcmp(copy, app.upload, app.upload_size),
            "dynamic upload bytes must reach the caller unchanged");
    failures += expect(app.upload_calls == 1u,
            "application upload hook must run exactly once per read");
    lely_rtt_local_od_free(copy);

    failures += expect(lely_rtt_runtime_local_od_write(&runtime, 0x2000u,
            0x01u, local_value, sizeof(local_value)) == RT_EOK,
            "owner-safe write must retain the existing download path");
    failures += expect(sub.value_size == sizeof(local_value)
            && !memcmp(sub.value, local_value, sizeof(local_value)),
            "existing download indication must commit the local write");
    failures += expect(app.change_calls == 1u
            && app.last_change.source == LELY_RTT_LOCAL_OD_CHANGE_LOCAL_API
            && app.last_change.sequence == 1u
            && app.last_change.size == sizeof(local_value),
            "local API write must notify after publication");
    failures += expect(lely_rtt_runtime_get_local_od_change(&runtime,
            &snapshot) == RT_EOK && snapshot.sequence == 1u
            && snapshot.index == 0x2000u && snapshot.subindex == 0x01u,
            "polling snapshot must remain available with callback enabled");

    co_sdo_req_init(&req);
    req.size = sizeof(protocol_value);
    req.buf = protocol_value;
    req.nbyte = sizeof(protocol_value);
    failures += expect(co_sub_dn_ind(&sub, &req) == 0,
            "protocol-originated write must keep the existing download path");
    failures += expect(app.change_calls == 2u
            && app.last_change.source == LELY_RTT_LOCAL_OD_CHANGE_PROTOCOL
            && app.last_change.sequence == 2u,
            "protocol write must publish and notify with protocol source");

    sub.dn_abort = CO_SDO_AC_PARAM_VAL;
    failures += expect(co_sub_dn_ind(&sub, &req) == CO_SDO_AC_PARAM_VAL,
            "existing application/protocol rejection must be preserved");
    failures += expect(app.change_calls == 2u,
            "rejected writes must not publish a notification");
    sub.dn_abort = 0;

    app.upload_abort = CO_SDO_AC_PARAM_VAL;
    co_sdo_req_init(&req);
    failures += expect(co_sub_up_ind(&sub, &req) == CO_SDO_AC_PARAM_VAL,
            "application upload abort code must propagate to Lely");
    app.upload_abort = 0;

    lely_rtt_local_od_unbind(&runtime);
    failures += expect(sub.dn_ind == original_dn && sub.up_ind == original_up,
            "stop/unbind must restore both pre-existing indications");
    failures += expect(runtime.local_od_app_hooks != RT_NULL,
            "application upload registration must persist across stop/start");
    failures += expect(lely_rtt_local_od_bind(&runtime) == RT_EOK,
            "persistent registration must bind again after restart");
    lely_rtt_local_od_unbind(&runtime);

    runtime.owner_thread = RT_NULL;
    failures += expect(lely_rtt_runtime_configure_local_od_upload_ind(
            &runtime, 0x2000u, 0x01u, RT_NULL, RT_NULL) == RT_EOK,
            "stopped runtime must allow upload hook removal");
    failures += expect(runtime.local_od_app_hooks == RT_NULL,
            "upload hook removal must release its persistent registration");
    failures += expect(lely_rtt_runtime_configure_local_od_change_ind(
            &runtime, RT_NULL, RT_NULL) == RT_EOK,
            "stopped runtime must allow notification removal");

    failures += expect(lely_rtt_runtime_configure_local_od_upload_ind(
            &runtime, 0x2001u, 0x00u, &test_upload, &app) == RT_EOK,
            "missing target is validated at owner bind, not registration time");
    runtime.owner_thread = (rt_thread_t)(uintptr_t)0x11u;
    failures += expect(lely_rtt_local_od_bind(&runtime) == -RT_ERROR,
            "runtime start must fail closed for a missing registered OD entry");
    failures += expect(runtime.local_od_hooks == RT_NULL,
            "failed validation must not leave owner hook wrappers installed");

    runtime.owner_thread = RT_NULL;
    lely_rtt_local_od_app_hooks_fini(&runtime);
    failures += expect(runtime.local_od_app_hooks == RT_NULL
            && runtime.local_od_change_ind == RT_NULL,
            "runtime destruction cleanup must release hook registrations");

    if (failures)
        return 1;
    puts("MASTER_OD_HOOKS_PASS");
    return 0;
}
