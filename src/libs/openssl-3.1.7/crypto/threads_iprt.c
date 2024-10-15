/* $Id: threads_iprt.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Crypto threading and atomic functions built upon IPRT.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <openssl/crypto.h>
#include "internal/cryptlib.h"

#if defined(OPENSSL_THREADS)

# include <iprt/asm.h>
# include <iprt/assert.h>
# include <iprt/critsect.h>
# include <iprt/errcore.h>
# include <iprt/log.h>
# include <iprt/process.h>

/* Use read/write sections. */
/*# define USE_RW_CRITSECT */ /** @todo test the code */

# ifndef USE_RW_CRITSECT
/*
 * Of course it's wrong to use a critical section to implement a read/write
 * lock. But as the OpenSSL interface is too simple (there is only read_lock()/
 * write_lock() and only unspecified unlock() and the Windows implementatio
 * (threads_win.c) uses {Enter,Leave}CriticalSection we do that here as well.
 */
# endif

CRYPTO_RWLOCK *CRYPTO_THREAD_lock_new(void)
{
# ifdef USE_RW_CRITSECT
    PRTCRITSECTRW const pCritSect = (PRTCRITSECTRW)OPENSSL_zalloc(sizeof(*pCritSect));
# else
    PRTCRITSECT const pCritSect = (PRTCRITSECT)OPENSSL_zalloc(sizeof(*pCritSect));
# endif
    if (pCritSect)
    {
# ifdef USE_RW_CRITSECT
        int const rc = RTCritSectRwInitEx(pCritSect, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
# else
        int const rc = RTCritSectInitEx(pCritSect, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
# endif
        if (RT_SUCCESS(rc))
            return (CRYPTO_RWLOCK *)pCritSect;
        OPENSSL_free(pCritSect);
    }
    return NULL;
}

int CRYPTO_THREAD_read_lock(CRYPTO_RWLOCK *lock)
{
# ifdef USE_RW_CRITSECT
    PRTCRITSECTRW const pCritSect = (PRTCRITSECTRW)lock;
    int rc;

    /* writers cannot acquire read locks the way CRYPTO_THREAD_unlock works
       right now. It's also looks incompatible with pthread_rwlock_rdlock,
       so this should never trigger. */
    Assert(!RTCritSectRwIsWriteOwner(pCritSect));

    rc = RTCritSectRwEnterShared(pCritSect);
# else
    int const rc = RTCritSectEnter((PRTCRITSECT)lock);
# endif
    AssertRCReturn(rc, 0);
    return 1;
}

int CRYPTO_THREAD_write_lock(CRYPTO_RWLOCK *lock)
{
# ifdef USE_RW_CRITSECT
    int const rc = RTCritSectRwEnterExcl((PRTCRITSECTRW)lock);
# else
    int const rc = RTCritSectEnter((PRTCRITSECT)lock);
# endif
    AssertRCReturn(rc, 0);
    return 1;
}

int CRYPTO_THREAD_unlock(CRYPTO_RWLOCK *lock)
{
# ifdef USE_RW_CRITSECT
    PRTCRITSECTRW const pCritSect = (PRTCRITSECTRW)lock;
    if (RTCritSectRwIsWriteOwner(pCritSect))
    {
        int const rc1 = RTCritSectRwLeaveExcl(pCritSect);
        AssertRCReturn(rc1, 0);
    }
    else
    {
        int const rc2 = RTCritSectRwLeaveShared(pCritSect);
        AssertRCReturn(rc2, 0);
    }
# else
    int const rc = RTCritSectLeave((PRTCRITSECT)lock);
    AssertRCReturn(rc, 0);
# endif
    return 1;
}

void CRYPTO_THREAD_lock_free(CRYPTO_RWLOCK *lock)
{
    if (lock)
    {
# ifdef USE_RW_CRITSECT
        PRTCRITSECTRW const pCritSect = (PRTCRITSECTRW)lock;
        int const rc = RTCritSectRwDelete(pCritSect);
# else
        PRTCRITSECT const pCritSect = (PRTCRITSECT)lock;
        int const rc = RTCritSectDelete(pCritSect);
# endif
        AssertRC(rc);
        OPENSSL_free(pCritSect);
    }
}

int CRYPTO_THREAD_init_local(CRYPTO_THREAD_LOCAL *key, void (*cleanup)(void *))
{
    int rc = RTTlsAllocEx(key, (PFNRTTLSDTOR)cleanup); /* ASSUMES default calling convention is __cdecl, or close enough to it. */
    AssertRCReturn(rc, 0);
    return 1;
}

void *CRYPTO_THREAD_get_local(CRYPTO_THREAD_LOCAL *key)
{
    return RTTlsGet(*key);
}

int CRYPTO_THREAD_set_local(CRYPTO_THREAD_LOCAL *key, void *val)
{
    int rc = RTTlsSet(*key, val);
    AssertRCReturn(rc, 0);
    return 1;
}

int CRYPTO_THREAD_cleanup_local(CRYPTO_THREAD_LOCAL *key)
{
    int rc = RTTlsFree(*key);
    AssertRCReturn(rc, 0);
    return 1;
}

CRYPTO_THREAD_ID CRYPTO_THREAD_get_current_id(void)
{
    return RTThreadSelf();
}

int CRYPTO_THREAD_compare_id(CRYPTO_THREAD_ID a, CRYPTO_THREAD_ID b)
{
    return (a == b);
}

/** @callback_method_impl{FNRTONCE,
 * Wrapper that calls the @a init function given CRYPTO_THREAD_run_once().}
 */
static DECLCALLBACK(int32_t) cryptoThreadRunOnceWrapper(void *pvUser)
{
    void (*pfnInit)(void) = (void (*)(void))pvUser;
    pfnInit();
    return VINF_SUCCESS;
}

int CRYPTO_THREAD_run_once(CRYPTO_ONCE *once, void (*init)(void))
{
    int rc = RTOnce(once, cryptoThreadRunOnceWrapper, (void *)(uintptr_t)init);
    AssertRCReturn(rc, 0);
    return 1;
}

int CRYPTO_atomic_add(int *val, int amount, int *ret, CRYPTO_RWLOCK *lock)
{
    *ret = ASMAtomicAddS32((int32_t volatile*)val, amount) + amount;
    return 1;
}

int CRYPTO_atomic_or(uint64_t *val, uint64_t op, uint64_t *ret,
                     CRYPTO_RWLOCK *lock)
{
    uint64_t u64RetOld = ASMAtomicUoReadU64(val);
    uint64_t u64New;
    do
        u64New = u64RetOld | op;
    while (!ASMAtomicCmpXchgExU64(val, u64New, u64RetOld, &u64RetOld));
    *ret = u64RetOld;

    return 1;
}

int CRYPTO_atomic_load(uint64_t *val, uint64_t *ret, CRYPTO_RWLOCK *lock)
{
    *ret = ASMAtomicReadU64((uint64_t volatile *)val);
    return 1;
}

#endif /* defined(OPENSSL_THREADS) */

int openssl_init_fork_handlers(void)
{
    return 0;
}

int openssl_get_fork_id(void)
{
     return (int)RTProcSelf();
}
