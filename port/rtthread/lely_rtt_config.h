/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-02     wdfk-prog         first version
 */

/**
 * @file lely_rtt_config.h
 * @brief RT-Thread target feature policy for the vendored Lely CANopen runtime.
 *
 * The target uses Lely's single-thread build mode. RT-Thread itself remains a
 * multithreaded system, but all access to Lely EV/IO2/CANopen objects must be
 * serialized through one owner thread.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_CONFIG_H_
#define LELY_RTT_CONFIG_H_

#include <rtconfig.h>

/*
 * This is the single RT-Thread target policy for ABI-affecting Lely feature
 * macros. It is applied by port/rtthread/include/lely/features.h after the
 * upstream compiler-feature detection. Do not define LELY_NO_* differently in
 * generated device_sdev.c or application translation units; all consumers must
 * resolve the same features wrapper through the package CPPPATH.
 */

/* Pure C with dynamic allocation. */
#undef LELY_NO_CXX
#define LELY_NO_CXX 1
#undef LELY_NO_MALLOC
#define LELY_NO_MALLOC 0

/*
 * Single-owner policy: Lely performs no internal thread synchronization.
 * Cross-thread/ISR producers must hand work to the owner thread before calling
 * EV, IO2 or CANopen APIs. This also removes compiler TLS and C11 thread ABI
 * dependencies from the target.
 */
#undef LELY_NO_THREADS
#define LELY_NO_THREADS 1
#undef LELY_NO_ATOMICS
#define LELY_NO_ATOMICS 1

/*
 * Blocking/deadline waits in ev_loop/io_user_can are not part of the target
 * architecture. CANopen protocol timers remain provided by io_user_timer.
 */
#undef LELY_NO_TIMEOUT
#define LELY_NO_TIMEOUT 1

#undef LELY_NO_ERRNO
#define LELY_NO_ERRNO 0
#undef LELY_NO_RT
#define LELY_NO_RT 0

/* No target filesystem/stdio runtime is required by the selected architecture. */
#undef LELY_NO_STDIO
#define LELY_NO_STDIO 1
#undef LELY_NO_DAEMON
#define LELY_NO_DAEMON 1
#undef LELY_NO_CO_DCF
#define LELY_NO_CO_DCF 1
#undef LELY_NO_CO_GW
#define LELY_NO_CO_GW 1
#undef LELY_NO_CO_GW_TXT
#define LELY_NO_CO_GW_TXT 1
#undef LELY_NO_CO_OBJ_FILE
#define LELY_NO_CO_OBJ_FILE 1
#undef LELY_NO_CO_WTM
#define LELY_NO_CO_WTM 1

/* The target does not provide C11 threads or pthread as a Lely backend. */
#undef LELY_HAVE_THREADS_H
#define LELY_HAVE_THREADS_H 0
#undef LELY_HAVE_PTHREAD_H
#define LELY_HAVE_PTHREAD_H 0

/* MCU queue policy fixed by the RT-Thread port. */
#undef LELY_IO_USER_CAN_RXLEN
#define LELY_IO_USER_CAN_RXLEN 32
#undef LELY_IO_CAN_NET_TXLEN
#define LELY_IO_CAN_NET_TXLEN 32

#ifdef PKG_LELY_USING_CANFD
#undef LELY_NO_CANFD
#define LELY_NO_CANFD 0
#else
#undef LELY_NO_CANFD
#define LELY_NO_CANFD 1
#endif /* PKG_LELY_USING_CANFD */

#ifdef PKG_LELY_USING_CO_CSDO
#undef LELY_NO_CO_CSDO
#define LELY_NO_CO_CSDO 0
#else
#undef LELY_NO_CO_CSDO
#define LELY_NO_CO_CSDO 1
#endif /* PKG_LELY_USING_CO_CSDO */

#ifdef PKG_LELY_USING_CO_EMCY
#undef LELY_NO_CO_EMCY
#define LELY_NO_CO_EMCY 0
#else
#undef LELY_NO_CO_EMCY
#define LELY_NO_CO_EMCY 1
#endif /* PKG_LELY_USING_CO_EMCY */

#ifdef PKG_LELY_USING_CO_LSS
#undef LELY_NO_CO_LSS
#define LELY_NO_CO_LSS 0
#else
#undef LELY_NO_CO_LSS
#define LELY_NO_CO_LSS 1
#endif /* PKG_LELY_USING_CO_LSS */

#ifdef PKG_LELY_USING_CO_MASTER
#undef LELY_NO_CO_MASTER
#define LELY_NO_CO_MASTER 0
#else
#undef LELY_NO_CO_MASTER
#define LELY_NO_CO_MASTER 1
#endif /* PKG_LELY_USING_CO_MASTER */

#ifdef PKG_LELY_USING_CO_NMT_BOOT
#undef LELY_NO_CO_NMT_BOOT
#define LELY_NO_CO_NMT_BOOT 0
#else
#undef LELY_NO_CO_NMT_BOOT
#define LELY_NO_CO_NMT_BOOT 1
#endif /* PKG_LELY_USING_CO_NMT_BOOT */

#ifdef PKG_LELY_USING_CO_NMT_CFG
#undef LELY_NO_CO_NMT_CFG
#define LELY_NO_CO_NMT_CFG 0
#else
#undef LELY_NO_CO_NMT_CFG
#define LELY_NO_CO_NMT_CFG 1
#endif /* PKG_LELY_USING_CO_NMT_CFG */

#ifdef PKG_LELY_USING_CO_NODE_GUARDING
#undef LELY_NO_CO_NG
#define LELY_NO_CO_NG 0
#else
#undef LELY_NO_CO_NG
#define LELY_NO_CO_NG 1
#endif /* PKG_LELY_USING_CO_NODE_GUARDING */

#ifdef PKG_LELY_USING_CO_RPDO
#undef LELY_NO_CO_RPDO
#define LELY_NO_CO_RPDO 0
#else
#undef LELY_NO_CO_RPDO
#define LELY_NO_CO_RPDO 1
#endif /* PKG_LELY_USING_CO_RPDO */

#ifdef PKG_LELY_USING_CO_TPDO
#undef LELY_NO_CO_TPDO
#define LELY_NO_CO_TPDO 0
#else
#undef LELY_NO_CO_TPDO
#define LELY_NO_CO_TPDO 1
#endif /* PKG_LELY_USING_CO_TPDO */

#ifdef PKG_LELY_USING_CO_SYNC
#undef LELY_NO_CO_SYNC
#define LELY_NO_CO_SYNC 0
#else
#undef LELY_NO_CO_SYNC
#define LELY_NO_CO_SYNC 1
#endif /* PKG_LELY_USING_CO_SYNC */

#ifdef PKG_LELY_USING_CO_TIME
#undef LELY_NO_CO_TIME
#define LELY_NO_CO_TIME 0
#else
#undef LELY_NO_CO_TIME
#define LELY_NO_CO_TIME 1
#endif /* PKG_LELY_USING_CO_TIME */

#ifdef PKG_LELY_USING_CO_MPDO
#undef LELY_NO_CO_MPDO
#define LELY_NO_CO_MPDO 0
#else
#undef LELY_NO_CO_MPDO
#define LELY_NO_CO_MPDO 1
#endif /* PKG_LELY_USING_CO_MPDO */

#ifdef PKG_LELY_USING_CO_SSDO_BLOCK
#undef LELY_NO_CO_SSDO_BLK
#define LELY_NO_CO_SSDO_BLK 0
#else
#undef LELY_NO_CO_SSDO_BLK
#define LELY_NO_CO_SSDO_BLK 1
#endif /* PKG_LELY_USING_CO_SSDO_BLOCK */

#ifdef PKG_LELY_USING_CO_OBJ_NAME
#undef LELY_NO_CO_OBJ_NAME
#define LELY_NO_CO_OBJ_NAME 0
#else
#undef LELY_NO_CO_OBJ_NAME
#define LELY_NO_CO_OBJ_NAME 1
#endif /* PKG_LELY_USING_CO_OBJ_NAME */

#ifdef PKG_LELY_USING_CO_OBJ_LIMITS
#undef LELY_NO_CO_OBJ_LIMITS
#define LELY_NO_CO_OBJ_LIMITS 0
#else
#undef LELY_NO_CO_OBJ_LIMITS
#define LELY_NO_CO_OBJ_LIMITS 1
#endif /* PKG_LELY_USING_CO_OBJ_LIMITS */

#ifdef PKG_LELY_USING_CO_OBJ_DEFAULT
#undef LELY_NO_CO_OBJ_DEFAULT
#define LELY_NO_CO_OBJ_DEFAULT 0
#else
#undef LELY_NO_CO_OBJ_DEFAULT
#define LELY_NO_CO_OBJ_DEFAULT 1
#endif /* PKG_LELY_USING_CO_OBJ_DEFAULT */

#ifdef PKG_LELY_USING_CO_OBJ_UPLOAD
#undef LELY_NO_CO_OBJ_UPLOAD
#define LELY_NO_CO_OBJ_UPLOAD 0
#else
#undef LELY_NO_CO_OBJ_UPLOAD
#define LELY_NO_CO_OBJ_UPLOAD 1
#endif /* PKG_LELY_USING_CO_OBJ_UPLOAD */

/* Static co_sdev generated by dcf2c is mandatory for this port architecture. */
#undef LELY_NO_CO_SDEV
#define LELY_NO_CO_SDEV 0

#endif /* LELY_RTT_CONFIG_H_ */
