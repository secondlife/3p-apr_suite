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

APR_DECLARE(apr_status_t) apr_atomic_init(apr_pool_t *p)
{
#if defined (NEED_ATOMICS_GENERIC64)
    return apr__atomic_generic64_init(p);
#else
    return APR_SUCCESS;
#endif
}

/* 
 * Remapping function pointer type to accept apr_uint32_t's type-safely
 * as the arguments for as our apr_atomic_foo32 Functions
 */
typedef WINBASEAPI apr_uint32_t (WINAPI * apr_atomic_win32_ptr_fn)
    (apr_uint32_t volatile *);
typedef WINBASEAPI apr_uint32_t (WINAPI * apr_atomic_win32_ptr_val_fn)
    (apr_uint32_t volatile *, 
     apr_uint32_t);
typedef WINBASEAPI apr_uint32_t (WINAPI * apr_atomic_win32_ptr_val_val_fn)
    (apr_uint32_t volatile *, 
     apr_uint32_t, apr_uint32_t);
typedef WINBASEAPI void * (WINAPI * apr_atomic_win32_ptr_ptr_ptr_fn)
    (volatile void **, 
     void *, const void *);
typedef WINBASEAPI void * (WINAPI * apr_atomic_win32_ptr_ptr_fn)
    (volatile void **,
     void *);

// nat 2014-10-30: In response to link errors:
// apr_atomic.obj : error LNK2019: unresolved external symbol __InterlockedIncrement referenced in function _apr_atomic_inc32@4
// apr_atomic.obj : error LNK2019: unresolved external symbol __InterlockedExchangeAdd referenced in function _apr_atomic_add32@8
// apr_atomic.obj : error LNK2019: unresolved external symbol __InterlockedExchange referenced in function _apr_atomic_set32@8
// apr_atomic.obj : error LNK2019: unresolved external symbol __InterlockedDecrement referenced in function _apr_atomic_dec32@4
// apr_atomic.obj : error LNK2019: unresolved external symbol __InterlockedCompareExchange referenced in function _apr_atomic_cas32@12
// and per https://groups.google.com/forum/#!topic/unimrcp/Iybpn51UYnI :
// "The resolution would be not to use the casts as for x64 or properly
// typedef them taking into account v120 specifics."
// I have inserted 'defined(WIN32) ||' into the #if stanza for the FORWARD()
// macro (below). That may or may not be what the author of the above post
// meant.

// I observe that the body of each wrapper function below takes essentially
// the form:
//#if conditions...
//    return InterlockedIncrement(mem);
//#elif defined(__MINGW32__)
//    return InterlockedIncrement((long *)mem);
//#else
//    return ((apr_atomic_win32_ptr_fn)InterlockedIncrement)(mem);
//#endif
// The FORWARD() macro encapsulates this pattern. We actually define both
// FORWARD1() and FORWARD() because of the vexing problem of whether to put a
// comma after the first argument.
#if (defined(WIN32) || defined(_M_IA64) || defined(_M_AMD64)) && !defined(RC_INVOKED)
#define FORWARD1(cast, func, arg1) \
    func(arg1)
#define FORWARD(cast, func, arg1, ...) \
    func(arg1, __VA_ARGS__)

#elif defined(__MINGW32__)
#define FORWARD1(cast, func, arg1) \
    func((long *)arg1)
#define FORWARD(cast, func, arg1, ...) \
    func((long *)arg1, __VA_ARGS__)

#else
#define FORWARD1(cast, func, arg1) \
    ((cast)func)(arg1)
#define FORWARD(cast, func, arg1, ...) \
    ((cast)func)(arg1, __VA_ARGS__)
#endif


APR_DECLARE(apr_uint32_t) apr_atomic_add32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    return FORWARD(apr_atomic_win32_ptr_val_fn, InterlockedExchangeAdd, mem, val);
}

/* Of course we want the 2's complement of the unsigned value, val */
#ifdef _MSC_VER
#pragma warning(disable: 4146)
#endif

APR_DECLARE(void) apr_atomic_sub32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    FORWARD(apr_atomic_win32_ptr_val_fn, InterlockedExchangeAdd, mem, -val);
}

APR_DECLARE(apr_uint32_t) apr_atomic_inc32(volatile apr_uint32_t *mem)
{
    /* we return old value, win32 returns new value :( */
    return FORWARD1(apr_atomic_win32_ptr_fn, InterlockedIncrement, mem) - 1;
}

APR_DECLARE(int) apr_atomic_dec32(volatile apr_uint32_t *mem)
{
    return FORWARD1(apr_atomic_win32_ptr_fn, InterlockedDecrement, mem);
}

APR_DECLARE(void) apr_atomic_set32(volatile apr_uint32_t *mem, apr_uint32_t val)
{
    FORWARD(apr_atomic_win32_ptr_val_fn, InterlockedExchange, mem, val);
}

APR_DECLARE(apr_uint32_t) apr_atomic_read32(volatile apr_uint32_t *mem)
{
    return *mem;
}

APR_DECLARE(apr_uint32_t) apr_atomic_cas32(volatile apr_uint32_t *mem, apr_uint32_t with,
                                           apr_uint32_t cmp)
{
    return FORWARD(apr_atomic_win32_ptr_val_val_fn, InterlockedCompareExchange, mem, with, cmp);
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
    return FORWARD(apr_atomic_win32_ptr_val_fn, InterlockedExchange, mem, val);
}

APR_DECLARE(void*) apr_atomic_xchgptr(volatile void **mem, void *with)
{
/* This function conflates __MINGW32__ with the other cases, plus the cast
 * diverges from FORWARD; expand it explicitly. */
#if (defined(WIN32) || defined(_M_IA64) || defined(_M_AMD64) || defined(__MINGW32__)) && !defined(RC_INVOKED)
    return InterlockedExchangePointer((void**)mem, with);
}
