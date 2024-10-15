/* $Id: HBDMgmt-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: Host block device management API - darwin specifics.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_VD
#include <VBox/cdefs.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/once.h>
#include <iprt/semaphore.h>
#include <iprt/path.h>
#include <iprt/thread.h>

#include <DiskArbitration/DiskArbitration.h>

#include "HBDMgmt.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Claimed block device state.
 */
typedef struct HBDMGRDEV
{
    /** List node. */
    RTLISTNODE         ListNode;
    /** Handle to the DA Disk object. */
    DADiskRef          hDiskRef;
} HBDMGRDEV;
/** Pointer to a claimed block device. */
typedef HBDMGRDEV *PHBDMGRDEV;

/**
 * Internal Host block device manager state.
 */
typedef struct HBDMGRINT
{
    /** Session handle to the DiskArbitration daemon. */
    DASessionRef       hSessionRef;
    /** Runloop reference of the worker thread. */
    CFRunLoopRef       hRunLoopRef;
    /** Runloop source for waking up the worker thread. */
    CFRunLoopSourceRef hRunLoopSrcWakeRef;
    /** List of claimed block devices. */
    RTLISTANCHOR       ListClaimed;
    /** Fast mutex protecting the list. */
    RTSEMFASTMUTEX     hMtxList;
    /** Event sempahore to signal callback completion. */
    RTSEMEVENT         hEvtCallback;
    /** Thread processing DA events. */
    RTTHREAD           hThrdDAEvts;
    /** Flag whether the thread should keep running. */
    volatile bool      fRunning;
} HBDMGRINT;
/** Pointer to an interal block device manager state. */
typedef HBDMGRINT *PHBDMGRINT;

/**
 * Helper structure containing the arguments
 * for the claim/unmount callbacks.
 */
typedef struct HBDMGRDACLBKARGS
{
    /** Pointer to the block device manager. */
    PHBDMGRINT         pThis;
    /** The status code returned by the callback, after the operation completed. */
    DAReturn           rcDA;
    /** A detailed error string in case of an error, can be NULL.
     * Must be freed with RTStrFree(). */
    char              *pszErrDetail;
} HBDMGRDACLBKARGS;
typedef HBDMGRDACLBKARGS *PHBDMGRDACLBKARGS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Unclaims the given block device and frees its state removing it from the list.
 *
 * @param   pDev           The block device to unclaim.
 */
static void hbdMgrDevUnclaim(PHBDMGRDEV pDev)
{
    DADiskUnclaim(pDev->hDiskRef);
    CFRelease(pDev->hDiskRef);
    RTListNodeRemove(&pDev->ListNode);
    RTMemFree(pDev);
}

/**
 * Returns the block device given by the filename if claimed or NULL.
 *
 * @returns Pointer to the claimed block device or NULL if not claimed.
 * @param   pThis          The block device manager.
 * @param   pszFilename    The name to look for.
 */
static PHBDMGRDEV hbdMgrDevFindByName(PHBDMGRINT pThis, const char *pszFilename)
{
    bool fFound = false;
    const char *pszFilenameStripped = RTPathFilename(pszFilename);

    AssertPtrReturn(pszFilenameStripped, NULL);

    PHBDMGRDEV pIt;
    RTListForEach(&pThis->ListClaimed, pIt, HBDMGRDEV, ListNode)
    {
        const char *pszBSDName = DADiskGetBSDName(pIt->hDiskRef);
        if (!RTStrCmp(pszFilenameStripped, pszBSDName))
        {
            fFound = true;
            break;
        }
    }

    return fFound ? pIt : NULL;
}

/**
 * Converts a given DA return code to a VBox status code.
 *
 * @returns VBox status code.
 * @param   hReturn        The status code returned by a DA API call.
 */
static int hbdMgrDAReturn2VBoxStatus(DAReturn hReturn)
{
    int rc = VERR_UNRESOLVED_ERROR;

    switch (hReturn)
    {
        case kDAReturnBusy:
            rc = VERR_RESOURCE_BUSY;
            break;
        case kDAReturnNotMounted:
        case kDAReturnBadArgument:
            rc = VERR_INVALID_PARAMETER;
            break;
        case kDAReturnNotPermitted:
        case kDAReturnNotPrivileged:
        case kDAReturnExclusiveAccess:
            rc = VERR_ACCESS_DENIED;
            break;
        case kDAReturnNoResources:
            rc = VERR_NO_MEMORY;
            break;
        case kDAReturnNotFound:
            rc = VERR_NOT_FOUND;
            break;
        case kDAReturnNotReady:
            rc = VERR_TRY_AGAIN;
            break;
        case kDAReturnNotWritable:
            rc = VERR_WRITE_PROTECT;
            break;
        case kDAReturnUnsupported:
            rc = VERR_NOT_SUPPORTED;
            break;
        case kDAReturnError:
        default:
            rc = VERR_UNRESOLVED_ERROR;
    }

    return rc;
}

/**
 * Implements the OS X callback DADiskClaimCallback.
 *
 * This notifies us that the async DADiskClaim()/DADiskUnmount call has
 * completed.
 *
 * @param   hDiskRef         The disk that was attempted claimed / unmounted.
 * @param   hDissenterRef    NULL on success, contains details on failure.
 * @param   pvContext        Pointer to the return code variable.
 */
static void hbdMgrDACallbackComplete(DADiskRef hDiskRef, DADissenterRef hDissenterRef, void *pvContext)
{
    RT_NOREF(hDiskRef);
    PHBDMGRDACLBKARGS pArgs = (PHBDMGRDACLBKARGS)pvContext;
    pArgs->pszErrDetail = NULL;

    if (!hDissenterRef)
        pArgs->rcDA = kDAReturnSuccess;
    else
    {
        CFStringRef hStrErr = DADissenterGetStatusString(hDissenterRef);
        if (hStrErr)
        {
            const char *pszErrDetail = CFStringGetCStringPtr(hStrErr, kCFStringEncodingUTF8);
            if (pszErrDetail)
                pArgs->pszErrDetail = RTStrDup(pszErrDetail);
            CFRelease(hStrErr);
        }
        pArgs->rcDA = DADissenterGetStatus(hDissenterRef);

    }
    RTSemEventSignal(pArgs->pThis->hEvtCallback);
}

/**
 * Implements the OS X callback DADiskMountApprovalCallback.
 *
 * This notifies us about any attempt to mount a volume.  If we claimed the
 * volume or the complete disk containing the volume we will deny the attempt.
 *
 * @returns Reference to a DADissenter object which contains the result.
 * @param   hDiskRef         The disk that is about to be mounted.
 * @param   pvContext        Pointer to the block device manager.
 */
static DADissenterRef hbdMgrDAMountApprovalCallback(DADiskRef hDiskRef, void *pvContext)
{
    PHBDMGRINT pThis = (PHBDMGRINT)pvContext;
    DADiskRef hDiskParentRef = DADiskCopyWholeDisk(hDiskRef);
    const char *pszBSDName = DADiskGetBSDName(hDiskRef);
    const char *pszBSDNameParent = hDiskParentRef ? DADiskGetBSDName(hDiskParentRef) : NULL;
    DADissenterRef hDissenterRef = NULL;

    RTSemFastMutexRequest(pThis->hMtxList);
    PHBDMGRDEV pIt;
    RTListForEach(&pThis->ListClaimed, pIt, HBDMGRDEV, ListNode)
    {
        const char *pszBSDNameCur = DADiskGetBSDName(pIt->hDiskRef);
        /*
         * Prevent mounting any volume we have in use. This applies to the case
         * where we have the whole disk occupied but a single volume is about to be
         * mounted.
         */
        if (   !RTStrCmp(pszBSDNameCur, pszBSDName)
            || (   pszBSDNameParent
                && !RTStrCmp(pszBSDNameParent, pszBSDNameCur)))
        {
            CFStringRef hStrReason = CFStringCreateWithCString(kCFAllocatorDefault, "The disk is currently in use by VirtualBox and cannot be mounted", kCFStringEncodingUTF8);
            hDissenterRef = DADissenterCreate(kCFAllocatorDefault, kDAReturnExclusiveAccess, hStrReason);
            break;
        }
    }

    RTSemFastMutexRelease(pThis->hMtxList);

    if (hDiskParentRef)
        CFRelease(hDiskParentRef);
    return hDissenterRef;
}


/**
 * Implements OS X callback CFRunLoopSourceContext::perform.
 *
 * Dummy handler for the wakeup source to kick the worker thread.
 *
 * @param   pInfo            Opaque user data given during source creation, unused.
 */
static void hbdMgrDAPerformWakeup(void *pInfo)
{
    RT_NOREF(pInfo);
}


/**
 * Worker function of the thread processing messages from the Disk Arbitration daemon.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf      The thread handle.
 * @param   pvUser           Opaque user data, the block device manager instance.
 */
static DECLCALLBACK(int) hbdMgrDAWorker(RTTHREAD hThreadSelf, void *pvUser)
{
    PHBDMGRINT pThis = (PHBDMGRINT)pvUser;

    /* Provide the runloop reference. */
    pThis->hRunLoopRef = CFRunLoopGetCurrent();
    RTThreadUserSignal(hThreadSelf);

    /* Add the wake source to our runloop so we get notified about state changes. */
    CFRunLoopAddSource(pThis->hRunLoopRef, pThis->hRunLoopSrcWakeRef, kCFRunLoopCommonModes);

    /* Do what we are here for. */
    while (ASMAtomicReadBool(&pThis->fRunning))
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 10.0, true);
    }

    /* Remove the wakeup source form our runloop. */
    CFRunLoopRemoveSource(pThis->hRunLoopRef, pThis->hRunLoopSrcWakeRef, kCFRunLoopCommonModes);

    return VINF_SUCCESS;
}

DECLHIDDEN(int) HBDMgrCreate(PHBDMGR phHbdMgr)
{
    AssertPtrReturn(phHbdMgr, VERR_INVALID_POINTER);

    PHBDMGRINT pThis = (PHBDMGRINT)RTMemAllocZ(sizeof(HBDMGRINT));
    if (RT_UNLIKELY(!pThis))
        return VERR_NO_MEMORY;

    int rc = VINF_SUCCESS;
    RTListInit(&pThis->ListClaimed);
    pThis->fRunning = true;
    pThis->hSessionRef = DASessionCreate(kCFAllocatorDefault);
    if (pThis->hSessionRef)
    {
        rc = RTSemFastMutexCreate(&pThis->hMtxList);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventCreate(&pThis->hEvtCallback);
            if (RT_SUCCESS(rc))
            {
                CFRunLoopSourceContext CtxRunLoopSource;
                CtxRunLoopSource.version = 0;
                CtxRunLoopSource.info = NULL;
                CtxRunLoopSource.retain = NULL;
                CtxRunLoopSource.release = NULL;
                CtxRunLoopSource.copyDescription = NULL;
                CtxRunLoopSource.equal = NULL;
                CtxRunLoopSource.hash = NULL;
                CtxRunLoopSource.schedule = NULL;
                CtxRunLoopSource.cancel = NULL;
                CtxRunLoopSource.perform = hbdMgrDAPerformWakeup;
                pThis->hRunLoopSrcWakeRef = CFRunLoopSourceCreate(NULL, 0, &CtxRunLoopSource);
                if (CFRunLoopSourceIsValid(pThis->hRunLoopSrcWakeRef))
                {
                    rc = RTThreadCreate(&pThis->hThrdDAEvts, hbdMgrDAWorker, pThis, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "HbdDA-Wrk");
                    if (RT_SUCCESS(rc))
                    {
                        /* Wait for the thread to start up and provide the runloop reference. */
                        rc = RTThreadUserWait(pThis->hThrdDAEvts, RT_INDEFINITE_WAIT);
                        AssertRC(rc);
                        AssertPtr(pThis->hRunLoopRef);

                        DARegisterDiskMountApprovalCallback(pThis->hSessionRef, NULL, hbdMgrDAMountApprovalCallback, pThis);
                        DASessionScheduleWithRunLoop(pThis->hSessionRef, pThis->hRunLoopRef, kCFRunLoopDefaultMode);
                        *phHbdMgr = pThis;
                        return VINF_SUCCESS;
                    }
                    CFRelease(pThis->hRunLoopSrcWakeRef);
                }
            }

            RTSemFastMutexDestroy(pThis->hMtxList);
        }

        CFRelease(pThis->hSessionRef);
    }
    else
        rc = VERR_NO_MEMORY;

    RTMemFree(pThis);
    return rc;
}


DECLHIDDEN(void) HBDMgrDestroy(HBDMGR hHbdMgr)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturnVoid(pThis);

    /* Unregister the mount approval and DA session from the runloop. */
    DASessionUnscheduleFromRunLoop(pThis->hSessionRef, pThis->hRunLoopRef, kCFRunLoopDefaultMode);
    DAUnregisterApprovalCallback(pThis->hSessionRef, (void *)hbdMgrDAMountApprovalCallback, pThis);

    /* Kick the worker thread to exit. */
    ASMAtomicXchgBool(&pThis->fRunning, false);
    CFRunLoopSourceSignal(pThis->hRunLoopSrcWakeRef);
    CFRunLoopWakeUp(pThis->hRunLoopRef);
    int rcThrd = VINF_SUCCESS;
    int rc = RTThreadWait(pThis->hThrdDAEvts, RT_INDEFINITE_WAIT, &rcThrd);
    AssertRC(rc); AssertRC(rcThrd);

    CFRelease(pThis->hRunLoopSrcWakeRef);

    /* Go through all claimed block devices and release them. */
    RTSemFastMutexRequest(pThis->hMtxList);
    PHBDMGRDEV pIt, pItNext;
    RTListForEachSafe(&pThis->ListClaimed, pIt, pItNext, HBDMGRDEV, ListNode)
    {
        hbdMgrDevUnclaim(pIt);
    }
    RTSemFastMutexRelease(pThis->hMtxList);

    CFRelease(pThis->hSessionRef);
    RTSemFastMutexDestroy(pThis->hMtxList);
    RTSemEventDestroy(pThis->hEvtCallback);
    RTMemFree(pThis);
}


DECLHIDDEN(bool) HBDMgrIsBlockDevice(const char *pszFilename)
{
    bool fIsBlockDevice = RTStrNCmp(pszFilename, "/dev/disk", sizeof("/dev/disk") - 1) == 0 ? true : false;
    if (!fIsBlockDevice)
        fIsBlockDevice = RTStrNCmp(pszFilename, "/dev/rdisk", sizeof("/dev/rdisk") - 1) == 0 ? true : false;
    return fIsBlockDevice;
}


DECLHIDDEN(int) HBDMgrClaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(HBDMgrIsBlockDevice(pszFilename), VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    PHBDMGRDEV pDev = hbdMgrDevFindByName(pThis, pszFilename);
    if (!pDev)
    {
        DADiskRef hDiskRef = DADiskCreateFromBSDName(kCFAllocatorDefault, pThis->hSessionRef, pszFilename);
        if (hDiskRef)
        {
            HBDMGRDACLBKARGS CalllbackArgs;
            CalllbackArgs.pThis = pThis;
            CalllbackArgs.rcDA  = kDAReturnSuccess;

            /* Claim the device. */
            DADiskClaim(hDiskRef, kDADiskClaimOptionDefault, NULL, NULL, hbdMgrDACallbackComplete, &CalllbackArgs);
            rc = RTSemEventWait(pThis->hEvtCallback, 120 * RT_MS_1SEC);
            if (   RT_SUCCESS(rc)
                && CalllbackArgs.rcDA == kDAReturnSuccess)
            {
                /* Unmount anything which might be mounted. */
                DADiskUnmount(hDiskRef, kDADiskUnmountOptionWhole, hbdMgrDACallbackComplete, &CalllbackArgs);
                rc = RTSemEventWait(pThis->hEvtCallback, 120 * RT_MS_1SEC);
                if (    RT_SUCCESS(rc)
                    &&  (   CalllbackArgs.rcDA == kDAReturnSuccess
                         || CalllbackArgs.rcDA == kDAReturnNotMounted))
                {
                    pDev = (PHBDMGRDEV)RTMemAllocZ(sizeof(HBDMGRDEV));
                    if (RT_LIKELY(pDev))
                    {
                        pDev->hDiskRef = hDiskRef;
                        RTSemFastMutexRequest(pThis->hMtxList);
                        RTListAppend(&pThis->ListClaimed, &pDev->ListNode);
                        RTSemFastMutexRelease(pThis->hMtxList);
                        rc = VINF_SUCCESS;
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                else if (RT_SUCCESS(rc))
                {
                    rc = hbdMgrDAReturn2VBoxStatus(CalllbackArgs.rcDA);
                    LogRel(("HBDMgrClaimBlockDevice: DADiskUnmount(\"%s\") failed with %Rrc (%s)\n",
                            pszFilename, rc, CalllbackArgs.pszErrDetail ? CalllbackArgs.pszErrDetail : "<no detail>"));
                    if (CalllbackArgs.pszErrDetail)
                        RTStrFree(CalllbackArgs.pszErrDetail);
                }
            }
            else if (RT_SUCCESS(rc))
            {
                rc = hbdMgrDAReturn2VBoxStatus(CalllbackArgs.rcDA);
                LogRel(("HBDMgrClaimBlockDevice: DADiskClaim(\"%s\") failed with %Rrc (%s)\n",
                        pszFilename, rc, CalllbackArgs.pszErrDetail ? CalllbackArgs.pszErrDetail : "<no detail>"));
                if (CalllbackArgs.pszErrDetail)
                    RTStrFree(CalllbackArgs.pszErrDetail);
            }
            if (RT_FAILURE(rc))
                CFRelease(hDiskRef);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_ALREADY_EXISTS;

    return rc;
}


DECLHIDDEN(int) HBDMgrUnclaimBlockDevice(HBDMGR hHbdMgr, const char *pszFilename)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    RTSemFastMutexRequest(pThis->hMtxList);
    int rc = VINF_SUCCESS;
    PHBDMGRDEV pDev = hbdMgrDevFindByName(pThis, pszFilename);
    if (pDev)
        hbdMgrDevUnclaim(pDev);
    else
        rc = VERR_NOT_FOUND;
    RTSemFastMutexRelease(pThis->hMtxList);

    return rc;
}


DECLHIDDEN(bool) HBDMgrIsBlockDeviceClaimed(HBDMGR hHbdMgr, const char *pszFilename)
{
    PHBDMGRINT pThis = hHbdMgr;
    AssertPtrReturn(pThis, false);

    RTSemFastMutexRequest(pThis->hMtxList);
    PHBDMGRDEV pIt = hbdMgrDevFindByName(pThis, pszFilename);
    RTSemFastMutexRelease(pThis->hMtxList);

    return pIt ? true : false;
}
