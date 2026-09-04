/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author            Notes
 * 2026-09-04     wdfk-prog         first version
 */

/**
 * @file stdlib.h
 * @brief RT-Thread aligned-allocation overlay for Lely's libc compatibility API.
 *
 * Lely's io_can_net_t contains cache-line-aligned ring-buffer members and must
 * therefore be allocated with its requested alignment. On RT-Thread with
 * Newlib, the C11 aligned_alloc() path enters Newlib's _memalign_r(), while
 * _malloc_r()/_free_r() are redirected to RT-Thread's heap. Mixing those
 * allocator metadata formats is unsafe. This overlay keeps Lely's upstream
 * header contract but redirects the aligned allocation pair to RT-Thread's
 * native aligned heap API.
 *
 * @author wdfk-prog
 */

#ifndef LELY_RTT_LIBC_STDLIB_WRAPPER_H_
#define LELY_RTT_LIBC_STDLIB_WRAPPER_H_

#include "../../../../../upstream/include/lely/libc/stdlib.h"

#include <errno.h>
#include <rtthread.h>

/**
 * @brief Allocate Lely storage with an RT-Thread-native alignment contract.
 *
 * @param alignment Required power-of-two alignment in bytes.
 * @param size Requested allocation size in bytes.
 * @return Aligned storage on success, or RT_NULL with errno set to ENOMEM when
 *         the RT-Thread heap cannot satisfy the request.
 */
static inline void *
lely_rtt_aligned_alloc(size_t alignment, size_t size)
{
    void *ptr = rt_malloc_align((rt_size_t)size, (rt_size_t)alignment);

    if (!ptr)
        errno = ENOMEM;

    return ptr;
}

/**
 * @brief Release storage returned by lely_rtt_aligned_alloc().
 *
 * @param ptr Aligned storage pointer; RT_NULL is accepted.
 */
static inline void
lely_rtt_aligned_free(void *ptr)
{
    rt_free_align(ptr);
}

#ifdef aligned_alloc
#undef aligned_alloc
#endif
#define aligned_alloc(alignment, size) \
    lely_rtt_aligned_alloc((alignment), (size))

#ifdef aligned_free
#undef aligned_free
#endif
#define aligned_free(ptr) lely_rtt_aligned_free((ptr))

#endif /* LELY_RTT_LIBC_STDLIB_WRAPPER_H_ */
