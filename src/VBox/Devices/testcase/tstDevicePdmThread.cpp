/* $Id: tstDevicePdmThread.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM Thread - Thread Management (copied from PDMThread).
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
/// @todo \#define LOG_GROUP LOG_GROUP_PDM_THREAD
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/thread.h>

#include "tstDeviceInternal.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) pdmR3ThreadMain(RTTHREAD Thread, void *pvUser);


/**
 * Wrapper around ASMAtomicCmpXchgSize.
 */
DECLINLINE(bool) pdmR3AtomicCmpXchgState(PPDMTHREAD pThread, PDMTHREADSTATE enmNewState, PDMTHREADSTATE enmOldState)
{
    bool fRc;
    ASMAtomicCmpXchgSize(&pThread->enmState, enmNewState, enmOldState, fRc);
    return fRc;
}


/**
 * Does the wakeup call.
 *
 * @returns VBox status code. Already asserted on failure.
 * @param   pThread     The PDM thread.
 */
static DECLCALLBACK(int) pdmR3ThreadWakeUp(PPDMTHREAD pThread)
{
    RTSemEventMultiSignal(pThread->Internal.s.SleepEvent);

    int rc;
    switch (pThread->Internal.s.enmType)
    {
        case PDMTHREADTYPE_DEVICE:
            rc = pThread->u.Dev.pfnWakeUp(pThread->u.Dev.pDevIns, pThread);
            break;

        case PDMTHREADTYPE_USB:
            rc = pThread->u.Usb.pfnWakeUp(pThread->u.Usb.pUsbIns, pThread);
            break;

        case PDMTHREADTYPE_DRIVER:
            rc = pThread->u.Drv.pfnWakeUp(pThread->u.Drv.pDrvIns, pThread);
            break;

        case PDMTHREADTYPE_INTERNAL:
            rc = pThread->u.Int.pfnWakeUp(NULL /*pVM*/, pThread);
            break;

        case PDMTHREADTYPE_EXTERNAL:
            rc = pThread->u.Ext.pfnWakeUp(pThread);
            break;

        default:
            AssertMsgFailed(("%d\n", pThread->Internal.s.enmType));
            rc = VERR_PDM_THREAD_IPE_1;
            break;
    }
    AssertRC(rc);
    return rc;
}


/**
 * Allocates new thread instance.
 *
 * @returns VBox status code.
 * @param   pDut        The device under test.
 * @param   ppThread    Where to store the pointer to the instance.
 */
static int pdmR3ThreadNew(PTSTDEVDUTINT pDut, PPPDMTHREAD ppThread)
{
    PPDMTHREAD pThread = (PPDMTHREAD)RTMemAllocZ(sizeof(*pThread));
    if (!pThread)
        return VERR_NO_MEMORY;

    pThread->u32Version      = PDMTHREAD_VERSION;
    pThread->enmState        = PDMTHREADSTATE_INITIALIZING;
    pThread->Thread          = NIL_RTTHREAD;
    pThread->Internal.s.pDut = pDut;

    *ppThread = pThread;
    return VINF_SUCCESS;
}



/**
 * Initialize a new thread, this actually creates the thread.
 *
 * @returns VBox status code.
 * @param   pDut        The device under test.
 * @param   ppThread    Where the thread instance data handle is.
 * @param   cbStack     The stack size, see RTThreadCreate().
 * @param   enmType     The thread type, see RTThreadCreate().
 * @param   pszName     The thread name, see RTThreadCreate().
 */
static int pdmR3ThreadInit(PTSTDEVDUTINT pDut, PPPDMTHREAD ppThread, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    PPDMTHREAD  pThread = *ppThread;

    int rc = RTSemEventMultiCreate(&pThread->Internal.s.BlockEvent);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventMultiCreate(&pThread->Internal.s.SleepEvent);
        if (RT_SUCCESS(rc))
        {
            /*
             * Create the thread and wait for it to initialize.
             * The newly created thread will set the PDMTHREAD::Thread member.
             */
            RTTHREAD Thread;
            rc = RTThreadCreate(&Thread, pdmR3ThreadMain, pThread, cbStack, enmType, RTTHREADFLAGS_WAITABLE, pszName);
            if (RT_SUCCESS(rc))
            {
                rc = RTThreadUserWait(Thread, 60*1000);
                if (    RT_SUCCESS(rc)
                    &&  pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                    rc = VERR_PDM_THREAD_IPE_2;
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Insert it into the thread list.
                     */
                    tstDevDutLockExcl(pDut);
                    RTListAppend(&pDut->LstPdmThreads, &pThread->Internal.s.NdPdmThrds);
                    tstDevDutUnlockExcl(pDut);

                    rc = RTThreadUserReset(Thread);
                    AssertRC(rc);
                    return rc;
                }

                /* bailout */
                RTThreadWait(Thread, 60*1000, NULL);
            }
            RTSemEventMultiDestroy(pThread->Internal.s.SleepEvent);
            pThread->Internal.s.SleepEvent = NIL_RTSEMEVENTMULTI;
        }
        RTSemEventMultiDestroy(pThread->Internal.s.BlockEvent);
        pThread->Internal.s.BlockEvent = NIL_RTSEMEVENTMULTI;
    }
    RTMemFree(pThread);
    *ppThread = NULL;

    return rc;
}


/**
 * Device Helper for creating a thread associated with a device.
 *
 * @returns VBox status code.
 * @param   pDut        The device under test.
 * @param   pDevIns     The device instance.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeUp   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadCreateDevice(PTSTDEVDUTINT pDut, PPDMDEVINS pDevIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDEV pfnThread,
                                              PFNPDMTHREADWAKEUPDEV pfnWakeUp, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pDut, ppThread);
    if (RT_SUCCESS(rc))
    {
        PPDMTHREAD pThread = *ppThread;
        pThread->pvUser = pvUser;
        pThread->Internal.s.enmType = PDMTHREADTYPE_DEVICE;
        pThread->u.Dev.pDevIns = pDevIns;
        pThread->u.Dev.pfnThread = pfnThread;
        pThread->u.Dev.pfnWakeUp = pfnWakeUp;
        rc = pdmR3ThreadInit(pDut, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * USB Device Helper for creating a thread associated with an USB device.
 *
 * @returns VBox status code.
 * @param   pDut        The device under test.
 * @param   pUsbIns     The USB device instance.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeUp   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadCreateUsb(PTSTDEVDUTINT pDut, PPDMUSBINS pUsbIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADUSB pfnThread,
                                           PFNPDMTHREADWAKEUPUSB pfnWakeUp, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pDut, ppThread);
    if (RT_SUCCESS(rc))
    {
        PPDMTHREAD pThread = *ppThread;
        pThread->pvUser = pvUser;
        pThread->Internal.s.enmType = PDMTHREADTYPE_USB;
        pThread->u.Usb.pUsbIns = pUsbIns;
        pThread->u.Usb.pfnThread = pfnThread;
        pThread->u.Usb.pfnWakeUp = pfnWakeUp;
        rc = pdmR3ThreadInit(pDut, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Driver Helper for creating a thread associated with a driver.
 *
 * @returns VBox status code.
 * @param   pDut        The device under test.
 * @param   pDrvIns     The driver instance.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeUp   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadCreateDriver(PTSTDEVDUTINT pDut, PPDMDRVINS pDrvIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADDRV pfnThread,
                                              PFNPDMTHREADWAKEUPDRV pfnWakeUp, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pDut, ppThread);
    if (RT_SUCCESS(rc))
    {
        PPDMTHREAD pThread = *ppThread;
        pThread->pvUser = pvUser;
        pThread->Internal.s.enmType = PDMTHREADTYPE_DRIVER;
        pThread->u.Drv.pDrvIns = pDrvIns;
        pThread->u.Drv.pfnThread = pfnThread;
        pThread->u.Drv.pfnWakeUp = pfnWakeUp;
        rc = pdmR3ThreadInit(pDut, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Creates a PDM thread for internal use in the VM.
 *
 * @returns VBox status code.
 * @param   pDut        The device under test.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeUp   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadCreate(PTSTDEVDUTINT pDut, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADINT pfnThread,
                                        PFNPDMTHREADWAKEUPINT pfnWakeUp, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pDut, ppThread);
    if (RT_SUCCESS(rc))
    {
        PPDMTHREAD pThread = *ppThread;
        pThread->pvUser = pvUser;
        pThread->Internal.s.enmType = PDMTHREADTYPE_INTERNAL;
        pThread->u.Int.pfnThread = pfnThread;
        pThread->u.Int.pfnWakeUp = pfnWakeUp;
        rc = pdmR3ThreadInit(pDut, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Creates a PDM thread for VM use by some external party.
 *
 * @returns VBox status code.
 * @param   pDut        The device under test.
 * @param   ppThread    Where to store the thread 'handle'.
 * @param   pvUser      The user argument to the thread function.
 * @param   pfnThread   The thread function.
 * @param   pfnWakeUp   The wakup callback. This is called on the EMT thread when
 *                      a state change is pending.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   pszName     See RTThreadCreate.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadCreateExternal(PTSTDEVDUTINT pDut, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADEXT pfnThread,
                                                PFNPDMTHREADWAKEUPEXT pfnWakeUp, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    int rc = pdmR3ThreadNew(pDut, ppThread);
    if (RT_SUCCESS(rc))
    {
        PPDMTHREAD pThread = *ppThread;
        pThread->pvUser = pvUser;
        pThread->Internal.s.enmType = PDMTHREADTYPE_EXTERNAL;
        pThread->u.Ext.pfnThread = pfnThread;
        pThread->u.Ext.pfnWakeUp = pfnWakeUp;
        rc = pdmR3ThreadInit(pDut, ppThread, cbStack, enmType, pszName);
    }
    return rc;
}


/**
 * Destroys a PDM thread.
 *
 * This will wakeup the thread, tell it to terminate, and wait for it terminate.
 *
 * @returns VBox status code.
 *          This reflects the success off destroying the thread and not the exit code
 *          of the thread as this is stored in *pRcThread.
 * @param   pThread         The thread to destroy.
 * @param   pRcThread       Where to store the thread exit code. Optional.
 * @thread  The emulation thread (EMT).
 */
DECLHIDDEN(int) tstDevPdmR3ThreadDestroy(PPDMTHREAD pThread, int *pRcThread)
{
    /*
     * Assert sanity.
     */
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread != RTThreadSelf());
    AssertPtrNullReturn(pRcThread, VERR_INVALID_POINTER);

    PTSTDEVDUTINT pDut = pThread->Internal.s.pDut;

    /*
     * Advance the thread to the terminating state.
     */
    int rc = VINF_SUCCESS;
    if (pThread->enmState <= PDMTHREADSTATE_TERMINATING)
    {
        for (;;)
        {
            PDMTHREADSTATE enmState = pThread->enmState;
            switch (enmState)
            {
                case PDMTHREADSTATE_RUNNING:
                    if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                        continue;
                    rc = pdmR3ThreadWakeUp(pThread);
                    break;

                case PDMTHREADSTATE_SUSPENDED:
                case PDMTHREADSTATE_SUSPENDING:
                case PDMTHREADSTATE_RESUMING:
                case PDMTHREADSTATE_INITIALIZING:
                    if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                        continue;
                    break;

                case PDMTHREADSTATE_TERMINATING:
                case PDMTHREADSTATE_TERMINATED:
                    break;

                default:
                    AssertMsgFailed(("enmState=%d\n", enmState));
                    rc = VERR_PDM_THREAD_IPE_2;
                    break;
            }
            break;
        }
    }
    int rc2 = RTSemEventMultiSignal(pThread->Internal.s.BlockEvent);
    AssertRC(rc2);

    /*
     * Wait for it to terminate and the do cleanups.
     */
    rc2 = RTThreadWait(pThread->Thread, RT_SUCCESS(rc) ? 60*1000 : 150, pRcThread);
    if (RT_SUCCESS(rc2))
    {
        /* make it invalid. */
        pThread->u32Version = 0xffffffff;
        pThread->enmState = PDMTHREADSTATE_INVALID;
        pThread->Thread = NIL_RTTHREAD;

        /* unlink */
        tstDevDutLockExcl(pDut);
        RTListNodeRemove(&pThread->Internal.s.NdPdmThrds);
        tstDevDutUnlockExcl(pDut);

        /* free the resources */
        RTSemEventMultiDestroy(pThread->Internal.s.BlockEvent);
        pThread->Internal.s.BlockEvent = NIL_RTSEMEVENTMULTI;

        RTSemEventMultiDestroy(pThread->Internal.s.SleepEvent);
        pThread->Internal.s.SleepEvent = NIL_RTSEMEVENTMULTI;

        RTMemFree(pThread);
    }
    else if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}


/**
 * Destroys all threads associated with a device.
 *
 * This function is called by PDMDevice when a device is
 * destroyed (not currently implemented).
 *
 * @returns VBox status code of the first failure.
 * @param   pDut        The device under test.
 * @param   pDevIns     the device instance.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadDestroyDevice(PTSTDEVDUTINT pDut, PPDMDEVINS pDevIns)
{
    int rc = VINF_SUCCESS;

    AssertPtr(pDevIns);

    tstDevDutLockExcl(pDut);
#if 0
    PPDMTHREAD pThread = pUVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        if (    pThread->Internal.s.enmType == PDMTHREADTYPE_DEVICE
            &&  pThread->u.Dev.pDevIns == pDevIns)
        {
            int rc2 = PDMR3ThreadDestroy(pThread, NULL);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        pThread = pNext;
    }
#endif
    tstDevDutUnlockExcl(pDut);
    return rc;
}


/**
 * Destroys all threads associated with an USB device.
 *
 * This function is called by PDMUsb when a device is destroyed.
 *
 * @returns VBox status code of the first failure.
 * @param   pDut        The device under test.
 * @param   pUsbIns     The USB device instance.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadDestroyUsb(PTSTDEVDUTINT pDut, PPDMUSBINS pUsbIns)
{
    int rc = VINF_SUCCESS;

    AssertPtr(pUsbIns);

    tstDevDutLockExcl(pDut);
#if 0
    PPDMTHREAD pThread = pUVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        if (    pThread->Internal.s.enmType == PDMTHREADTYPE_DEVICE
            &&  pThread->u.Usb.pUsbIns == pUsbIns)
        {
            int rc2 = PDMR3ThreadDestroy(pThread, NULL);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        pThread = pNext;
    }
#endif
    tstDevDutUnlockExcl(pDut);
    return rc;
}


/**
 * Destroys all threads associated with a driver.
 *
 * This function is called by PDMDriver when a driver is destroyed.
 *
 * @returns VBox status code of the first failure.
 * @param   pDut        The device under test.
 * @param   pDrvIns     The driver instance.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadDestroyDriver(PTSTDEVDUTINT pDut, PPDMDRVINS pDrvIns)
{
    int rc = VINF_SUCCESS;

    AssertPtr(pDrvIns);

    tstDevDutLockExcl(pDut);
#if 0
    PPDMTHREAD pThread = pUVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        if (    pThread->Internal.s.enmType == PDMTHREADTYPE_DRIVER
            &&  pThread->u.Drv.pDrvIns == pDrvIns)
        {
            int rc2 = PDMR3ThreadDestroy(pThread, NULL);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        pThread = pNext;
    }
#endif
    tstDevDutUnlockExcl(pDut);
    return rc;
}


/**
 * Called For VM power off.
 *
 * @param   pDut        The device under test.
 */
DECLHIDDEN(void) tstDevPdmR3ThreadDestroyAll(PTSTDEVDUTINT pDut)
{
    tstDevDutLockExcl(pDut);
#if 0
    PPDMTHREAD pThread = pUVM->pdm.s.pThreads;
    while (pThread)
    {
        PPDMTHREAD pNext = pThread->Internal.s.pNext;
        int rc2 = PDMR3ThreadDestroy(pThread, NULL);
        AssertRC(rc2);
        pThread = pNext;
    }
    Assert(!pUVM->pdm.s.pThreads && !pUVM->pdm.s.pThreadsTail);
#endif
    tstDevDutUnlockExcl(pDut);
}


/**
 * Initiate termination of the thread (self) because something failed in a bad way.
 *
 * @param   pThread         The PDM thread.
 */
static void pdmR3ThreadBailMeOut(PPDMTHREAD pThread)
{
    for (;;)
    {
        PDMTHREADSTATE enmState = pThread->enmState;
        switch (enmState)
        {
            case PDMTHREADSTATE_SUSPENDING:
            case PDMTHREADSTATE_SUSPENDED:
            case PDMTHREADSTATE_RESUMING:
            case PDMTHREADSTATE_RUNNING:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                break;

            case PDMTHREADSTATE_TERMINATING:
            case PDMTHREADSTATE_TERMINATED:
                break;

            case PDMTHREADSTATE_INITIALIZING:
            default:
                AssertMsgFailed(("enmState=%d\n", enmState));
                break;
        }
        break;
    }
}


/**
 * Called by the PDM thread in response to a wakeup call with
 * suspending as the new state.
 *
 * The thread will block in side this call until the state is changed in
 * response to a VM state change or to the device/driver/whatever calling the
 * PDMR3ThreadResume API.
 *
 * @returns VBox status code.
 *          On failure, terminate the thread.
 * @param   pThread     The PDM thread.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadIAmSuspending(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    AssertPtr(pThread);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread == RTThreadSelf() || pThread->enmState == PDMTHREADSTATE_INITIALIZING);
    PDMTHREADSTATE enmState = pThread->enmState;
    Assert(     enmState == PDMTHREADSTATE_SUSPENDING
           ||   enmState == PDMTHREADSTATE_INITIALIZING);

    /*
     * Update the state, notify the control thread (the API caller) and go to sleep.
     */
    int rc = VERR_WRONG_ORDER;
    if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_SUSPENDED, enmState))
    {
        rc = RTThreadUserSignal(pThread->Thread);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventMultiWait(pThread->Internal.s.BlockEvent, RT_INDEFINITE_WAIT);
            if (    RT_SUCCESS(rc)
                &&  pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                return rc;

            if (RT_SUCCESS(rc))
                rc = VERR_PDM_THREAD_IPE_2;
        }
    }

    AssertMsgFailed(("rc=%d enmState=%d\n", rc, pThread->enmState));
    pdmR3ThreadBailMeOut(pThread);
    return rc;
}


/**
 * Called by the PDM thread in response to a resuming state.
 *
 * The purpose of this API is to tell the PDMR3ThreadResume caller that
 * the PDM thread has successfully resumed. It will also do the
 * state transition from the resuming to the running state.
 *
 * @returns VBox status code.
 *          On failure, terminate the thread.
 * @param   pThread     The PDM thread.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadIAmRunning(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    Assert(pThread->enmState == PDMTHREADSTATE_RESUMING);
    Assert(pThread->Thread == RTThreadSelf());

    /*
     * Update the state and tell the control thread (the guy calling the resume API).
     */
    int rc = VERR_WRONG_ORDER;
    if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_RUNNING, PDMTHREADSTATE_RESUMING))
    {
        rc = RTThreadUserSignal(pThread->Thread);
        if (RT_SUCCESS(rc))
            return rc;
    }

    AssertMsgFailed(("rc=%d enmState=%d\n", rc, pThread->enmState));
    pdmR3ThreadBailMeOut(pThread);
    return rc;
}


/**
 * Called by the PDM thread instead of RTThreadSleep.
 *
 * The difference is that the sleep will be interrupted on state change. The
 * thread must be in the running state, otherwise it will return immediately.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success or state change.
 * @retval  VERR_INTERRUPTED on signal or APC.
 *
 * @param   pThread     The PDM thread.
 * @param   cMillies    The number of milliseconds to sleep.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadSleep(PPDMTHREAD pThread, RTMSINTERVAL cMillies)
{
    /*
     * Assert sanity.
     */
    AssertReturn(pThread->enmState > PDMTHREADSTATE_INVALID && pThread->enmState < PDMTHREADSTATE_TERMINATED, VERR_PDM_THREAD_IPE_2);
    AssertReturn(pThread->Thread == RTThreadSelf(), VERR_PDM_THREAD_INVALID_CALLER);

    /*
     * Reset the event semaphore, check the state and sleep.
     */
    RTSemEventMultiReset(pThread->Internal.s.SleepEvent);
    if (pThread->enmState != PDMTHREADSTATE_RUNNING)
        return VINF_SUCCESS;
    return RTSemEventMultiWaitNoResume(pThread->Internal.s.SleepEvent, cMillies);
}


/**
 * The PDM thread function.
 *
 * @returns return from pfnThread.
 *
 * @param   Thread  The thread handle.
 * @param   pvUser  Pointer to the PDMTHREAD structure.
 */
static DECLCALLBACK(int) pdmR3ThreadMain(RTTHREAD Thread, void *pvUser)
{
    PPDMTHREAD pThread = (PPDMTHREAD)pvUser;
    Log(("PDMThread: Initializing thread %RTthrd / %p / '%s'...\n", Thread, pThread, RTThreadGetName(Thread)));
    pThread->Thread = Thread;

    /*
     * The run loop.
     *
     * It handles simple thread functions which returns when they see a suspending
     * request and leaves the PDMR3ThreadIAmSuspending and PDMR3ThreadIAmRunning
     * parts to us.
     */
    int rc;
    for (;;)
    {
        switch (pThread->Internal.s.enmType)
        {
            case PDMTHREADTYPE_DEVICE:
                rc = pThread->u.Dev.pfnThread(pThread->u.Dev.pDevIns, pThread);
                break;

            case PDMTHREADTYPE_USB:
                rc = pThread->u.Usb.pfnThread(pThread->u.Usb.pUsbIns, pThread);
                break;

            case PDMTHREADTYPE_DRIVER:
                rc = pThread->u.Drv.pfnThread(pThread->u.Drv.pDrvIns, pThread);
                break;

            case PDMTHREADTYPE_INTERNAL:
                rc = pThread->u.Int.pfnThread(NULL /*pVM*/, pThread);
                break;

            case PDMTHREADTYPE_EXTERNAL:
                rc = pThread->u.Ext.pfnThread(pThread);
                break;

            default:
                AssertMsgFailed(("%d\n", pThread->Internal.s.enmType));
                rc = VERR_PDM_THREAD_IPE_1;
                break;
        }
        if (RT_FAILURE(rc))
            break;

        /*
         * If this is a simple thread function, the state will be suspending
         * or initializing now. If it isn't we're supposed to terminate.
         */
        if (    pThread->enmState != PDMTHREADSTATE_SUSPENDING
            &&  pThread->enmState != PDMTHREADSTATE_INITIALIZING)
        {
            Assert(pThread->enmState == PDMTHREADSTATE_TERMINATING);
            break;
        }
        rc = tstDevPdmR3ThreadIAmSuspending(pThread);
        if (RT_FAILURE(rc))
            break;
        if (pThread->enmState != PDMTHREADSTATE_RESUMING)
        {
            Assert(pThread->enmState == PDMTHREADSTATE_TERMINATING);
            break;
        }

        rc = tstDevPdmR3ThreadIAmRunning(pThread);
        if (RT_FAILURE(rc))
            break;
    }

    if (RT_FAILURE(rc))
        LogRel(("PDMThread: Thread '%s' (%RTthrd) quit unexpectedly with rc=%Rrc.\n", RTThreadGetName(Thread), Thread, rc));

    /*
     * Advance the state to terminating and then on to terminated.
     */
    for (;;)
    {
        PDMTHREADSTATE enmState = pThread->enmState;
        if (    enmState == PDMTHREADSTATE_TERMINATING
            ||  pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
            break;
    }

    ASMAtomicXchgSize(&pThread->enmState, PDMTHREADSTATE_TERMINATED);
    int rc2 = RTThreadUserSignal(Thread); AssertRC(rc2);

    Log(("PDMThread: Terminating thread %RTthrd / %p / '%s': %Rrc\n", Thread, pThread, RTThreadGetName(Thread), rc));
    return rc;
}


/**
 * Initiate termination of the thread because something failed in a bad way.
 *
 * @param   pThread         The PDM thread.
 */
static void pdmR3ThreadBailOut(PPDMTHREAD pThread)
{
    for (;;)
    {
        PDMTHREADSTATE enmState = pThread->enmState;
        switch (enmState)
        {
            case PDMTHREADSTATE_SUSPENDING:
            case PDMTHREADSTATE_SUSPENDED:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                RTSemEventMultiSignal(pThread->Internal.s.BlockEvent);
                break;

            case PDMTHREADSTATE_RESUMING:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                break;

            case PDMTHREADSTATE_RUNNING:
                if (!pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_TERMINATING, enmState))
                    continue;
                pdmR3ThreadWakeUp(pThread);
                break;

            case PDMTHREADSTATE_TERMINATING:
            case PDMTHREADSTATE_TERMINATED:
                break;

            case PDMTHREADSTATE_INITIALIZING:
            default:
                AssertMsgFailed(("enmState=%d\n", enmState));
                break;
        }
        break;
    }
}


/**
 * Suspends the thread.
 *
 * This can be called at the power off / suspend notifications to suspend the
 * PDM thread a bit early. The thread will be automatically suspend upon
 * completion of the device/driver notification cycle.
 *
 * The caller is responsible for serializing the control operations on the
 * thread. That basically means, always do these calls from the EMT.
 *
 * @returns VBox status code.
 * @param   pThread     The PDM thread.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadSuspend(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread != RTThreadSelf());

    /*
     * This is a noop if the thread is already suspended.
     */
    if (pThread->enmState == PDMTHREADSTATE_SUSPENDED)
        return VINF_SUCCESS;

    /*
     * Change the state to resuming and kick the thread.
     */
    int rc = RTSemEventMultiReset(pThread->Internal.s.BlockEvent);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadUserReset(pThread->Thread);
        if (RT_SUCCESS(rc))
        {
            rc = VERR_WRONG_ORDER;
            if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_SUSPENDING, PDMTHREADSTATE_RUNNING))
            {
                rc = pdmR3ThreadWakeUp(pThread);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Wait for the thread to reach the suspended state.
                     */
                    if (pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                        rc = RTThreadUserWait(pThread->Thread, 60*1000);
                    if (    RT_SUCCESS(rc)
                        &&  pThread->enmState != PDMTHREADSTATE_SUSPENDED)
                        rc = VERR_PDM_THREAD_IPE_2;
                    if (RT_SUCCESS(rc))
                        return rc;
                }
            }
        }
    }

    /*
     * Something failed, initialize termination.
     */
    AssertMsgFailed(("PDMR3ThreadSuspend -> rc=%Rrc enmState=%d suspending '%s'\n",
                     rc, pThread->enmState, RTThreadGetName(pThread->Thread)));
    pdmR3ThreadBailOut(pThread);
    return rc;
}


/**
 * Resumes the thread.
 *
 * This can be called the power on / resume notifications to resume the
 * PDM thread a bit early. The thread will be automatically resumed upon
 * return from these two notification callbacks (devices/drivers).
 *
 * The caller is responsible for serializing the control operations on the
 * thread. That basically means, always do these calls from the EMT.
 *
 * @returns VBox status code.
 * @param   pThread     The PDM thread.
 */
DECLHIDDEN(int) tstDevPdmR3ThreadResume(PPDMTHREAD pThread)
{
    /*
     * Assert sanity.
     */
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertReturn(pThread->u32Version == PDMTHREAD_VERSION, VERR_INVALID_MAGIC);
    Assert(pThread->Thread != RTThreadSelf());

    /*
     * Change the state to resuming and kick the thread.
     */
    int rc = RTThreadUserReset(pThread->Thread);
    if (RT_SUCCESS(rc))
    {
        rc = VERR_WRONG_ORDER;
        if (pdmR3AtomicCmpXchgState(pThread, PDMTHREADSTATE_RESUMING, PDMTHREADSTATE_SUSPENDED))
        {
            rc = RTSemEventMultiSignal(pThread->Internal.s.BlockEvent);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Wait for the thread to reach the running state.
                 */
                rc = RTThreadUserWait(pThread->Thread, 60*1000);
                if (    RT_SUCCESS(rc)
                    &&  pThread->enmState != PDMTHREADSTATE_RUNNING)
                    rc = VERR_PDM_THREAD_IPE_2;
                if (RT_SUCCESS(rc))
                    return rc;
            }
        }
    }

    /*
     * Something failed, initialize termination.
     */
    AssertMsgFailed(("PDMR3ThreadResume -> rc=%Rrc enmState=%d\n", rc, pThread->enmState));
    pdmR3ThreadBailOut(pThread);
    return rc;
}

