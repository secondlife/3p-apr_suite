/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr_arch_atomic.h"

#if (defined(_M_IA64) || defined(_M_AMD64)) && !defined(RC_INVOKED)
#define castptr(ptr) (ptr)
#else
#define castptr(ptr) ((long *)ptr)
#endif

APR_DECLARE(apr_status_t) apr_atomic_init(apr_pool_t *p)
{
#if defined (NEED_ATOMICS_GENERIC64)
    return apr__atomic_generic64_init(p);
#else
    return APR_SUCCESS;
#endif
}

APR_DECLARE(apr_uint32_t) apr_atomic_add32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    return InterlockedExchangeAdd(castptr(mem), val);
}

/* Of course we want the 2's complement of the unsigned value, val */
#ifdef _MSC_VER
#pragma warning(disable: 4146)
#endif

APR_DECLARE(void) apr_atomic_sub32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    InterlockedExchangeAdd(castptr(mem), -val);
}

APR_DECLARE(apr_uint32_t) apr_atomic_inc32(volatile apr_uint32_t *mem)
{
    /* we return old value, win32 returns new value :( */
    return InterlockedIncrement(castptr(mem)) - 1;
}

APR_DECLARE(int) apr_atomic_dec32(volatile apr_uint32_t *mem)
{
    return InterlockedDecrement(castptr(mem));
}

APR_DECLARE(void) apr_atomic_set32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    InterlockedExchange(castptr(mem), val);
}

APR_DECLARE(apr_uint32_t) apr_atomic_read32(volatile apr_uint32_t *mem)
{
    return *mem;
}

APR_DECLARE(apr_uint32_t) apr_atomic_cas32(volatile apr_uint32_t *mem, apr_uint32_t with,
                                           apr_uint32_t cmp)
{
    return InterlockedCompareExchange(castptr(mem), with, cmp);
}

APR_DECLARE(void *) apr_atomic_casptr(volatile void **mem, void *with, const void *cmp)
{
/* The casting in the body of this function diverges from FORWARD();
   expand it explicitly. */
#if (defined(WIN32) || defined(_M_IA64) || defined(_M_AMD64)) && !defined(RC_INVOKED)
    return InterlockedCompareExchangePointer((void* volatile*)mem, with, (void*)cmp);
#else
    return InterlockedCompareExchangePointer((void**)mem, with, (void*)cmp);
#endif
}

APR_DECLARE(apr_uint32_t) apr_atomic_xchg32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    return InterlockedExchange(castptr(mem), val);
}

APR_DECLARE(void*) apr_atomic_xchgptr(volatile void **mem, void *with)
{
    return InterlockedExchangePointer((void**)mem, with);
}
