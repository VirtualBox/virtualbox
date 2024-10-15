/* $Id: net.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Shared Folders - File System Driver network redirector subsystem routines
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


NTSTATUS VBoxMRxUpdateNetRootState(IN OUT PMRX_NET_ROOT pNetRoot)
{
    RT_NOREF(pNetRoot);
    Log(("VBOXSF: MRxUpdateNetRootState\n"));
    return STATUS_NOT_IMPLEMENTED;
}

static void vbsfUpdateNetRoot(PMRX_NET_ROOT pNetRoot)
{
    Log(("VBOXSF: vbsfUpdateNetRoot: NetRoot = 0x%x Type = 0x%x\n",
         pNetRoot, pNetRoot->Type));

    switch (pNetRoot->Type)
    {
        case NET_ROOT_DISK:
            pNetRoot->DeviceType = RxDeviceType(DISK);
            break;
        case NET_ROOT_PIPE:
            pNetRoot->DeviceType = RxDeviceType(NAMED_PIPE);
            break;
        case NET_ROOT_COMM:
            pNetRoot->DeviceType = RxDeviceType(SERIAL_PORT);
            break;
        case NET_ROOT_PRINT:
            pNetRoot->DeviceType = RxDeviceType(PRINTER);
            break;
        case NET_ROOT_MAILSLOT:
            pNetRoot->DeviceType = RxDeviceType(MAILSLOT);
            break;
        case NET_ROOT_WILD:
            /* We get this type when for example Windows Media player opens an MP3 file.
             * This NetRoot has the same remote path (\\vboxsrv\dir) as other NetRoots,
             * which were created earlier and which were NET_ROOT_DISK.
             *
             * In the beginning of the function (UpdateNetRoot) the DDK sample sets
             * pNetRoot->Type of newly created NetRoots using a value previously
             * pstored in a NetRootExtension. One NetRootExtensions is used for a single
             * remote path and reused by a few NetRoots, if they point to the same path.
             *
             * To simplify things we just set the type to DISK here (we do not support
             * anything else anyway), and update the DeviceType correspondingly.
             */
            pNetRoot->Type = NET_ROOT_DISK;
            pNetRoot->DeviceType = RxDeviceType(DISK);
            break;
        default:
            AssertMsgFailed(("VBOXSF: vbsfUpdateNetRoot: Invalid net root type! Type = 0x%x\n",
                             pNetRoot->Type));
            break;
    }

    Log(("VBOXSF: vbsfUpdateNetRoot: leaving pNetRoot->DeviceType = 0x%x\n",
         pNetRoot->DeviceType));
}

NTSTATUS VBoxMRxCreateVNetRoot(IN PMRX_CREATENETROOT_CONTEXT pCreateNetRootContext)
{
    NTSTATUS Status;

    PMRX_V_NET_ROOT pVNetRoot = (PMRX_V_NET_ROOT)pCreateNetRootContext->pVNetRoot;

    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(pVNetRoot->pNetRoot);

    PMRX_NET_ROOT pNetRoot = pVNetRoot->pNetRoot;
    PMRX_SRV_CALL pSrvCall = pNetRoot->pSrvCall;

    BOOLEAN fInitializeNetRoot = FALSE;

    Log(("VBOXSF: MRxCreateVNetRoot: pNetRoot = %p, pNetRootExtension = %p, name = [%.*ls]\n",
         pNetRoot, pNetRootExtension, pNetRoot->pNetRootName->Length / sizeof(WCHAR), pNetRoot->pNetRootName->Buffer));

    /* IMPORTANT:
     *
     * This function must always call 'pCreateNetRootContext->Callback(pCreateNetRootContext)' before
     * returning and then return STATUS_PENDING. Otherwise Win64 will hang.
     */

    if (pNetRoot->Type == NET_ROOT_PIPE)
    {
        /* VBoxSF claims everything which starts with '\vboxsrv'.
         *
         * So sometimes the system tries to open \vboxsrv\ipc$ pipe for DFS
         * and fails the application call if an unexpected code is returned.
         *
         * According to MSDN: The Windows client returns STATUS_MORE_PROCESSING_REQUIRED to the calling
         * application to indicate that the path does not correspond to a DFS Namespace.
         */
        pVNetRoot->Context = NULL;

        if (pNetRoot->pNetRootName->Length >= 13 * sizeof (WCHAR)) /* Number of bytes in '\vboxsrv\ipc$' unicode string. */
        {
            const WCHAR *Suffix = &pNetRoot->pNetRootName->Buffer[8]; /* Number of chars in '\vboxsrv' */

            if (   Suffix[0] == L'\\'
                && (Suffix[1] == L'I' || Suffix[1] == L'i')
                && (Suffix[2] == L'P' || Suffix[2] == L'p')
                && (Suffix[3] == L'C' || Suffix[3] == L'c')
                && Suffix[4] == L'$'
               )
            {
                if (   pNetRoot->pNetRootName->Length == 13 * sizeof (WCHAR)
                    || (Suffix[5] == L'\\' || Suffix[5] == 0)
                   )
                {
                    /* It is '\vboxsrv\IPC$[\*]'. */
                    Log(("VBOXSF: MRxCreateVNetRoot: IPC$\n"));
                    Status = STATUS_MORE_PROCESSING_REQUIRED;
                    goto l_Exit;
                }
            }
        }

        /* Fail all other pipe open requests. */
        Log(("VBOXSF: MRxCreateVNetRoot: Pipe open not supported!\n"));
        Status = STATUS_NOT_SUPPORTED;
        goto l_Exit;
    }
    else if (pNetRoot->Type == NET_ROOT_MAILSLOT)
    {
        Log(("VBOXSF: MRxCreateVNetRoot: Mailslot open not supported!\n"));
        pVNetRoot->Context = NULL;
        Status = STATUS_NOT_SUPPORTED;
        goto l_Exit;
    }

    if (!pNetRoot->Context)
    {
        /* MRxNetRootSize is not zero in VBoxSF, so it is expected
         * that the Context, which is NetRootExtension, is already allocated.
         */
        Log(("VBOXSF: MRxCreateVNetRoot: NULL netroot context\n"));
        pVNetRoot->Context = NULL;
        Status = STATUS_NOT_SUPPORTED;
        goto l_Exit;
    }

    /* Detect an already initialized NetRoot.
     * pNetRootExtension is actually the pNetRoot->Context and it is not NULL.
     */
    Status = STATUS_SUCCESS;

    if (!pNetRootExtension->fInitialized)
    {
        PWCHAR pRootName;
        ULONG RootNameLength;
        int vrc;
        PSHFLSTRING ParsedPath = 0;

        Log(("VBOXSF: MRxCreateVNetRoot: initialize NET_ROOT\n"));

        pNetRoot->MRxNetRootState = MRX_NET_ROOT_STATE_GOOD;
        pNetRootExtension->map.root = SHFL_ROOT_NIL;

        RootNameLength = pNetRoot->pNetRootName->Length - pSrvCall->pSrvCallName->Length;
        if (RootNameLength < sizeof(WCHAR))
        {
            /* Refuse a netroot path with an empty shared folder name */
            Log(("VBOXSF: MRxCreateVNetRoot: Empty shared folder name!\n"));
            pNetRoot->MRxNetRootState = MRX_NET_ROOT_STATE_ERROR;

            Status = STATUS_BAD_NETWORK_NAME;
            goto l_Exit;
        }

        RootNameLength -= sizeof(WCHAR); /* Remove leading backslash. */
        pRootName = (PWCHAR)(pNetRoot->pNetRootName->Buffer + (pSrvCall->pSrvCallName->Length / sizeof(WCHAR)));
        pRootName++; /* Remove leading backslash. */

        /* Strip the trailing \0. Sometimes there is one, sometimes not... */
        if (   RootNameLength >= sizeof(WCHAR)
            && pRootName[RootNameLength / sizeof(WCHAR) - 1] == 0)
            RootNameLength -= sizeof(WCHAR);

        if (!pNetRootExtension->fInitialized)
        {
            Log(("VBOXSF: MRxCreateVNetRoot: Initialize netroot length = %d, name = %.*ls\n",
                 RootNameLength, RootNameLength / sizeof(WCHAR), pRootName));

            Status = vbsfNtShflStringFromUnicodeAlloc(&ParsedPath, pRootName, (uint16_t)RootNameLength);
            if (Status != STATUS_SUCCESS)
            {
                goto l_Exit;
            }

            vrc = VbglR0SfMapFolder(&g_SfClient, ParsedPath, &pNetRootExtension->map);
            vbsfNtFreeNonPagedMem(ParsedPath);
            if (RT_SUCCESS(vrc))
            {
                pNetRootExtension->fInitialized = true;
                Status = STATUS_SUCCESS;
            }
            else
            {
                Log(("VBOXSF: MRxCreateVNetRoot: VbglR0SfMapFolder failed with %d\n", vrc));
                pNetRootExtension->map.root = SHFL_ROOT_NIL;
                Status = STATUS_BAD_NETWORK_NAME;
            }
        }
    }
    else
        Log(("VBOXSF: MRxCreateVNetRoot: Creating V_NET_ROOT on existing NET_ROOT!\n"));

    vbsfUpdateNetRoot(pNetRoot);

l_Exit:
    if (Status != STATUS_PENDING)
    {
        Log(("VBOXSF: MRxCreateVNetRoot: Returning 0x%08X\n", Status));
        pCreateNetRootContext->VirtualNetRootStatus = Status;
        if (fInitializeNetRoot)
            pCreateNetRootContext->NetRootStatus = Status;
        else
            pCreateNetRootContext->NetRootStatus = STATUS_SUCCESS;

        /* Inform RDBSS. */
        pCreateNetRootContext->Callback(pCreateNetRootContext);

        /* RDBSS expects this. */
        Status = STATUS_PENDING;
    }

    Log(("VBOXSF: MRxCreateVNetRoot: Returned STATUS_PENDING\n"));
    return Status;
}

NTSTATUS VBoxMRxFinalizeVNetRoot(IN PMRX_V_NET_ROOT pVNetRoot, IN PBOOLEAN ForceDisconnect)
{
    RT_NOREF(pVNetRoot, ForceDisconnect);
    Log(("VBOXSF: MRxFinalizeVNetRoot: V_NET_ROOT %p, NET_ROOT %p\n", pVNetRoot, pVNetRoot->pNetRoot));

    return STATUS_SUCCESS;
}

NTSTATUS VBoxMRxFinalizeNetRoot(IN PMRX_NET_ROOT pNetRoot, IN PBOOLEAN ForceDisconnect)
{
    PMRX_VBOX_NETROOT_EXTENSION pNetRootExtension = VBoxMRxGetNetRootExtension(pNetRoot);
    RT_NOREF(pNetRoot, ForceDisconnect);

    Log(("VBOXSF: MRxFinalizeNetRoot: NET_ROOT %p\n", pNetRoot));

    if (   pNetRootExtension->fInitialized
        && g_SfClient.handle != NULL)
    {
        int vrc = VbglR0SfUnmapFolder(&g_SfClient, &pNetRootExtension->map);
        if (vrc != VINF_SUCCESS)
            Log(("VBOXSF: MRxFinalizeVNetRoot: VbglR0SfUnmapFolder failed with %d\n",
                 vrc));
        pNetRootExtension->map.root = SHFL_ROOT_NIL;
        pNetRootExtension->fInitialized = false;
    }

    return STATUS_SUCCESS;
}

VOID VBoxMRxExtractNetRootName(IN PUNICODE_STRING FilePathName,
                               IN PMRX_SRV_CALL SrvCall,
                               OUT PUNICODE_STRING NetRootName,
                               OUT PUNICODE_STRING RestOfName OPTIONAL)
{
    int cChars = FilePathName->Length/sizeof(WCHAR);
    int iNetRoot;
    int i;

    /* Split "\vboxsvr\share\path" to
     * NetRootName = "\share"
     * RestOfName = "\path"
     *
     * Note that SrvCall->pSrvCallName contains "\vboxsrv".
     */

    Log(("VBOXSF: MRxExtractNetRootName: [%.*ls], RestOfName %p\n",
         FilePathName->Length/sizeof(WCHAR), FilePathName->Buffer, RestOfName));

    /* Assume that the server prefix is OK.
     * iNetRoot points to the first char after server name, the delimiter.
     */
    iNetRoot = SrvCall->pSrvCallName->Length/sizeof(WCHAR);

    /* Find the NetRoot length: end of FilePathName or the next delimiter. */
    i = iNetRoot;
    while (i < cChars)
    {
        if (   FilePathName->Buffer[i] == L'\\'
            && i > iNetRoot)
        {
            break;
        }
        i++;
    }

    Log(("VBOXSF: MRxExtractNetRootName: cChars %d, iNetRoot %d, iRest %d\n",
         cChars, iNetRoot, i));

    NetRootName->Buffer = &FilePathName->Buffer[iNetRoot];
    NetRootName->Length = (USHORT)((i - iNetRoot) * sizeof(WCHAR));
    NetRootName->MaximumLength = NetRootName->Length;

    Log(("VBOXSF: MRxExtractNetRootName: Srv = %.*ls, Root = %.*ls\n",
         SrvCall->pSrvCallName->Length / sizeof(WCHAR), SrvCall->pSrvCallName->Buffer,
         NetRootName->Length / sizeof(WCHAR), NetRootName->Buffer));

    if (RestOfName)
    {
        RestOfName->Buffer = &FilePathName->Buffer[i];
        RestOfName->Length = (USHORT)((cChars - i) * sizeof(WCHAR));
        RestOfName->MaximumLength = RestOfName->Length;

        Log(("VBOXSF: MRxExtractNetRootName: Rest = %.*ls\n",
             RestOfName->Length / sizeof(WCHAR), RestOfName->Buffer));
    }
}

static VOID vbsfExecuteCreateSrvCall(PMRX_SRVCALL_CALLBACK_CONTEXT pCallbackContext)
{
    NTSTATUS Status;
    PWCHAR pSrvName = 0;
    BOOLEAN Verifier;

    PMRX_SRVCALL_CALLBACK_CONTEXT SCCBC = pCallbackContext;
    PMRX_SRVCALLDOWN_STRUCTURE SrvCalldownStructure = (PMRX_SRVCALLDOWN_STRUCTURE)(SCCBC->SrvCalldownStructure);
    PMRX_SRV_CALL pSrvCall = SrvCalldownStructure->SrvCall;

    /* Validate the server name with the test name of 'vboxsvr'. */
    Log(("VBOXSF: vbsfExecuteCreateSrvCall: Connection Name %.*ls Length: %d, pSrvCall = %p\n",
         pSrvCall->pSrvCallName->Length / sizeof(WCHAR), pSrvCall->pSrvCallName->Buffer, pSrvCall->pSrvCallName->Length, pSrvCall));

    if (pSrvCall->pPrincipalName && pSrvCall->pPrincipalName->Length)
    {
        Log(("VBOXSF: vbsfExecuteCreateSrvCall: Principal name = %.*ls\n",
             pSrvCall->pPrincipalName->Length / sizeof(WCHAR), pSrvCall->pPrincipalName->Buffer));
    }

    if (pSrvCall->pDomainName && pSrvCall->pDomainName->Length)
    {
        Log(("VBOXSF: vbsfExecuteCreateSrvCall: Domain name = %.*ls\n",
             pSrvCall->pDomainName->Length / sizeof(WCHAR), pSrvCall->pDomainName->Buffer));
    }

    if (pSrvCall->pSrvCallName->Length >= 14)
    {
        pSrvName = pSrvCall->pSrvCallName->Buffer;

        Verifier = (pSrvName[0] == L'\\');
        Verifier &= (pSrvName[1] == L'V') || (pSrvName[1] == L'v');
        Verifier &= (pSrvName[2] == L'B') || (pSrvName[2] == L'b');
        Verifier &= (pSrvName[3] == L'O') || (pSrvName[3] == L'o');
        Verifier &= (pSrvName[4] == L'X') || (pSrvName[4] == L'x');
        Verifier &= (pSrvName[5] == L'S') || (pSrvName[5] == L's');
        /* Both vboxsvr & vboxsrv are now accepted */
        if ((pSrvName[6] == L'V') || (pSrvName[6] == L'v'))
        {
            Verifier &= (pSrvName[6] == L'V') || (pSrvName[6] == L'v');
            Verifier &= (pSrvName[7] == L'R') || (pSrvName[7] == L'r');
        }
        else
        {
            Verifier &= (pSrvName[6] == L'R') || (pSrvName[6] == L'r');
            Verifier &= (pSrvName[7] == L'V') || (pSrvName[7] == L'v');
        }
        Verifier &= (pSrvName[8] == L'\\') || (pSrvName[8] == 0);
    }
    else
        Verifier = FALSE;

    if (Verifier)
    {
        Log(("VBOXSF: vbsfExecuteCreateSrvCall: Verifier succeeded!\n"));
        Status = STATUS_SUCCESS;
    }
    else
    {
        Log(("VBOXSF: vbsfExecuteCreateSrvCall: Verifier failed!\n"));
        Status = STATUS_BAD_NETWORK_PATH;
    }

    SCCBC->Status = Status;
    SrvCalldownStructure->CallBack(SCCBC);
}

NTSTATUS VBoxMRxCreateSrvCall(PMRX_SRV_CALL pSrvCall, PMRX_SRVCALL_CALLBACK_CONTEXT pCallbackContext)
{
    PMRX_SRVCALLDOWN_STRUCTURE SrvCalldownStructure = (PMRX_SRVCALLDOWN_STRUCTURE)(pCallbackContext->SrvCalldownStructure);
    RT_NOREF(pSrvCall);

    Log(("VBOXSF: MRxCreateSrvCall: %p.\n", pSrvCall));

    if (IoGetCurrentProcess() == RxGetRDBSSProcess())
    {
        Log(("VBOXSF: MRxCreateSrvCall: Called in context of RDBSS process\n"));

        vbsfExecuteCreateSrvCall(pCallbackContext);
    }
    else
    {
        NTSTATUS Status;

        Log(("VBOXSF: MRxCreateSrvCall: Dispatching to worker thread\n"));

        Status = RxDispatchToWorkerThread(VBoxMRxDeviceObject, DelayedWorkQueue,
                                          (PWORKER_THREAD_ROUTINE)vbsfExecuteCreateSrvCall,
                                          pCallbackContext);

        if (Status == STATUS_SUCCESS)
            Log(("VBOXSF: MRxCreateSrvCall: queued\n"));
        else
        {
            pCallbackContext->Status = Status;
            SrvCalldownStructure->CallBack(pCallbackContext);
        }
    }

    /* RDBSS expect this. */
    return STATUS_PENDING;
}

NTSTATUS VBoxMRxFinalizeSrvCall(PMRX_SRV_CALL pSrvCall, BOOLEAN Force)
{
    RT_NOREF(Force);
    Log(("VBOXSF: MRxFinalizeSrvCall %p, ctx = %p.\n", pSrvCall, pSrvCall->Context));

    pSrvCall->Context = NULL;

    return STATUS_SUCCESS;
}

NTSTATUS VBoxMRxSrvCallWinnerNotify(IN PMRX_SRV_CALL pSrvCall, IN BOOLEAN ThisMinirdrIsTheWinner, IN OUT PVOID pSrvCallContext)
{
    RT_NOREF(ThisMinirdrIsTheWinner, pSrvCallContext);
    Log(("VBOXSF: MRxSrvCallWinnerNotify: pSrvCall %p, pSrvCall->Ctx %p, winner %d, context %p\n",
         pSrvCall, pSrvCall->Context, ThisMinirdrIsTheWinner, pSrvCallContext));

    /* Set it to not NULL. */
    pSrvCall->Context = pSrvCall;

    return STATUS_SUCCESS;
}
