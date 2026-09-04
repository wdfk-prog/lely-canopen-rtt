/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-04     wdfk-prog         first version
 */

/**
 * @file log.c
 * @brief Lely diagnostic bridge to RT-Thread ULOG.
 *
 * @author wdfk-prog
 */

#include "log.h"

#if defined(PKG_LELY_USING_ULOG)

#include <lely/util/diag.h>
#include <lely/util/errnum.h>

#include <stdarg.h>
#include <stdlib.h>
#include <ulog.h>

/** @brief ULOG tag used by diagnostics emitted inside Lely itself. */
#define LELY_RTT_DIAG_TAG "lely"

/** @brief ULOG tag used by the RT-Thread adaptation layer. */
#define LELY_RTT_PORT_TAG "lely.rtt"

/**
 * @brief Forward one RT-Thread port message to ULOG with a caller-selected level.
 * @param level ULOG severity.
 * @param format printf-style message format.
 * @param ap Format arguments.
 */
static void
lely_rtt_port_voutput(rt_uint32_t level, const char *format, va_list ap)
{
    ulog_voutput(level, LELY_RTT_PORT_TAG, RT_TRUE, RT_NULL, 0, 0, 0,
            format, ap);
}

void
lely_rtt_log_error(const char *format, ...)
{
#if ULOG_OUTPUT_LVL >= LOG_LVL_ERROR
    va_list ap;

    va_start(ap, format);
    lely_rtt_port_voutput(LOG_LVL_ERROR, format, ap);
    va_end(ap);
#else
    RT_UNUSED(format);
#endif
}

void
lely_rtt_log_warning(const char *format, ...)
{
#if ULOG_OUTPUT_LVL >= LOG_LVL_WARNING
    va_list ap;

    va_start(ap, format);
    lely_rtt_port_voutput(LOG_LVL_WARNING, format, ap);
    va_end(ap);
#else
    RT_UNUSED(format);
#endif
}

void
lely_rtt_log_info(const char *format, ...)
{
#if ULOG_OUTPUT_LVL >= LOG_LVL_INFO
    va_list ap;

    va_start(ap, format);
    lely_rtt_port_voutput(LOG_LVL_INFO, format, ap);
    va_end(ap);
#else
    RT_UNUSED(format);
#endif
}

void
lely_rtt_log_debug(const char *format, ...)
{
#if ULOG_OUTPUT_LVL >= LOG_LVL_DBG
    va_list ap;

    va_start(ap, format);
    lely_rtt_port_voutput(LOG_LVL_DBG, format, ap);
    va_end(ap);
#else
    RT_UNUSED(format);
#endif
}

/**
 * @brief Shared diagnostic formatting buffer.
 *
 * Lely is configured with LELY_NO_THREADS=1 and this port requires all Lely
 * calls to execute on the dedicated owner thread. A single static buffer keeps
 * diagnostic stack use bounded without adding a lock that would imply false
 * cross-thread safety for the rest of the Lely object graph.
 */
static char lely_rtt_diag_buffer[ULOG_LINE_BUF_SIZE];

/**
 * @brief Clamp a snprintf-style result to the remaining writable payload.
 * @param written Return value from rt_snprintf()/rt_vsnprintf().
 * @param capacity Remaining buffer capacity including the trailing null byte.
 * @return Number of payload bytes that may be considered appended.
 */
static rt_size_t
lely_rtt_log_written(int written, rt_size_t capacity)
{
    if (written <= 0 || capacity <= 1)
        return 0;
    if ((rt_size_t)written >= capacity)
        return capacity - 1;
    return (rt_size_t)written;
}

/**
 * @brief Emit one fully formatted Lely diagnostic at the matching ULOG level.
 * @param severity Lely diagnostic severity.
 * @param line Null-terminated diagnostic payload without a ULOG prefix.
 */
static void
lely_rtt_diag_emit(enum diag_severity severity, const char *line)
{
    switch (severity) {
    case DIAG_DEBUG:
        ulog_d(LELY_RTT_DIAG_TAG, "%s", line);
        break;
    case DIAG_INFO:
        ulog_i(LELY_RTT_DIAG_TAG, "%s", line);
        break;
    case DIAG_WARNING:
        ulog_w(LELY_RTT_DIAG_TAG, "%s", line);
        break;
    case DIAG_ERROR:
        ulog_e(LELY_RTT_DIAG_TAG, "%s", line);
        break;
    case DIAG_FATAL:
        ulog_e(LELY_RTT_DIAG_TAG, "%s", line);
        ulog_flush();
        abort();
        break;
    default:
        ulog_w(LELY_RTT_DIAG_TAG, "%s", line);
        break;
    }
}

/**
 * @brief Format a Lely diagnostic and forward it to ULOG.
 * @param severity Lely diagnostic severity.
 * @param errc Optional native Lely error code.
 * @param at Optional source/configuration file location.
 * @param format printf-style Lely diagnostic format string.
 * @param ap Format arguments owned by the caller.
 */
static void
lely_rtt_diag_voutput(enum diag_severity severity, int errc,
        const struct floc *at, const char *format, va_list ap)
{
    rt_size_t offset = 0;
    rt_size_t remaining = sizeof(lely_rtt_diag_buffer);
    int written;

    lely_rtt_diag_buffer[0] = '\0';

    if (at) {
        const char *filename = at->filename ? at->filename : "?";

        if (at->line > 0 && at->column > 0) {
            written = rt_snprintf(lely_rtt_diag_buffer, remaining,
                    "%s:%d:%d: ", filename, at->line, at->column);
        } else if (at->line > 0) {
            written = rt_snprintf(lely_rtt_diag_buffer, remaining,
                    "%s:%d: ", filename, at->line);
        } else {
            written = rt_snprintf(lely_rtt_diag_buffer, remaining,
                    "%s: ", filename);
        }
        offset += lely_rtt_log_written(written, remaining);
        remaining = sizeof(lely_rtt_diag_buffer) - offset;
    }

    if (format && remaining > 1) {
        written = rt_vsnprintf(lely_rtt_diag_buffer + offset, remaining,
                format, ap);
        offset += lely_rtt_log_written(written, remaining);
        remaining = sizeof(lely_rtt_diag_buffer) - offset;
    }

    if (errc && remaining > 1) {
        const char *errstr = errc2str(errc);

        written = rt_snprintf(lely_rtt_diag_buffer + offset, remaining,
                "%s%s (errc=%d)", offset ? ": " : "",
                errstr ? errstr : "unknown error", errc);
        offset += lely_rtt_log_written(written, remaining);
    }

    lely_rtt_diag_buffer[sizeof(lely_rtt_diag_buffer) - 1] = '\0';
    lely_rtt_diag_emit(severity, lely_rtt_diag_buffer);
}

/**
 * @brief Lely diag() handler that forwards to the common ULOG formatter.
 * @param handle Unused Lely handler context.
 * @param severity Lely diagnostic severity.
 * @param errc Optional native Lely error code.
 * @param format printf-style Lely diagnostic format string.
 * @param ap Format arguments owned by the caller.
 */
static void
lely_rtt_diag_handler(void *handle, enum diag_severity severity, int errc,
        const char *format, va_list ap)
{
    RT_UNUSED(handle);
    lely_rtt_diag_voutput(severity, errc, RT_NULL, format, ap);
}

/**
 * @brief Lely diag_at() handler that preserves file/line/column metadata.
 * @param handle Unused Lely handler context.
 * @param severity Lely diagnostic severity.
 * @param errc Optional native Lely error code.
 * @param at Optional source/configuration file location.
 * @param format printf-style Lely diagnostic format string.
 * @param ap Format arguments owned by the caller.
 */
static void
lely_rtt_diag_at_handler(void *handle, enum diag_severity severity, int errc,
        const struct floc *at, const char *format, va_list ap)
{
    RT_UNUSED(handle);
    lely_rtt_diag_voutput(severity, errc, at, format, ap);
}

/**
 * @brief Install the RT-Thread ULOG backend for Lely diagnostics.
 */
void
lely_rtt_log_init(void)
{
#if !LELY_NO_DIAG
    diag_set_handler(lely_rtt_diag_handler, RT_NULL);
    diag_at_set_handler(lely_rtt_diag_at_handler, RT_NULL);
#endif /* !LELY_NO_DIAG */
}

#endif /* defined(PKG_LELY_USING_ULOG) */
