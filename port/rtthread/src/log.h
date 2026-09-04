/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-04     wdfk-prog         first version
 */

/**
 * @file log.h
 * @brief Internal logging facade for the RT-Thread Lely port.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_LOG_H_
#define LELY_RTT_LOG_H_

#include <rtconfig.h>

#if defined(PKG_LELY_USING_ULOG)

/**
 * @brief Emit an RT-Thread port error through ULOG tag "lely.rtt".
 * @param format printf-style message format.
 * @param ... Optional format arguments.
 */
void lely_rtt_log_error(const char *format, ...);

/**
 * @brief Emit an RT-Thread port warning through ULOG tag "lely.rtt".
 * @param format printf-style message format.
 * @param ... Optional format arguments.
 */
void lely_rtt_log_warning(const char *format, ...);

/**
 * @brief Emit an RT-Thread port informational message through ULOG tag "lely.rtt".
 * @param format printf-style message format.
 * @param ... Optional format arguments.
 */
void lely_rtt_log_info(const char *format, ...);

/**
 * @brief Emit an RT-Thread port debug message through ULOG tag "lely.rtt".
 * @param format printf-style message format.
 * @param ... Optional format arguments.
 */
void lely_rtt_log_debug(const char *format, ...);

#define LELY_RTT_LOG_E(...) lely_rtt_log_error(__VA_ARGS__)
#define LELY_RTT_LOG_W(...) lely_rtt_log_warning(__VA_ARGS__)
#define LELY_RTT_LOG_I(...) lely_rtt_log_info(__VA_ARGS__)
#define LELY_RTT_LOG_D(...) lely_rtt_log_debug(__VA_ARGS__)

/**
 * @brief Route Lely's global diag()/diag_at() handlers into RT-Thread ULOG.
 *
 * The handler is process-global inside Lely. The current port is intentionally
 * single-owner, so all Lely diagnostics are expected to originate from the
 * owner thread after the runtime is started.
 */
void lely_rtt_log_init(void);

#else

#define LELY_RTT_LOG_E(...) ((void)0)
#define LELY_RTT_LOG_W(...) ((void)0)
#define LELY_RTT_LOG_I(...) ((void)0)
#define LELY_RTT_LOG_D(...) ((void)0)

static inline void
lely_rtt_log_init(void)
{
}

#endif /* defined(PKG_LELY_USING_ULOG) */

#endif /* LELY_RTT_LOG_H_ */
