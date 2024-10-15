/* $Id: display-ipc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - DRM IPC communication core functions.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

/*
 * This module implements connection handling routine which is common for
 * both IPC server and client (see vbDrmIpcConnectionHandler()). This function
 * at first tries to read incoming command from IPC socket and if no data has
 * arrived within VBOX_DRMIPC_RX_TIMEOUT_MS, it checks is there is some data in
 * TX queue and sends it. TX queue and IPC connection handle is unique per IPC
 * client and handled in a separate thread of either server or client process.
 *
 * Logging is implemented in a way that errors are always printed out,
 * VBClLogVerbose(2) is used for debugging purposes and reflects what is related to
 * IPC communication. In order to see logging on a host side it is enough to do:
 *
 *      echo 1 > /sys/module/vboxguest/parameters/r3_log_to_host.
 */

#include "VBoxClient.h"
#include "display-ipc.h"

#include <VBox/VBoxGuestLib.h>

#include <iprt/localipc.h>
#include <iprt/err.h>
#include <iprt/crc.h>
#include <iprt/mem.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/assert.h>

#include <grp.h>
#include <pwd.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

/**
 * Calculate size of TX list entry.
 *
 * TX list entry consists of RTLISTNODE, DRM IPC message header and message payload.
 * Given IpcCmd already includes message header and payload. So, TX list entry size
 * equals to size of IpcCmd plus size of RTLISTNODE.
 *
 * @param IpcCmd    A structure which represents DRM IPC command.
 */
#define DRMIPCCOMMAND_TX_LIST_ENTRY_SIZE(IpcCmd) (sizeof(IpcCmd) + RT_UOFFSETOF(VBOX_DRMIPC_TX_LIST_ENTRY, Hdr))

/**
 * Initialize IPC client private data.
 *
 * @return  IPRT status code.
 * @param   pClient             IPC client private data to be initialized.
 * @param   hThread             A thread which server IPC client connection.
 * @param   hClientSession      IPC session handle obtained from RTLocalIpcSessionXXX().
 * @param   cTxListCapacity     Maximum number of messages which can be queued for TX for this IPC session.
 * @param   pfnRxCb             IPC RX callback function pointer.
 */
RTDECL(int) vbDrmIpcClientInit(PVBOX_DRMIPC_CLIENT pClient, RTTHREAD hThread, RTLOCALIPCSESSION hClientSession,
                             uint32_t cTxListCapacity, PFNDRMIPCRXCB pfnRxCb)
{
    AssertReturn(pClient,           VERR_INVALID_PARAMETER);
    AssertReturn(hThread,           VERR_INVALID_PARAMETER);
    AssertReturn(hClientSession,    VERR_INVALID_PARAMETER);
    AssertReturn(cTxListCapacity,   VERR_INVALID_PARAMETER);
    AssertReturn(pfnRxCb,           VERR_INVALID_PARAMETER);

    pClient->hThread                = hThread;
    pClient->hClientSession         = hClientSession;

    RT_ZERO(pClient->TxList);
    RTListInit(&pClient->TxList.Node);

    pClient->cTxListCapacity = cTxListCapacity;
    ASMAtomicWriteU32(&pClient->cTxListSize, 0);

    pClient->pfnRxCb = pfnRxCb;

    return RTCritSectInit(&pClient->CritSect);
}

/**
 * Releases IPC client private data resources.
 *
 * @return  IPRT status code.
 * @param   pClient     IPC session private data to be initialized.
 */
RTDECL(int) vbDrmIpcClientReleaseResources(PVBOX_DRMIPC_CLIENT pClient)
{
    PVBOX_DRMIPC_TX_LIST_ENTRY pEntry, pNextEntry;
    int rc;

    pClient->hClientSession = 0;

    rc = RTCritSectEnter(&pClient->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (!RTListIsEmpty(&pClient->TxList.Node))
        {
            RTListForEachSafe(&pClient->TxList.Node, pEntry, pNextEntry, VBOX_DRMIPC_TX_LIST_ENTRY, Node)
            {
                RTListNodeRemove(&pEntry->Node);
                RTMemFree(pEntry);
                ASMAtomicDecU32(&pClient->cTxListSize);
            }
        }

        rc = RTCritSectLeave(&pClient->CritSect);
        if (RT_SUCCESS(rc))
        {
            rc = RTCritSectDelete(&pClient->CritSect);
            if (RT_FAILURE(rc))
                VBClLogError("vbDrmIpcClientReleaseResources: unable to delete critical section, rc=%Rrc\n", rc);
        }
        else
            VBClLogError("vbDrmIpcClientReleaseResources: unable to leave critical section, rc=%Rrc\n", rc);
    }
    else
        VBClLogError("vbDrmIpcClientReleaseResources: unable to enter critical section, rc=%Rrc\n", rc);

    Assert(ASMAtomicReadU32(&pClient->cTxListSize) == 0);

    RT_ZERO(*pClient);

    return rc;
}

/**
 * Add message to IPC session TX queue.
 *
 * @return  IPRT status code.
 * @param   pClient     IPC session private data.
 * @param   pEntry          Pointer to the message.
 */
static int vbDrmIpcSessionScheduleTx(PVBOX_DRMIPC_CLIENT pClient, PVBOX_DRMIPC_TX_LIST_ENTRY pEntry)
{
    int rc;

    AssertReturn(pClient,   VERR_INVALID_PARAMETER);
    AssertReturn(pEntry,        VERR_INVALID_PARAMETER);

    rc = RTCritSectEnter(&pClient->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pClient->cTxListSize < pClient->cTxListCapacity)
        {
            RTListAppend(&pClient->TxList.Node, &pEntry->Node);
            pClient->cTxListSize++;
        }
        else
            VBClLogError("vbDrmIpcSessionScheduleTx: TX queue is full\n");

        int rc2 = RTCritSectLeave(&pClient->CritSect);
        if (RT_FAILURE(rc2))
            VBClLogError("vbDrmIpcSessionScheduleTx: cannot leave critical section, rc=%Rrc\n", rc2);
    }
    else
        VBClLogError("vbDrmIpcSessionScheduleTx: cannot enter critical section, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Pick up message from TX queue if available.
 *
 * @return  Pointer to list entry or NULL if queue is empty.
 */
static PVBOX_DRMIPC_TX_LIST_ENTRY vbDrmIpcSessionPickupTxMessage(PVBOX_DRMIPC_CLIENT pClient)
{
    PVBOX_DRMIPC_TX_LIST_ENTRY  pEntry = NULL;
    int                 rc;

    AssertReturn(pClient, NULL);

    rc = RTCritSectEnter(&pClient->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (!RTListIsEmpty(&pClient->TxList.Node))
        {
            pEntry = (PVBOX_DRMIPC_TX_LIST_ENTRY)RTListRemoveFirst(&pClient->TxList.Node, VBOX_DRMIPC_TX_LIST_ENTRY, Node);
            pClient->cTxListSize--;
            Assert(pEntry);
        }

        int rc2 = RTCritSectLeave(&pClient->CritSect);
        if (RT_FAILURE(rc2))
            VBClLogError("vbDrmIpcSessionPickupTxMessage: cannot leave critical section, rc=%Rrc\n", rc2);
    }
    else
        VBClLogError("vbDrmIpcSessionPickupTxMessage: cannot enter critical section, rc=%Rrc\n", rc);

    return pEntry;
}

RTDECL(int) vbDrmIpcAuth(RTLOCALIPCSESSION hClientSession)
{
    int rc = VERR_ACCESS_DENIED;
    RTUID uUid;
    struct group *pAllowedGroup;

    AssertReturn(hClientSession, VERR_INVALID_PARAMETER);

    /* Get DRM IPC user group entry from system database. */
    pAllowedGroup = getgrnam(VBOX_DRMIPC_USER_GROUP);
    if (!pAllowedGroup)
        return RTErrConvertFromErrno(errno);

    /* Get remote user ID and check if it is in allowed user group. */
    rc = RTLocalIpcSessionQueryUserId(hClientSession, &uUid);
    if (RT_SUCCESS(rc))
    {
        /* Get user record from system database and look for it in group's members list. */
        struct passwd *UserRecord = getpwuid(uUid);

        if (UserRecord && UserRecord->pw_name)
        {
            while (*pAllowedGroup->gr_mem)
            {
                if (RTStrNCmp(*pAllowedGroup->gr_mem, UserRecord->pw_name, LOGIN_NAME_MAX) == 0)
                    return VINF_SUCCESS;

                pAllowedGroup->gr_mem++;
            }
        }
    }

    return rc;
}

RTDECL(int) vbDrmIpcSetPrimaryDisplay(PVBOX_DRMIPC_CLIENT pClient, uint32_t idDisplay)
{
    int rc = VERR_GENERAL_FAILURE;

    PVBOX_DRMIPC_TX_LIST_ENTRY pTxListEntry =
        (PVBOX_DRMIPC_TX_LIST_ENTRY)RTMemAllocZ(DRMIPCCOMMAND_TX_LIST_ENTRY_SIZE(VBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY));

    if (pTxListEntry)
    {
        PVBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY pCmd = (PVBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY)(&pTxListEntry->Hdr);

        pCmd->Hdr.idCmd = VBOXDRMIPCCLTCMD_SET_PRIMARY_DISPLAY;
        pCmd->Hdr.cbData = sizeof(VBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY);
        pCmd->idDisplay = idDisplay;
        pCmd->Hdr.u64Crc = RTCrc64(pCmd, pCmd->Hdr.cbData);
        Assert(pCmd->Hdr.u64Crc);

        /* Put command into queue and trigger TX. */
        rc = vbDrmIpcSessionScheduleTx(pClient, pTxListEntry);
        if (RT_SUCCESS(rc))
        {
            VBClLogVerbose(2, "vbDrmIpcSetPrimaryDisplay: %u bytes scheduled for TX, crc=0x%x\n", pCmd->Hdr.cbData, pCmd->Hdr.u64Crc);
        }
        else
        {
            RTMemFree(pTxListEntry);
            VBClLogError("vbDrmIpcSetPrimaryDisplay: unable to schedule TX, rc=%Rrc\n", rc);
        }
    }
    else
    {
        VBClLogInfo("cannot allocate SET_PRIMARY_DISPLAY command\n");
        rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * Report to IPC server that display layout offsets have been changed (called by IPC client).
 *
 * @return  IPRT status code.
 * @param   pClient     IPC session private data.
 * @param   cDisplays   Number of monitors which have offsets changed.
 * @param   aDisplays   Offsets data.
 */
RTDECL(int) vbDrmIpcReportDisplayOffsets(PVBOX_DRMIPC_CLIENT pClient, uint32_t cDisplays, struct VBOX_DRMIPC_VMWRECT *aDisplays)
{
    int rc = VERR_GENERAL_FAILURE;

    PVBOX_DRMIPC_TX_LIST_ENTRY pTxListEntry =
        (PVBOX_DRMIPC_TX_LIST_ENTRY)RTMemAllocZ(
            DRMIPCCOMMAND_TX_LIST_ENTRY_SIZE(VBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS));

    if (pTxListEntry)
    {
        PVBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS pCmd = (PVBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS)(&pTxListEntry->Hdr);

        pCmd->Hdr.idCmd = VBOXDRMIPCSRVCMD_REPORT_DISPLAY_OFFSETS;
        pCmd->Hdr.cbData = sizeof(VBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS);
        pCmd->cDisplays = cDisplays;
        memcpy(pCmd->aDisplays, aDisplays, cDisplays * sizeof(struct VBOX_DRMIPC_VMWRECT));
        pCmd->Hdr.u64Crc = RTCrc64(pCmd, pCmd->Hdr.cbData);
        Assert(pCmd->Hdr.u64Crc);

        /* Put command into queue and trigger TX. */
        rc = vbDrmIpcSessionScheduleTx(pClient, pTxListEntry);
        if (RT_SUCCESS(rc))
        {
            VBClLogVerbose(2, "vbDrmIpcReportDisplayOffsets: %u bytes scheduled for TX, crc=0x%x\n", pCmd->Hdr.cbData, pCmd->Hdr.u64Crc);
        }
        else
        {
            RTMemFree(pTxListEntry);
            VBClLogError("vbDrmIpcReportDisplayOffsets: unable to schedule TX, rc=%Rrc\n", rc);
        }
    }
    else
    {
        VBClLogInfo("cannot allocate REPORT_DISPLAY_OFFSETS command\n");
        rc = VERR_NO_MEMORY;
    }

    return rc;
}

/**
 * Common function for both IPC server and client which is responsible
 * for handling IPC communication flow.
 *
 * @return  IPRT status code.
 * @param   pClient     IPC connection private data.
 */
RTDECL(int) vbDrmIpcConnectionHandler(PVBOX_DRMIPC_CLIENT pClient)
{
    int                 rc;
    static uint8_t      aInputBuf[VBOX_DRMIPC_RX_BUFFER_SIZE];
    size_t              cbRead = 0;
    PVBOX_DRMIPC_TX_LIST_ENTRY  pTxListEntry;

    AssertReturn(pClient, VERR_INVALID_PARAMETER);

    /* Make sure we are still connected to IPC server. */
    if (!pClient->hClientSession)
    {
        VBClLogVerbose(2, "connection to IPC server lost\n");
        return VERR_NET_CONNECTION_RESET_BY_PEER;
    }

    AssertReturn(pClient->pfnRxCb, VERR_INVALID_PARAMETER);

    /* Make sure we have valid connection handle. By reporting VERR_BROKEN_PIPE,
     * we trigger reconnect to IPC server. */
    if (!RT_VALID_PTR(pClient->hClientSession))
        return VERR_BROKEN_PIPE;

    rc = RTLocalIpcSessionWaitForData(pClient->hClientSession, VBOX_DRMIPC_RX_TIMEOUT_MS);
    if (RT_SUCCESS(rc))
    {
        /* Read IPC message header. */
        rc = RTLocalIpcSessionRead(pClient->hClientSession, aInputBuf, sizeof(VBOX_DRMIPC_COMMAND_HEADER), &cbRead);
        if (RT_SUCCESS(rc))
        {
            if (cbRead == sizeof(VBOX_DRMIPC_COMMAND_HEADER))
            {
                PVBOX_DRMIPC_COMMAND_HEADER pHdr = (PVBOX_DRMIPC_COMMAND_HEADER)aInputBuf;
                if (pHdr)
                {
                    AssertReturn(pHdr->cbData <= sizeof(aInputBuf) - sizeof(VBOX_DRMIPC_COMMAND_HEADER), VERR_INVALID_PARAMETER);

                    /* Read the rest of a message. */
                    rc = RTLocalIpcSessionRead(pClient->hClientSession, aInputBuf + sizeof(VBOX_DRMIPC_COMMAND_HEADER), pHdr->cbData - sizeof(VBOX_DRMIPC_COMMAND_HEADER), &cbRead);
                    AssertRCReturn(rc, rc);
                    AssertReturn(cbRead == (pHdr->cbData - sizeof(VBOX_DRMIPC_COMMAND_HEADER)), VERR_INVALID_PARAMETER);

                    uint64_t u64Crc = pHdr->u64Crc;

                    /* Verify checksum. */
                    pHdr->u64Crc = 0;
                    if (u64Crc != 0 && RTCrc64(pHdr, pHdr->cbData) == u64Crc)
                    {
                        /* Restore original CRC. */
                        pHdr->u64Crc = u64Crc;

                        /* Trigger RX callback. */
                        rc = pClient->pfnRxCb(pHdr->idCmd, (void *)pHdr, pHdr->cbData);
                        VBClLogVerbose(2, "command 0x%X executed, rc=%Rrc\n", pHdr->idCmd, rc);
                    }
                    else
                    {
                        VBClLogError("unable to read from IPC: CRC mismatch, provided crc=0x%X, cmd=0x%X\n", u64Crc, pHdr->idCmd);
                        rc = VERR_NOT_EQUAL;
                    }
                }
                else
                {
                    VBClLogError("unable to read from IPC: zero data received\n");
                    rc = VERR_INVALID_PARAMETER;
                }
            }
            else
            {
                VBClLogError("received partial IPC message header (%u bytes)\n", cbRead);
                rc = VERR_INVALID_PARAMETER;
            }

            VBClLogVerbose(2, "received %u bytes from IPC\n", cbRead);
        }
        else
        {
            VBClLogError("unable to read from IPC, rc=%Rrc\n", rc);
        }
    }

    /* Check if TX queue has some messages to transfer. */
    while ((pTxListEntry = vbDrmIpcSessionPickupTxMessage(pClient)) != NULL)
    {
        PVBOX_DRMIPC_COMMAND_HEADER pMessageHdr = (PVBOX_DRMIPC_COMMAND_HEADER)(&pTxListEntry->Hdr);
        Assert(pMessageHdr);

        rc = RTLocalIpcSessionWrite(
            pClient->hClientSession, (void *)(&pTxListEntry->Hdr), pMessageHdr->cbData);
        if (RT_SUCCESS(rc))
        {
            rc = RTLocalIpcSessionFlush(pClient->hClientSession);
            if (RT_SUCCESS(rc))
                VBClLogVerbose(2, "vbDrmIpcConnectionHandler: transferred %u bytes\n", pMessageHdr->cbData);
            else
                VBClLogError("vbDrmIpcConnectionHandler: cannot flush IPC connection, transfer of %u bytes failed\n", pMessageHdr->cbData);
        }
        else
            VBClLogError("vbDrmIpcConnectionHandler: cannot TX, rc=%Rrc\n", rc);

        RTMemFree(pTxListEntry);
    }

    return rc;
}
