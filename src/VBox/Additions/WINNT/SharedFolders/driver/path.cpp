/* $Id: path.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders - Path related routines.
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
#include <iprt/err.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
static UNICODE_STRING g_UnicodeBackslash = { 2, 4, (PWSTR)L"\\" };


/**
 * Handles failure scenarios where we may have to close the handle.
 */
DECL_NO_INLINE(static, NTSTATUS) vbsfNtCreateWorkerBail(NTSTATUS Status, VBOXSFCREATEREQ *pReq,
                                                        PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension)
{
    Log(("VBOXSF: vbsfNtCreateWorker: Returns %#x (Handle was %#RX64)\n", Status, pReq->CreateParms.Handle));
    if (pReq->CreateParms.Handle != SHFL_HANDLE_NIL)
    {
        AssertCompile(sizeof(VBOXSFCLOSEREQ) <= RT_UOFFSETOF(VBOXSFCREATEREQ, CreateParms));
        VbglR0SfHostReqClose(pNetRootExtension->map.root, (VBOXSFCLOSEREQ *)pReq, pReq->CreateParms.Handle);
    }
    return Status;
}


/**
 * Worker for VBoxMRxCreate that converts parameters and calls the host.
 *
 * The caller takes care of freeing the request buffer, so this function is free
 * to just return at will.
 */
static NTSTATUS vbsfNtCreateWorker(PRX_CONTEXT RxContext, VBOXSFCREATEREQ *pReq, ULONG *pulCreateAction,
                                   PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension, PMRX_FCB pFcb)
{
    /*
     * Check out the options.
     */
    ULONG const fOptions            = RxContext->Create.NtCreateParameters.CreateOptions & FILE_VALID_OPTION_FLAGS;
    ULONG const CreateDisposition   = RxContext->Create.NtCreateParameters.Disposition;
    bool const  fCreateDir          = (fOptions & FILE_DIRECTORY_FILE)
                                   && (CreateDisposition == FILE_CREATE || CreateDisposition == FILE_OPEN_IF);
    bool const  fTemporaryFile      = (RxContext->Create.NtCreateParameters.FileAttributes & FILE_ATTRIBUTE_TEMPORARY)
                                   || (pFcb->FcbState & FCB_STATE_TEMPORARY);

    Log(("VBOXSF: vbsfNtCreateWorker: fTemporaryFile %d, fCreateDir %d%s%s%s\n", fTemporaryFile, fCreateDir,
         fOptions & FILE_DIRECTORY_FILE ? ", FILE_DIRECTORY_FILE" : "",
         fOptions & FILE_NON_DIRECTORY_FILE ? ", FILE_NON_DIRECTORY_FILE" : "",
         fOptions & FILE_DELETE_ON_CLOSE ? ", FILE_DELETE_ON_CLOSE" : ""));

    /* Check consistency in specified flags. */
    if (fTemporaryFile && fCreateDir) /* Directories with temporary flag set are not allowed! */
    {
        Log(("VBOXSF: vbsfNtCreateWorker: Not allowed: Temporary directories!\n"));
        return STATUS_INVALID_PARAMETER;
    }

    if ((fOptions & (FILE_DIRECTORY_FILE | FILE_NON_DIRECTORY_FILE)) == (FILE_DIRECTORY_FILE | FILE_NON_DIRECTORY_FILE))
    {
        /** @todo r=bird: Check if FILE_DIRECTORY_FILE+FILE_NON_DIRECTORY_FILE really is illegal in all combinations... */
        Log(("VBOXSF: vbsfNtCreateWorker: Unsupported combination: dir && !dir\n"));
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Initialize create parameters.
     */
    RT_ZERO(pReq->CreateParms);
    pReq->CreateParms.Handle = SHFL_HANDLE_NIL;
    pReq->CreateParms.Result = SHFL_NO_RESULT;

    /*
     * Directory.
     */
    if (fOptions & FILE_DIRECTORY_FILE)
    {
        if (CreateDisposition != FILE_CREATE && CreateDisposition != FILE_OPEN && CreateDisposition != FILE_OPEN_IF)
        {
            Log(("VBOXSF: vbsfNtCreateWorker: Invalid disposition 0x%08X for directory!\n",
                 CreateDisposition));
            return STATUS_INVALID_PARAMETER;
        }

        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_DIRECTORY\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_DIRECTORY;
    }

    /*
     * Disposition.
     */
    switch (CreateDisposition)
    {
        case FILE_SUPERSEDE:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        case FILE_OPEN:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW\n"));
            break;

        case FILE_CREATE:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        case FILE_OPEN_IF:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        case FILE_OVERWRITE:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW\n"));
            break;

        case FILE_OVERWRITE_IF:
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW\n"));
            break;

        default:
            Log(("VBOXSF: vbsfNtCreateWorker: Unexpected create disposition: 0x%08X\n", CreateDisposition));
            return STATUS_INVALID_PARAMETER;
    }

    /*
     * Access mode.
     */
    ACCESS_MASK const DesiredAccess = RxContext->Create.NtCreateParameters.DesiredAccess;
    if (DesiredAccess & FILE_READ_DATA)
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_READ\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_READ;
    }

    /* FILE_WRITE_DATA means write access regardless of FILE_APPEND_DATA bit.
       FILE_APPEND_DATA without FILE_WRITE_DATA means append only mode. */
    if (DesiredAccess & FILE_WRITE_DATA)
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_WRITE\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_WRITE;
    }
    else if (DesiredAccess & FILE_APPEND_DATA)
    {
        /* Both write and append access flags are required for shared folders,
         * as on Windows FILE_APPEND_DATA implies write access. */
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_WRITE | SHFL_CF_ACCESS_APPEND\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_WRITE | SHFL_CF_ACCESS_APPEND;
    }

    if (DesiredAccess & FILE_READ_ATTRIBUTES)
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_ATTR_READ\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_ATTR_READ;
    }
    if (DesiredAccess & FILE_WRITE_ATTRIBUTES)
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_ATTR_WRITE\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_ATTR_WRITE;
    }

    /*
     * Sharing.
     */
    ULONG const ShareAccess = RxContext->Create.NtCreateParameters.ShareAccess;
    if (ShareAccess & (FILE_SHARE_READ | FILE_SHARE_WRITE))
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_DENYNONE\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYNONE;
    }
    else if (ShareAccess & FILE_SHARE_READ)
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_DENYWRITE\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYWRITE;
    }
    else if (ShareAccess & FILE_SHARE_WRITE)
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_DENYREAD\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYREAD;
    }
    else
    {
        Log(("VBOXSF: vbsfNtCreateWorker: CreateFlags |= SHFL_CF_ACCESS_DENYALL\n"));
        pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYALL;
    }

    /*
     * Set initial allocation size and attributes.
     * There aren't too many attributes that need to be passed over.
     */
    pReq->CreateParms.Info.cbObject   = RxContext->Create.NtCreateParameters.AllocationSize.QuadPart;
    pReq->CreateParms.Info.Attr.fMode = NTToVBoxFileAttributes(  RxContext->Create.NtCreateParameters.FileAttributes
                                                               & (  FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN
                                                                  | FILE_ATTRIBUTE_SYSTEM   | FILE_ATTRIBUTE_ARCHIVE));

    /*
     * Call the host.
     */
    Log(("VBOXSF: vbsfNtCreateWorker: Calling VbglR0SfHostReqCreate(fCreate=%#RX32)...\n", pReq->CreateParms.CreateFlags));
    int vrc = VbglR0SfHostReqCreate(pNetRootExtension->map.root, pReq);
    Log(("VBOXSF: vbsfNtCreateWorker: VbglR0SfHostReqCreate returns vrc = %Rrc, Result = 0x%x, Handle = %#RX64\n",
         vrc, pReq->CreateParms.Result, pReq->CreateParms.Handle));

    if (RT_SUCCESS(vrc))
    {
        /*
         * The request succeeded. Analyze host response,
         */
        switch (pReq->CreateParms.Result)
        {
            case SHFL_PATH_NOT_FOUND:
                /* Path to the object does not exist. */
                Log(("VBOXSF: vbsfNtCreateWorker: Path not found -> STATUS_OBJECT_PATH_NOT_FOUND + FILE_DOES_NOT_EXIST\n"));
                *pulCreateAction = FILE_DOES_NOT_EXIST;
                return STATUS_OBJECT_PATH_NOT_FOUND;

            case SHFL_FILE_NOT_FOUND:
                *pulCreateAction = FILE_DOES_NOT_EXIST;
                if (pReq->CreateParms.Handle == SHFL_HANDLE_NIL)
                {
                    Log(("VBOXSF: vbsfNtCreateWorker: File not found -> STATUS_OBJECT_NAME_NOT_FOUND + FILE_DOES_NOT_EXIST\n"));
                    return STATUS_OBJECT_NAME_NOT_FOUND;
                }
                AssertMsgFailed(("VBOXSF: vbsfNtCreateWorker: WTF? File not found but have a handle!\n"));
                return vbsfNtCreateWorkerBail(STATUS_UNSUCCESSFUL, pReq, pNetRootExtension);

            case SHFL_FILE_EXISTS:
                Log(("VBOXSF: vbsfNtCreateWorker: File exists, Handle = %#RX64\n", pReq->CreateParms.Handle));
                if (pReq->CreateParms.Handle == SHFL_HANDLE_NIL)
                {
                    *pulCreateAction = FILE_EXISTS;
                    if (CreateDisposition == FILE_CREATE)
                    {
                        /* File was not opened because we requested a create. */
                        Log(("VBOXSF: vbsfNtCreateWorker: File exists already, create failed -> STATUS_OBJECT_NAME_COLLISION\n"));
                        return STATUS_OBJECT_NAME_COLLISION;
                    }

                    /* Actually we should not go here, unless we have no rights to open the object. */
                    Log(("VBOXSF: vbsfNtCreateWorker: Existing file was not opened! -> STATUS_ACCESS_DENIED\n"));
                    return STATUS_ACCESS_DENIED;
                }

                /* An existing file was opened. */
                *pulCreateAction = FILE_OPENED;
                break;

            case SHFL_FILE_CREATED:
                Log(("VBOXSF: vbsfNtCreateWorker: File created (Handle=%#RX64) / FILE_CREATED\n", pReq->CreateParms.Handle));
                /* A new file was created. */
                Assert(pReq->CreateParms.Handle != SHFL_HANDLE_NIL);
                *pulCreateAction = FILE_CREATED;
                break;

            case SHFL_FILE_REPLACED:
                /* An existing file was replaced or overwritten. */
                Assert(pReq->CreateParms.Handle != SHFL_HANDLE_NIL);
                if (CreateDisposition == FILE_SUPERSEDE)
                {
                    Log(("VBOXSF: vbsfNtCreateWorker: File replaced (Handle=%#RX64) / FILE_SUPERSEDED\n", pReq->CreateParms.Handle));
                    *pulCreateAction = FILE_SUPERSEDED;
                }
                else
                {
                    Log(("VBOXSF: vbsfNtCreateWorker: File replaced (Handle=%#RX64) / FILE_OVERWRITTEN\n", pReq->CreateParms.Handle));
                    *pulCreateAction = FILE_OVERWRITTEN;
                }
                break;

            default:
                Log(("VBOXSF: vbsfNtCreateWorker: Invalid CreateResult from host (0x%08X)\n", pReq->CreateParms.Result));
                *pulCreateAction = FILE_DOES_NOT_EXIST;
                return vbsfNtCreateWorkerBail(STATUS_OBJECT_PATH_NOT_FOUND, pReq, pNetRootExtension);
        }

        /*
         * Check flags.
         */
        if (!(fOptions & FILE_NON_DIRECTORY_FILE) || !FlagOn(pReq->CreateParms.Info.Attr.fMode, RTFS_DOS_DIRECTORY))
        { /* likely */ }
        else
        {
            /* Caller wanted only a file, but the object is a directory. */
            Log(("VBOXSF: vbsfNtCreateWorker: -> STATUS_FILE_IS_A_DIRECTORY!\n"));
            return vbsfNtCreateWorkerBail(STATUS_FILE_IS_A_DIRECTORY, pReq, pNetRootExtension);
        }

        if (!(fOptions & FILE_DIRECTORY_FILE) || FlagOn(pReq->CreateParms.Info.Attr.fMode, RTFS_DOS_DIRECTORY))
        { /* likely */ }
        else
        {
            /* Caller wanted only a directory, but the object is not a directory. */
            Log(("VBOXSF: vbsfNtCreateWorker: -> STATUS_NOT_A_DIRECTORY!\n"));
            return vbsfNtCreateWorkerBail(STATUS_NOT_A_DIRECTORY, pReq, pNetRootExtension);
        }

        return STATUS_SUCCESS;
    }

    /*
     * Failed. Map some VBoxRC to STATUS codes expected by the system.
     */
    switch (vrc)
    {
        case VERR_ALREADY_EXISTS:
            Log(("VBOXSF: vbsfNtCreateWorker: VERR_ALREADY_EXISTS -> STATUS_OBJECT_NAME_COLLISION + FILE_EXISTS\n"));
            *pulCreateAction = FILE_EXISTS;
            return STATUS_OBJECT_NAME_COLLISION;

        /* On POSIX systems, the "mkdir" command returns VERR_FILE_NOT_FOUND when
           doing a recursive directory create. Handle this case.

           bird: We end up here on windows systems too if opening a dir that doesn't
                 exists.  Thus, I've changed the SHFL_PATH_NOT_FOUND to SHFL_FILE_NOT_FOUND
                 so that FsPerf is happy. */
        case VERR_FILE_NOT_FOUND: /** @todo r=bird: this is a host bug, isn't it? */
            pReq->CreateParms.Result = SHFL_FILE_NOT_FOUND;
            pReq->CreateParms.Handle = SHFL_HANDLE_NIL;
            *pulCreateAction = FILE_DOES_NOT_EXIST;
            Log(("VBOXSF: vbsfNtCreateWorker: VERR_FILE_NOT_FOUND -> STATUS_OBJECT_NAME_NOT_FOUND + FILE_DOES_NOT_EXIST\n"));
            return STATUS_OBJECT_NAME_NOT_FOUND;

        default:
        {
            *pulCreateAction = FILE_DOES_NOT_EXIST;
            NTSTATUS Status = vbsfNtVBoxStatusToNt(vrc);
            Log(("VBOXSF: vbsfNtCreateWorker: %Rrc -> %#010x + FILE_DOES_NOT_EXIST\n", vrc, Status));
            return Status;
        }
    }
}

/**
 * Create/open a file, directory, ++.
 *
 * The RDBSS library will do a table lookup on the path passed in by the user
 * and therefore share FCBs for objects with the same path.
 *
 * The FCB needs to be locked exclusively upon successful return, however it
 * seems like it's not always locked when we get here (only older RDBSS library
 * versions?), so we have to check this before returning.
 *
 */
NTSTATUS VBoxMRxCreate(IN OUT PRX_CONTEXT RxContext)
{
    RxCaptureFcb;
    PMRX_NET_ROOT               pNetRoot          = capFcb->pNetRoot;
    PMRX_SRV_OPEN               pSrvOpen          = RxContext->pRelevantSrvOpen;
    PUNICODE_STRING             RemainingName     = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);
    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);


    /*
     * Log stuff and make some small adjustments to empty paths and caching flags.
     */
    Log(("VBOXSF: VBoxMRxCreate:  CreateOptions = %#010x\n", RxContext->Create.NtCreateParameters.CreateOptions));
    Log(("VBOXSF: VBoxMRxCreate:    Disposition = %#010x\n", RxContext->Create.NtCreateParameters.Disposition));
    Log(("VBOXSF: VBoxMRxCreate:  DesiredAccess = %#010x\n", RxContext->Create.NtCreateParameters.DesiredAccess));
    Log(("VBOXSF: VBoxMRxCreate:    ShareAccess = %#010x\n", RxContext->Create.NtCreateParameters.ShareAccess));
    Log(("VBOXSF: VBoxMRxCreate: FileAttributes = %#010x\n", RxContext->Create.NtCreateParameters.FileAttributes));
    Log(("VBOXSF: VBoxMRxCreate: AllocationSize = %#RX64\n", RxContext->Create.NtCreateParameters.AllocationSize.QuadPart));
    Log(("VBOXSF: VBoxMRxCreate: name ptr %p length=%d, SrvOpen->Flags %#010x\n",
         RemainingName, RemainingName->Length, pSrvOpen->Flags));

    /* Disable FastIO. It causes a verifier bugcheck. */
#ifdef SRVOPEN_FLAG_DONTUSE_READ_CACHING
    SetFlag(pSrvOpen->Flags, SRVOPEN_FLAG_DONTUSE_READ_CACHING | SRVOPEN_FLAG_DONTUSE_WRITE_CACHING);
#else
    SetFlag(pSrvOpen->Flags, SRVOPEN_FLAG_DONTUSE_READ_CACHEING | SRVOPEN_FLAG_DONTUSE_WRITE_CACHEING);
#endif

    if (RemainingName->Length)
        Log(("VBOXSF: VBoxMRxCreate: Attempt to open %.*ls\n",
             RemainingName->Length/sizeof(WCHAR), RemainingName->Buffer));
    else if (FlagOn(RxContext->Create.Flags, RX_CONTEXT_CREATE_FLAG_STRIPPED_TRAILING_BACKSLASH))
    {
        Log(("VBOXSF: VBoxMRxCreate: Empty name -> Only backslash used\n"));
        RemainingName = &g_UnicodeBackslash;
    }

    /*
     * Fend off unsupported and invalid requests before we start allocating memory.
     */
    if (   pNetRoot->Type != NET_ROOT_WILD
        && pNetRoot->Type != NET_ROOT_DISK)
    {
        Log(("VBOXSF: VBoxMRxCreate: netroot type %d not supported\n",
             pNetRoot->Type));
        return STATUS_NOT_IMPLEMENTED;
    }

    if (RxContext->Create.EaLength == 0)
    { /* likely */ }
    else
    {
        Log(("VBOXSF: VBoxMRxCreate: Unsupported: extended attributes!\n"));
        return STATUS_EAS_NOT_SUPPORTED;
    }

    if (!(capFcb->FcbState & FCB_STATE_PAGING_FILE))
    { /* likely */ }
    else
    {
        Log(("VBOXSF: VBoxMRxCreate: Unsupported: paging file!\n"));
        return STATUS_NOT_IMPLEMENTED;
    }

    if (!(RxContext->Create.NtCreateParameters.CreateOptions & FILE_OPEN_BY_FILE_ID))
    { /* likely */ }
    else
    {
        Log(("VBOXSF: VBoxMRxCreate: Unsupported: file open by id!\n"));
        return STATUS_NOT_IMPLEMENTED;
    }

    /*
     * Allocate memory for the request.
     */
    bool const     fSlashHack = RxContext->CurrentIrpSp
                             && (RxContext->CurrentIrpSp->Parameters.Create.ShareAccess & VBOX_MJ_CREATE_SLASH_HACK);
    uint16_t const  cbPath    = RemainingName->Length;
    uint32_t const  cbPathAll = cbPath + fSlashHack * sizeof(RTUTF16) + sizeof(RTUTF16);
    AssertReturn(cbPathAll < _64K, STATUS_NAME_TOO_LONG);

    uint32_t const  cbReq     = RT_UOFFSETOF(VBOXSFCREATEREQ, StrPath.String) + cbPathAll;
    VBOXSFCREATEREQ *pReq     = (VBOXSFCREATEREQ *)VbglR0PhysHeapAlloc(cbReq);
    if (pReq)
    { }
    else
        return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * Copy out the path string.
     */
    pReq->StrPath.u16Size = (uint16_t)cbPathAll;
    if (!fSlashHack)
    {
        pReq->StrPath.u16Length = cbPath;
        memcpy(&pReq->StrPath.String, RemainingName->Buffer, cbPath);
        pReq->StrPath.String.utf16[cbPath / sizeof(RTUTF16)] = '\0';
    }
    else
    {
        /* HACK ALERT! Here we add back the lsash we had to hide from RDBSS. */
        pReq->StrPath.u16Length = cbPath + sizeof(RTUTF16);
        memcpy(&pReq->StrPath.String, RemainingName->Buffer, cbPath);
        pReq->StrPath.String.utf16[cbPath / sizeof(RTUTF16)] = '\\';
        pReq->StrPath.String.utf16[cbPath / sizeof(RTUTF16) + 1] = '\0';
    }
    Log(("VBOXSF: VBoxMRxCreate: %.*ls\n", pReq->StrPath.u16Length / sizeof(RTUTF16), pReq->StrPath.String.utf16));

    /*
     * Hand the bulk work off to a worker function to simplify bailout and cleanup.
     */
    ULONG       CreateAction = FILE_CREATED;
    NTSTATUS    Status = vbsfNtCreateWorker(RxContext, pReq, &CreateAction, pNetRootExtension, capFcb);
    if (Status == STATUS_SUCCESS)
    {
        Log(("VBOXSF: VBoxMRxCreate: EOF is 0x%RX64 AllocSize is 0x%RX64\n",
             pReq->CreateParms.Info.cbObject, pReq->CreateParms.Info.cbAllocated));
        Log(("VBOXSF: VBoxMRxCreate: CreateAction = %#010x\n", CreateAction));

        /*
         * Create the file object extension.
         * After this we're out of the woods and nothing more can go wrong.
         */
        PMRX_FOBX pFobx;
        RxContext->pFobx = pFobx = RxCreateNetFobx(RxContext, pSrvOpen);
        PMRX_VBOX_FOBX pVBoxFobx = pFobx ? VBoxMRxGetFileObjectExtension(pFobx) : NULL;
        if (pFobx && pVBoxFobx)
        {
            /*
             * Make sure we've got the FCB locked exclusivly before updating it and returning.
             * (bird: not entirely sure if this is needed for the W10 RDBSS, but cannot hurt.)
             */
            if (!RxIsFcbAcquiredExclusive(capFcb))
                RxAcquireExclusiveFcbResourceInMRx(capFcb);

            /*
             * Initialize our file object extension data.
             */
            pVBoxFobx->Info         = pReq->CreateParms.Info;
            pVBoxFobx->nsUpToDate   = RTTimeSystemNanoTS();
            pVBoxFobx->hFile        = pReq->CreateParms.Handle;
            pVBoxFobx->pSrvCall     = RxContext->Create.pSrvCall;

            /* bird: Dunno what this really's about. */
            pFobx->OffsetOfNextEaToReturn = 1;

            /*
             * Initialize the FCB if this is the first open.
             *
             * Note! The RxFinishFcbInitialization call expects node types as the 2nd parameter,
             *       but is for  some reason given enum RX_FILE_TYPE as type.
             */
            if (capFcb->OpenCount == 0)
            {
                Log(("VBOXSF: VBoxMRxCreate: Initializing the FCB.\n"));
                FCB_INIT_PACKET               InitPacket;
                FILE_NETWORK_OPEN_INFORMATION Data;
                ULONG                         NumberOfLinks = 0; /** @todo ?? */
                Data.CreationTime.QuadPart   = RTTimeSpecGetNtTime(&pReq->CreateParms.Info.BirthTime);
                Data.LastAccessTime.QuadPart = RTTimeSpecGetNtTime(&pReq->CreateParms.Info.AccessTime);
                Data.LastWriteTime.QuadPart  = RTTimeSpecGetNtTime(&pReq->CreateParms.Info.ModificationTime);
                Data.ChangeTime.QuadPart     = RTTimeSpecGetNtTime(&pReq->CreateParms.Info.ChangeTime);
                /** @todo test sparse files.  CcSetFileSizes is documented to not want allocation size smaller than EOF offset. */
                Data.AllocationSize.QuadPart = pReq->CreateParms.Info.cbAllocated;
                Data.EndOfFile.QuadPart      = pReq->CreateParms.Info.cbObject;
                Data.FileAttributes          = VBoxToNTFileAttributes(pReq->CreateParms.Info.Attr.fMode);
                RxFormInitPacket(InitPacket,
                                 &Data.FileAttributes,
                                 &NumberOfLinks,
                                 &Data.CreationTime,
                                 &Data.LastAccessTime,
                                 &Data.LastWriteTime,
                                 &Data.ChangeTime,
                                 &Data.AllocationSize,
                                 &Data.EndOfFile,
                                 &Data.EndOfFile);
                if (pReq->CreateParms.Info.Attr.fMode & RTFS_DOS_DIRECTORY)
                    RxFinishFcbInitialization(capFcb, (RX_FILE_TYPE)RDBSS_NTC_STORAGE_TYPE_DIRECTORY, &InitPacket);
                else
                    RxFinishFcbInitialization(capFcb, (RX_FILE_TYPE)RDBSS_NTC_STORAGE_TYPE_FILE, &InitPacket);
            }


            /*
             * See if the size has changed and update the FCB if it has.
             */
            if (   capFcb->OpenCount > 0
                && capFcb->Header.FileSize.QuadPart != pReq->CreateParms.Info.cbObject)
            {
                PFILE_OBJECT pFileObj = RxContext->CurrentIrpSp->FileObject;
                Assert(pFileObj);
                if (pFileObj)
                    vbsfNtUpdateFcbSize(pFileObj, capFcb, pVBoxFobx, pReq->CreateParms.Info.cbObject,
                                        capFcb->Header.FileSize.QuadPart, pReq->CreateParms.Info.cbAllocated);
            }

            /*
             * Set various return values.
             */

            /* This is "our" contribution to the buffering flags (no buffering, please). */
            pSrvOpen->BufferingFlags = 0;

            /* This is the IO_STATUS_BLOCK::Information value, I think. */
            RxContext->Create.ReturnedCreateInformation = CreateAction;

            /*
             * Do logging.
             */
            Log(("VBOXSF: VBoxMRxCreate: Info: BirthTime        %RI64\n", RTTimeSpecGetNano(&pVBoxFobx->Info.BirthTime)));
            Log(("VBOXSF: VBoxMRxCreate: Info: ChangeTime       %RI64\n", RTTimeSpecGetNano(&pVBoxFobx->Info.ChangeTime)));
            Log(("VBOXSF: VBoxMRxCreate: Info: ModificationTime %RI64\n", RTTimeSpecGetNano(&pVBoxFobx->Info.ModificationTime)));
            Log(("VBOXSF: VBoxMRxCreate: Info: AccessTime       %RI64\n", RTTimeSpecGetNano(&pVBoxFobx->Info.AccessTime)));
            Log(("VBOXSF: VBoxMRxCreate: Info: fMode            %#RX32\n", pVBoxFobx->Info.Attr.fMode));
            if (!(pVBoxFobx->Info.Attr.fMode & RTFS_DOS_DIRECTORY))
            {
                Log(("VBOXSF: VBoxMRxCreate: Info: cbObject         %#RX64\n", pVBoxFobx->Info.cbObject));
                Log(("VBOXSF: VBoxMRxCreate: Info: cbAllocated      %#RX64\n", pVBoxFobx->Info.cbAllocated));
            }
            Log(("VBOXSF: VBoxMRxCreate: NetRoot is %p, Fcb is %p, pSrvOpen is %p, Fobx is %p\n",
                 pNetRoot, capFcb, pSrvOpen, RxContext->pFobx));
            Log(("VBOXSF: VBoxMRxCreate: returns STATUS_SUCCESS\n"));
        }
        else
        {
            Log(("VBOXSF: VBoxMRxCreate: RxCreateNetFobx failed (pFobx=%p)\n", pFobx));
            Assert(!pFobx);
            AssertCompile(sizeof(VBOXSFCLOSEREQ) <= RT_UOFFSETOF(VBOXSFCREATEREQ, CreateParms));
            VbglR0SfHostReqClose(pNetRootExtension->map.root, (VBOXSFCLOSEREQ *)pReq, pReq->CreateParms.Handle);
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else
        Log(("VBOXSF: VBoxMRxCreate: vbsfNtCreateWorker failed %#010x\n", Status));
    VbglR0PhysHeapFree(pReq);
    return Status;
}

NTSTATUS VBoxMRxComputeNewBufferingState(IN OUT PMRX_SRV_OPEN pMRxSrvOpen, IN PVOID pMRxContext, OUT PULONG pNewBufferingState)
{
    RT_NOREF(pMRxSrvOpen, pMRxContext, pNewBufferingState);
    Log(("VBOXSF: MRxComputeNewBufferingState\n"));
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS VBoxMRxDeallocateForFcb(IN OUT PMRX_FCB pFcb)
{
    RT_NOREF(pFcb);
    Log(("VBOXSF: MRxDeallocateForFcb\n"));
    return STATUS_SUCCESS;
}

NTSTATUS VBoxMRxDeallocateForFobx(IN OUT PMRX_FOBX pFobx)
{
    RT_NOREF(pFobx);
    Log(("VBOXSF: MRxDeallocateForFobx\n"));
    return STATUS_SUCCESS;
}

NTSTATUS VBoxMRxTruncate(IN PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxTruncate\n"));
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS VBoxMRxCleanupFobx(IN PRX_CONTEXT RxContext)
{
    PMRX_VBOX_FOBX pVBoxFobx = VBoxMRxGetFileObjectExtension(RxContext->pFobx);

    Log(("VBOXSF: MRxCleanupFobx: pVBoxFobx = %p, Handle = 0x%RX64\n", pVBoxFobx, pVBoxFobx? pVBoxFobx->hFile: 0));

    if (!pVBoxFobx)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

NTSTATUS VBoxMRxForceClosed(IN PMRX_SRV_OPEN pSrvOpen)
{
    RT_NOREF(pSrvOpen);
    Log(("VBOXSF: MRxForceClosed\n"));
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * Ensures the FCBx doesn't have dangling pointers to @a pVBoxFobx.
 *
 * This isn't strictly speaking needed, as nobody currently dereference these
 * pointers, however better keeping things neath and tidy.
 */
DECLINLINE(void) vbsfNtCleanupFcbxTimestampRefsOnClose(PMRX_VBOX_FOBX pVBoxFobx, PVBSFNTFCBEXT pVBoxFcbx)
{
    pVBoxFobx->fTimestampsSetByUser          = 0;
    pVBoxFobx->fTimestampsUpdatingSuppressed = 0;
    pVBoxFobx->fTimestampsImplicitlyUpdated  = 0;
    if (pVBoxFcbx->pFobxLastAccessTime == pVBoxFobx)
        pVBoxFcbx->pFobxLastAccessTime = NULL;
    if (pVBoxFcbx->pFobxLastWriteTime  == pVBoxFobx)
        pVBoxFcbx->pFobxLastWriteTime  = NULL;
    if (pVBoxFcbx->pFobxChangeTime     == pVBoxFobx)
        pVBoxFcbx->pFobxChangeTime     = NULL;
}

/**
 * Closes an opened file handle of a MRX_VBOX_FOBX.
 *
 * Updates file attributes if necessary.
 *
 * Used by VBoxMRxCloseSrvOpen and vbsfNtRename.
 */
NTSTATUS vbsfNtCloseFileHandle(PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension,
                               PMRX_VBOX_FOBX pVBoxFobx,
                               PVBSFNTFCBEXT pVBoxFcbx)
{
    if (pVBoxFobx->hFile == SHFL_HANDLE_NIL)
    {
        Log(("VBOXSF: vbsfCloseFileHandle: SHFL_HANDLE_NIL\n"));
        return STATUS_SUCCESS;
    }

    Log(("VBOXSF: vbsfCloseFileHandle: 0x%RX64, fTimestampsUpdatingSuppressed = %#x, fTimestampsImplicitlyUpdated = %#x\n",
         pVBoxFobx->hFile, pVBoxFobx->fTimestampsUpdatingSuppressed, pVBoxFobx->fTimestampsImplicitlyUpdated));

    /*
     * We allocate a single request buffer for the timestamp updating and the closing
     * to save time (at the risk of running out of heap, but whatever).
     */
    union MyCloseAndInfoReq
    {
        VBOXSFCLOSEREQ   Close;
        VBOXSFOBJINFOREQ Info;
    } *pReq = (union MyCloseAndInfoReq *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
        RT_ZERO(*pReq);
    else
        return STATUS_INSUFF_SERVER_RESOURCES;

    /*
     * Restore timestamp that we may implicitly been updated via this handle
     * after the user explicitly set them or turn off implict updating (the -1 value).
     *
     * Note! We ignore the status of this operation.
     */
    Assert(pVBoxFcbx);
    uint8_t fUpdateTs = pVBoxFobx->fTimestampsUpdatingSuppressed & pVBoxFobx->fTimestampsImplicitlyUpdated;
    if (fUpdateTs)
    {
        /** @todo skip this if the host is windows and fTimestampsUpdatingSuppressed == fTimestampsSetByUser */
        /** @todo pass -1 timestamps thru so we can always skip this on windows hosts! */
        if (   (fUpdateTs & VBOX_FOBX_F_INFO_LASTACCESS_TIME)
            && pVBoxFcbx->pFobxLastAccessTime == pVBoxFobx)
            pReq->Info.ObjInfo.AccessTime        = pVBoxFobx->Info.AccessTime;
        else
            fUpdateTs &= ~VBOX_FOBX_F_INFO_LASTACCESS_TIME;

        if (   (fUpdateTs & VBOX_FOBX_F_INFO_LASTWRITE_TIME)
            && pVBoxFcbx->pFobxLastWriteTime  == pVBoxFobx)
            pReq->Info.ObjInfo.ModificationTime  = pVBoxFobx->Info.ModificationTime;
        else
            fUpdateTs &= ~VBOX_FOBX_F_INFO_LASTWRITE_TIME;

        if (   (fUpdateTs & VBOX_FOBX_F_INFO_CHANGE_TIME)
            && pVBoxFcbx->pFobxChangeTime     == pVBoxFobx)
            pReq->Info.ObjInfo.ChangeTime        = pVBoxFobx->Info.ChangeTime;
        else
            fUpdateTs &= ~VBOX_FOBX_F_INFO_CHANGE_TIME;
        if (fUpdateTs)
        {
            Log(("VBOXSF: vbsfCloseFileHandle: Updating timestamp: %#x\n", fUpdateTs));
            int vrc = VbglR0SfHostReqSetObjInfo(pNetRootExtension->map.root, &pReq->Info, pVBoxFobx->hFile);
            if (RT_FAILURE(vrc))
                Log(("VBOXSF: vbsfCloseFileHandle: VbglR0SfHostReqSetObjInfo failed for fUpdateTs=%#x: %Rrc\n", fUpdateTs, vrc));
            RT_NOREF(vrc);
        }
        else
            Log(("VBOXSF: vbsfCloseFileHandle: no timestamp needing updating\n"));
    }

    vbsfNtCleanupFcbxTimestampRefsOnClose(pVBoxFobx, pVBoxFcbx);

    /*
     * Now close the handle.
     */
    int vrc = VbglR0SfHostReqClose(pNetRootExtension->map.root, &pReq->Close, pVBoxFobx->hFile);

    pVBoxFobx->hFile = SHFL_HANDLE_NIL;

    VbglR0PhysHeapFree(pReq);

    NTSTATUS const Status = RT_SUCCESS(vrc) ? STATUS_SUCCESS : vbsfNtVBoxStatusToNt(vrc);
    Log(("VBOXSF: vbsfCloseFileHandle: Returned 0x%08X (vrc=%Rrc)\n", Status, vrc));
    return Status;
}

/**
 * @note We don't collapse opens, this is called whenever a handle is closed.
 */
NTSTATUS VBoxMRxCloseSrvOpen(IN PRX_CONTEXT RxContext)
{
    RxCaptureFcb;
    RxCaptureFobx;

    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX pVBoxFobx = VBoxMRxGetFileObjectExtension(capFobx);
    PMRX_SRV_OPEN pSrvOpen = capFobx->pSrvOpen;


    Log(("VBOXSF: MRxCloseSrvOpen: capFcb = %p, capFobx = %p, pVBoxFobx = %p, pSrvOpen = %p\n",
          capFcb, capFobx, pVBoxFobx, pSrvOpen));

#ifdef LOG_ENABLED
    PUNICODE_STRING pRemainingName = pSrvOpen->pAlreadyPrefixedName;
    Log(("VBOXSF: MRxCloseSrvOpen: Remaining name = %.*ls, Len = %d\n",
         pRemainingName->Length / sizeof(WCHAR), pRemainingName->Buffer, pRemainingName->Length));
#endif

    if (!pVBoxFobx)
        return STATUS_INVALID_PARAMETER;

    if (FlagOn(pSrvOpen->Flags, (SRVOPEN_FLAG_FILE_RENAMED | SRVOPEN_FLAG_FILE_DELETED)))
    {
        /* If we renamed or delete the file/dir, then it's already closed */
        Assert(pVBoxFobx->hFile == SHFL_HANDLE_NIL);
        Log(("VBOXSF: MRxCloseSrvOpen: File was renamed, handle 0x%RX64 ignore close.\n",
             pVBoxFobx->hFile));
        return STATUS_SUCCESS;
    }

    /*
     * Remove file or directory if delete action is pending and the this is the last open handle.
     */
    NTSTATUS Status = STATUS_SUCCESS;
    if (capFcb->FcbState & FCB_STATE_DELETE_ON_CLOSE)
    {
        Log(("VBOXSF: MRxCloseSrvOpen: Delete on close. Open count = %d\n",
             capFcb->OpenCount));

        if (capFcb->OpenCount == 0)
            Status = vbsfNtRemove(RxContext);
    }

    /*
     * Close file if we still have a handle to it.
     */
    if (pVBoxFobx->hFile != SHFL_HANDLE_NIL)
        vbsfNtCloseFileHandle(pNetRootExtension, pVBoxFobx, VBoxMRxGetFcbExtension(capFcb));

    return Status;
}

/**
 * Worker for vbsfNtSetBasicInfo and VBoxMRxCloseSrvOpen.
 *
 * Only called by vbsfNtSetBasicInfo if there is exactly one open handle.  And
 * VBoxMRxCloseSrvOpen calls it when the last handle is being closed.
 */
NTSTATUS vbsfNtRemove(IN PRX_CONTEXT RxContext)
{
    RxCaptureFcb;
    RxCaptureFobx;
    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(capFcb->pNetRoot);
    PMRX_VBOX_FOBX              pVBoxFobx         = VBoxMRxGetFileObjectExtension(capFobx);
    PUNICODE_STRING             pRemainingName    = GET_ALREADY_PREFIXED_NAME_FROM_CONTEXT(RxContext);
    uint16_t const              cwcRemainingName  = pRemainingName->Length / sizeof(WCHAR);

    Log(("VBOXSF: vbsfNtRemove: Delete %.*ls. open count = %d\n",
         cwcRemainingName, pRemainingName->Buffer, capFcb->OpenCount));
    Assert(RxIsFcbAcquiredExclusive(capFcb));

    /*
     * We've got function that does both deletion and handle closing starting with 6.0.8,
     * this saves us a host call when just deleting the file/dir.
     */
    uint32_t const  fRemove = pVBoxFobx->Info.Attr.fMode & RTFS_DOS_DIRECTORY ? SHFL_REMOVE_DIR : SHFL_REMOVE_FILE;
    NTSTATUS        Status;
    int             vrc;
    if (g_uSfLastFunction >= SHFL_FN_CLOSE_AND_REMOVE)
    {
        size_t const cbReq = RT_UOFFSETOF(VBOXSFCLOSEANDREMOVEREQ, StrPath.String) + (cwcRemainingName + 1) * sizeof(RTUTF16);
        VBOXSFCLOSEANDREMOVEREQ *pReq = (VBOXSFCLOSEANDREMOVEREQ *)VbglR0PhysHeapAlloc((uint32_t)cbReq);
        if (pReq)
            RT_ZERO(*pReq);
        else
            return STATUS_INSUFFICIENT_RESOURCES;

        memcpy(&pReq->StrPath.String, pRemainingName->Buffer, cwcRemainingName * sizeof(RTUTF16));
        pReq->StrPath.String.utf16[cwcRemainingName] = '\0';
        pReq->StrPath.u16Length = cwcRemainingName * 2;
        pReq->StrPath.u16Size   = cwcRemainingName * 2 + (uint16_t)sizeof(RTUTF16);
        vrc = VbglR0SfHostReqCloseAndRemove(pNetRootExtension->map.root, pReq, fRemove, pVBoxFobx->hFile);
        pVBoxFobx->hFile = SHFL_HANDLE_NIL;

        VbglR0PhysHeapFree(pReq);
    }
    else
    {
        /*
         * We allocate a single request buffer for the closing and deletion to save time.
         */
        AssertCompile(sizeof(VBOXSFCLOSEREQ) <= sizeof(VBOXSFREMOVEREQ));
        AssertReturn((cwcRemainingName + 1) * sizeof(RTUTF16) < _64K, STATUS_NAME_TOO_LONG);
        size_t cbReq = RT_UOFFSETOF(VBOXSFREMOVEREQ, StrPath.String) + (cwcRemainingName + 1) * sizeof(RTUTF16);
        union MyCloseAndRemoveReq
        {
            VBOXSFCLOSEREQ  Close;
            VBOXSFREMOVEREQ Remove;
        } *pReq = (union MyCloseAndRemoveReq *)VbglR0PhysHeapAlloc((uint32_t)cbReq);
        if (pReq)
            RT_ZERO(*pReq);
        else
            return STATUS_INSUFFICIENT_RESOURCES;

        /*
         * Close file first if not already done.  We dont use vbsfNtCloseFileHandle here
         * as we got our own request buffer and have no need to update any file info.
         */
        if (pVBoxFobx->hFile != SHFL_HANDLE_NIL)
        {
            int vrcClose = VbglR0SfHostReqClose(pNetRootExtension->map.root, &pReq->Close, pVBoxFobx->hFile);
            pVBoxFobx->hFile = SHFL_HANDLE_NIL;
            if (RT_FAILURE(vrcClose))
                Log(("VBOXSF: vbsfNtRemove: Closing the handle failed! vrcClose %Rrc, hFile %#RX64 (probably)\n",
                     vrcClose, pReq->Close.Parms.u64Handle.u.value64));
        }

        /*
         * Try remove the file.
         */
        uint16_t const cwcToCopy = pRemainingName->Length / sizeof(WCHAR);
        AssertMsgReturnStmt(cwcToCopy == cwcRemainingName,
                            ("%#x, was %#x; FCB exclusivity: %d\n", cwcToCopy, cwcRemainingName, RxIsFcbAcquiredExclusive(capFcb)),
                            VbglR0PhysHeapFree(pReq), STATUS_INTERNAL_ERROR);
        memcpy(&pReq->Remove.StrPath.String, pRemainingName->Buffer, cwcToCopy * sizeof(RTUTF16));
        pReq->Remove.StrPath.String.utf16[cwcToCopy] = '\0';
        pReq->Remove.StrPath.u16Length = cwcToCopy * 2;
        pReq->Remove.StrPath.u16Size   = cwcToCopy * 2 + (uint16_t)sizeof(RTUTF16);
        vrc = VbglR0SfHostReqRemove(pNetRootExtension->map.root, &pReq->Remove, fRemove);

        VbglR0PhysHeapFree(pReq);
    }

    if (RT_SUCCESS(vrc))
    {
        SetFlag(capFobx->pSrvOpen->Flags, SRVOPEN_FLAG_FILE_DELETED);
        vbsfNtCleanupFcbxTimestampRefsOnClose(pVBoxFobx, VBoxMRxGetFcbExtension(capFcb));
        Status = STATUS_SUCCESS;
    }
    else
    {
        Log(("VBOXSF: vbsfNtRemove: %s failed with %Rrc\n",
             g_uSfLastFunction >= SHFL_FN_CLOSE_AND_REMOVE ? "VbglR0SfHostReqCloseAndRemove" : "VbglR0SfHostReqRemove", vrc));
        Status = vbsfNtVBoxStatusToNt(vrc);
    }

    Log(("VBOXSF: vbsfNtRemove: Returned %#010X (%Rrc)\n", Status, vrc));
    return Status;
}

NTSTATUS VBoxMRxShouldTryToCollapseThisOpen(IN PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxShouldTryToCollapseThisOpen\n"));
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS VBoxMRxCollapseOpen(IN OUT PRX_CONTEXT RxContext)
{
    RT_NOREF(RxContext);
    Log(("VBOXSF: MRxCollapseOpen\n"));
    return STATUS_MORE_PROCESSING_REQUIRED;
}
