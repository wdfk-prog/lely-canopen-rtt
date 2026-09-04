/**@file
 * This header file is part of the C11 and POSIX compatibility library; it
 * includes `<stdlib.h>` and defines any missing functionality.
 *
 * @copyright 2014-2018 Lely Industries N.V.
 *
 * @author J. S. Seldenthuis <jseldenthuis@lely.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LELY_LIBC_STDLIB_H_
#define LELY_LIBC_STDLIB_H_

#include <lely/features.h>

#include <stdlib.h>

#include <rtthread.h>

#ifdef aligned_alloc
#undef aligned_alloc
#endif

#ifdef aligned_free
#undef aligned_free
#endif

#define aligned_alloc(alignment, size) \
    rt_malloc_align((rt_size_t)(size), (rt_size_t)(alignment))

#define aligned_free(ptr) \
    rt_free_align((ptr))

#if !(_POSIX_C_SOURCE > 200112L)

/**
 * Updates or adds a variable in the environment of the calling process.
 * <b>envname</b> points to a string containing the name of the variable to be
 * added or altered. If the variable does not exist, or overwrite is non-zero,
 * it SHALL be set to the value two which <b>envval</b> points. Otherwise the
 * environment SHALL remain unchanged.
 *
 * @returns 0 on success, or -1 on error. In the latter case, the environment is
 * unchanged.
 */
int setenv(const char *envname, const char *envval, int overwrite);

#endif // !(_POSIX_C_SOURCE > 200112L)

#endif // !LELY_LIBC_STDLIB_H_
