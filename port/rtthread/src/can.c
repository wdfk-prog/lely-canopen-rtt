/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-03     wdfk-prog         first version
 */

/**
 * @file can.c
 * @brief RT-Thread CAN device bridge for Lely io_user_can.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#include <lely/io2/can/err.h>
#include <lely/util/errnum.h>

#include <string.h>

/**
 * @brief Head of the CAN-device-to-runtime callback registry.
 *
 * RT-Thread RX/status callbacks do not receive a stable Lely runtime handle
 * from the generic device API. The registry maps rt_device_t to a runtime while
 * also providing a callback lifetime pin. Access is protected by the spinlock
 * below because callbacks may execute in ISR/driver context.
 */
static struct lely_rtt_runtime *lely_rtt_can_registry;

/** @brief Spinlock protecting registry membership and acquire-vs-remove lifetime. */
static struct rt_spinlock lely_rtt_can_registry_lock = RT_SPINLOCK_INIT;

/**
 * @brief Publish one runtime to CAN callbacks after lifetime state is reset.
 * @param runtime Runtime instance whose CAN device becomes callback-visible.
 */
static void
lely_rtt_can_registry_add(struct lely_rtt_runtime *runtime)
{
    rt_base_t level;

    level = rt_spin_lock_irqsave(&lely_rtt_can_registry_lock);
    runtime->registry_next = lely_rtt_can_registry;
    lely_rtt_can_registry = runtime;
    rt_spin_unlock_irqrestore(&lely_rtt_can_registry_lock, level);
}

/**
 * @brief Acquire a callback lifetime pin for the runtime associated with @p dev.
 *
 * The increment happens while holding the same registry lock used by removal.
 * Therefore removal has a clean cut: a callback either increments before the
 * runtime is unlinked, or it cannot find the runtime after unlink.
 *
 * @param dev RT-Thread CAN device that triggered the callback.
 * @return Pinned runtime instance, or RT_NULL if no callback may enter.
 */
static struct lely_rtt_runtime *
lely_rtt_can_registry_acquire(rt_device_t dev)
{
    struct lely_rtt_runtime *runtime;
    rt_base_t level;

    level = rt_spin_lock_irqsave(&lely_rtt_can_registry_lock);
    for (runtime = lely_rtt_can_registry; runtime; runtime = runtime->registry_next) {
        if (runtime->can_dev == dev) {
            if (!lely_rtt_callback_acquire(runtime))
                runtime = RT_NULL;
            break;
        }
    }
    rt_spin_unlock_irqrestore(&lely_rtt_can_registry_lock, level);

    return runtime;
}

/**
 * @brief Remove a runtime from the CAN lookup registry.
 *
 * Owner cleanup has already set callback_quiescing before this function runs,
 * so new callbacks either fail the generic lifetime acquire or cannot find the
 * runtime after unlink. The shared reference-count drain is performed later, after
 * both CAN and timer producers have been detached/stopped.
 *
 * @param runtime Runtime instance being removed.
 */
static void
lely_rtt_can_registry_remove(struct lely_rtt_runtime *runtime)
{
    struct lely_rtt_runtime **link;
    rt_base_t level;

    level = rt_spin_lock_irqsave(&lely_rtt_can_registry_lock);
    for (link = &lely_rtt_can_registry; *link; link = &(*link)->registry_next) {
        if (*link == runtime) {
            *link = runtime->registry_next;
            runtime->registry_next = RT_NULL;
            break;
        }
    }
    rt_spin_unlock_irqrestore(&lely_rtt_can_registry_lock, level);
}

#if defined(RT_CAN_USING_CANFD) && defined(PKG_LELY_USING_CANFD)
/**
 * @brief Convert raw CAN FD DLC to payload byte capacity.
 * @param dlc Raw CAN FD DLC value.
 * @return Payload capacity in bytes, or 0 for an invalid DLC.
 */
static rt_size_t
lely_rtt_can_dlc_to_len(rt_uint32_t dlc)
{
    static const rt_uint8_t length_by_dlc[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };

    return dlc < sizeof(length_by_dlc) ? length_by_dlc[dlc] : 0;
}

/**
 * @brief Round a CAN FD payload byte count up to the corresponding raw DLC.
 * @param len Payload length in bytes.
 * @return Raw DLC value 0..15, or 16 when the length exceeds 64 bytes.
 */
static rt_uint32_t
lely_rtt_can_len_to_dlc(rt_size_t len)
{
    if (len <= 8)
        return (rt_uint32_t)len;
    if (len <= 12)
        return 9;
    if (len <= 16)
        return 10;
    if (len <= 20)
        return 11;
    if (len <= 24)
        return 12;
    if (len <= 32)
        return 13;
    if (len <= 48)
        return 14;
    if (len <= 64)
        return 15;
    return 16;
}
#endif /* RT_CAN_USING_CANFD && PKG_LELY_USING_CANFD */

/**
 * @brief RT-Thread CAN RX indication callback.
 *
 * The callback may run outside the Lely owner thread, so it only pins runtime
 * lifetime and sends an event. Frame reads and Lely injection happen later in
 * lely_rtt_can_drain_rx().
 *
 * @param dev RT-Thread CAN device reporting RX data.
 * @param size Driver-provided available-data hint; not used by this bridge.
 * @return RT_EOK after the indication has been handled or ignored safely.
 */
static rt_err_t
lely_rtt_can_rx_indicate(rt_device_t dev, rt_size_t size)
{
    struct lely_rtt_runtime *runtime;

    RT_UNUSED(size);

    runtime = lely_rtt_can_registry_acquire(dev);
    if (runtime) {
        if (runtime->event_initialized)
            rt_event_send(&runtime->event, LELY_RTT_EVENT_RX_READY);
        lely_rtt_callback_release(runtime);
    }

    return RT_EOK;
}

/**
 * @brief RT-Thread CAN status indication callback.
 *
 * The callback only acquires a runtime lifetime pin and wakes the owner; it
 * never translates status or calls Lely directly.
 *
 * @param can RT-Thread CAN device reporting a status transition.
 * @param args Driver callback argument; unused by the generic bridge.
 * @return RT_EOK after the indication has been handled or ignored safely.
 */
static rt_err_t
lely_rtt_can_status_indicate(struct rt_can_device *can, void *args)
{
    struct lely_rtt_runtime *runtime;

    RT_UNUSED(args);

    runtime = lely_rtt_can_registry_acquire(can ? &can->parent : RT_NULL);
    if (runtime) {
        if (runtime->event_initialized)
            rt_event_send(&runtime->event, LELY_RTT_EVENT_CAN_STATUS);
        lely_rtt_callback_release(runtime);
    }

    return RT_EOK;
}

/**
 * @brief Convert one RT-Thread CAN/CAN-FD frame into Lely can_msg format.
 * @param runtime Runtime providing CAN FD capability and length convention.
 * @param src Source RT-Thread CAN frame.
 * @param dst Destination Lely CAN frame.
 * @return 0 on success or -1 when the frame cannot be represented safely.
 */
static int
lely_rtt_can_from_rt(const struct lely_rtt_runtime *runtime,
        const struct rt_can_msg *src, struct can_msg *dst)
{
    rt_size_t len;

    memset(dst, 0, sizeof(*dst));

    if (src->ide == RT_CAN_EXTID) {
        if (src->id > CAN_MASK_EID)
            return -1;
        dst->flags |= CAN_FLAG_IDE;
    } else {
        if (src->id > CAN_MASK_BID)
            return -1;
    }
    dst->id = src->id;

#ifdef RT_CAN_USING_CANFD
    if (src->fd_frame) {
#ifdef PKG_LELY_USING_CANFD
        if (!runtime->config.can_fd || src->rtr)
            return -1;

        if (runtime->config.can_fd_len_mode == LELY_RTT_CANFD_LEN_DLC) {
            len = lely_rtt_can_dlc_to_len(src->len);
            if (src->len > 15 || (!len && src->len))
                return -1;
        } else {
            if (src->len > CANFD_MAX_LEN)
                return -1;
            len = src->len;
        }

        dst->flags |= CAN_FLAG_FDF;
        if (src->brs) {
            if (!runtime->config.can_brs)
                return -1;
            dst->flags |= CAN_FLAG_BRS;
        }
#else
        RT_UNUSED(runtime);
        return -1;
#endif /* PKG_LELY_USING_CANFD */
    } else
#endif /* RT_CAN_USING_CANFD */
    {
        if (src->len > CAN_MAX_LEN)
            return -1;
        len = src->len;
        if (src->rtr == RT_CAN_RTR)
            dst->flags |= CAN_FLAG_RTR;
    }

    dst->len = (uint_least8_t)len;
    if (!(dst->flags & CAN_FLAG_RTR) && len)
        memcpy(dst->data, src->data, len);

    return 0;
}

/**
 * @brief Convert a Lely can_msg to the selected RT-Thread BSP frame convention.
 * @param runtime Runtime providing CAN FD capability and length convention.
 * @param src Source Lely CAN frame.
 * @param dst Destination RT-Thread CAN frame.
 * @return 0 on success or -1 when the frame cannot be represented safely.
 */
static int
lely_rtt_can_to_rt(const struct lely_rtt_runtime *runtime,
        const struct can_msg *src, struct rt_can_msg *dst)
{
    rt_size_t copy_len = src->len;

    memset(dst, 0, sizeof(*dst));

    if (src->flags & CAN_FLAG_IDE) {
        if (src->id > CAN_MASK_EID)
            return -1;
        dst->ide = RT_CAN_EXTID;
    } else {
        if (src->id > CAN_MASK_BID)
            return -1;
        dst->ide = RT_CAN_STDID;
    }

    dst->id = (rt_uint32_t)src->id;
    dst->rtr = (src->flags & CAN_FLAG_RTR) ? RT_CAN_RTR : RT_CAN_DTR;

#ifdef PKG_LELY_USING_CANFD
    /* Generic RT-Thread rt_can_msg has no ESI field, so loss is rejected. */
    if (src->flags & CAN_FLAG_ESI)
        return -1;

    if (src->flags & CAN_FLAG_FDF) {
#if defined(RT_CAN_USING_CANFD)
        rt_uint32_t dlc;

        if (!runtime->config.can_fd || dst->rtr == RT_CAN_RTR
                || src->len > CANFD_MAX_LEN)
            return -1;
        if ((src->flags & CAN_FLAG_BRS) && !runtime->config.can_brs)
            return -1;

        dlc = lely_rtt_can_len_to_dlc(src->len);
        if (dlc > 15)
            return -1;

        dst->fd_frame = 1;
        dst->brs = !!(src->flags & CAN_FLAG_BRS);
        if (runtime->config.can_fd_len_mode == LELY_RTT_CANFD_LEN_DLC) {
            dst->len = dlc;
            copy_len = lely_rtt_can_dlc_to_len(dlc);
        } else {
            dst->len = src->len;
        }
#else
        return -1;
#endif /* RT_CAN_USING_CANFD */
    } else
#endif /* PKG_LELY_USING_CANFD */
    {
        if (src->len > CAN_MAX_LEN)
            return -1;
        dst->len = src->len;
    }

    if (dst->rtr == RT_CAN_DTR && src->len)
        memcpy(dst->data, src->data, src->len);

    /* CAN FD DLC mode may round payload capacity upward; padding is deterministic. */
    if (copy_len > src->len)
        memset(dst->data + src->len, 0, copy_len - src->len);

    /* The owner must never block inside the RT-Thread CAN transmit path. */
    dst->nonblocking = 1;
    return 0;
}

/**
 * @brief Transmit one Lely frame through the RT-Thread non-blocking CAN path.
 * @param msg Lely CAN frame to transmit.
 * @param timeout Lely timeout argument; ignored because B3 requires non-blocking TX.
 * @param arg Runtime instance supplied to io_user_can.
 * @return 0 when RT-Thread accepts the frame, otherwise -1 with errnum set.
 */
static int
lely_rtt_can_write(const struct can_msg *msg, int timeout, void *arg)
{
    struct lely_rtt_runtime *runtime = arg;
    struct rt_can_msg rtmsg;
    rt_ssize_t written;

    RT_UNUSED(timeout);

    if (!runtime || !runtime->can_dev
            || lely_rtt_can_to_rt(runtime, msg, &rtmsg) == -1) {
        set_errnum(ERRNUM_INVAL);
        return -1;
    }

    written = rt_device_write(runtime->can_dev, 0, &rtmsg, sizeof(rtmsg));
    if (written == (rt_ssize_t)sizeof(rtmsg))
        return 0;

    /*
     * io_user_can reposts writes for AGAIN/WOULDBLOCK. A full RT-Thread
     * non-blocking TX ring is therefore NOBUFS, not AGAIN, so the single-owner
     * loop cannot busy-spin by immediately reposting the same frame.
     */
    if (written == 0)
        set_errnum(ERRNUM_NOBUFS);
    else
        set_errnum(ERRNUM_IO);

    return -1;
}

/**
 * @brief Open/configure RT CAN and create the Lely passive user CAN channel.
 * @param runtime Runtime instance owned by the Lely owner thread.
 * @return RT_EOK on success or the first RT-Thread/Lely initialization error.
 */
rt_err_t
lely_rtt_can_init(struct lely_rtt_runtime *runtime)
{
    struct rt_can_device *can;
    struct rt_can_status_ind_type status_ind;
    int flags = IO_CAN_BUS_FLAG_ERR;
    rt_err_t err;

    if (!runtime || !runtime->config.can_name)
        return -RT_EINVAL;

    runtime->can_dev = rt_device_find(runtime->config.can_name);
    if (!runtime->can_dev) {
        LELY_RTT_LOG_E("CAN device not found: %s", runtime->config.can_name);
        return -RT_ENOSYS;
    }

    /*
     * B3 TX relies on rt_can_msg.nonblocking. Fail startup rather than silently
     * falling back to a potentially blocking driver send path.
     */
    can = (struct rt_can_device *)runtime->can_dev;
    if (!can->ops || !can->ops->sendmsg_nonblocking) {
        LELY_RTT_LOG_E("CAN device lacks non-blocking TX: %s",
                runtime->config.can_name);
        return -RT_ENOSYS;
    }

    err = rt_device_open(runtime->can_dev,
            RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_INT_TX);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("CAN open failed: dev=%s err=%d",
                runtime->config.can_name, err);
        return err;
    }
    runtime->can_opened = RT_TRUE;

    err = rt_device_control(runtime->can_dev, RT_CAN_CMD_SET_BAUD,
            (void *)(rt_ubase_t)runtime->config.can_bitrate);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("CAN bitrate setup failed: dev=%s bitrate=%u err=%d",
                runtime->config.can_name,
                (unsigned int)runtime->config.can_bitrate, err);
        return err;
    }

#ifdef PKG_LELY_USING_CANFD
    if (runtime->config.can_fd) {
#ifndef RT_CAN_USING_CANFD
        LELY_RTT_LOG_E("CAN FD requested but RT_CAN_USING_CANFD is disabled");
        return -RT_ENOSYS;
#else
        err = rt_device_control(runtime->can_dev, RT_CAN_CMD_SET_CANFD,
                (void *)(rt_ubase_t)1);
        if (err != RT_EOK) {
            LELY_RTT_LOG_E("CAN FD setup failed: dev=%s err=%d",
                    runtime->config.can_name, err);
            return err;
        }

        flags |= IO_CAN_BUS_FLAG_FDF;
        if (runtime->config.can_brs)
            flags |= IO_CAN_BUS_FLAG_BRS;
#endif /* RT_CAN_USING_CANFD */
    }
#else
    if (runtime->config.can_fd || runtime->config.can_brs) {
        LELY_RTT_LOG_E("CAN FD requested but PKG_LELY_USING_CANFD is disabled");
        return -RT_EINVAL;
    }
#endif /* PKG_LELY_USING_CANFD */

    /*
     * Match the RT-Thread CAN bring-up sequence used by canopennode-rtt:
     * finish bitrate/CAN-FD configuration first, then force normal mode before
     * installing acceptance filters and publishing the RX indication callback.
     */
    err = rt_device_control(runtime->can_dev, RT_CAN_CMD_SET_MODE,
            (void *)(rt_ubase_t)RT_CAN_MODE_NORMAL);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("CAN normal mode setup failed: dev=%s err=%d",
                runtime->config.can_name, err);
        return err;
    }

    /*
     * Hardware acceptance filtering is optional. NULL means the port installs
     * no restrictive filter after normal mode is selected, leaving Lely's
     * software receive dispatch authoritative. A supplied hook
     * is therefore an optimization contract owned by the BSP/application.
     */
    if (runtime->config.filter_setup) {
        /*
         * BSP-specific filter state is opaque here. On failure the hook must
         * have rolled back its own partial changes, or at least leave the
         * device safe for the generic close/retry path.
         */
        err = runtime->config.filter_setup(runtime->can_dev,
                runtime->config.filter_setup_arg);
        if (err != RT_EOK) {
            LELY_RTT_LOG_E("CAN filter setup hook failed: dev=%s err=%d",
                    runtime->config.can_name, err);
            return err;
        }
    }

    lely_rtt_can_registry_add(runtime);
    runtime->registry_registered = RT_TRUE;

    err = rt_device_set_rx_indicate(runtime->can_dev, lely_rtt_can_rx_indicate);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("CAN RX indication setup failed: dev=%s err=%d",
                runtime->config.can_name, err);
        return err;
    }

    if (runtime->config.use_status_indication) {
        status_ind.ind = lely_rtt_can_status_indicate;
        status_ind.args = runtime;
        err = rt_device_control(runtime->can_dev, RT_CAN_CMD_SET_STATUS_IND,
                &status_ind);
        if (err != RT_EOK) {
            LELY_RTT_LOG_E("CAN status indication setup failed: dev=%s err=%d",
                    runtime->config.can_name, err);
            return err;
        }
        runtime->status_registered = RT_TRUE;
    }

    if (runtime->config.start_controller) {
        err = rt_device_control(runtime->can_dev, RT_CAN_CMD_START,
                (void *)(rt_ubase_t)RT_TRUE);
        if (err != RT_EOK) {
            LELY_RTT_LOG_E("CAN controller start failed: dev=%s err=%d",
                    runtime->config.can_name, err);
            return err;
        }
        runtime->controller_started = RT_TRUE;
    }

    runtime->can_chan = io_user_can_chan_create(runtime->ctx,
            ev_loop_get_exec(runtime->loop), flags, 0, -1,
            lely_rtt_can_write, runtime);
    if (!runtime->can_chan) {
        LELY_RTT_LOG_E("io_user_can_chan_create failed: dev=%s",
                runtime->config.can_name);
        return -RT_ERROR;
    }

    LELY_RTT_LOG_I("CAN ready: dev=%s bitrate=%u fd=%u brs=%u",
            runtime->config.can_name,
            (unsigned int)runtime->config.can_bitrate,
            (unsigned int)runtime->config.can_fd,
            (unsigned int)runtime->config.can_brs);
    return RT_EOK;
}

/**
 * @brief Drain one bounded RT CAN RX batch and inject valid frames into Lely.
 * @param runtime Runtime instance; must be called by the owner thread.
 */
void
lely_rtt_can_drain_rx(struct lely_rtt_runtime *runtime)
{
    rt_size_t count = 0;

    while (runtime && runtime->can_dev && count < runtime->config.rx_batch) {
        struct rt_can_msg rtmsg;
        struct can_msg msg;
        rt_ssize_t n;

        memset(&rtmsg, 0, sizeof(rtmsg));
#ifdef RT_CAN_USING_HDR
        /* -1 asks the generic RT CAN read path rather than a specific HDR FIFO. */
        rtmsg.hdr_index = -1;
#endif /* RT_CAN_USING_HDR */

        n = rt_device_read(runtime->can_dev, 0, &rtmsg, sizeof(rtmsg));
        if (n != (rt_ssize_t)sizeof(rtmsg))
            break;
        count++;

        if (lely_rtt_can_from_rt(runtime, &rtmsg, &msg) == -1)
            continue;

        /* timeout=0 is mandatory: producer and consumer share this owner thread. */
        if (io_user_can_chan_on_msg(runtime->can_chan, &msg, RT_NULL, 0) == -1)
            continue;
    }

    /* A bounded batch preserves timer/status fairness under sustained RX load. */
    if (count == runtime->config.rx_batch)
        rt_event_send(&runtime->event, LELY_RTT_EVENT_RX_READY);
}

/**
 * @brief Conservative generic RT CAN status mapper.
 *
 * RT-Thread BSPs do not expose one universally reliable errcode meaning, so
 * this fallback derives controller state from REC/TEC thresholds and reports
 * specific bus errors only when their cumulative counters change.
 *
 * @param status Current RT-Thread CAN status.
 * @param previous Previous accepted status or RT_NULL for the first sample.
 * @param err Output Lely CAN state/error description.
 * @return Non-zero when a state/error event should be injected.
 */
static int
lely_rtt_can_default_status_mapper(const struct rt_can_status *status,
        const struct rt_can_status *previous, struct can_err *err)
{
    rt_bool_t changed = RT_FALSE;

    if (status->snderrcnt >= 256)
        err->state = CAN_STATE_BUSOFF;
    else if (status->snderrcnt >= 128 || status->rcverrcnt >= 128)
        err->state = CAN_STATE_PASSIVE;
    else
        err->state = CAN_STATE_ACTIVE;

    if (!previous)
        return err->state != CAN_STATE_ACTIVE;

    if ((previous->snderrcnt >= 256 ? CAN_STATE_BUSOFF
                    : (previous->snderrcnt >= 128 || previous->rcverrcnt >= 128)
                    ? CAN_STATE_PASSIVE : CAN_STATE_ACTIVE) != err->state)
        changed = RT_TRUE;

    if (status->biterrcnt != previous->biterrcnt)
        err->error |= CAN_ERROR_BIT;
    if (status->bitpaderrcnt != previous->bitpaderrcnt)
        err->error |= CAN_ERROR_STUFF;
    if (status->crcerrcnt != previous->crcerrcnt)
        err->error |= CAN_ERROR_CRC;
    if (status->formaterrcnt != previous->formaterrcnt)
        err->error |= CAN_ERROR_FORM;
    if (status->ackerrcnt != previous->ackerrcnt)
        err->error |= CAN_ERROR_ACK;

    return changed || err->error;
}

/**
 * @brief Query RT CAN status and inject accepted transitions into Lely.
 * @param runtime Runtime instance; must be called by the owner thread.
 */
void
lely_rtt_can_process_status(struct lely_rtt_runtime *runtime)
{
    struct rt_can_status status;
    struct can_err err = CAN_ERR_INIT;
    const struct rt_can_status *previous;
    int changed;

    if (!runtime || !runtime->can_dev || !runtime->can_chan)
        return;

    memset(&status, 0, sizeof(status));
    if (rt_device_control(runtime->can_dev, RT_CAN_CMD_GET_STATUS, &status) != RT_EOK)
        return;

    previous = runtime->status_valid ? &runtime->last_can_status : RT_NULL;
    if (runtime->config.status_mapper) {
        changed = runtime->config.status_mapper(&status, previous, &err,
                runtime->config.status_mapper_arg);
    } else {
        changed = lely_rtt_can_default_status_mapper(&status, previous, &err);
    }

    if (!changed) {
        runtime->last_can_status = status;
        runtime->status_valid = RT_TRUE;
        return;
    }

    /*
     * Keep the old snapshot when Lely's RX/error queue is full. The next status
     * sample will then retry the same transition instead of losing it forever.
     */
    if (io_user_can_chan_on_err(runtime->can_chan, &err, RT_NULL, 0) == 0) {
        runtime->last_can_status = status;
        runtime->status_valid = RT_TRUE;
    }
}

/**
 * @brief Detach CAN callback producers and remove callback registry visibility.
 * @param runtime Runtime instance being shut down.
 */
void
lely_rtt_can_quiesce(struct lely_rtt_runtime *runtime)
{
    struct rt_can_status_ind_type status_ind = { RT_NULL, RT_NULL };
    rt_err_t err;

    if (!runtime || !runtime->can_dev)
        return;

    /*
     * Detach CAN producers before removing the lookup entry. A callback that
     * entered immediately before unregister remains protected by callback_refs;
     * the owner waits for the shared callback refcount only after CAN and timer
     * producers have both been stopped.
     */
    err = rt_device_set_rx_indicate(runtime->can_dev, RT_NULL);
    if (err != RT_EOK) {
        LELY_RTT_LOG_W("CAN RX indication detach failed: dev=%s err=%d",
                runtime->config.can_name, err);
    }
    if (err != RT_EOK && runtime->runtime_error == RT_EOK)
        runtime->runtime_error = err;

    if (runtime->status_registered) {
        err = rt_device_control(runtime->can_dev,
                RT_CAN_CMD_SET_STATUS_IND, &status_ind);
        if (err != RT_EOK) {
            LELY_RTT_LOG_W("CAN status indication detach failed: dev=%s err=%d",
                    runtime->config.can_name, err);
        }
        if (err != RT_EOK && runtime->runtime_error == RT_EOK)
            runtime->runtime_error = err;
        runtime->status_registered = RT_FALSE;
    }

    if (runtime->controller_started) {
        err = rt_device_control(runtime->can_dev, RT_CAN_CMD_START,
                (void *)(rt_ubase_t)RT_FALSE);
        if (err != RT_EOK) {
            LELY_RTT_LOG_W("CAN controller stop failed: dev=%s err=%d",
                    runtime->config.can_name, err);
        }
        if (err != RT_EOK && runtime->runtime_error == RT_EOK)
            runtime->runtime_error = err;
        runtime->controller_started = RT_FALSE;
    }

    if (runtime->registry_registered) {
        lely_rtt_can_registry_remove(runtime);
        runtime->registry_registered = RT_FALSE;
    }
}

/**
 * @brief Destroy the passive Lely CAN channel and close the RT CAN device.
 * @param runtime Runtime instance being shut down.
 */
void
lely_rtt_can_fini(struct lely_rtt_runtime *runtime)
{
    if (!runtime)
        return;

    /* Quiesce is idempotent and makes object destruction callback-safe. */
    lely_rtt_can_quiesce(runtime);

    if (runtime->can_chan) {
        io_user_can_chan_destroy(runtime->can_chan);
        runtime->can_chan = RT_NULL;
    }

    if (runtime->can_dev && runtime->can_opened) {
        rt_device_close(runtime->can_dev);
        runtime->can_opened = RT_FALSE;
    }

    runtime->can_dev = RT_NULL;
    runtime->status_valid = RT_FALSE;
}
