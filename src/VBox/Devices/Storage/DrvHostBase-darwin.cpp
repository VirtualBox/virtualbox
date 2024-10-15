/* $Id: DrvHostBase-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostBase - Host base drive access driver, OS X specifics.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_BASE
#include <mach/mach.h>
#include <Carbon/Carbon.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/scsi/SCSICommandOperationCodes.h>
#include <IOKit/IOBSD.h>
#include <DiskArbitration/DiskArbitration.h>
#include <mach/mach_error.h>
#include <VBox/err.h>
#include <VBox/scsi.h>
#include <iprt/string.h>


/**
 * Host backend specific data.
 */
typedef struct DRVHOSTBASEOS
{
    /** The master port. */
    mach_port_t             MasterPort;
    /** The MMC-2 Device Interface. (This is only used to get the scsi task interface.) */
    MMCDeviceInterface      **ppMMCDI;
    /** The SCSI Task Device Interface. */
    SCSITaskDeviceInterface **ppScsiTaskDI;
    /** The block size. Set when querying the media size. */
    uint32_t                cbBlock;
    /** The disk arbitration session reference. NULL if we didn't have to claim & unmount the device. */
    DASessionRef            pDASession;
    /** The disk arbitration disk reference. NULL if we didn't have to claim & unmount the device. */
    DADiskRef               pDADisk;
    /** The number of errors that could go into the release log. (flood gate) */
    uint32_t                cLogRelErrors;
} DRVHOSTBASEOS;
/** Pointer to the host backend specific data. */
typedef DRVHOSTBASEOS *PDRVHOSBASEOS;
AssertCompile(sizeof(DRVHOSTBASEOS) <= 64);

#define DRVHOSTBASE_OS_INT_DECLARED
#include "DrvHostBase.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum buffer size we support, check whether darwin has some real upper limit. */
#define DARWIN_SCSI_MAX_BUFFER_SIZE (100 * _1K)

/** The runloop input source name for the disk arbitration events. */
#define MY_RUN_LOOP_MODE  CFSTR("drvHostBaseDA") /** @todo r=bird: Check if this will cause trouble in the same way that the one in the USB code did. */



/**
 * Gets the BSD Name (/dev/disc[0-9]+) for the service.
 *
 * This is done by recursing down the I/O registry until we hit upon an entry
 * with a BSD Name. Usually we find it two levels down. (Further down under
 * the IOCDPartitionScheme, the volume (slices) BSD Name is found. We don't
 * seem to have to go this far fortunately.)
 *
 * @return  VINF_SUCCESS if found, VERR_FILE_NOT_FOUND otherwise.
 * @param   Entry       The current I/O registry entry reference.
 * @param   pszName     Where to store the name. 128 bytes.
 * @param   cRecursions Number of recursions. This is used as an precaution
 *                      just to limit the depth and avoid blowing the stack
 *                      should we hit a bug or something.
 */
static int drvHostBaseGetBSDName(io_registry_entry_t Entry, char *pszName, unsigned cRecursions)
{
    int rc = VERR_FILE_NOT_FOUND;
    io_iterator_t Children = 0;
    kern_return_t krc = IORegistryEntryGetChildIterator(Entry, kIOServicePlane, &Children);
    if (krc == KERN_SUCCESS)
    {
        io_object_t Child;
        while (     rc == VERR_FILE_NOT_FOUND
               &&   (Child = IOIteratorNext(Children)) != 0)
        {
            CFStringRef BSDNameStrRef = (CFStringRef)IORegistryEntryCreateCFProperty(Child, CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
            if (BSDNameStrRef)
            {
                if (CFStringGetCString(BSDNameStrRef, pszName, 128, kCFStringEncodingUTF8))
                    rc = VINF_SUCCESS;
                else
                    AssertFailed();
                CFRelease(BSDNameStrRef);
            }
            if (rc == VERR_FILE_NOT_FOUND && cRecursions < 10)
                rc = drvHostBaseGetBSDName(Child, pszName, cRecursions + 1);
            IOObjectRelease(Child);
        }
        IOObjectRelease(Children);
    }
    return rc;
}


/**
 * Callback notifying us that the async DADiskClaim()/DADiskUnmount call has completed.
 *
 * @param   DiskRef         The disk that was attempted claimed / unmounted.
 * @param   DissenterRef    NULL on success, contains details on failure.
 * @param   pvContext       Pointer to the return code variable.
 */
static void drvHostBaseDADoneCallback(DADiskRef DiskRef, DADissenterRef DissenterRef, void *pvContext)
{
    RT_NOREF(DiskRef);
    int *prc = (int *)pvContext;
    if (!DissenterRef)
        *prc = 0;
    else
        *prc = DADissenterGetStatus(DissenterRef) ? DADissenterGetStatus(DissenterRef) : -1;
    CFRunLoopStop(CFRunLoopGetCurrent());
}


/**
 * Obtain exclusive access to the DVD device, umount it if necessary.
 *
 * @return  VBox status code.
 * @param   pThis       The driver instance.
 * @param   DVDService  The DVD service object.
 */
static int drvHostBaseObtainExclusiveAccess(PDRVHOSTBASE pThis, io_object_t DVDService)
{
    PPDMDRVINS pDrvIns = pThis->pDrvIns; NOREF(pDrvIns);

    for (unsigned iTry = 0;; iTry++)
    {
        IOReturn irc = (*pThis->Os.ppScsiTaskDI)->ObtainExclusiveAccess(pThis->Os.ppScsiTaskDI);
        if (irc == kIOReturnSuccess)
        {
            /*
             * This is a bit weird, but if we unmounted the DVD drive we also need to
             * unlock it afterwards or the guest won't be able to eject it later on.
             */
            if (pThis->Os.pDADisk)
            {
                uint8_t abCmd[16] =
                {
                    SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL, 0, 0, 0, false, 0,
                    0,0,0,0,0,0,0,0,0,0
                };
                drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_NONE, NULL, NULL, NULL, 0, 0);
            }
            return VINF_SUCCESS;
        }
        if (irc == kIOReturnExclusiveAccess)
            return VERR_SHARING_VIOLATION;      /* already used exclusivly. */
        if (irc != kIOReturnBusy)
            return VERR_GENERAL_FAILURE;        /* not mounted */

        /*
         * Attempt to the unmount all volumes of the device.
         * It seems we can can do this all in one go without having to enumerate the
         * volumes (sessions) and deal with them one by one. This is very fortuitous
         * as the disk arbitration API is a bit cumbersome to deal with.
         */
        if (iTry > 2)
            return VERR_DRIVE_LOCKED;
        char szName[128];
        int rc = drvHostBaseGetBSDName(DVDService, &szName[0], 0);
        if (RT_SUCCESS(rc))
        {
            pThis->Os.pDASession = DASessionCreate(kCFAllocatorDefault);
            if (pThis->Os.pDASession)
            {
                DASessionScheduleWithRunLoop(pThis->Os.pDASession, CFRunLoopGetCurrent(), MY_RUN_LOOP_MODE);
                pThis->Os.pDADisk = DADiskCreateFromBSDName(kCFAllocatorDefault, pThis->Os.pDASession, szName);
                if (pThis->Os.pDADisk)
                {
                    /*
                     * Try claim the device.
                     */
                    Log(("%s-%d: calling DADiskClaim on '%s'.\n", pDrvIns->pReg->szName, pDrvIns->iInstance, szName));
                    int rcDA = -2;
                    DADiskClaim(pThis->Os.pDADisk, kDADiskClaimOptionDefault, NULL, NULL, drvHostBaseDADoneCallback, &rcDA);
                    SInt32 rc32 = CFRunLoopRunInMode(MY_RUN_LOOP_MODE, 120.0, FALSE);
                    AssertMsg(rc32 == kCFRunLoopRunStopped, ("rc32=%RI32 (%RX32)\n", rc32, rc32));
                    if (    rc32 == kCFRunLoopRunStopped
                        &&  !rcDA)
                    {
                        /*
                         * Try unmount the device.
                         */
                        Log(("%s-%d: calling DADiskUnmount on '%s'.\n", pDrvIns->pReg->szName, pDrvIns->iInstance, szName));
                        rcDA = -2;
                        DADiskUnmount(pThis->Os.pDADisk, kDADiskUnmountOptionWhole, drvHostBaseDADoneCallback, &rcDA);
                        rc32 = CFRunLoopRunInMode(MY_RUN_LOOP_MODE, 120.0, FALSE);
                        AssertMsg(rc32 == kCFRunLoopRunStopped, ("rc32=%RI32 (%RX32)\n", rc32, rc32));
                        if (    rc32 == kCFRunLoopRunStopped
                            &&  !rcDA)
                        {
                            iTry = 99;
                            DASessionUnscheduleFromRunLoop(pThis->Os.pDASession, CFRunLoopGetCurrent(), MY_RUN_LOOP_MODE);
                            Log(("%s-%d: unmount succeed - retrying.\n", pDrvIns->pReg->szName, pDrvIns->iInstance));
                            continue;
                        }
                        Log(("%s-%d: umount => rc32=%d & rcDA=%#x\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc32, rcDA));

                        /* failed - cleanup */
                        DADiskUnclaim(pThis->Os.pDADisk);
                    }
                    else
                        Log(("%s-%d: claim => rc32=%d & rcDA=%#x\n", pDrvIns->pReg->szName, pDrvIns->iInstance, rc32, rcDA));

                    CFRelease(pThis->Os.pDADisk);
                    pThis->Os.pDADisk = NULL;
                }
                else
                    Log(("%s-%d: failed to open disk '%s'!\n", pDrvIns->pReg->szName, pDrvIns->iInstance, szName));

                DASessionUnscheduleFromRunLoop(pThis->Os.pDASession, CFRunLoopGetCurrent(), MY_RUN_LOOP_MODE);
                CFRelease(pThis->Os.pDASession);
                pThis->Os.pDASession = NULL;
            }
            else
                Log(("%s-%d: failed to create DA session!\n", pDrvIns->pReg->szName, pDrvIns->iInstance));
        }
        RTThreadSleep(10);
    }
}

DECLHIDDEN(int) drvHostBaseScsiCmdOs(PDRVHOSTBASE pThis, const uint8_t *pbCmd, size_t cbCmd, PDMMEDIATXDIR enmTxDir,
                                     void *pvBuf, uint32_t *pcbBuf, uint8_t *pbSense, size_t cbSense, uint32_t cTimeoutMillies)
{
    /*
     * Minimal input validation.
     */
    Assert(enmTxDir == PDMMEDIATXDIR_NONE || enmTxDir == PDMMEDIATXDIR_FROM_DEVICE || enmTxDir == PDMMEDIATXDIR_TO_DEVICE);
    Assert(!pvBuf || pcbBuf);
    Assert(pvBuf || enmTxDir == PDMMEDIATXDIR_NONE);
    Assert(pbSense || !cbSense);
    AssertPtr(pbCmd);
    Assert(cbCmd <= 16 && cbCmd >= 1);
    const uint32_t cbBuf = pcbBuf ? *pcbBuf : 0;
    if (pcbBuf)
        *pcbBuf = 0;

    Assert(pThis->Os.ppScsiTaskDI);

    int rc = VERR_GENERAL_FAILURE;
    SCSITaskInterface **ppScsiTaskI = (*pThis->Os.ppScsiTaskDI)->CreateSCSITask(pThis->Os.ppScsiTaskDI);
    if (!ppScsiTaskI)
        return VERR_NO_MEMORY;
    do
    {
        /* Setup the scsi command. */
        SCSICommandDescriptorBlock cdb = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        memcpy(&cdb[0], pbCmd, cbCmd);
        IOReturn irc = (*ppScsiTaskI)->SetCommandDescriptorBlock(ppScsiTaskI, cdb, cbCmd);
        AssertBreak(irc == kIOReturnSuccess);

        /* Setup the buffer. */
        if (enmTxDir == PDMMEDIATXDIR_NONE)
            irc = (*ppScsiTaskI)->SetScatterGatherEntries(ppScsiTaskI, NULL, 0, 0, kSCSIDataTransfer_NoDataTransfer);
        else
        {
            IOVirtualRange Range = { (IOVirtualAddress)pvBuf, cbBuf };
            irc = (*ppScsiTaskI)->SetScatterGatherEntries(ppScsiTaskI, &Range, 1, cbBuf,
                                                          enmTxDir == PDMMEDIATXDIR_FROM_DEVICE
                                                          ? kSCSIDataTransfer_FromTargetToInitiator
                                                          : kSCSIDataTransfer_FromInitiatorToTarget);
        }
        AssertBreak(irc == kIOReturnSuccess);

        /* Set the timeout. */
        irc = (*ppScsiTaskI)->SetTimeoutDuration(ppScsiTaskI, cTimeoutMillies ? cTimeoutMillies : 30000 /*ms*/);
        AssertBreak(irc == kIOReturnSuccess);

        /* Execute the command and get the response. */
        SCSI_Sense_Data SenseData = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        SCSIServiceResponse     ServiceResponse = kSCSIServiceResponse_Request_In_Process;
        SCSITaskStatus TaskStatus = kSCSITaskStatus_GOOD;
        UInt64 cbReturned = 0;
        irc = (*ppScsiTaskI)->ExecuteTaskSync(ppScsiTaskI, &SenseData, &TaskStatus, &cbReturned);
        AssertBreak(irc == kIOReturnSuccess);
        if (pcbBuf)
            *pcbBuf = (int32_t)cbReturned;

        irc = (*ppScsiTaskI)->GetSCSIServiceResponse(ppScsiTaskI, &ServiceResponse);
        AssertBreak(irc == kIOReturnSuccess);
        AssertBreak(ServiceResponse == kSCSIServiceResponse_TASK_COMPLETE);

        if (TaskStatus == kSCSITaskStatus_GOOD)
            rc = VINF_SUCCESS;
        else if (   TaskStatus == kSCSITaskStatus_CHECK_CONDITION
                 && pbSense)
        {
            memset(pbSense, 0, cbSense); /* lazy */
            memcpy(pbSense, &SenseData, RT_MIN(sizeof(SenseData), cbSense));
            rc = VERR_UNRESOLVED_ERROR;
        }
        /** @todo convert sense codes when caller doesn't wish to do this himself. */
        /*else if (   TaskStatus == kSCSITaskStatus_CHECK_CONDITION
                 && SenseData.ADDITIONAL_SENSE_CODE == 0x3A)
            rc = VERR_MEDIA_NOT_PRESENT; */
        else
        {
            rc = enmTxDir == PDMMEDIATXDIR_NONE
               ? VERR_DEV_IO_ERROR
               : enmTxDir == PDMMEDIATXDIR_FROM_DEVICE
               ? VERR_READ_ERROR
               : VERR_WRITE_ERROR;
            if (pThis->Os.cLogRelErrors++ < 10)
                LogRel(("DVD scsi error: cmd={%.*Rhxs} TaskStatus=%#x key=%#x ASC=%#x ASCQ=%#x (%Rrc)\n",
                        cbCmd, pbCmd, TaskStatus, SenseData.SENSE_KEY, SenseData.ADDITIONAL_SENSE_CODE,
                        SenseData.ADDITIONAL_SENSE_CODE_QUALIFIER, rc));
        }
    } while (0);

    (*ppScsiTaskI)->Release(ppScsiTaskI);

    return rc;
}


DECLHIDDEN(size_t) drvHostBaseScsiCmdGetBufLimitOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);

    return DARWIN_SCSI_MAX_BUFFER_SIZE;
}


DECLHIDDEN(int) drvHostBaseGetMediaSizeOs(PDRVHOSTBASE pThis, uint64_t *pcb)
{
    /*
     * Try a READ_CAPACITY command...
     */
    struct
    {
        uint32_t cBlocks;
        uint32_t cbBlock;
    }           Buf = {0, 0};
    uint32_t    cbBuf = sizeof(Buf);
    uint8_t     abCmd[16] =
    {
        SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0,
        0,0,0,0,0,0,0,0,0
    };
    int rc = drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_FROM_DEVICE, &Buf, &cbBuf, NULL, 0, 0);
    if (RT_SUCCESS(rc))
    {
        Assert(cbBuf == sizeof(Buf));
        Buf.cBlocks = RT_BE2H_U32(Buf.cBlocks);
        Buf.cbBlock = RT_BE2H_U32(Buf.cbBlock);
        //if (Buf.cbBlock > 2048) /* everyone else is doing this... check if it needed/right.*/
        //    Buf.cbBlock = 2048;
        pThis->Os.cbBlock = Buf.cbBlock;

        *pcb = (uint64_t)Buf.cBlocks * Buf.cbBlock;
    }
    return rc;
}


DECLHIDDEN(int) drvHostBaseReadOs(PDRVHOSTBASE pThis, uint64_t off, void *pvBuf, size_t cbRead)
{
    int rc = VINF_SUCCESS;

    if (    pThis->Os.ppScsiTaskDI
        &&  pThis->Os.cbBlock)
    {
        /*
         * Issue a READ(12) request.
         */
        do
        {
            const uint32_t  LBA       = off / pThis->Os.cbBlock;
            AssertReturn(!(off % pThis->Os.cbBlock), VERR_INVALID_PARAMETER);
            uint32_t        cbRead32  =   cbRead > SCSI_MAX_BUFFER_SIZE
                                        ? SCSI_MAX_BUFFER_SIZE
                                        : (uint32_t)cbRead;
            const uint32_t  cBlocks   = cbRead32 / pThis->Os.cbBlock;
            AssertReturn(!(cbRead % pThis->Os.cbBlock), VERR_INVALID_PARAMETER);
            uint8_t         abCmd[16] =
            {
                SCSI_READ_12, 0,
                RT_BYTE4(LBA),     RT_BYTE3(LBA),     RT_BYTE2(LBA),     RT_BYTE1(LBA),
                RT_BYTE4(cBlocks), RT_BYTE3(cBlocks), RT_BYTE2(cBlocks), RT_BYTE1(cBlocks),
                0, 0, 0, 0, 0
            };
            rc = drvHostBaseScsiCmdOs(pThis, abCmd, 12, PDMMEDIATXDIR_FROM_DEVICE, pvBuf, &cbRead32, NULL, 0, 0);

            off    += cbRead32;
            cbRead -= cbRead32;
            pvBuf   = (uint8_t *)pvBuf + cbRead32;
        } while ((cbRead > 0) && RT_SUCCESS(rc));
    }
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    return rc;
}


DECLHIDDEN(int) drvHostBaseWriteOs(PDRVHOSTBASE pThis, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    RT_NOREF4(pThis, off, pvBuf, cbWrite);
    return VERR_WRITE_PROTECT;
}


DECLHIDDEN(int) drvHostBaseFlushOs(PDRVHOSTBASE pThis)
{
    RT_NOREF1(pThis);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) drvHostBaseDoLockOs(PDRVHOSTBASE pThis, bool fLock)
{
    uint8_t abCmd[16] =
    {
        SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL, 0, 0, 0, fLock, 0,
        0,0,0,0,0,0,0,0,0,0
    };
    return drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_NONE, NULL, NULL, NULL, 0, 0);
}


DECLHIDDEN(int) drvHostBaseEjectOs(PDRVHOSTBASE pThis)
{
    uint8_t abCmd[16] =
    {
        SCSI_START_STOP_UNIT, 0, 0, 0, 2 /*eject+stop*/, 0,
        0,0,0,0,0,0,0,0,0,0
    };
    return drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_NONE, NULL, NULL, NULL, 0, 0);
}


DECLHIDDEN(int) drvHostBaseQueryMediaStatusOs(PDRVHOSTBASE pThis, bool *pfMediaChanged, bool *pfMediaPresent)
{
    AssertReturn(pThis->Os.ppScsiTaskDI, VERR_INTERNAL_ERROR);

    /*
     * Issue a TEST UNIT READY request.
     */
    *pfMediaChanged = false;
    *pfMediaPresent = false;
    uint8_t abCmd[16] = { SCSI_TEST_UNIT_READY, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    uint8_t abSense[32];
    int rc = drvHostBaseScsiCmdOs(pThis, abCmd, 6, PDMMEDIATXDIR_NONE, NULL, NULL, abSense, sizeof(abSense), 0);
    if (RT_SUCCESS(rc))
        *pfMediaPresent = true;
    else if (   rc == VERR_UNRESOLVED_ERROR
             && abSense[2] == 6 /* unit attention */
             && (   (abSense[12] == 0x29 && abSense[13] < 5 /* reset */)
                 || (abSense[12] == 0x2a && abSense[13] == 0 /* parameters changed */)                        //???
                 || (abSense[12] == 0x3f && abSense[13] == 0 /* target operating conditions have changed */)  //???
                 || (abSense[12] == 0x3f && abSense[13] == 2 /* changed operating definition */)              //???
                 || (abSense[12] == 0x3f && abSense[13] == 3 /* inquiry parameters changed */)
                 || (abSense[12] == 0x3f && abSense[13] == 5 /* device identifier changed */)
                 )
            )
    {
        *pfMediaPresent = false;
        *pfMediaChanged = true;
        rc = VINF_SUCCESS;
        /** @todo check this media change stuff on Darwin. */
    }

    return rc;
}


DECLHIDDEN(void) drvHostBaseInitOs(PDRVHOSTBASE pThis)
{
    pThis->Os.MasterPort   = IO_OBJECT_NULL;
    pThis->Os.ppMMCDI      = NULL;
    pThis->Os.ppScsiTaskDI = NULL;
    pThis->Os.cbBlock      = 0;
    pThis->Os.pDADisk      = NULL;
    pThis->Os.pDASession   = NULL;
}


DECLHIDDEN(int) drvHostBaseOpenOs(PDRVHOSTBASE pThis, bool fReadOnly)
{
    RT_NOREF(fReadOnly);

    /* Darwin is kind of special... */
    Assert(!pThis->Os.cbBlock);
    Assert(pThis->Os.MasterPort == IO_OBJECT_NULL);
    Assert(!pThis->Os.ppMMCDI);
    Assert(!pThis->Os.ppScsiTaskDI);

    /*
     * Open the master port on the first invocation.
     */
    RT_GCC_NO_WARN_DEPRECATED_BEGIN
    kern_return_t krc = IOMasterPort(MACH_PORT_NULL, &pThis->Os.MasterPort); /* Deprecated since 12.0. */
    RT_GCC_NO_WARN_DEPRECATED_END
    AssertReturn(krc == KERN_SUCCESS, VERR_GENERAL_FAILURE);

    /*
     * Create a matching dictionary for searching for CD, DVD and BlueRay services in the IOKit.
     *
     * The idea is to find all the devices which are of class IOCDBlockStorageDevice.
     * CD devices are represented by IOCDBlockStorageDevice class itself, while DVD and BlueRay ones
     * have it as a parent class.
     */
    CFMutableDictionaryRef RefMatchingDict = IOServiceMatching("IOCDBlockStorageDevice");
    AssertReturn(RefMatchingDict, VERR_NOT_FOUND);

    /*
     * do the search and get a collection of keyboards.
     */
    io_iterator_t DVDServices = IO_OBJECT_NULL;
    IOReturn irc = IOServiceGetMatchingServices(pThis->Os.MasterPort, RefMatchingDict, &DVDServices);
    AssertMsgReturn(irc == kIOReturnSuccess, ("irc=%d\n", irc), VERR_NOT_FOUND);
    RefMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /*
     * Enumerate the matching drives (services).
     * (This enumeration must be identical to the one performed in Main/src-server/darwin/iokit.cpp.)
     */
    int rc = VERR_FILE_NOT_FOUND;
    unsigned i = 0;
    io_object_t DVDService;
    while ((DVDService = IOIteratorNext(DVDServices)) != 0)
    {
        /*
         * Get the properties we use to identify the DVD drive.
         *
         * While there is a (weird 12 byte) GUID, it isn't persistent
         * across boots. So, we have to use a combination of the
         * vendor name and product name properties with an optional
         * sequence number for identification.
         */
        CFMutableDictionaryRef PropsRef = 0;
        krc = IORegistryEntryCreateCFProperties(DVDService, &PropsRef, kCFAllocatorDefault, kNilOptions);
        if (krc == KERN_SUCCESS)
        {
            /* Get the Device Characteristics dictionary. */
            CFDictionaryRef DevCharRef = (CFDictionaryRef)CFDictionaryGetValue(PropsRef, CFSTR(kIOPropertyDeviceCharacteristicsKey));
            if (DevCharRef)
            {
                /* The vendor name. */
                char szVendor[128];
                char *pszVendor = &szVendor[0];
                CFTypeRef ValueRef = CFDictionaryGetValue(DevCharRef, CFSTR(kIOPropertyVendorNameKey));
                if (    ValueRef
                    &&  CFGetTypeID(ValueRef) == CFStringGetTypeID()
                    &&  CFStringGetCString((CFStringRef)ValueRef, szVendor, sizeof(szVendor), kCFStringEncodingUTF8))
                    pszVendor = RTStrStrip(szVendor);
                else
                    *pszVendor = '\0';

                /* The product name. */
                char szProduct[128];
                char *pszProduct = &szProduct[0];
                ValueRef = CFDictionaryGetValue(DevCharRef, CFSTR(kIOPropertyProductNameKey));
                if (    ValueRef
                    &&  CFGetTypeID(ValueRef) == CFStringGetTypeID()
                    &&  CFStringGetCString((CFStringRef)ValueRef, szProduct, sizeof(szProduct), kCFStringEncodingUTF8))
                    pszProduct = RTStrStrip(szProduct);
                else
                    *pszProduct = '\0';

                /* Construct the two names and compare thwm with the one we're searching for. */
                char szName1[256 + 32];
                char szName2[256 + 32];
                if (*pszVendor || *pszProduct)
                {
                    if (*pszVendor && *pszProduct)
                    {
                        RTStrPrintf(szName1, sizeof(szName1), "%s %s", pszVendor, pszProduct);
                        RTStrPrintf(szName2, sizeof(szName2), "%s %s (#%u)", pszVendor, pszProduct, i);
                    }
                    else
                    {
                        strcpy(szName1, *pszVendor ? pszVendor : pszProduct);
                        RTStrPrintf(szName2, sizeof(szName2), "%s (#%u)", *pszVendor ? pszVendor : pszProduct, i);
                    }
                }
                else
                {
                    RTStrPrintf(szName1, sizeof(szName1), "(#%u)", i);
                    strcpy(szName2, szName1);
                }

                if (    !strcmp(szName1, pThis->pszDevice)
                    ||  !strcmp(szName2, pThis->pszDevice))
                {
                    /*
                     * Found it! Now, get the client interface and stuff.
                     * Note that we could also query kIOSCSITaskDeviceUserClientTypeID here if the
                     * MMC client plugin is missing. For now we assume this won't be necessary.
                     */
                    SInt32 Score = 0;
                    IOCFPlugInInterface **ppPlugInInterface = NULL;
                    krc = IOCreatePlugInInterfaceForService(DVDService, kIOMMCDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                                                            &ppPlugInInterface, &Score);
                    if (krc == KERN_SUCCESS)
                    {
                        HRESULT hrc = (*ppPlugInInterface)->QueryInterface(ppPlugInInterface,
                                                                           CFUUIDGetUUIDBytes(kIOMMCDeviceInterfaceID),
                                                                           (LPVOID *)&pThis->Os.ppMMCDI);
                        (*ppPlugInInterface)->Release(ppPlugInInterface);
                        ppPlugInInterface = NULL;
                        if (hrc == S_OK)
                        {
                            pThis->Os.ppScsiTaskDI = (*pThis->Os.ppMMCDI)->GetSCSITaskDeviceInterface(pThis->Os.ppMMCDI);
                            if (pThis->Os.ppScsiTaskDI)
                                rc = VINF_SUCCESS;
                            else
                            {
                                LogRel(("GetSCSITaskDeviceInterface failed on '%s'\n", pThis->pszDevice));
                                rc = VERR_NOT_SUPPORTED;
                                (*pThis->Os.ppMMCDI)->Release(pThis->Os.ppMMCDI);
                            }
                        }
                        else
                        {
                            rc = VERR_GENERAL_FAILURE;//RTErrConvertFromDarwinCOM(krc);
                            pThis->Os.ppMMCDI = NULL;
                        }
                    }
                    else /* Check for kIOSCSITaskDeviceUserClientTypeID? */
                        rc = VERR_GENERAL_FAILURE;//RTErrConvertFromDarwinKern(krc);

                    /* Obtain exclusive access to the device so we can send SCSI commands. */
                    if (RT_SUCCESS(rc))
                        rc = drvHostBaseObtainExclusiveAccess(pThis, DVDService);

                    /* Cleanup on failure. */
                    if (RT_FAILURE(rc))
                    {
                        if (pThis->Os.ppScsiTaskDI)
                        {
                            (*pThis->Os.ppScsiTaskDI)->Release(pThis->Os.ppScsiTaskDI);
                            pThis->Os.ppScsiTaskDI = NULL;
                        }
                        if (pThis->Os.ppMMCDI)
                        {
                            (*pThis->Os.ppMMCDI)->Release(pThis->Os.ppMMCDI);
                            pThis->Os.ppMMCDI = NULL;
                        }
                    }

                    IOObjectRelease(DVDService);
                    break;
                }
            }
            CFRelease(PropsRef);
        }
        else
            AssertMsgFailed(("krc=%#x\n", krc));

        IOObjectRelease(DVDService);
        i++;
    }

    IOObjectRelease(DVDServices);
    return rc;

}


DECLHIDDEN(int) drvHostBaseMediaRefreshOs(PDRVHOSTBASE pThis)
{
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


DECLHIDDEN(bool) drvHostBaseIsMediaPollingRequiredOs(PDRVHOSTBASE pThis)
{
    if (pThis->enmType == PDMMEDIATYPE_CDROM || pThis->enmType == PDMMEDIATYPE_DVD)
        return true;

    AssertMsgFailed(("Darwin supports only CD/DVD host drive access\n"));
    return false;
}


DECLHIDDEN(void) drvHostBaseDestructOs(PDRVHOSTBASE pThis)
{
    /*
     * Unlock the drive if we've locked it or we're in passthru mode.
     */
    if (    (   pThis->fLocked
             || pThis->IMedia.pfnSendCmd)
        &&  pThis->Os.ppScsiTaskDI
        &&  pThis->pfnDoLock)
    {
        int rc = pThis->pfnDoLock(pThis, false);
        if (RT_SUCCESS(rc))
            pThis->fLocked = false;
    }

    /*
     * The unclaiming doesn't seem to mean much, the DVD is actually
     * remounted when we release exclusive access. I'm not quite sure
     * if I should put the unclaim first or not...
     *
     * Anyway, that it's automatically remounted very good news for us,
     * because that means we don't have to mess with that ourselves. Of
     * course there is the unlikely scenario that we've succeeded in claiming
     * and umount the DVD but somehow failed to gain exclusive scsi access...
     */
    if (pThis->Os.ppScsiTaskDI)
    {
        LogFlow(("%s-%d: releasing exclusive scsi access!\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
        (*pThis->Os.ppScsiTaskDI)->ReleaseExclusiveAccess(pThis->Os.ppScsiTaskDI);
        (*pThis->Os.ppScsiTaskDI)->Release(pThis->Os.ppScsiTaskDI);
        pThis->Os.ppScsiTaskDI = NULL;
    }
    if (pThis->Os.pDADisk)
    {
        LogFlow(("%s-%d: unclaiming the disk!\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
        DADiskUnclaim(pThis->Os.pDADisk);
        CFRelease(pThis->Os.pDADisk);
        pThis->Os.pDADisk = NULL;
    }
    if (pThis->Os.ppMMCDI)
    {
        LogFlow(("%s-%d: releasing the MMC object!\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
        (*pThis->Os.ppMMCDI)->Release(pThis->Os.ppMMCDI);
        pThis->Os.ppMMCDI = NULL;
    }
    if (pThis->Os.MasterPort != IO_OBJECT_NULL)
    {
        mach_port_deallocate(mach_task_self(), pThis->Os.MasterPort);
        pThis->Os.MasterPort = IO_OBJECT_NULL;
    }
    if (pThis->Os.pDASession)
    {
        LogFlow(("%s-%d: releasing the DA session!\n", pThis->pDrvIns->pReg->szName, pThis->pDrvIns->iInstance));
        CFRelease(pThis->Os.pDASession);
        pThis->Os.pDASession = NULL;
    }
}

