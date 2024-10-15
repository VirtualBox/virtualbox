/* $Id: vbsf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders - File System Driver initialization and generic routines
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include "vbsf.h"
#include <iprt/buildconfig.h>
#include <iprt/initterm.h>
#include <iprt/dbg.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The current state of the driver.
 */
typedef enum _MRX_VBOX_STATE_
{
    MRX_VBOX_STARTABLE,
    MRX_VBOX_START_IN_PROGRESS,
    MRX_VBOX_STARTED
} MRX_VBOX_STATE, *PMRX_VBOX_STATE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static MRX_VBOX_STATE VBoxMRxState = MRX_VBOX_STARTABLE;

/**
 * The VBoxSF dispatch table.
 */
static struct _MINIRDR_DISPATCH VBoxMRxDispatch;

/**
 * The VBoxSF device object.
 */
PRDBSS_DEVICE_OBJECT VBoxMRxDeviceObject;

/** Pointer to CcCoherencyFlushAndPurgeCache if present in ntoskrnl. */
PFNCCCOHERENCYFLUSHANDPURGECACHE g_pfnCcCoherencyFlushAndPurgeCache;

/** The shared folder service client structure. */
VBGLSFCLIENT g_SfClient;
/** VMMDEV_HVF_XXX (set during init). */
uint32_t     g_fHostFeatures = 0;
/** Last valid shared folders function number. */
uint32_t     g_uSfLastFunction = SHFL_FN_SET_FILE_SIZE;
/** Shared folders features (SHFL_FEATURE_XXX). */
uint64_t     g_fSfFeatures = 0;


static NTSTATUS VBoxMRxFsdDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    NTSTATUS Status;
#ifdef LOG_ENABLED
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Log(("VBOXSF: MRxFsdDispatch: major %d, minor %d: %s\n",
         IrpSp->MajorFunction, IrpSp->MinorFunction, vbsfNtMajorFunctionName(IrpSp->MajorFunction, IrpSp->MinorFunction)));
#endif

    if (DeviceObject != (PDEVICE_OBJECT)VBoxMRxDeviceObject)
    {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        Log(("VBOXSF: MRxFsdDispatch: Invalid device request detected %p %p\n",
             DeviceObject, (PDEVICE_OBJECT)VBoxMRxDeviceObject));

        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = RxFsdDispatch((PRDBSS_DEVICE_OBJECT)VBoxMRxDeviceObject, Irp);
    Log(("VBOXSF: MRxFsdDispatch: Returned 0x%X\n", Status));
    return Status;
}

static void VBoxMRxUnload(IN PDRIVER_OBJECT DriverObject)
{
    NTSTATUS Status;
    UNICODE_STRING UserModeDeviceName;

    Log(("VBOXSF: MRxUnload\n"));

    if (VBoxMRxDeviceObject)
    {
        PMRX_VBOX_DEVICE_EXTENSION pDeviceExtension;
        pDeviceExtension = (PMRX_VBOX_DEVICE_EXTENSION)((PBYTE)VBoxMRxDeviceObject + sizeof(RDBSS_DEVICE_OBJECT));
    }

    if (VBoxMRxDeviceObject)
    {
        PRX_CONTEXT RxContext;
        RxContext = RxCreateRxContext(NULL, VBoxMRxDeviceObject, RX_CONTEXT_FLAG_IN_FSP);

        if (RxContext != NULL)
        {
            Status = RxStopMinirdr(RxContext, &RxContext->PostRequest);

            if (Status == STATUS_SUCCESS)
            {
                MRX_VBOX_STATE State;

                State = (MRX_VBOX_STATE)InterlockedCompareExchange((LONG *)&VBoxMRxState, MRX_VBOX_STARTABLE, MRX_VBOX_STARTED);

                if (State != MRX_VBOX_STARTABLE)
                    Status = STATUS_REDIRECTOR_STARTED;
            }

            RxDereferenceAndDeleteRxContext(RxContext);
        }
        else
            Status = STATUS_INSUFFICIENT_RESOURCES;

        RxUnregisterMinirdr(VBoxMRxDeviceObject);
    }

    RtlInitUnicodeString(&UserModeDeviceName, DD_MRX_VBOX_USERMODE_SHADOW_DEV_NAME_U);
    Status = IoDeleteSymbolicLink(&UserModeDeviceName);
    if (Status != STATUS_SUCCESS)
        Log(("VBOXSF: MRxUnload: IoDeleteSymbolicLink Status 0x%08X\n", Status));

    RxUnload(DriverObject);

    VbglR0SfDisconnect(&g_SfClient);
    VbglR0SfTerm();

    Log(("VBOXSF: MRxUnload: VBoxSF.sys driver object %p almost unloaded, just RTR0Term left...\n", DriverObject));
    RTR0Term(); /* No logging after this. */
}

static void vbsfInitMRxDispatch(void)
{
    Log(("VBOXSF: vbsfInitMRxDispatch: Called.\n"));

    ZeroAndInitializeNodeType(&VBoxMRxDispatch, RDBSS_NTC_MINIRDR_DISPATCH, sizeof(MINIRDR_DISPATCH));

    VBoxMRxDispatch.MRxFlags = RDBSS_MANAGE_NET_ROOT_EXTENSION | RDBSS_MANAGE_FCB_EXTENSION | RDBSS_MANAGE_FOBX_EXTENSION;

    VBoxMRxDispatch.MRxSrvCallSize = 0;
    VBoxMRxDispatch.MRxNetRootSize = sizeof(MRX_VBOX_NETROOT_EXTENSION);
    VBoxMRxDispatch.MRxVNetRootSize = 0;
    VBoxMRxDispatch.MRxFcbSize = sizeof(VBSFNTFCBEXT);
    VBoxMRxDispatch.MRxSrvOpenSize = 0;
    VBoxMRxDispatch.MRxFobxSize = sizeof(MRX_VBOX_FOBX);

    VBoxMRxDispatch.MRxStart = VBoxMRxStart;
    VBoxMRxDispatch.MRxStop = VBoxMRxStop;

    VBoxMRxDispatch.MRxCreate = VBoxMRxCreate;
    VBoxMRxDispatch.MRxCollapseOpen = VBoxMRxCollapseOpen;
    VBoxMRxDispatch.MRxShouldTryToCollapseThisOpen = VBoxMRxShouldTryToCollapseThisOpen;
    VBoxMRxDispatch.MRxFlush = VBoxMRxFlush;
    VBoxMRxDispatch.MRxTruncate = VBoxMRxTruncate;
    VBoxMRxDispatch.MRxCleanupFobx = VBoxMRxCleanupFobx;
    VBoxMRxDispatch.MRxCloseSrvOpen = VBoxMRxCloseSrvOpen;
    VBoxMRxDispatch.MRxDeallocateForFcb = VBoxMRxDeallocateForFcb;
    VBoxMRxDispatch.MRxDeallocateForFobx = VBoxMRxDeallocateForFobx;
    VBoxMRxDispatch.MRxForceClosed = VBoxMRxForceClosed;

    VBoxMRxDispatch.MRxQueryDirectory = VBoxMRxQueryDirectory;
    VBoxMRxDispatch.MRxQueryFileInfo = VBoxMRxQueryFileInfo;
    VBoxMRxDispatch.MRxSetFileInfo = VBoxMRxSetFileInfo;
    VBoxMRxDispatch.MRxSetFileInfoAtCleanup = VBoxMRxSetFileInfoAtCleanup;
    VBoxMRxDispatch.MRxQueryEaInfo = VBoxMRxQueryEaInfo;
    VBoxMRxDispatch.MRxSetEaInfo = VBoxMRxSetEaInfo;
    VBoxMRxDispatch.MRxQuerySdInfo = VBoxMRxQuerySdInfo;
    VBoxMRxDispatch.MRxSetSdInfo = VBoxMRxSetSdInfo;
    VBoxMRxDispatch.MRxQueryVolumeInfo = VBoxMRxQueryVolumeInfo;

    VBoxMRxDispatch.MRxComputeNewBufferingState = VBoxMRxComputeNewBufferingState;

    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_READ] = VBoxMRxRead;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_WRITE] = VBoxMRxWrite;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_SHAREDLOCK] = VBoxMRxLocks;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_EXCLUSIVELOCK] = VBoxMRxLocks;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_UNLOCK] = VBoxMRxLocks;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_UNLOCK_MULTIPLE] = VBoxMRxLocks;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_FSCTL] = VBoxMRxFsCtl;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_IOCTL] = VBoxMRxIoCtl;
    VBoxMRxDispatch.MRxLowIOSubmit[LOWIO_OP_NOTIFY_CHANGE_DIRECTORY] = VBoxMRxNotifyChangeDirectory;

    VBoxMRxDispatch.MRxExtendForCache = VBoxMRxExtendStub;
    VBoxMRxDispatch.MRxExtendForNonCache = VBoxMRxExtendStub;
    VBoxMRxDispatch.MRxCompleteBufferingStateChangeRequest = VBoxMRxCompleteBufferingStateChangeRequest;

    VBoxMRxDispatch.MRxCreateVNetRoot = VBoxMRxCreateVNetRoot;
    VBoxMRxDispatch.MRxFinalizeVNetRoot = VBoxMRxFinalizeVNetRoot;
    VBoxMRxDispatch.MRxFinalizeNetRoot = VBoxMRxFinalizeNetRoot;
    VBoxMRxDispatch.MRxUpdateNetRootState = VBoxMRxUpdateNetRootState;
    VBoxMRxDispatch.MRxExtractNetRootName = VBoxMRxExtractNetRootName;

    VBoxMRxDispatch.MRxCreateSrvCall = VBoxMRxCreateSrvCall;
    VBoxMRxDispatch.MRxSrvCallWinnerNotify = VBoxMRxSrvCallWinnerNotify;
    VBoxMRxDispatch.MRxFinalizeSrvCall = VBoxMRxFinalizeSrvCall;

    VBoxMRxDispatch.MRxDevFcbXXXControlFile = VBoxMRxDevFcbXXXControlFile;

    Log(("VBOXSF: vbsfInitMRxDispatch: Success.\n"));
    return;
}

static BOOL vboxIsPrefixOK (const WCHAR *FilePathName, ULONG PathNameLength)
{
    BOOL PrefixOK;

    /* The FilePathName here looks like: \vboxsrv\... */
    if (PathNameLength >= 8 * sizeof (WCHAR)) /* Number of bytes in '\vboxsrv' unicode string. */
    {
        PrefixOK =  (FilePathName[0] == L'\\');
        PrefixOK &= (FilePathName[1] == L'V') || (FilePathName[1] == L'v');
        PrefixOK &= (FilePathName[2] == L'B') || (FilePathName[2] == L'b');
        PrefixOK &= (FilePathName[3] == L'O') || (FilePathName[3] == L'o');
        PrefixOK &= (FilePathName[4] == L'X') || (FilePathName[4] == L'x');
        PrefixOK &= (FilePathName[5] == L'S') || (FilePathName[5] == L's');
        /* Both vboxsvr & vboxsrv are now accepted */
        if ((FilePathName[6] == L'V') || (FilePathName[6] == L'v'))
        {
            PrefixOK &= (FilePathName[6] == L'V') || (FilePathName[6] == L'v');
            PrefixOK &= (FilePathName[7] == L'R') || (FilePathName[7] == L'r');
        }
        else
        {
            PrefixOK &= (FilePathName[6] == L'R') || (FilePathName[6] == L'r');
            PrefixOK &= (FilePathName[7] == L'V') || (FilePathName[7] == L'v');
        }
        if (PathNameLength > 8 * sizeof (WCHAR))
        {
            /* There is something after '\vboxsrv'. */
            PrefixOK &= (FilePathName[8] == L'\\') || (FilePathName[8] == 0);
        }
    }
    else
        PrefixOK = FALSE;

    return PrefixOK;
}

static NTSTATUS VBoxMRXDeviceControl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    NTSTATUS Status = STATUS_SUCCESS;

    QUERY_PATH_REQUEST *pReq = NULL;
    QUERY_PATH_REQUEST_EX *pReqEx = NULL;
    QUERY_PATH_RESPONSE *pResp = NULL;

    BOOL PrefixOK = FALSE;

    PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);

    /* Make a local copy, it will be needed after the Irp completion. */
    ULONG IoControlCode = pStack->Parameters.DeviceIoControl.IoControlCode;

    PMRX_VBOX_DEVICE_EXTENSION pDeviceExtension = (PMRX_VBOX_DEVICE_EXTENSION)((PBYTE)pDevObj + sizeof(RDBSS_DEVICE_OBJECT));

    Log(("VBOXSF: MRXDeviceControl: pDevObj %p, pDeviceExtension %p, code %x\n",
         pDevObj, pDevObj->DeviceExtension, IoControlCode));

    switch (IoControlCode)
    {
        case IOCTL_REDIR_QUERY_PATH_EX: /* Vista */
        case IOCTL_REDIR_QUERY_PATH:    /* XP and earlier */
        {
            /* This IOCTL is intercepted for 2 reasons:
             * 1) Claim the vboxsvr and vboxsrv prefixes. All name-based operations for them
             *    will be routed to the VBox provider automatically without any prefix resolution
             *    since the prefix is already in the prefix cache.
             * 2) Reject other prefixes immediately to speed up the UNC path resolution a bit,
             *    because RDBSS will not be involved then.
             */

            const WCHAR *FilePathName = NULL;
            ULONG PathNameLength = 0;

            if (pIrp->RequestorMode != KernelMode)
            {
                /* MSDN: Network redirectors should only honor kernel-mode senders of this IOCTL, by verifying
                 * that RequestorMode member of the IRP structure is KernelMode.
                 */
                Log(("VBOXSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH(_EX): not kernel mode!!!\n",
                      pStack->Parameters.DeviceIoControl.InputBufferLength));
                /* Continue to RDBSS. */
                break;
            }

            if (IoControlCode == IOCTL_REDIR_QUERY_PATH)
            {
                Log(("VBOXSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH: Called (pid %x).\n", IoGetCurrentProcess()));

                if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(QUERY_PATH_REQUEST))
                {
                    Log(("VBOXSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH: short input buffer %d.\n",
                          pStack->Parameters.DeviceIoControl.InputBufferLength));
                    /* Continue to RDBSS. */
                    break;
                }

                pReq = (QUERY_PATH_REQUEST *)pStack->Parameters.DeviceIoControl.Type3InputBuffer;

                Log(("VBOXSF: MRxDeviceControl: PathNameLength = %d.\n", pReq->PathNameLength));
                Log(("VBOXSF: MRxDeviceControl: SecurityContext = %p.\n", pReq->SecurityContext));
                Log(("VBOXSF: MRxDeviceControl: FilePathName = %.*ls.\n", pReq->PathNameLength / sizeof (WCHAR), pReq->FilePathName));

                FilePathName = pReq->FilePathName;
                PathNameLength = pReq->PathNameLength;
            }
            else
            {
                Log(("VBOXSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH_EX: Called.\n"));

                if (pStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(QUERY_PATH_REQUEST_EX))
                {
                    Log(("VBOXSF: MRxDeviceControl: IOCTL_REDIR_QUERY_PATH_EX: short input buffer %d.\n",
                          pStack->Parameters.DeviceIoControl.InputBufferLength));
                    /* Continue to RDBSS. */
                    break;
                }

                pReqEx = (QUERY_PATH_REQUEST_EX *)pStack->Parameters.DeviceIoControl.Type3InputBuffer;

                Log(("VBOXSF: MRxDeviceControl: pSecurityContext = %p.\n", pReqEx->pSecurityContext));
                Log(("VBOXSF: MRxDeviceControl: EaLength = %d.\n", pReqEx->EaLength));
                Log(("VBOXSF: MRxDeviceControl: pEaBuffer = %p.\n", pReqEx->pEaBuffer));
                Log(("VBOXSF: MRxDeviceControl: PathNameLength = %d.\n", pReqEx->PathName.Length));
                Log(("VBOXSF: MRxDeviceControl: FilePathName = %.*ls.\n", pReqEx->PathName.Length / sizeof (WCHAR), pReqEx->PathName.Buffer));

                FilePathName = pReqEx->PathName.Buffer;
                PathNameLength = pReqEx->PathName.Length;
            }

            pResp = (QUERY_PATH_RESPONSE *)pIrp->UserBuffer;

            PrefixOK = vboxIsPrefixOK (FilePathName, PathNameLength);
            Log(("VBOXSF: MRxDeviceControl PrefixOK %d\n", PrefixOK));

            if (!PrefixOK)
            {
                /* Immediately fail the IOCTL with STATUS_BAD_NETWORK_NAME as recommended by MSDN.
                 * No need to involve RDBSS.
                 */
                Status = STATUS_BAD_NETWORK_NAME;

                pIrp->IoStatus.Status = Status;
                pIrp->IoStatus.Information = 0;

                IoCompleteRequest(pIrp, IO_NO_INCREMENT);

                Log(("VBOXSF: MRxDeviceControl: returned STATUS_BAD_NETWORK_NAME\n"));
                return Status;
            }

            Log(("VBOXSF: MRxDeviceControl pResp %p verifying the path.\n", pResp));
            if (pResp)
            {
                /* Always claim entire \vboxsrv prefix. The LengthAccepted initially is equal to entire path.
                 * Here it is assigned to the length of \vboxsrv prefix.
                 */
                pResp->LengthAccepted = 8 * sizeof (WCHAR);

                Status = STATUS_SUCCESS;

                pIrp->IoStatus.Status = Status;
                pIrp->IoStatus.Information = 0;

                IoCompleteRequest(pIrp, IO_NO_INCREMENT);

                Log(("VBOXSF: MRxDeviceControl: claiming the path.\n"));
                return Status;
            }

            /* No pResp pointer, should not happen. Just a precaution. */
            Status = STATUS_INVALID_PARAMETER;

            pIrp->IoStatus.Status = Status;
            pIrp->IoStatus.Information = 0;

            IoCompleteRequest(pIrp, IO_NO_INCREMENT);

            Log(("VBOXSF: MRxDeviceControl: returned STATUS_INVALID_PARAMETER\n"));
            return Status;
        }

        default:
            break;
    }

    /* Pass the IOCTL to RDBSS. */
    if (pDeviceExtension && pDeviceExtension->pfnRDBSSDeviceControl)
    {
        Log(("VBOXSF: MRxDeviceControl calling RDBSS %p\n", pDeviceExtension->pfnRDBSSDeviceControl));
        Status = pDeviceExtension->pfnRDBSSDeviceControl (pDevObj, pIrp);
        Log(("VBOXSF: MRxDeviceControl RDBSS status 0x%08X\n", Status));
    }
    else
    {
        /* No RDBSS, should not happen. Just a precaution. */
        Status = STATUS_NOT_IMPLEMENTED;

        pIrp->IoStatus.Status = Status;
        pIrp->IoStatus.Information = 0;

        IoCompleteRequest(pIrp, IO_NO_INCREMENT);

        Log(("VBOXSF: MRxDeviceControl: returned STATUS_NOT_IMPLEMENTED\n"));
    }

    return Status;
}

/**
 * Intercepts IRP_MJ_CREATE to workaround a RDBSS quirk.
 *
 * Our RDBSS library will return STATUS_OBJECT_NAME_INVALID when FILE_NON_DIRECTORY_FILE
 * is set and the path ends with a slash.  NTFS and FAT will fail with
 * STATUS_OBJECT_NAME_NOT_FOUND if the final component does not exist or isn't a directory,
 * STATUS_OBJECT_PATH_NOT_FOUND if some path component doesn't exist or isn't a directory,
 * or STATUS_ACCESS_DENIED if the final component is a directory.
 *
 * So, our HACK is to drop the trailing slash and set an unused flag in the ShareAccess
 * parameter to tell vbsfProcessCreate about it.
 *
 */
static NTSTATUS VBoxHookMjCreate(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PMRX_VBOX_DEVICE_EXTENSION  pDevExt  = (PMRX_VBOX_DEVICE_EXTENSION)((PBYTE)pDevObj + sizeof(RDBSS_DEVICE_OBJECT));
    PIO_STACK_LOCATION          pStack   = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT                pFileObj = pStack->FileObject;
    NTSTATUS                    rcNt;

    Log(("VBOXSF: VBoxHookMjCreate: pDevObj %p, pDevExt %p, pFileObj %p, options %#x, attr %#x, share %#x, ealength %#x, secctx %p, IrpFlags %#x\n",
         pDevObj, pDevObj->DeviceExtension, pFileObj, pStack->Parameters.Create.Options, pStack->Parameters.Create.FileAttributes,
         pStack->Parameters.Create.ShareAccess, pStack->Parameters.Create.EaLength, pStack->Parameters.Create.SecurityContext, pIrp->Flags));
    if (pFileObj)
        Log(("VBOXSF: VBoxHookMjCreate: FileName=%.*ls\n", pFileObj->FileName.Length / sizeof(WCHAR), pFileObj->FileName.Buffer));

    /*
     * Check if we need to apply the hack.  If we do, we grab a reference to
     * the file object to be absolutely sure it's around for the cleanup work.
     */
    AssertMsg(!(pStack->Parameters.Create.ShareAccess & VBOX_MJ_CREATE_SLASH_HACK), ("%#x\n", pStack->Parameters.Create.ShareAccess));
    if (   (pStack->Parameters.Create.Options & (FILE_NON_DIRECTORY_FILE | FILE_DIRECTORY_FILE)) == FILE_NON_DIRECTORY_FILE
        && pFileObj
        && pFileObj->FileName.Length > 18
        && pFileObj->FileName.Buffer
        && pFileObj->FileName.Buffer[pFileObj->FileName.Length / sizeof(WCHAR) - 1] == '\\'
        && pFileObj->FileName.Buffer[pFileObj->FileName.Length / sizeof(WCHAR) - 2] != '\\')
    {
        NTSTATUS rcNtRef = ObReferenceObjectByPointer(pFileObj, (ACCESS_MASK)0, *IoFileObjectType, KernelMode);
        pFileObj->FileName.Length -= 2;
        pStack->Parameters.Create.ShareAccess |= VBOX_MJ_CREATE_SLASH_HACK; /* secret flag for vbsfProcessCreate */

        rcNt = pDevExt->pfnRDBSSCreate(pDevObj, pIrp);

        if (rcNt != STATUS_PENDING)
            pStack->Parameters.Create.ShareAccess &= ~VBOX_MJ_CREATE_SLASH_HACK;
        if (NT_SUCCESS(rcNtRef))
        {
            pFileObj->FileName.Length += 2;
            ObDereferenceObject(pFileObj);
        }

        Log(("VBOXSF: VBoxHookMjCreate: returns %#x (hacked; rcNtRef=%#x)\n", rcNt, rcNtRef));
    }
    /*
     * No hack needed.
     */
    else
    {
        rcNt = pDevExt->pfnRDBSSCreate(pDevObj, pIrp);
        Log(("VBOXSF: VBoxHookMjCreate: returns %#x\n", rcNt));
    }
    return rcNt;
}

/**
 * Intercepts IRP_MJ_SET_INFORMATION to workaround a RDBSS quirk in the
 * FileEndOfFileInformation handling.
 *
 * We will add 4096 to the FileEndOfFileInformation function value and pick it
 * up in VBoxMRxSetFileInfo after RxCommonSetInformation has done the necessary
 * locking.  If we find that the desired file size matches the cached one, just
 * issue the call directly, otherwise subtract 4096 and call the
 * RxSetEndOfFileInfo worker.
 */
static NTSTATUS VBoxHookMjSetInformation(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PMRX_VBOX_DEVICE_EXTENSION  pDevExt  = (PMRX_VBOX_DEVICE_EXTENSION)((PBYTE)pDevObj + sizeof(RDBSS_DEVICE_OBJECT));
    PIO_STACK_LOCATION          pStack   = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT                pFileObj = pStack->FileObject;
    NTSTATUS                    rcNt;

    Log(("VBOXSF: VBoxHookMjSetInformation: pDevObj %p, pDevExt %p, pFileObj %p, FileInformationClass %d, Length %#x\n",
         pDevObj, pDevObj->DeviceExtension, pFileObj, pStack->Parameters.SetFile.FileInformationClass, pStack->Parameters.SetFile.Length));
    if (pFileObj)
        Log2(("VBOXSF: VBoxHookMjSetInformation: FileName=%.*ls\n", pFileObj->FileName.Length / sizeof(WCHAR), pFileObj->FileName.Buffer));

    /*
     * Setting EOF info?
     */
    if (pStack->Parameters.SetFile.FileInformationClass == FileEndOfFileInformation)
    {
#if 0 /* This only works for more recent versions of the RDBSS library, not for the one we're using (WDK 7600.16385.1). */
        pStack->Parameters.SetFile.FileInformationClass = (FILE_INFORMATION_CLASS)(FileEndOfFileInformation + 4096);
        rcNt = pDevExt->pfnRDBSSSetInformation(pDevObj, pIrp);
        Log(("VBOXSF: VBoxHookMjSetInformation: returns %#x (hacked)\n", rcNt));
        return rcNt;
#else
        /*
         * For the older WDK, we have to detect the same-size situation up front and hack
         * it here instead of in VBoxMRxSetFileInfo.  This means we need to lock the FCB
         * before modifying the Fcb.Header.FileSize value and ASSUME the locking is
         * reentrant and nothing else happens during RDBSS dispatching wrt that...
         */
        PMRX_FCB pFcb = (PMRX_FCB)pFileObj->FsContext;
        if (   (NODE_TYPE_CODE)pFcb->Header.NodeTypeCode == RDBSS_NTC_STORAGE_TYPE_FILE
            && pIrp->AssociatedIrp.SystemBuffer != NULL
            && pStack->Parameters.SetFile.Length >= sizeof(FILE_END_OF_FILE_INFORMATION))
        {
            LONGLONG cbFileNew = -42;
            __try
            {
                cbFileNew = ((PFILE_END_OF_FILE_INFORMATION)pIrp->AssociatedIrp.SystemBuffer)->EndOfFile.QuadPart;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                cbFileNew = -42;
            }
            if (   cbFileNew >= 0
                && pFcb->Header.FileSize.QuadPart == cbFileNew
                && !(pFcb->FcbState & FCB_STATE_PAGING_FILE))
            {
                /* Now exclusivly lock the FCB like RxCommonSetInformation would do
                   to reduce chances of races and of anyone else grabbing the value
                   while it's incorrect on purpose. */
                NTSTATUS rcNtLock = RxAcquireExclusiveFcb(NULL, (PFCB)pFcb);
                if (NT_SUCCESS(rcNtLock))
                {
                    if (pFcb->Header.FileSize.QuadPart == cbFileNew)
                    {
                        int64_t const cbHackedSize = cbFileNew ? cbFileNew - 1 : 1;
                        pFcb->Header.FileSize.QuadPart = cbHackedSize;
                        rcNt = pDevExt->pfnRDBSSSetInformation(pDevObj, pIrp);
                        if (   !NT_SUCCESS(rcNt)
                            && pFcb->Header.FileSize.QuadPart == cbHackedSize)
                            pFcb->Header.FileSize.QuadPart = cbFileNew;
# ifdef VBOX_STRICT
                        else
                        {
                            PMRX_FOBX pFobx = (PMRX_FOBX)pFileObj->FsContext2;
                            PMRX_VBOX_FOBX pVBoxFobX = VBoxMRxGetFileObjectExtension(pFobx);
                            Assert(   pFcb->Header.FileSize.QuadPart != cbHackedSize
                                   || (pVBoxFobX && pVBoxFobX->Info.cbObject == cbHackedSize));
                        }
# endif
                        RxReleaseFcb(NULL, pFcb);
                        Log(("VBOXSF: VBoxHookMjSetInformation: returns %#x (hacked, cbFileNew=%#RX64)\n", rcNt, cbFileNew));
                        return rcNt;
                    }
                    RxReleaseFcb(NULL, pFcb);
                }
            }
        }
#endif
    }

    /*
     * No hack needed.
     */
    rcNt = pDevExt->pfnRDBSSSetInformation(pDevObj, pIrp);
    Log(("VBOXSF: VBoxHookMjSetInformation: returns %#x\n", rcNt));
    return rcNt;
}


NTSTATUS VBoxMRxStart(PRX_CONTEXT RxContext, IN OUT PRDBSS_DEVICE_OBJECT RxDeviceObject)
{
    NTSTATUS Status;
    MRX_VBOX_STATE CurrentState;
    RT_NOREF(RxContext, RxDeviceObject);

    Log(("VBOXSF: MRxStart\n"));

    CurrentState = (MRX_VBOX_STATE)InterlockedCompareExchange((PLONG)&VBoxMRxState, MRX_VBOX_STARTED, MRX_VBOX_START_IN_PROGRESS);

    if (CurrentState == MRX_VBOX_START_IN_PROGRESS)
    {
        Log(("VBOXSF: MRxStart: Start in progress -> started\n"));
        Status = STATUS_SUCCESS;
    }
    else if (VBoxMRxState == MRX_VBOX_STARTED)
    {
        Log(("VBOXSF: MRxStart: Already started\n"));
        Status = STATUS_REDIRECTOR_STARTED;
    }
    else
    {
        Log(("VBOXSF: MRxStart: Bad state! VBoxMRxState = %d\n", VBoxMRxState));
        Status = STATUS_UNSUCCESSFUL;
    }

    return Status;
}

NTSTATUS VBoxMRxStop(PRX_CONTEXT RxContext, IN OUT PRDBSS_DEVICE_OBJECT RxDeviceObject)
{
    RT_NOREF(RxContext, RxDeviceObject);
    Log(("VBOXSF: MRxStop\n"));
    return STATUS_SUCCESS;
}

NTSTATUS VBoxMRxIoCtl(IN OUT PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxIoCtl: IoControlCode = 0x%08X\n", RxContext->LowIoContext.ParamsFor.FsCtl.FsControlCode));
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSYSAPI NTSTATUS NTAPI ZwSetSecurityObject(IN HANDLE Handle,
                                            IN SECURITY_INFORMATION SecurityInformation,
                                            IN PSECURITY_DESCRIPTOR SecurityDescriptor);

NTSTATUS VBoxMRxDevFcbXXXControlFile(IN OUT PRX_CONTEXT RxContext)
{
    NTSTATUS Status = STATUS_SUCCESS;
    RxCaptureFobx;
    PMRX_VBOX_DEVICE_EXTENSION pDeviceExtension = VBoxMRxGetDeviceExtension(RxContext);
    PLOWIO_CONTEXT LowIoContext = &RxContext->LowIoContext;

    Log(("VBOXSF: MRxDevFcbXXXControlFile: MajorFunction = 0x%02X\n",
         RxContext->MajorFunction));

    switch (RxContext->MajorFunction)
    {
        case IRP_MJ_FILE_SYSTEM_CONTROL:
        {
            Log(("VBOXSF: MRxDevFcbXXXControlFile: IRP_MN_USER_FS_REQUEST: 0x%08X\n",
                 LowIoContext->ParamsFor.FsCtl.MinorFunction));
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        case IRP_MJ_DEVICE_CONTROL:
        {
            Log(("VBOXSF: MRxDevFcbXXXControlFile: IRP_MJ_DEVICE_CONTROL: InputBuffer %p/%d, OutputBuffer %p/%d\n",
                 LowIoContext->ParamsFor.IoCtl.pInputBuffer,
                 LowIoContext->ParamsFor.IoCtl.InputBufferLength,
                 LowIoContext->ParamsFor.IoCtl.pOutputBuffer,
                 LowIoContext->ParamsFor.IoCtl.OutputBufferLength));

            switch (LowIoContext->ParamsFor.IoCtl.IoControlCode)
            {
                case IOCTL_MRX_VBOX_ADDCONN:
                {
                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_ADDCONN\n"));
                    Status = vbsfNtCreateConnection(RxContext, &RxContext->PostRequest);
                    break;
                }

                case IOCTL_MRX_VBOX_DELCONN:
                {
                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_DELCONN\n"));
                    Status = vbsfNtDeleteConnection(RxContext, &RxContext->PostRequest);
                    break;
                }

                case IOCTL_MRX_VBOX_GETLIST:
                {
                    ULONG cbOut = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    uint8_t *pu8Out = (uint8_t *)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    BOOLEAN fLocked = FALSE;

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETLIST\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbOut < _MRX_MAX_DRIVE_LETTERS)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETLIST: Copying local connections\n"));

                    fLocked = ExTryToAcquireFastMutex(&pDeviceExtension->mtxLocalCon);

                    __try
                    {
                        RtlCopyMemory(pu8Out, pDeviceExtension->cLocalConnections, _MRX_MAX_DRIVE_LETTERS);
                        RxContext->InformationToReturn = _MRX_MAX_DRIVE_LETTERS;
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                    }

                    if (fLocked)
                    {
                        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                        fLocked = FALSE;
                    }

                    break;
                }

                /*
                 * Returns the root IDs of shared folder mappings.
                 */
                case IOCTL_MRX_VBOX_GETGLOBALLIST:
                {
                    ULONG cbOut = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    uint8_t *pu8Out = (uint8_t *)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    int vrc;
                    SHFLMAPPING mappings[_MRX_MAX_DRIVE_LETTERS];
                    uint32_t cMappings = RT_ELEMENTS(mappings);

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETGLOBALLIST\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbOut < _MRX_MAX_DRIVE_LETTERS)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    vrc = VbglR0SfQueryMappings(&g_SfClient, mappings, &cMappings);
                    if (vrc == VINF_SUCCESS)
                    {
                        __try
                        {
                            uint32_t i;

                            RtlZeroMemory(pu8Out, _MRX_MAX_DRIVE_LETTERS);

                            for (i = 0; i < RT_MIN(cMappings, cbOut); i++)
                            {
                                pu8Out[i] = mappings[i].root;
                                pu8Out[i] |= 0x80; /* mark active */ /** @todo fix properly */
                            }

                            RxContext->InformationToReturn = _MRX_MAX_DRIVE_LETTERS;
                        }
                        __except(EXCEPTION_EXECUTE_HANDLER)
                        {
                            Status = STATUS_INVALID_PARAMETER;
                        }
                    }
                    else
                    {
                        Status = vbsfNtVBoxStatusToNt(vrc);
                        Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETGLOBALLIST failed: 0x%08X\n",
                             Status));
                    }

                    break;
                }

                /*
                 * Translates a local connection name (e.g. drive "S:") to the
                 * corresponding remote name (e.g. \\vboxsrv\share).
                 */
                case IOCTL_MRX_VBOX_GETCONN:
                {
                    ULONG cbConnectName = LowIoContext->ParamsFor.IoCtl.InputBufferLength;
                    PWCHAR pwcConnectName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pInputBuffer;
                    ULONG cbRemoteName = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    PWCHAR pwcRemoteName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    BOOLEAN fMutexAcquired = FALSE;

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETCONN\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbConnectName < sizeof(WCHAR))
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETCONN: Looking up connection name and connections\n"));

                    __try
                    {
                        uint32_t idx = *pwcConnectName - L'A';

                        Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETCONN: ConnectName = %.*ls, Len = %d, Index = %d\n",
                             cbConnectName / sizeof(WCHAR), pwcConnectName, cbConnectName, idx));

                        if (idx < RTL_NUMBER_OF(pDeviceExtension->wszLocalConnectionName))
                        {
                            ExAcquireFastMutex(&pDeviceExtension->mtxLocalCon);
                            fMutexAcquired = TRUE;

                            if (pDeviceExtension->wszLocalConnectionName[idx])
                            {
                                ULONG cbLocalConnectionName = pDeviceExtension->wszLocalConnectionName[idx]->Length;

                                Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETCONN: LocalConnectionName = %.*ls\n",
                                     cbLocalConnectionName / sizeof(WCHAR), pDeviceExtension->wszLocalConnectionName[idx]->Buffer));

                                if ((pDeviceExtension->cLocalConnections[idx]) && (cbLocalConnectionName <= cbRemoteName))
                                {
                                    RtlZeroMemory(pwcRemoteName, cbRemoteName);
                                    RtlCopyMemory(pwcRemoteName,
                                                  pDeviceExtension->wszLocalConnectionName[idx]->Buffer,
                                                  cbLocalConnectionName);

                                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETCONN: Remote name = %.*ls, Len = %d\n",
                                         cbLocalConnectionName / sizeof(WCHAR), pwcRemoteName, cbLocalConnectionName));
                                }
                                else
                                {
                                    Status = STATUS_BUFFER_TOO_SMALL;
                                }

                                RxContext->InformationToReturn = cbLocalConnectionName;
                            }
                            else
                            {
                                Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETCONN: LocalConnectionName is NULL!\n"));
                                Status = STATUS_BAD_NETWORK_NAME;
                            }
                        }
                        else
                        {
                            Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETCONN: Index is invalid!\n"));
                            Status = STATUS_INVALID_PARAMETER;
                        }
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                    }

                    if (fMutexAcquired)
                    {
                        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = FALSE;
                    }

                    break;
                }

                case IOCTL_MRX_VBOX_GETGLOBALCONN:
                {
                    ULONG cbConnectId = LowIoContext->ParamsFor.IoCtl.InputBufferLength;
                    uint8_t *pu8ConnectId = (uint8_t *)LowIoContext->ParamsFor.IoCtl.pInputBuffer;
                    ULONG cbRemoteName = LowIoContext->ParamsFor.IoCtl.OutputBufferLength;
                    PWCHAR pwcRemoteName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pOutputBuffer;

                    int vrc;
                    PSHFLSTRING pString;

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETGLOBALCONN\n"));

                    RxContext->InformationToReturn = 0;

                    if (   !pDeviceExtension
                        || cbConnectId < sizeof(uint8_t))
                    {
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    /* Allocate empty string where the host can store cbRemoteName bytes. */
                    Status = vbsfNtShflStringFromUnicodeAlloc(&pString, NULL, (uint16_t)cbRemoteName);
                    if (Status != STATUS_SUCCESS)
                        break;

                    __try
                    {
                        Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETGLOBALCONN: Connection ID = %d\n",
                             *pu8ConnectId));

                        vrc = VbglR0SfQueryMapName(&g_SfClient,
                                                   *pu8ConnectId & ~0x80 /** @todo fix properly */,
                                                   pString, ShflStringSizeOfBuffer(pString));
                        if (   vrc == VINF_SUCCESS
                            && pString->u16Length < cbRemoteName)
                        {
                            RtlCopyMemory(pwcRemoteName, pString->String.ucs2, pString->u16Length);
                            Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_GETGLOBALCONN: Returned name = %.*ls, Len = %d\n",
                                 pString->u16Length / sizeof(WCHAR), pwcRemoteName, pString->u16Length));
                            RxContext->InformationToReturn = pString->u16Length;
                        }
                        else
                        {
                            Status = STATUS_BAD_NETWORK_NAME;
                        }
                    }
                    __except(EXCEPTION_EXECUTE_HANDLER)
                    {
                        Status = STATUS_INVALID_PARAMETER;
                    }

                    vbsfNtFreeNonPagedMem(pString);

                    break;
                }

                case IOCTL_MRX_VBOX_START:
                {
                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: capFobx %p\n",
                         capFobx));

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: process: current 0x%X, RDBSS 0x%X\n",
                         IoGetCurrentProcess(), RxGetRDBSSProcess()));

                    switch (VBoxMRxState)
                    {
                        case MRX_VBOX_STARTABLE:

                            Log(("VBOXSF: MRxDevFcbXXXControlFile: MRX_VBOX_STARTABLE\n"));

                            if (capFobx)
                            {
                                Status = STATUS_INVALID_DEVICE_REQUEST;
                                break;;
                            }

                            InterlockedCompareExchange((PLONG)&VBoxMRxState, MRX_VBOX_START_IN_PROGRESS, MRX_VBOX_STARTABLE);

                        case MRX_VBOX_START_IN_PROGRESS:
                            Status = RxStartMinirdr(RxContext, &RxContext->PostRequest);

                            Log(("VBOXSF: MRxDevFcbXXXControlFile: MRX_VBOX_START_IN_PROGRESS RxStartMiniRdr Status 0x%08X, post %d\n",
                                 Status, RxContext->PostRequest));

                            if (Status == STATUS_REDIRECTOR_STARTED)
                            {
                                Status = STATUS_SUCCESS;
                                break;
                            }

                            if (   Status == STATUS_PENDING
                                && RxContext->PostRequest == TRUE)
                            {
                                /* Will be restarted in RDBSS process. */
                                Status = STATUS_MORE_PROCESSING_REQUIRED;
                                break;
                            }

#if 0 /* 2022-09-07 bird: I've disabled this as it prevents VBoxService from accessing the redirector on Windows XP RTM.
       *
       * The default is:     Administrators=RWDPO, Everyone=R, LOCAL=RW, RESTRICTED=R, SYSTEM=RWDPO
       * This changes it to: LOCAL=RW
       * (R=read, W=write, D=delete, P=Change Permissions, O=Change Owner. WinObj advanced permissions view.)
       *
       * I'm not sure if the intention here is to create a NULL DACL, which grants unrestricted access to the device.
       * However, it's somehow now working for Windows XP RTM.  I think an explicit call to RtlSetDaclSecurityDescriptor
       * is required for that to work, indicating that a DACL is present but NULL.
       * (See https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlsetdaclsecuritydescriptor and
       *  https://docs.microsoft.com/en-us/windows/win32/secauthz/null-dacls-and-empty-dacls for NULL DACL details.)
       *
       * If the problem is that RESTRICTED (S-1-5-12) should have RWS access, read the descriptor and modify the ACE
       * for RESTRICTED to grant them write access.
       */
                            /* Allow restricted users to use shared folders; works only in XP and Vista. (@@todo hack) */
                            if (Status == STATUS_SUCCESS)
                            {
                                SECURITY_DESCRIPTOR SecurityDescriptor;
                                OBJECT_ATTRIBUTES InitializedAttributes;
                                HANDLE hDevice;
                                IO_STATUS_BLOCK IoStatusBlock;
                                UNICODE_STRING UserModeDeviceName;

                                RtlInitUnicodeString(&UserModeDeviceName, DD_MRX_VBOX_USERMODE_SHADOW_DEV_NAME_U);

                                /* Create empty security descriptor */
                                RtlZeroMemory (&SecurityDescriptor, sizeof (SecurityDescriptor));
                                Status = RtlCreateSecurityDescriptor(&SecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: MRX_VBOX_START_IN_PROGRESS: RtlCreateSecurityDescriptor failed with 0x%08X!\n",
                                         Status));
                                    return Status;
                                }

                                RtlZeroMemory (&InitializedAttributes, sizeof (InitializedAttributes));
                                InitializeObjectAttributes(&InitializedAttributes, &UserModeDeviceName, OBJ_KERNEL_HANDLE, 0, 0);

                                /* Open our symbolic link device name */
                                Status = ZwOpenFile(&hDevice, WRITE_DAC, &InitializedAttributes, &IoStatusBlock, 0, 0);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: MRX_VBOX_START_IN_PROGRESS: ZwOpenFile %ls failed with 0x%08X!\n",
                                         DD_MRX_VBOX_USERMODE_SHADOW_DEV_NAME_U, Status));
                                    return Status;
                                }

                                /* Override the discretionary access control list (DACL) settings */
                                Status = ZwSetSecurityObject(hDevice, DACL_SECURITY_INFORMATION, &SecurityDescriptor);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: MRX_VBOX_START_IN_PROGRESS: ZwSetSecurityObject failed with 0x%08X!\n",
                                         Status));
                                    return Status;
                                }

                                Status = ZwClose(hDevice);
                                if (Status != STATUS_SUCCESS)
                                {
                                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: MRX_VBOX_START_IN_PROGRESS: ZwClose failed with 0x%08X\n",
                                         Status));
                                    return Status;
                                }
                            }
#endif
                            break;

                        case MRX_VBOX_STARTED:
                            Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: MRX_VBOX_STARTED: Already started\n"));
                            Status = STATUS_SUCCESS;
                            break;

                        default:
                            Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: Invalid state (%d)!\n",
                                 VBoxMRxState));
                            Status = STATUS_INVALID_PARAMETER;
                            break;
                    }

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_START: Returned 0x%08X\n",
                         Status));
                    break;
                }

                case IOCTL_MRX_VBOX_STOP:
                {
                    MRX_VBOX_STATE CurrentState;

                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_STOP: capFobx %p\n",
                         capFobx));

                    if (capFobx)
                    {
                        Status = STATUS_INVALID_DEVICE_REQUEST;
                        break;
                    }

                    if (RxContext->RxDeviceObject->NumberOfActiveFcbs > 0)
                    {
                        Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_STOP: Open handles = %d\n",
                             RxContext->RxDeviceObject->NumberOfActiveFcbs));
                        Status = STATUS_REDIRECTOR_HAS_OPEN_HANDLES;
                        break;
                    }

                    CurrentState = (MRX_VBOX_STATE)InterlockedCompareExchange((PLONG) & VBoxMRxState, MRX_VBOX_STARTABLE, MRX_VBOX_STARTED);

                    Status = RxStopMinirdr(RxContext, &RxContext->PostRequest);
                    Log(("VBOXSF: MRxDevFcbXXXControlFile: IOCTL_MRX_VBOX_STOP: Returned 0x%08X\n",
                         Status));

                    if (Status == STATUS_PENDING && RxContext->PostRequest == TRUE)
                        Status = STATUS_MORE_PROCESSING_REQUIRED;
                    break;
                }

                default:
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;
        }

        case IRP_MJ_INTERNAL_DEVICE_CONTROL:
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        default:
            Log(("VBOXSF: MRxDevFcbXXXControlFile: unimplemented major function 0x%02X\n",
                 RxContext->MajorFunction));
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Log(("VBOXSF: MRxDevFcbXXXControlFile: Status = 0x%08X, Info = 0x%08X\n",
         Status, RxContext->InformationToReturn));

    return Status;
}

static NTSTATUS vbsfVerifyConnectionName(PUNICODE_STRING ConnectionName)
{
    /* Check that the connection name is valid:
     * "\Device\VBoxMiniRdr\;X:\vboxsvr\sf"
     */
    NTSTATUS Status = STATUS_BAD_NETWORK_NAME;

    static WCHAR const s_wszPrefix[] = L"\\Device\\VBoxMiniRdr\\;";

    /* Unicode chars in the string. */
    ULONG cConnectionName = ConnectionName->Length / sizeof(WCHAR);
    ULONG cRemainingName;

    /* Check that the name starts with correct prefix. */
    WCHAR const *pwc1 = &s_wszPrefix[0];
    WCHAR const *pwc  = ConnectionName->Buffer;
    ULONG   i;
    for (i = 0; i < cConnectionName; i++, pwc1++, pwc++)
    {
        if (*pwc1 == 0 || *pwc == 0 || *pwc1 != *pwc)
            break;
    }

    cRemainingName = cConnectionName - i;

    Log(("VBOXSF: vbsfVerifyConnectionName: prefix %d remaining %d [%.*ls]\n",
         *pwc1 == 0, cRemainingName, cRemainingName, &ConnectionName->Buffer[i]));

    if (*pwc1 == 0)
    {
        /* pwc should point to a drive letter followed by ':\' that is at least 3 chars more. */
        if (cRemainingName >= 3)
        {
           if (   pwc[0] >= L'A' && pwc[0] <= L'Z'
               && pwc[1] == L':')
           {
               pwc += 2;
               cRemainingName -= 2;

               /** @todo should also check that the drive letter corresponds to the name. */
               if (vboxIsPrefixOK(pwc, cRemainingName * sizeof (WCHAR)))
                   Status = STATUS_SUCCESS;
           }
        }
    }

    return Status;
}

static HANDLE vbsfOpenConnectionHandle(PUNICODE_STRING ConnectionName, NTSTATUS *prcNt)
{
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    OBJECT_ATTRIBUTES ObjectAttributes;

    HANDLE Handle = INVALID_HANDLE_VALUE;

    Log(("VBOXSF: vbsfOpenConnectionHandle: ConnectionName = %.*ls\n",
         ConnectionName->Length / sizeof(WCHAR), ConnectionName->Buffer));

    Status = vbsfVerifyConnectionName(ConnectionName);

    if (NT_SUCCESS(Status))
    {
        /* Have to create a OBJ_KERNEL_HANDLE. Otherwise the driver verifier on Windows 7 bugchecks. */
        InitializeObjectAttributes(&ObjectAttributes,
                                   ConnectionName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);

        Status = ZwCreateFile(&Handle,
                              SYNCHRONIZE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              NULL,
                              FILE_ATTRIBUTE_NORMAL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              FILE_OPEN_IF,
                              FILE_CREATE_TREE_CONNECTION | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL,
                              0);
    }

    if (   Status != STATUS_SUCCESS
        || Handle == INVALID_HANDLE_VALUE)
    {
        Log(("VBOXSF: vbsfOpenConnectionHandle: ZwCreateFile failed status 0x%08X or invalid handle!\n", Status));
        if (prcNt)
            *prcNt = !NT_SUCCESS(Status) ? Status : STATUS_UNSUCCESSFUL;
        Handle = INVALID_HANDLE_VALUE;
    }

    return Handle;
}

NTSTATUS vbsfNtCreateConnection(IN PRX_CONTEXT RxContext, OUT PBOOLEAN PostToFsp)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PMRX_VBOX_DEVICE_EXTENSION pDeviceExtension;

    PLOWIO_CONTEXT LowIoContext;
    ULONG cbConnectName;
    PWCHAR pwcConnectName;

    HANDLE Handle;
    UNICODE_STRING FileName;

    BOOLEAN fMutexAcquired = FALSE;

    Log(("VBOXSF: vbsfNtCreateConnection\n"));

    if (!BooleanFlagOn(RxContext->Flags, RX_CONTEXT_FLAG_WAIT))
    {
        Log(("VBOXSF: vbsfNtCreateConnection: post to file system process\n"));
        *PostToFsp = TRUE;
        return STATUS_PENDING;
    }

    pDeviceExtension = VBoxMRxGetDeviceExtension(RxContext);
    if (!pDeviceExtension)
        return STATUS_INVALID_PARAMETER;

    LowIoContext = &RxContext->LowIoContext;
    cbConnectName = LowIoContext->ParamsFor.IoCtl.InputBufferLength;
    pwcConnectName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pInputBuffer;

    if (cbConnectName == 0 || !pwcConnectName)
    {
        Log(("VBOXSF: vbsfNtCreateConnection: Connection name / length is invalid!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    __try
    {
        Log(("VBOXSF: vbsfNtCreateConnection: Name = %.*ls, Len = %d\n",
             cbConnectName / sizeof(WCHAR), pwcConnectName, cbConnectName));

        FileName.Buffer = pwcConnectName;
        FileName.Length = (USHORT)cbConnectName;
        FileName.MaximumLength = (USHORT)cbConnectName;

        Handle = vbsfOpenConnectionHandle(&FileName, NULL);

        if (Handle != INVALID_HANDLE_VALUE)
        {
            PWCHAR pwc;
            ULONG i;

            ZwClose(Handle);

            /* Skip the "\Device\VBoxMiniRdr\;X:" of the string "\Device\VBoxMiniRdr\;X:\vboxsrv\sf" */
            pwc = pwcConnectName;
            for (i = 0; i < cbConnectName; i += sizeof(WCHAR))
            {
                if (*pwc == L':')
                    break;
                pwc++;
            }

            if (i >= sizeof(WCHAR) && i < cbConnectName)
            {
                pwc--; /* Go back to the drive letter, "X" for example. */

                if (*pwc >= L'A' && *pwc <= L'Z') /* Are we in range? */
                {
                    uint32_t idx = *pwc - L'A'; /* Get the index based on the driver letter numbers (26). */

                    if (idx >= RTL_NUMBER_OF(pDeviceExtension->cLocalConnections))
                    {
                        Log(("VBOXSF: vbsfNtCreateConnection: Index 0x%x is invalid!\n",
                             idx));
                        Status = STATUS_BAD_NETWORK_NAME;
                    }
                    else
                    {
                        ExAcquireFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = TRUE;

                        if (pDeviceExtension->wszLocalConnectionName[idx] != NULL)
                        {
                            Log(("VBOXSF: vbsfNtCreateConnection: LocalConnectionName at index %d is NOT empty!\n",
                                 idx));
                        }

                        pDeviceExtension->wszLocalConnectionName[idx] = (PUNICODE_STRING)vbsfNtAllocNonPagedMem(sizeof(UNICODE_STRING) + cbConnectName);

                        if (!pDeviceExtension->wszLocalConnectionName[idx])
                        {
                            Log(("VBOXSF: vbsfNtCreateConnection: LocalConnectionName at index %d NOT allocated!\n",
                                 idx));
                            Status = STATUS_INSUFFICIENT_RESOURCES;
                        }
                        else
                        {
                            PUNICODE_STRING pRemoteName = pDeviceExtension->wszLocalConnectionName[idx];

                            pRemoteName->Buffer = (PWSTR)(pRemoteName + 1);
                            pRemoteName->Length = (USHORT)(cbConnectName - i - sizeof(WCHAR));
                            pRemoteName->MaximumLength = pRemoteName->Length;
                            RtlCopyMemory(&pRemoteName->Buffer[0], pwc+2, pRemoteName->Length);

                            Log(("VBOXSF: vbsfNtCreateConnection: RemoteName %.*ls, Len = %d\n",
                                 pRemoteName->Length / sizeof(WCHAR), pRemoteName->Buffer, pRemoteName->Length));

                            pDeviceExtension->cLocalConnections[idx] = TRUE;
                        }

                        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                        fMutexAcquired = FALSE;
                    }
                }
            }
            else
            {
                Log(("VBOXSF: vbsfNtCreateConnection: bad format\n"));
                Status = STATUS_BAD_NETWORK_NAME;
            }
        }
        else
        {
            Log(("VBOXSF: vbsfNtCreateConnection: connection was not found\n"));
            Status = STATUS_BAD_NETWORK_NAME;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (fMutexAcquired)
    {
        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
        fMutexAcquired = FALSE;
    }

    return Status;
}

NTSTATUS vbsfNtDeleteConnection(IN PRX_CONTEXT RxContext, OUT PBOOLEAN PostToFsp)
{
    NTSTATUS Status;
    UNICODE_STRING FileName;
    HANDLE Handle;
    PLOWIO_CONTEXT LowIoContext;
    PWCHAR pwcConnectName;
    ULONG cbConnectName;
    PMRX_VBOX_DEVICE_EXTENSION pDeviceExtension;

    BOOLEAN fMutexAcquired = FALSE;

    Log(("VBOXSF: vbsfNtDeleteConnection\n"));

    if (!BooleanFlagOn(RxContext->Flags, RX_CONTEXT_FLAG_WAIT))
    {
        Log(("VBOXSF: vbsfNtDeleteConnection: post to file system process\n"));
        *PostToFsp = TRUE;
        return STATUS_PENDING;
    }

    LowIoContext = &RxContext->LowIoContext;
    pwcConnectName = (PWCHAR)LowIoContext->ParamsFor.IoCtl.pInputBuffer;
    cbConnectName = LowIoContext->ParamsFor.IoCtl.InputBufferLength;

    pDeviceExtension = VBoxMRxGetDeviceExtension(RxContext);
    if (!pDeviceExtension)
        return STATUS_INVALID_PARAMETER;

    __try
    {
        Log(("VBOXSF: vbsfNtDeleteConnection: pwcConnectName = %.*ls\n",
             cbConnectName / sizeof(WCHAR), pwcConnectName));

        FileName.Buffer = pwcConnectName;
        FileName.Length = (USHORT)cbConnectName;
        FileName.MaximumLength = (USHORT)cbConnectName;

        Handle = vbsfOpenConnectionHandle(&FileName, &Status);
        if (Handle != INVALID_HANDLE_VALUE)
        {
            PFILE_OBJECT pFileObject;
            Status = ObReferenceObjectByHandle(Handle, 0L, NULL, KernelMode, (PVOID *)&pFileObject, NULL);

            Log(("VBOXSF: vbsfNtDeleteConnection: ObReferenceObjectByHandle Status 0x%08X\n",
                 Status));

            if (NT_SUCCESS(Status))
            {
                PFOBX Fobx = (PFOBX)pFileObject->FsContext2;
                Log(("VBOXSF: vbsfNtDeleteConnection: Fobx %p\n", Fobx));

                if (Fobx && NodeType(Fobx) == RDBSS_NTC_V_NETROOT)
                {
                    PV_NET_ROOT VNetRoot = (PV_NET_ROOT)Fobx;

#ifdef __cplusplus /* C version points at NET_ROOT, C++ points at MRX_NET_ROOT. Weird. */
                    Status = RxFinalizeConnection((PNET_ROOT)VNetRoot->pNetRoot, VNetRoot, TRUE);
#else
                    Status = RxFinalizeConnection(VNetRoot->NetRoot, VNetRoot, TRUE);
#endif
                }
                else
                {
                    Log(("VBOXSF: vbsfNtDeleteConnection: wrong FsContext2\n"));
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                }

                ObDereferenceObject(pFileObject);
            }

            ZwClose(Handle);

            if (NT_SUCCESS(Status))
            {
                PWCHAR pwc;
                ULONG i;

                /* Skip the "\Device\VBoxMiniRdr\;X:" of the string "\Device\VBoxMiniRdr\;X:\vboxsrv\sf" */
                pwc = pwcConnectName;
                for (i = 0; i < cbConnectName; i += sizeof(WCHAR))
                {
                    if (*pwc == L':')
                    {
                        break;
                    }
                    pwc++;
                }

                if (i >= sizeof(WCHAR) && i < cbConnectName)
                {
                    pwc--;

                    if (*pwc >= L'A' && *pwc <= L'Z')
                    {
                        uint32_t idx = *pwc - L'A';

                        if (idx >= RTL_NUMBER_OF(pDeviceExtension->cLocalConnections))
                        {
                            Log(("VBOXSF: vbsfNtDeleteConnection: Index 0x%x is invalid!\n",
                                 idx));
                            Status = STATUS_BAD_NETWORK_NAME;
                        }
                        else
                        {
                            ExAcquireFastMutex(&pDeviceExtension->mtxLocalCon);
                            fMutexAcquired = TRUE;

                            pDeviceExtension->cLocalConnections[idx] = FALSE;

                            /* Free saved name */
                            if (pDeviceExtension->wszLocalConnectionName[idx])
                            {
                                vbsfNtFreeNonPagedMem(pDeviceExtension->wszLocalConnectionName[idx]);
                                pDeviceExtension->wszLocalConnectionName[idx] = NULL;
                            }

                            ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
                            fMutexAcquired = FALSE;

                            Log(("VBOXSF: vbsfNtDeleteConnection: deleted index 0x%x\n",
                                 idx));
                        }
                    }
                }
                else
                {
                    Log(("VBOXSF: vbsfNtCreateConnection: bad format\n"));
                    Status = STATUS_BAD_NETWORK_NAME;
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (fMutexAcquired)
    {
        ExReleaseFastMutex(&pDeviceExtension->mtxLocalCon);
        fMutexAcquired = FALSE;
    }

    Log(("VBOXSF: vbsfNtDeleteConnection: Status 0x%08X\n", Status));
    return Status;
}

NTSTATUS VBoxMRxQueryEaInfo(IN OUT PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxQueryEaInfo: Ea buffer len remaining is %d\n", RxContext->Info.LengthRemaining));
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS VBoxMRxSetEaInfo(IN OUT PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxSetEaInfo\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS VBoxMRxFsCtl(IN OUT PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxFsCtl\n"));
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS VBoxMRxNotifyChangeDirectory(IN OUT PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxNotifyChangeDirectory\n"));
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS vbsfQuerySdInfo(PVOID pvBuffer, ULONG cbBuffer, SECURITY_INFORMATION SecurityInformation, ULONG *pcbOut)
{
    /* What a public SMB share would return. */
    static SID_IDENTIFIER_AUTHORITY sIA = SECURITY_NT_AUTHORITY;
    #define SUB_AUTHORITY_COUNT 2
    static const ULONG saSubAuthorityOwner[] = { SECURITY_NT_NON_UNIQUE, DOMAIN_USER_RID_GUEST   };
    static const ULONG saSubAuthorityGroup[] = { SECURITY_NT_NON_UNIQUE, DOMAIN_GROUP_RID_GUESTS };

    SECURITY_DESCRIPTOR_RELATIVE *pSD = (SECURITY_DESCRIPTOR_RELATIVE *)pvBuffer;
    ULONG cbSD = 0; /* Size of returned security descriptor. */
    ULONG cbAdd; /* How many bytes to add to the buffer for each component of the security descriptor. */

    cbAdd = sizeof(SECURITY_DESCRIPTOR_RELATIVE);
    if (cbSD + cbAdd <= cbBuffer)
    {
        pSD->Revision = SECURITY_DESCRIPTOR_REVISION1;
        pSD->Sbz1     = 0;
        pSD->Control  = SE_SELF_RELATIVE;
        pSD->Owner    = 0;
        pSD->Group    = 0;
        pSD->Sacl     = 0;
        pSD->Dacl     = 0;
    }
    cbSD += cbAdd;

    if (SecurityInformation & OWNER_SECURITY_INFORMATION)
    {
        cbAdd = RT_UOFFSETOF(SID, SubAuthority) + SUB_AUTHORITY_COUNT * sizeof(ULONG);
        if (cbSD + cbAdd <= cbBuffer)
        {
            SID *pSID = (SID *)((uint8_t *)pSD + cbSD);
            pSID->Revision            = 1;
            pSID->SubAuthorityCount   = SUB_AUTHORITY_COUNT;
            pSID->IdentifierAuthority = sIA;
            memcpy(pSID->SubAuthority, saSubAuthorityOwner, SUB_AUTHORITY_COUNT * sizeof(ULONG));

            pSD->Owner = cbSD;
        }
        cbSD += cbAdd;
    }

    if (SecurityInformation & GROUP_SECURITY_INFORMATION)
    {
        cbAdd = RT_UOFFSETOF(SID, SubAuthority) + SUB_AUTHORITY_COUNT * sizeof(ULONG);
        if (cbSD + cbAdd <= cbBuffer)
        {
            SID *pSID = (SID *)((uint8_t *)pSD + cbSD);
            pSID->Revision            = 1;
            pSID->SubAuthorityCount   = SUB_AUTHORITY_COUNT;
            pSID->IdentifierAuthority = sIA;
            memcpy(pSID->SubAuthority, saSubAuthorityGroup, SUB_AUTHORITY_COUNT * sizeof(ULONG));

            pSD->Group = cbSD;
        }
        cbSD += cbAdd;
    }

    #undef SUB_AUTHORITY_COUNT

    *pcbOut = cbSD;
    return STATUS_SUCCESS;
}

NTSTATUS VBoxMRxQuerySdInfo(IN OUT PRX_CONTEXT RxContext)
{
    NTSTATUS Status;

    PVOID pvBuffer = RxContext->Info.Buffer;
    ULONG cbBuffer = RxContext->Info.LengthRemaining;
    SECURITY_INFORMATION SecurityInformation = RxContext->QuerySecurity.SecurityInformation;

    ULONG cbSD = 0;

    Log(("VBOXSF: MRxQuerySdInfo: Buffer %p, Length %d, SecurityInformation 0x%x\n",
         pvBuffer, cbBuffer, SecurityInformation));

    Status = vbsfQuerySdInfo(pvBuffer, cbBuffer, SecurityInformation, &cbSD);
    if (NT_SUCCESS(Status))
    {
        RxContext->InformationToReturn = cbSD;
        if (RxContext->InformationToReturn > cbBuffer)
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
    }

    Log(("VBOXSF: MRxQuerySdInfo: Status 0x%08X, InformationToReturn %d\n",
         Status, RxContext->InformationToReturn));
    return Status;
}

NTSTATUS VBoxMRxSetSdInfo(IN OUT struct _RX_CONTEXT * RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxSetSdInfo\n"));
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * WML stubs which are referenced by rdbsslib.
 */
extern "C" NTSTATUS WmlTinySystemControl(IN OUT PVOID pWmiLibInfo, IN PVOID pDevObj, IN PVOID pIrp)
{
    RT_NOREF(pWmiLibInfo, pDevObj, pIrp);
    return STATUS_WMI_GUID_NOT_FOUND;
}

extern "C" ULONG WmlTrace(IN ULONG ulType, IN PVOID pTraceUuid, IN ULONG64 ullLogger, ...)
{
    RT_NOREF(ulType, pTraceUuid, ullLogger);
    return STATUS_SUCCESS;
}


/**
 * The "main" function for a driver binary.
 */
extern "C" NTSTATUS NTAPI DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    Log(("VBOXSF: DriverEntry: Driver object %p\n", DriverObject));
    AssertLogRelReturn(DriverObject, STATUS_UNSUCCESSFUL);

    /*
     * Initialize IPRT and Vbgl.
     */
    NTSTATUS rcNt = STATUS_UNSUCCESSFUL;
    int vrc = RTR0Init(0);
    if (RT_SUCCESS(vrc))
    {
        vrc = VbglR0SfInit();
        if (RT_SUCCESS(vrc))
        {
            /*
             * Connect to the shared folder service on the host.
             */
            vrc = VbglR0SfConnect(&g_SfClient);
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Query the features and check that the host does page lists as we need those
                 * for reading and writing.
                 */
                vrc = VbglR0QueryHostFeatures(&g_fHostFeatures);
                if (RT_FAILURE(vrc))
                {
                    LogRel(("vboxsf: VbglR0QueryHostFeatures failed: vrc=%Rrc (ignored)\n", vrc));
                    g_fHostFeatures = 0;
                }
                VbglR0SfHostReqQueryFeaturesSimple(&g_fSfFeatures, &g_uSfLastFunction);
                LogRel(("VBoxSF: %s r%s g_fHostFeatures=%#x g_fSfFeatures=%#RX64 g_uSfLastFunction=%u\n",
                        RTBldCfgVersion(), RTBldCfgRevisionStr(), g_fHostFeatures, g_fSfFeatures, g_uSfLastFunction));

                if (VbglR0CanUsePhysPageList())
                {
                    /*
                     * Tell the host to return windows-style errors (non-fatal).
                     */
                    if (g_uSfLastFunction >= SHFL_FN_SET_ERROR_STYLE)
                    {
                        vrc = VbglR0SfHostReqSetErrorStyleSimple(kShflErrorStyle_Windows);
                        if (RT_FAILURE(vrc))
                            LogRel(("VBoxSF: VbglR0HostReqSetErrorStyleSimple(windows) failed: %Rrc\n", vrc));
                    }

                    /*
                     * Resolve newer kernel APIs we might want to use.
                     * Note! Because of http://www.osronline.com/article.cfm%5eid=494.htm we cannot
                     *       use MmGetSystemRoutineAddress here as it will crash on xpsp2.
                     */
                    RTDBGKRNLINFO hKrnlInfo;
                    vrc = RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0/*fFlags*/);
                    AssertLogRelRC(vrc);
                    if (RT_SUCCESS(vrc))
                    {
                        g_pfnCcCoherencyFlushAndPurgeCache
                            = (PFNCCCOHERENCYFLUSHANDPURGECACHE)RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL,
                                                                                         "CcCoherencyFlushAndPurgeCache");
                        RTR0DbgKrnlInfoRelease(hKrnlInfo);
                    }

                    /*
                     * Init the driver object.
                     */
                    DriverObject->DriverUnload = VBoxMRxUnload;
                    for (size_t i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
                        DriverObject->MajorFunction[i] = (PDRIVER_DISPATCH)VBoxMRxFsdDispatch;

                    /*
                     * Do RDBSS driver entry processing.
                     */
                    rcNt = RxDriverEntry(DriverObject, RegistryPath);
                    if (rcNt == STATUS_SUCCESS)
                    {
                        /*
                         * Do the mini redirector registration.
                         * Note! Don't use RX_REGISTERMINI_FLAG_DONT_PROVIDE_UNCS or else UNC
                         *       mappings don't work (including Windows explorer browsing).
                         */
                        Log(("VBOXSF: DriverEntry: RxRegisterMinirdr: calling VBoxMRxDeviceObject %p\n", VBoxMRxDeviceObject));
                        UNICODE_STRING VBoxMRxName;
                        RtlInitUnicodeString(&VBoxMRxName, DD_MRX_VBOX_FS_DEVICE_NAME_U);
                        rcNt = RxRegisterMinirdr(&VBoxMRxDeviceObject,
                                                 DriverObject,
                                                 &VBoxMRxDispatch,
                                                 RX_REGISTERMINI_FLAG_DONT_PROVIDE_MAILSLOTS,
                                                 &VBoxMRxName,
                                                 sizeof(MRX_VBOX_DEVICE_EXTENSION),
                                                 FILE_DEVICE_NETWORK_FILE_SYSTEM,
                                                 FILE_REMOTE_DEVICE);
                        Log(("VBOXSF: DriverEntry: RxRegisterMinirdr: returned 0x%08X VBoxMRxDeviceObject %p\n",
                             rcNt, VBoxMRxDeviceObject));
                        if (rcNt == STATUS_SUCCESS)
                        {
                            /*
                             * Init the device extension.
                             *
                             * Note! The device extension actually points to fields in the RDBSS_DEVICE_OBJECT.
                             *       Our space is past the end of that struct!!
                             */
                            PMRX_VBOX_DEVICE_EXTENSION pVBoxDevX = (PMRX_VBOX_DEVICE_EXTENSION)(  (PBYTE)VBoxMRxDeviceObject
                                                                                                + sizeof(RDBSS_DEVICE_OBJECT));
                            pVBoxDevX->pDeviceObject = VBoxMRxDeviceObject;
                            for (size_t i = 0; i < RT_ELEMENTS(pVBoxDevX->cLocalConnections); i++)
                                pVBoxDevX->cLocalConnections[i] = FALSE;

                            /* Mutex for synchronizining our connection list */
                            ExInitializeFastMutex(&pVBoxDevX->mtxLocalCon);

                            /*
                             * The device object has been created. Need to setup a symbolic link
                             * in the Win32 name space for user mode applications.
                             */
                            UNICODE_STRING UserModeDeviceName;
                            RtlInitUnicodeString(&UserModeDeviceName, DD_MRX_VBOX_USERMODE_SHADOW_DEV_NAME_U);
                            Log(("VBOXSF: DriverEntry: Calling IoCreateSymbolicLink\n"));
                            rcNt = IoCreateSymbolicLink(&UserModeDeviceName, &VBoxMRxName);
                            if (rcNt == STATUS_SUCCESS)
                            {
                                Log(("VBOXSF: DriverEntry: Symbolic link created.\n"));

                                /*
                                 * Build the dispatch tables for the minirdr
                                 */
                                vbsfInitMRxDispatch();

                                /*
                                 * The redirector driver must intercept the IOCTL to avoid VBOXSVR name resolution
                                 * by other redirectors. These additional name resolutions cause long delays.
                                 */
                                Log(("VBOXSF: DriverEntry: VBoxMRxDeviceObject = %p, rdbss %p, devext %p\n",
                                     VBoxMRxDeviceObject, DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL], pVBoxDevX));
                                pVBoxDevX->pfnRDBSSDeviceControl = DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];
                                DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = VBoxMRXDeviceControl;

                                /*
                                 * Intercept IRP_MJ_CREATE to fix incorrect (wrt NTFS, FAT, ++) return
                                 * codes for NtOpenFile("r:\\asdf\\", FILE_NON_DIRECTORY_FILE).
                                 */
                                pVBoxDevX->pfnRDBSSCreate = DriverObject->MajorFunction[IRP_MJ_CREATE];
                                DriverObject->MajorFunction[IRP_MJ_CREATE] = VBoxHookMjCreate;

                                /*
                                 * Intercept IRP_MJ_SET_INFORMATION to ensure we call the host for all
                                 * FileEndOfFileInformation requestes, even if the new size matches the
                                 * old one.  We don't know if someone else might have modified the file
                                 * size cached in the FCB since the last time we update it.
                                 */
                                pVBoxDevX->pfnRDBSSSetInformation = DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION];
                                DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = VBoxHookMjSetInformation;

                                /** @todo start the redirector here RxStartMiniRdr. */

                                Log(("VBOXSF: DriverEntry: Init successful!\n"));
                                return STATUS_SUCCESS;
                            }
                            LogRel(("VBOXSF: DriverEntry: IoCreateSymbolicLink: %#x\n", rcNt));

                            RxUnregisterMinirdr(VBoxMRxDeviceObject);
                            VBoxMRxDeviceObject = NULL;
                        }
                        else
                            LogRel(("VBOXSF: DriverEntry: RxRegisterMinirdr failed: %#x\n", rcNt));
                    }
                    else
                        LogRel(("VBOXSF: DriverEntry: RxDriverEntry failed: 0x%08X\n", rcNt));
                }
                else
                    LogRel(("VBOXSF: Host does not support physical page lists.  Refusing to load!\n"));
                VbglR0SfDisconnect(&g_SfClient);
            }
            else
                LogRel(("VBOXSF: DriverEntry: Failed to connect to the host: %Rrc! (%s r%s)\n",
                        vrc, RTBldCfgVersion(), RTBldCfgRevisionStr()));
            VbglR0SfTerm();
        }
        else
            LogRel(("VBOXSF: DriverEntry: VbglR0SfInit! %Rrc! (%s r%s)\n", vrc, RTBldCfgVersion(), RTBldCfgRevisionStr()));
        RTR0Term();
    }
    else
        RTLogRelPrintf("VBOXSF: DriverEntry: RTR0Init failed! %Rrc! (%s r%s)\n", vrc, RTBldCfgVersion(), RTBldCfgRevisionStr());
    return rcNt;
}

