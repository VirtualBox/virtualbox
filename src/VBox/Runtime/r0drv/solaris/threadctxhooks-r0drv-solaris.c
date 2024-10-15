/* $Id: threadctxhooks-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Thread Context Switching Hook, Ring-0 Driver, Solaris.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-solaris-kernel.h"
#include "internal/iprt.h"

#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/log.h>
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal hook object for solaris.
 */
typedef struct RTTHREADCTXHOOKINT
{
    /** Magic value (RTTHREADCTXHOOKINT_MAGIC). */
    uint32_t volatile           u32Magic;
    /** The thread handle (owner) for which the context-hooks are registered. */
    RTNATIVETHREAD              hOwner;
    /** Pointer to the registered callback function. */
    PFNRTTHREADCTXHOOK          pfnCallback;
    /** User argument passed to the callback function. */
    void                       *pvUser;
    /** Whether the hook is enabled or not. */
    bool volatile               fEnabled;
    /** Number of references to this object. */
    uint32_t volatile           cRefs;
} RTTHREADCTXHOOKINT;
typedef RTTHREADCTXHOOKINT *PRTTHREADCTXHOOKINT;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a hook handle and returns rc if not valid. */
#define RTTHREADCTX_VALID_RETURN_RC(pThis, rc) \
    do { \
        AssertPtrReturn((pThis), (rc)); \
        AssertReturn((pThis)->u32Magic == RTTHREADCTXHOOKINT_MAGIC, (rc)); \
        AssertReturn((pThis)->cRefs > 0, (rc)); \
    } while (0)


/**
 * Hook function for the thread-save event.
 *
 * @param   pvThreadCtxInt  Opaque pointer to the internal hook object.
 *
 * @remarks Called with the with preemption disabled!
 */
static void rtThreadCtxHookSolOut(void *pvThreadCtxInt)
{
    PRTTHREADCTXHOOKINT pThis = (PRTTHREADCTXHOOKINT)pvThreadCtxInt;
    AssertPtr(pThis);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(pThis->cRefs > 0);

    if (pThis->fEnabled)
    {
        Assert(pThis->pfnCallback);
        pThis->pfnCallback(RTTHREADCTXEVENT_OUT, pThis->pvUser);
    }
}


/**
 * Hook function for the thread-restore event.
 *
 * @param   pvThreadCtxInt  Opaque pointer to the internal hook object.
 *
 * @remarks Called with preemption disabled!
 */
static void rtThreadCtxHookSolIn(void *pvThreadCtxInt)
{
    PRTTHREADCTXHOOKINT pThis = (PRTTHREADCTXHOOKINT)pvThreadCtxInt;
    AssertPtr(pThis);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(pThis->cRefs > 0);

    if (pThis->fEnabled)
    {
        Assert(pThis->pfnCallback);
        pThis->pfnCallback(RTTHREADCTXEVENT_IN, pThis->pvUser);
    }
}


/**
 * Hook function for the thread-free event.
 *
 * This is used for making sure the hook object is safely released - see
 * RTThreadCtxHookRelease for details.
 *
 * @param   pvThreadCtxInt      Opaque pointer to the internal hook object.
 * @param   fIsExec             Whether this event is triggered due to exec().
 */
static void rtThreadCtxHookSolFree(void *pvThreadCtxInt, int fIsExec)
{
    PRTTHREADCTXHOOKINT pThis = (PRTTHREADCTXHOOKINT)pvThreadCtxInt;
    AssertPtrReturnVoid(pThis);
    AssertMsgReturnVoid(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis));

    uint32_t cRefs = ASMAtomicReadU32(&pThis->cRefs);
    if (cRefs > 0)
    {
        cRefs = ASMAtomicDecU32(&pThis->cRefs);
        if (!cRefs)
        {
            Assert(!pThis->fEnabled);
            ASMAtomicWriteU32(&pThis->u32Magic, ~RTTHREADCTXHOOKINT_MAGIC);
            RTMemFree(pThis);
        }
    }
    else
    {
        /* Should never happen. */
        AssertMsgFailed(("rtThreadCtxHookSolFree with cRefs=0 pThis=%p\n", pThis));
    }
}


RTDECL(int) RTThreadCtxHookCreate(PRTTHREADCTXHOOK phCtxHook, uint32_t fFlags, PFNRTTHREADCTXHOOK pfnCallback, void *pvUser)
{
    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis;
    Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);

    /*
     * Allocate and initialize a new hook.
     */
    pThis = (PRTTHREADCTXHOOKINT)RTMemAllocZ(sizeof(*pThis));
    if (RT_UNLIKELY(!pThis))
        return VERR_NO_MEMORY;
    pThis->u32Magic     = RTTHREADCTXHOOKINT_MAGIC;
    pThis->hOwner       = RTThreadNativeSelf();
    pThis->pfnCallback  = pfnCallback;
    pThis->pvUser       = pvUser;
    pThis->fEnabled     = false;
    pThis->cRefs        = 2;        /* One reference for the thread, one for the caller. */

    /*
     * installctx() allocates memory and thus cannot be used in RTThreadCtxHookRegister() which can be used
     * with preemption disabled. We allocate the context-hooks here and use 'fEnabled' to determine if we can
     * invoke the consumer's hook or not.
     */
    if (g_frtSolOldThreadCtx)
    {
        g_rtSolThreadCtx.Install.pfnSol_installctx_old(curthread,
                                                       pThis,
                                                       rtThreadCtxHookSolOut,   /* save */
                                                       rtThreadCtxHookSolIn,    /* restore */
                                                       NULL,                    /* fork */
                                                       NULL,                    /* lwp_create */
                                                       rtThreadCtxHookSolFree);
    }
    else
    {
        g_rtSolThreadCtx.Install.pfnSol_installctx(curthread,
                                                   pThis,
                                                   rtThreadCtxHookSolOut,       /* save */
                                                   rtThreadCtxHookSolIn,        /* restore */
                                                   NULL,                        /* fork */
                                                   NULL,                        /* lwp_create */
                                                   NULL,                        /* exit */
                                                   rtThreadCtxHookSolFree);
    }

    *phCtxHook = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTThreadCtxHookDestroy(RTTHREADCTXHOOK hCtxHook)
{
    /*
     * Validate input, ignoring NIL.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    if (pThis == NIL_RTTHREADCTXHOOK)
        return VINF_SUCCESS;
    RTTHREADCTX_VALID_RETURN_RC(hCtxHook, VERR_INVALID_HANDLE);
    Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(!pThis->fEnabled || pThis->hOwner == RTThreadNativeSelf());

    /*
     * Make sure it's disabled.
     */
    ASMAtomicWriteBool(&pThis->fEnabled, false);

    /*
     * Decrement.
     */
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (   cRefs == 1
        && pThis->hOwner == RTThreadNativeSelf())
    {
        /*
         * removectx() will invoke rtThreadCtxHookSolFree() and there is no way to bypass it and still use
         * rtThreadCtxHookSolFree() at the same time.  Hence the convulated reference counting.
         *
         * When this function is called from the owner thread and is the last reference, we call removectx() which
         * will invoke rtThreadCtxHookSolFree() with cRefs = 1 and that will then free the hook object.
         *
         * When the function is called from a different thread, we simply decrement the reference. Whenever the
         * ring-0 thread dies, Solaris will call rtThreadCtxHookSolFree() which will free the hook object.
         */
        int rc;
        if (g_frtSolOldThreadCtx)
        {
            rc = g_rtSolThreadCtx.Remove.pfnSol_removectx_old(curthread,
                                                              pThis,
                                                              rtThreadCtxHookSolOut,    /* save */
                                                              rtThreadCtxHookSolIn,     /* restore */
                                                              NULL,                     /* fork */
                                                              NULL,                     /* lwp_create */
                                                              rtThreadCtxHookSolFree);
        }
        else
        {
            rc = g_rtSolThreadCtx.Remove.pfnSol_removectx(curthread,
                                                          pThis,
                                                          rtThreadCtxHookSolOut,        /* save */
                                                          rtThreadCtxHookSolIn,         /* restore */
                                                          NULL,                         /* fork */
                                                          NULL,                         /* lwp_create */
                                                          NULL,                         /* exit */
                                                          rtThreadCtxHookSolFree);
        }
        AssertMsg(rc, ("removectx() failed. rc=%d\n", rc));
        NOREF(rc);

#if 0 /*def RT_STRICT - access after free */
        cRefs = ASMAtomicReadU32(&pThis->cRefs);
        Assert(!cRefs);
#endif
        cRefs = 0;
    }
    else if (!cRefs)
    {
        /*
         * The ring-0 thread for this hook object has already died. Free up the object as we have no more references.
         */
        Assert(pThis->hOwner != RTThreadNativeSelf());
        ASMAtomicWriteU32(&pThis->u32Magic, ~RTTHREADCTXHOOKINT_MAGIC);
        RTMemFree(pThis);
    }

    return cRefs;
}


RTDECL(int) RTThreadCtxHookEnable(RTTHREADCTXHOOK hCtxHook)
{
    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    AssertPtr(pThis);
    AssertMsgReturn(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis),
                    VERR_INVALID_HANDLE);
    Assert(pThis->hOwner == RTThreadNativeSelf());
    Assert(!pThis->fEnabled);

    /*
     * Mark it as enabled.
     */
    pThis->fEnabled = true;

    return VINF_SUCCESS;
}


RTDECL(int) RTThreadCtxHookDisable(RTTHREADCTXHOOK hCtxHook)
{
    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    if (pThis != NIL_RTTHREADCTXHOOK)
    {
        AssertPtr(pThis);
        AssertMsgReturn(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis),
                        VERR_INVALID_HANDLE);
        Assert(pThis->hOwner == RTThreadNativeSelf());

        /*
         * Mark it as disabled.
         */
        pThis->fEnabled = false;
    }

    return VINF_SUCCESS;
}


RTDECL(bool) RTThreadCtxHookIsEnabled(RTTHREADCTXHOOK hCtxHook)
{
    /*
     * Validate input.
     */
    PRTTHREADCTXHOOKINT pThis = hCtxHook;
    if (pThis == NIL_RTTHREADCTXHOOK)
        return false;
    AssertPtr(pThis);
    AssertMsgReturn(pThis->u32Magic == RTTHREADCTXHOOKINT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis),
                    false);

    return pThis->fEnabled;
}

