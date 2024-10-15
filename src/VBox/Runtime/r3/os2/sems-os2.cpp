/* $Id: sems-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Semaphores, OS/2.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define INCL_DOSSEMAPHORES
#define INCL_ERRORS
#include <os2.h>
#undef RT_MAX

#include <iprt/semaphore.h>
#include <iprt/assert.h>
#include <iprt/err.h>


/** Converts semaphore to OS/2 handle. */
#define SEM2HND(Sem) ((LHANDLE)(uintptr_t)Sem)



RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));

    /*
     * Create the semaphore.
     * (Auto reset, not signaled, private event object.)
     */
    HEV hev;
    int rc = DosCreateEventSem(NULL, &hev, DCE_AUTORESET | DCE_POSTONE, 0);
    if (!rc)
    {
        *phEventSem = (RTSEMEVENT)(void *)hev;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromOS2(rc);
}


RTDECL(int)   RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    if (hEventSem == NIL_RTSEMEVENT)
        return VINF_SUCCESS;

    /*
     * Close semaphore handle.
     */
    int rc = DosCloseEventSem(SEM2HND(hEventSem));
    if (!rc)
        return VINF_SUCCESS;
    AssertMsgFailed(("Destroy hEventSem %p failed, rc=%d\n", hEventSem, rc));
    return RTErrConvertFromOS2(rc);
}


RTDECL(int)   RTSemEventWaitNoResume(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies)
{
    /*
     * Wait for condition.
     */
    int rc = DosWaitEventSem(SEM2HND(hEventSem), cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies);
    switch (rc)
    {
        case NO_ERROR:              return VINF_SUCCESS;
        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:         return VERR_TIMEOUT;
        case ERROR_INTERRUPT:       return VERR_INTERRUPTED;
        default:
        {
            AssertMsgFailed(("Wait on hEventSem %p failed, rc=%d\n", hEventSem, rc));
            return RTErrConvertFromOS2(rc);
        }
    }
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    /*
     * Signal the object.
     */
    int rc = DosPostEventSem(SEM2HND(hEventSem));
    switch (rc)
    {
        case NO_ERROR:
        case ERROR_ALREADY_POSTED:
        case ERROR_TOO_MANY_POSTS:
            return VINF_SUCCESS;
        default:
            return RTErrConvertFromOS2(rc);
    }
}


RTDECL(void) RTSemEventSetSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
/** @todo implement RTSemEventSetSignaller and friends for OS/2 */
}


RTDECL(void) RTSemEventAddSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{

}


RTDECL(void) RTSemEventRemoveSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{

}




RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Create the semaphore.
     * (Manual reset, not signaled, private event object.)
     */
    HEV hev;
    int rc = DosCreateEventSem(NULL, &hev, 0, FALSE);
    if (!rc)
    {
        *phEventMultiSem = (RTSEMEVENTMULTI)(void *)hev;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromOS2(rc);
}


RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    if (hEventMultiSem == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;

    /*
     * Close semaphore handle.
     */
    int rc = DosCloseEventSem(SEM2HND(hEventMultiSem));
    if (!rc)
        return VINF_SUCCESS;
    AssertMsgFailed(("Destroy hEventMultiSem %p failed, rc=%d\n", hEventMultiSem, rc));
    return RTErrConvertFromOS2(rc);
}


RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Signal the object.
     */
    int rc = DosPostEventSem(SEM2HND(hEventMultiSem));
    switch (rc)
    {
        case NO_ERROR:
        case ERROR_ALREADY_POSTED:
        case ERROR_TOO_MANY_POSTS:
            return VINF_SUCCESS;
        default:
            return RTErrConvertFromOS2(rc);
    }
}


RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Reset the object.
     */
    ULONG ulIgnore;
    int rc = DosResetEventSem(SEM2HND(hEventMultiSem), &ulIgnore);
    switch (rc)
    {
        case NO_ERROR:
        case ERROR_ALREADY_RESET:
            return VINF_SUCCESS;
        default:
            return RTErrConvertFromOS2(rc);
    }
}


RTDECL(int)  RTSemEventMultiWaitNoResume(RTSEMEVENTMULTI hEventMultiSem, RTMSINTERVAL cMillies)
{
    /*
     * Wait for condition.
     */
    int rc = DosWaitEventSem(SEM2HND(hEventMultiSem), cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies);
    switch (rc)
    {
        case NO_ERROR:              return VINF_SUCCESS;
        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:         return VERR_TIMEOUT;
        case ERROR_INTERRUPT:       return VERR_INTERRUPTED;
        default:
        {
            AssertMsgFailed(("Wait on hEventMultiSem %p failed, rc=%d\n", hEventMultiSem, rc));
            return RTErrConvertFromOS2(rc);
        }
    }
}


RTDECL(void) RTSemEventMultiSetSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
    /** @todo implement RTSemEventMultiSetSignaller on OS/2 */
}


RTDECL(void) RTSemEventMultiAddSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
}


RTDECL(void) RTSemEventMultiRemoveSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
}



#undef RTSemMutexCreate
RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    return RTSemMutexCreateEx(phMutexSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
}


RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMMUTEX_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Create the semaphore.
     */
    HMTX hmtx;
    int rc = DosCreateMutexSem(NULL, &hmtx, 0, FALSE);
    if (!rc)
    {
        /** @todo implement lock validation of OS/2 mutex semaphores. */
        *phMutexSem = (RTSEMMUTEX)(void *)hmtx;
        return VINF_SUCCESS;
    }

    return RTErrConvertFromOS2(rc);
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    if (hMutexSem == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;

    /*
     * Close semaphore handle.
     */
    int rc = DosCloseMutexSem(SEM2HND(hMutexSem));
    if (!rc)
        return VINF_SUCCESS;
    AssertMsgFailed(("Destroy hMutexSem %p failed, rc=%d\n", hMutexSem, rc));
    return RTErrConvertFromOS2(rc);
}



RTDECL(uint32_t) RTSemMutexSetSubClass(RTSEMMUTEX hMutexSem, uint32_t uSubClass)
{
#if 0 /** @todo def RTSEMMUTEX_STRICT */
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);

    return RTLockValidatorRecExclSetSubClass(&pThis->ValidatorRec, uSubClass);
#else
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}


#undef RTSemMutexRequestNoResume
RTDECL(int)  RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    /*
     * Lock mutex semaphore.
     */
    int rc = DosRequestMutexSem(SEM2HND(hMutexSem), cMillies == RT_INDEFINITE_WAIT ? SEM_INDEFINITE_WAIT : cMillies);
    switch (rc)
    {
        case NO_ERROR:              return VINF_SUCCESS;
        case ERROR_SEM_TIMEOUT:
        case ERROR_TIMEOUT:         return VERR_TIMEOUT;
        case ERROR_INTERRUPT:       return VERR_INTERRUPTED;
        case ERROR_SEM_OWNER_DIED:  return VERR_SEM_OWNER_DIED;
        default:
        {
            AssertMsgFailed(("Wait on hMutexSem %p failed, rc=%d\n", hMutexSem, rc));
            return RTErrConvertFromOS2(rc);
        }
    }
}

RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
//    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
//    return rtSemMutexRequestNoResume(hMutexSem, cMillies, &SrcPos);
    return RTSemMutexRequestNoResume(hMutexSem, cMillies);
}


RTDECL(int)  RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Unlock mutex semaphore.
     */
    int rc = DosReleaseMutexSem(SEM2HND(hMutexSem));
    if (!rc)
        return VINF_SUCCESS;
    AssertMsgFailed(("Release hMutexSem %p failed, rc=%d\n", hMutexSem, rc));
    return RTErrConvertFromOS2(rc);
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    /*
     * Unlock mutex semaphore.
     */
    PID     pid;
    TID     tid;
    ULONG   cRecursions;
    int rc = DosQueryMutexSem(SEM2HND(hMutexSem), &pid, &tid, &cRecursions);
    if (!rc)
        return cRecursions != 0;
    AssertMsgFailed(("DosQueryMutexSem %p failed, rc=%d\n", hMutexSem, rc));
    return rc == ERROR_SEM_OWNER_DIED;
}

