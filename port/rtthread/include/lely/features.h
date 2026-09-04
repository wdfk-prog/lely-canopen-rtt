/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-02     wdfk-prog         first version
 */

/**
 * @file features.h
 * @brief RT-Thread overlay for the upstream Lely compiler feature header.
 *
 * The upstream header is kept byte-identical. This overlay first imports its
 * compiler/library feature detection and then applies the target-wide policy
 * from lely_rtt_config.h so every translation unit sees the same ABI-affecting
 * LELY_NO_* values.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_FEATURES_WRAPPER_H_
#define LELY_RTT_FEATURES_WRAPPER_H_

#include "../../../../upstream/include/lely/features.h"
#include <lely_rtt_config.h>

#endif /* LELY_RTT_FEATURES_WRAPPER_H_ */
