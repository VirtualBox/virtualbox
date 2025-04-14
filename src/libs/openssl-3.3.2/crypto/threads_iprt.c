/* $Id: threads_iprt.c 109212 2025-04-14 17:29:48Z aleksey.ilyushin@oracle.com $ */
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
#include <crypto/cryptlib.h>
//#include "internal/cryptlib.h"
#include "internal/thread_arch.h"

#if defined(OPENSSL_THREADS)

# include <iprt/asm.h>
# include <iprt/assert.h>
# include <iprt/critsect.h>
# include <iprt/errcore.h>
# include <iprt/log.h>
# include <iprt/process.h>
# include <iprt/semaphore.h>

/*
 * @todo Replace this simple R/W lock implementation with proper RCU if better
 * multithreaded openssl performance is actually needed (and gained via using RCU).
 */
# define VBOX_OPENSSL_WITH_RCU_SUPPORT
# ifdef VBOX_OPENSSL_WITH_RCU_SUPPORT
#include "internal/rcu.h"
#include "rcu_internal.h"

typedef struct rcu_cb_item *prcu_cb_item;

/*
 * This is the internal version of a CRYPTO_RCU_LOCK
 * it is cast from CRYPTO_RCU_LOCK
 */
struct rcu_lock_st {
    /* Callbacks to call for next ossl_synchronize_rcu */
    struct rcu_cb_item *cb_items;

    /* Read/write semaphore */
    RTSEMRW rw_lock;
};

void *ossl_rcu_uptr_deref(void **p)
{
    /*
     * Our automic reads include memory fence, so the thread that dereferences a pointer should be able
     * to see the memory with all modifications that were made by other threads that did 'ossl_rcu_assign_uptr'
     * prior to this dereference.
     */
    return ASMAtomicReadPtr(p);
}

void ossl_rcu_assign_uptr(void **p, void **v)
{
    ASMAtomicWritePtr(p, *v);
}

void ossl_rcu_read_lock(CRYPTO_RCU_LOCK *lock)
{
    RTSemRWRequestRead(lock->rw_lock, RT_INDEFINITE_WAIT);
}

void ossl_rcu_read_unlock(CRYPTO_RCU_LOCK *lock)
{
    RTSemRWReleaseRead(lock->rw_lock);
}

void ossl_rcu_write_lock(CRYPTO_RCU_LOCK *lock)
{
    RTSemRWRequestWrite(lock->rw_lock, RT_INDEFINITE_WAIT);
}

void ossl_rcu_write_unlock(CRYPTO_RCU_LOCK *lock)
{
    RTSemRWReleaseWrite(lock->rw_lock);
}


int ossl_rcu_call(CRYPTO_RCU_LOCK *lock, rcu_cb_fn cb, void *data)
{
    struct rcu_cb_item *new =
        OPENSSL_zalloc(sizeof(*new));

    if (new == NULL)
        return 0;

    new->data = data;
    new->fn = cb;
    /*
     * Use __ATOMIC_ACQ_REL here to indicate that any prior writes to this
     * list are visible to us prior to reading, and publish the new value
     * immediately
     * VBOX: Our atomic primitives do memory fence, which should be equivalent
     * to __ATOMIC_ACQ_REL behavior.
     */
    new->next = ASMAtomicXchgPtrT(&lock->cb_items, new, prcu_cb_item);

    return 1;
}

/* VBOX: no need to do any synchronization here, as we use simple R/W lock */
void ossl_synchronize_rcu(CRYPTO_RCU_LOCK *lock)
{
    struct rcu_cb_item *cb_items, *tmpcb;

    cb_items = ASMAtomicXchgPtrT(&lock->cb_items, NULL, prcu_cb_item);
    /* handle any callbacks that we have */
    while (cb_items != NULL) {
        tmpcb = cb_items;
        cb_items = cb_items->next;
        tmpcb->fn(tmpcb->data);
        OPENSSL_free(tmpcb);
    }
}

CRYPTO_RCU_LOCK *ossl_rcu_lock_new(int num_writers, OSSL_LIB_CTX *ctx)
{
    struct rcu_lock_st *new;

    RT_NOREF(num_writers, ctx);

    new = OPENSSL_zalloc(sizeof(*new));
    if (new == NULL)
        return NULL;

    if (RT_FAILURE(RTSemRWCreate(&new->rw_lock)))
    {
        OPENSSL_free(new);
        return NULL;
    }
    return new;
}

void ossl_rcu_lock_free(CRYPTO_RCU_LOCK *lock)
{
    struct rcu_lock_st *rlock = (struct rcu_lock_st *)lock;

    if (lock == NULL)
        return;

    /* make sure we're synchronized */
    ossl_synchronize_rcu(rlock);

    AssertRC(RTSemRWDestroy(rlock->rw_lock));
    OPENSSL_free(rlock);
}
# endif /* VBOX_OPENSSL_WITH_RCU_SUPPORT */

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
