/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-05     wdfk-prog         first static Master definition
 */

/**
 * @file master_sdev.h
 * @brief Static local CANopen Master declaration for the remote Node1 example.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_EXAMPLE_MASTER_NODE1_SDEV_H_
#define LELY_RTT_EXAMPLE_MASTER_NODE1_SDEV_H_

#include <lely/co/sdev.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @brief Static local Master object dictionary used by the RT-Thread runtime. */
extern const struct co_sdev master_sdev;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LELY_RTT_EXAMPLE_MASTER_NODE1_SDEV_H_ */
