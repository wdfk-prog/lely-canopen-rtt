/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-11     wdfk-prog         first version
 * 2026-09-12     wdfk-prog         clarify pending FIFO link lifetime
 */

/**
 * @file master_sdo_internal.h
 * @brief Private state and cross-translation-unit helpers for Master Client-SDO.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_MASTER_SDO_INTERNAL_H_
#define LELY_RTT_MASTER_SDO_INTERNAL_H_

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_SDO)

#include <lely/co/csdo.h>

enum lely_rtt_sdo_request_state {
    LELY_RTT_SDO_REQUEST_NEW = 0,
    LELY_RTT_SDO_REQUEST_QUEUED,
    LELY_RTT_SDO_REQUEST_PENDING,
    LELY_RTT_SDO_REQUEST_CANCEL_PENDING,
    LELY_RTT_SDO_REQUEST_TEARDOWN_PENDING,
    LELY_RTT_SDO_REQUEST_PENDING_CANCEL_PINNED,
    LELY_RTT_SDO_REQUEST_PENDING_CANCEL_DONE,
    LELY_RTT_SDO_REQUEST_ACTIVE,
    LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_PINNED,
    LELY_RTT_SDO_REQUEST_ACTIVE_CANCEL_DONE,
    LELY_RTT_SDO_REQUEST_DONE,
};

struct lely_rtt_sdo_request {
    struct rt_event completion;
    rt_atomic_t state;
    rt_atomic_t completion_refs;

    lely_rtt_runtime_t *runtime;
    rt_uint32_t request_id;
    enum lely_rtt_sdo_operation operation;
    rt_uint8_t node_id;
    rt_uint16_t index;
    rt_uint8_t subindex;
    rt_uint32_t timeout_ms;
    rt_bool_t block_transfer;
    rt_uint8_t block_pst;

    void *buffer;
    rt_size_t size;

    enum lely_rtt_sdo_completion_status completion_status;
    rt_err_t local_error;
    rt_uint32_t abort_code;
    rt_bool_t cancel_requested;

    /** Owner-only FIFO link valid until the owner unlinks this pending request. */
    struct lely_rtt_sdo_request *next;
};

void lely_rtt_master_sdo_complete(lely_rtt_sdo_request_t *request,
        enum lely_rtt_sdo_completion_status status, rt_err_t local_error,
        rt_uint32_t abort_code);
rt_bool_t lely_rtt_master_sdo_uses_predefined(
        const struct lely_rtt_runtime *runtime, rt_uint8_t node_id);
co_csdo_t *lely_rtt_master_sdo_get_client(struct lely_rtt_runtime *runtime,
        rt_uint8_t node_id, rt_bool_t create_predefined);
rt_err_t lely_rtt_master_sdo_validate_custom_channel(
        struct lely_rtt_runtime *runtime, rt_uint8_t node_id,
        rt_uint8_t sdo_number, co_csdo_t **client);
void lely_rtt_master_sdo_start_active(struct lely_rtt_runtime *runtime,
        lely_rtt_sdo_request_t *request);

#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

#endif /* LELY_RTT_MASTER_SDO_INTERNAL_H_ */
