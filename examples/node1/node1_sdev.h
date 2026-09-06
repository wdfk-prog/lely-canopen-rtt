/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-04     wdfk-prog         first version
 */

/**
 * @file node1_sdev.h
 * @brief Static CANopen device declaration for the Node1 smoke-test slave.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_EXAMPLE_NODE1_SDEV_H_
#define LELY_RTT_EXAMPLE_NODE1_SDEV_H_

#include <lely/co/sdev.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Node-ID 1 static device generated from examples/node1/node1.dcf. */
extern const struct co_sdev node1_sdev;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LELY_RTT_EXAMPLE_NODE1_SDEV_H_ */
