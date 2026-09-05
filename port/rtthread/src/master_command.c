/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first version
 */

/**
 * @file master_command.c
 * @brief Cross-thread CANopen Master command ingress for the RT-Thread runtime.
 *
 * The message queue is only a transport across the single-owner boundary.
 * Every Lely API call remains in the owner thread.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_USING_MASTER_COMMAND)

/** @brief Try to pin the command queue while command admission is open. */
static rt_bool_t
lely_rtt_master_command_acquire(struct lely_rtt_runtime *runtime)
{
    if (!runtime || rt_atomic_load(&runtime->command_stop_latched)
            || rt_atomic_load(&runtime->command_quiescing))
        return RT_FALSE;

    rt_atomic_add(&runtime->command_refs, 1);
    if (rt_atomic_load(&runtime->command_stop_latched)
            || rt_atomic_load(&runtime->command_quiescing)) {
        rt_atomic_sub(&runtime->command_refs, 1);
        return RT_FALSE;
    }

    return RT_TRUE;
}

/** @brief Release one cross-thread command-queue lifetime pin. */
static void
lely_rtt_master_command_release(struct lely_rtt_runtime *runtime)
{
    if (runtime)
        rt_atomic_sub(&runtime->command_refs, 1);
}

void
lely_rtt_master_command_prepare(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    rt_atomic_store(&runtime->command_refs, 0);
    rt_atomic_store(&runtime->command_stop_latched, 0);
    rt_atomic_store(&runtime->command_quiescing, 1);
}

void
lely_rtt_master_command_admission_open(struct lely_rtt_runtime *runtime)
{
    if (!runtime || !runtime->command_mq || !runtime->master_nmt
            || rt_atomic_load(&runtime->command_stop_latched))
        return;

    /*
     * master_nmt is owner-only and is checked here, before publishing the
     * cross-thread admission boundary. A runtime without a configured local
     * Master therefore stays fail-closed instead of accepting commands that
     * the dispatcher cannot execute.
     *
     * The stop latch is monotonic for one run. Recheck it after opening: a
     * concurrent start-timeout/STOP may race this owner transition, but posters
     * also test the latch before and after taking their queue lifetime pin, so
     * no command can be admitted after cancellation becomes visible.
     */
    rt_atomic_store(&runtime->command_quiescing, 0);
    if (rt_atomic_load(&runtime->command_stop_latched))
        rt_atomic_store(&runtime->command_quiescing, 1);
}

void
lely_rtt_master_command_quiesce_begin(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    /*
     * Latch first so an owner finishing initialization cannot reopen admission
     * after start() has timed out or stop() has begun. prepare() is the only
     * place that clears this latch, and it runs only for a later runtime start.
     */
    rt_atomic_store(&runtime->command_stop_latched, 1);
    rt_atomic_store(&runtime->command_quiescing, 1);
}

void
lely_rtt_master_command_wait_idle(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    while (rt_atomic_load(&runtime->command_refs) != 0)
        rt_thread_mdelay(1);
}

rt_err_t
lely_rtt_master_command_init(struct lely_rtt_runtime *runtime)
{
    if (!runtime || runtime->command_mq)
        return -RT_EINVAL;

    runtime->command_mq = rt_mq_create("lelycmd",
            sizeof(struct lely_rtt_master_command),
            PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    if (!runtime->command_mq) {
        LELY_RTT_LOG_E("Master command queue creation failed");
        return -RT_ENOMEM;
    }

    return RT_EOK;
}

rt_err_t
lely_rtt_master_command_post(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command)
{
    rt_err_t err;

    if (!runtime || !command)
        return -RT_EINVAL;
    if (!lely_rtt_master_command_acquire(runtime))
        return -RT_EBUSY;

    if (!runtime->command_mq) {
        err = -RT_EBUSY;
        goto out;
    }

    err = rt_mq_send(runtime->command_mq, command, sizeof(*command));
    if (err != RT_EOK)
        goto out;

    err = rt_event_send(&runtime->event, LELY_RTT_EVENT_COMMAND);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("Master command event send failed: %d; "
                "owner safety poll will drain the queue", err);
        /*
         * mq_send() already transferred ownership to the runtime. Returning an
         * enqueue error here would let an SDO caller reclaim storage still
         * referenced by the queue. The owner has a bounded safety poll while
         * command ingress is enabled, so the queued command remains accepted
         * and will still reach dispatch/cleanup without relying on this wake.
         */
        err = RT_EOK;
    }

out:
    lely_rtt_master_command_release(runtime);
    return err;
}

/** @brief Translate and execute one NMT command in the owner thread. */
static void
lely_rtt_master_command_dispatch_nmt(struct lely_rtt_runtime *runtime,
        const struct lely_rtt_master_command *command)
{
    co_unsigned8_t cs;
    const rt_uint8_t node_id = command->data.nmt.node_id;

    if (!runtime || !runtime->master_nmt)
        return;

    switch ((enum lely_rtt_nmt_command)command->data.nmt.command) {
    case LELY_RTT_NMT_COMMAND_START:
        cs = CO_NMT_CS_START;
        break;
    case LELY_RTT_NMT_COMMAND_STOP:
        cs = CO_NMT_CS_STOP;
#if defined(PKG_LELY_USING_MASTER_SDO)
        lely_rtt_master_sdo_cancel_node(runtime, node_id);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
        break;
    case LELY_RTT_NMT_COMMAND_PREOP:
        cs = CO_NMT_CS_ENTER_PREOP;
        break;
    case LELY_RTT_NMT_COMMAND_RESET_NODE:
        cs = CO_NMT_CS_RESET_NODE;
#if defined(PKG_LELY_USING_MASTER_SDO)
        lely_rtt_master_sdo_cancel_node(runtime, node_id);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
        break;
    case LELY_RTT_NMT_COMMAND_RESET_COMM:
        cs = CO_NMT_CS_RESET_COMM;
#if defined(PKG_LELY_USING_MASTER_SDO)
        lely_rtt_master_sdo_cancel_node(runtime, node_id);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
        break;
    default:
        LELY_RTT_LOG_W("invalid queued NMT command: %u",
                (unsigned int)command->data.nmt.command);
        return;
    }

    if (co_nmt_cs_req(runtime->master_nmt, cs, node_id) == -1) {
        LELY_RTT_LOG_W("NMT command dispatch failed: cs=0x%02x node=%u",
                (unsigned int)cs, (unsigned int)node_id);
        return;
    }

#if defined(PKG_LELY_USING_MASTER_SDO)
    lely_rtt_master_sdo_on_nmt_command(runtime, node_id,
            (enum lely_rtt_nmt_command)command->data.nmt.command);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
}

void
lely_rtt_master_command_dispatch(struct lely_rtt_runtime *runtime)
{
    struct lely_rtt_master_command command;
    rt_size_t count = 0;

    if (!runtime || !runtime->command_mq)
        return;

    while (count < PKG_LELY_MASTER_COMMAND_QUEUE_DEPTH
            && rt_mq_recv(runtime->command_mq, &command, sizeof(command),
                    RT_WAITING_NO) == RT_EOK) {
        count++;

        switch ((enum lely_rtt_master_command_type)command.type) {
        case LELY_RTT_MASTER_COMMAND_NMT:
            lely_rtt_master_command_dispatch_nmt(runtime, &command);
            break;
#if defined(PKG_LELY_USING_MASTER_SDO)
        case LELY_RTT_MASTER_COMMAND_SDO:
            lely_rtt_master_sdo_dispatch(runtime, command.data.sdo.request);
            break;
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
        default:
            LELY_RTT_LOG_W("invalid Master command type: %u",
                    (unsigned int)command.type);
            break;
        }
    }
}

void
lely_rtt_master_command_fini(struct lely_rtt_runtime *runtime)
{
    struct lely_rtt_master_command command;

    if (!runtime)
        return;

    lely_rtt_master_command_quiesce_begin(runtime);
    lely_rtt_master_command_wait_idle(runtime);

    if (runtime->command_mq) {
        while (rt_mq_recv(runtime->command_mq, &command, sizeof(command),
                RT_WAITING_NO) == RT_EOK) {
#if defined(PKG_LELY_USING_MASTER_SDO)
            if (command.type == LELY_RTT_MASTER_COMMAND_SDO)
                lely_rtt_master_sdo_cancel_queued(command.data.sdo.request);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */
        }
    }

#if defined(PKG_LELY_USING_MASTER_SDO)
    lely_rtt_master_sdo_fini(runtime);
#endif /* defined(PKG_LELY_USING_MASTER_SDO) */

    if (runtime->command_mq) {
        rt_mq_delete(runtime->command_mq);
        runtime->command_mq = RT_NULL;
    }
}

rt_err_t
lely_rtt_runtime_post_nmt(lely_rtt_runtime_t *runtime,
        enum lely_rtt_nmt_command command, rt_uint8_t node_id)
{
    struct lely_rtt_master_command message;

    if (!runtime || node_id > CO_NUM_NODES)
        return -RT_EINVAL;
    if (node_id && (rt_uint8_t)rt_atomic_load(&runtime->local_node_id) == node_id)
        return -RT_EINVAL;
    if (command < LELY_RTT_NMT_COMMAND_START
            || command > LELY_RTT_NMT_COMMAND_RESET_COMM)
        return -RT_EINVAL;

    rt_memset(&message, 0, sizeof(message));
    message.type = LELY_RTT_MASTER_COMMAND_NMT;
    message.data.nmt.node_id = node_id;
    message.data.nmt.command = (rt_uint8_t)command;
    return lely_rtt_master_command_post(runtime, &message);
}

#endif /* defined(PKG_LELY_USING_MASTER_COMMAND) */
