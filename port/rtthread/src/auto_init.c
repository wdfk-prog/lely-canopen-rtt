/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-04     wdfk-prog         first version
 * 2026-09-04     wdfk-prog         bind optional static Node1 before runtime start
 * 2026-09-05     wdfk-prog         bind generated local Master instead of Node1
 * 2026-09-06     wdfk-prog         register Node1 manual CFG application DCF
 */

/**
 * @file auto_init.c
 * @brief Optional RT-Thread application-stage startup for the default Lely runtime.
 *
 * @author wdfk-prog
 */

#include "internal.h"

#if defined(PKG_LELY_EXAMPLE_MASTER_NODE1)
#include "master_sdev.h"
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
#include "master_cfg_dcf.h"
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#endif /* defined(PKG_LELY_EXAMPLE_MASTER_NODE1) */

#if defined(PKG_LELY_APP_AUTO_INIT)

/** @brief Default runtime created by the RT-Thread application init hook. */
static lely_rtt_runtime_t *lely_rtt_auto_runtime;

/** @brief Default runtime published only after a successful start. */
static lely_rtt_runtime_t *lely_rtt_auto_ready_runtime;

lely_rtt_runtime_t *
lely_rtt_runtime_get_default(void)
{
    return lely_rtt_auto_ready_runtime;
}

/**
 * @brief Fill the default runtime configuration from Kconfig.
 *
 * The base configuration remains the B3 transport contract. When the built-in
 * Master + remote Node1 example is selected, lely_rtt_auto_init() binds the
 * generated local Master co_sdev before start; all co_* construction still
 * occurs later in the owner thread.
 *
 * @param config Output runtime configuration.
 */
static void
lely_rtt_auto_config_init(struct lely_rtt_runtime_config *config)
{
    rt_memset(config, 0, sizeof(*config));

    config->can_name = PKG_LELY_CAN_DEV_NAME;
    config->can_bitrate = PKG_LELY_AUTO_INIT_BITRATE;
    config->rx_batch = PKG_LELY_AUTO_INIT_RX_BATCH;
    config->thread_stack_size = PKG_LELY_AUTO_INIT_THREAD_STACK_SIZE;
    config->thread_priority = PKG_LELY_AUTO_INIT_THREAD_PRIORITY;
    config->thread_timeslice = PKG_LELY_AUTO_INIT_THREAD_TIMESLICE;
    config->start_timeout_ms = PKG_LELY_AUTO_INIT_START_TIMEOUT_MS;
    config->stop_timeout_ms = PKG_LELY_AUTO_INIT_STOP_TIMEOUT_MS;

#if defined(PKG_LELY_AUTO_INIT_START_CONTROLLER)
    config->start_controller = RT_TRUE;
#endif /* defined(PKG_LELY_AUTO_INIT_START_CONTROLLER) */

#if defined(PKG_LELY_AUTO_INIT_STATUS_INDICATION)
    config->use_status_indication = RT_TRUE;
#endif /* defined(PKG_LELY_AUTO_INIT_STATUS_INDICATION) */

#if defined(PKG_LELY_AUTO_INIT_CANFD)
    config->can_fd = RT_TRUE;
#if defined(PKG_LELY_AUTO_INIT_BRS)
    config->can_brs = RT_TRUE;
#endif /* defined(PKG_LELY_AUTO_INIT_BRS) */
#if defined(PKG_LELY_AUTO_INIT_CANFD_LEN_DLC)
    config->can_fd_len_mode = LELY_RTT_CANFD_LEN_DLC;
#else
    config->can_fd_len_mode = LELY_RTT_CANFD_LEN_BYTES;
#endif /* defined(PKG_LELY_AUTO_INIT_CANFD_LEN_DLC) */
#else
    config->can_fd_len_mode = LELY_RTT_CANFD_LEN_BYTES;
#endif /* defined(PKG_LELY_AUTO_INIT_CANFD) */
}

/**
 * @brief Create and start the default runtime during RT-Thread application init.
 *
 * The hook mirrors the CANopenNode-RTT default-instance pattern: configuration
 * comes from Kconfig and INIT_APP_EXPORT() starts one default instance after
 * RT-Thread device/component initialization. If startup fails after the owner
 * thread has already been created, the runtime object is retained so an owner
 * that is still unwinding can never access freed storage.
 *
 * @return RT_EOK on success or the runtime creation/start error.
 */
static int
lely_rtt_auto_init(void)
{
    struct lely_rtt_runtime_config config;
    rt_err_t err;

    if (lely_rtt_auto_runtime)
        return RT_EOK;

    lely_rtt_auto_ready_runtime = RT_NULL;
    lely_rtt_auto_config_init(&config);
    lely_rtt_auto_runtime = lely_rtt_runtime_create(&config);
    if (!lely_rtt_auto_runtime) {
        LELY_RTT_LOG_E("auto init create failed");
        return -RT_ERROR;
    }

#if defined(PKG_LELY_EXAMPLE_MASTER_NODE1)
    err = lely_rtt_runtime_configure_master(lely_rtt_auto_runtime,
            &master_sdev);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("auto init Master configuration failed: %d", err);
        lely_rtt_runtime_destroy(lely_rtt_auto_runtime);
        lely_rtt_auto_runtime = RT_NULL;
        return err;
    }
#if defined(PKG_LELY_USING_MASTER_NMT_CFG)
    err = lely_rtt_runtime_configure_nmt_dcf(lely_rtt_auto_runtime, 1,
            master_node1_cfg_dcf, master_node1_cfg_dcf_size);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("auto init Node1 manual CFG data failed: %d", err);
        lely_rtt_runtime_destroy(lely_rtt_auto_runtime);
        lely_rtt_auto_runtime = RT_NULL;
        return err;
    }
#endif /* defined(PKG_LELY_USING_MASTER_NMT_CFG) */
#endif /* defined(PKG_LELY_EXAMPLE_MASTER_NODE1) */

    err = lely_rtt_runtime_start(lely_rtt_auto_runtime);
    if (err != RT_EOK) {
        LELY_RTT_LOG_E("auto init start failed: %d", err);

        /*
         * runtime_start() already requests STOP when its READY wait times out.
         * Give the owner one additional bounded stop window so a late startup
         * can complete teardown during boot. Preserve the original start error
         * as the init-hook result; cleanup must not hide the root cause.
         */
        if (lely_rtt_auto_runtime->owner_thread)
            (void)lely_rtt_runtime_stop(lely_rtt_auto_runtime);

        /* Never free storage while an owner can still be unwinding through it. */
        if (!lely_rtt_auto_runtime->owner_thread) {
            lely_rtt_runtime_destroy(lely_rtt_auto_runtime);
            lely_rtt_auto_runtime = RT_NULL;
        }
        return err;
    }

    lely_rtt_auto_ready_runtime = lely_rtt_auto_runtime;
    LELY_RTT_LOG_I("auto init started: can=%s bitrate=%u",
            config.can_name, (unsigned int)config.can_bitrate);
    return RT_EOK;
}
INIT_APP_EXPORT(lely_rtt_auto_init);

#endif /* defined(PKG_LELY_APP_AUTO_INIT) */
