/* $Id: VBoxGuestR3LibClipboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Shared Clipboard.
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
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/clipboard-helper.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <iprt/dir.h>
# include <iprt/file.h>
# include <iprt/path.h>
#endif
#include <iprt/string.h>
#include <iprt/cpp/ministring.h>

#ifdef LOG_GROUP
 #undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/log.h>

#include "VBoxGuestR3LibInternal.h"


/*
 * Function naming convention:
 *
 * FunctionNameRecv  = Receives a host message (request).
 * FunctionNameReply = Replies to a host message (request).
 * FunctionNameSend  = Sends a guest message to the host.
 */


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/


/**
 * Connects to the Shared Clipboard service, legacy version, do not use anymore.
 *
 * @returns VBox status code
 * @param   pidClient       Where to put the client id on success. The client id
 *                          must be passed to all the other clipboard calls.
 */
VBGLR3DECL(int) VbglR3ClipboardConnect(HGCMCLIENTID *pidClient)
{
    int rc = VbglR3HGCMConnect("VBoxSharedClipboard", pidClient);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_HGCM_SERVICE_NOT_FOUND)
            LogRel(("Shared Clipboard: Unabled to connect, as host service was not found, skipping\n"));
        else
            LogRel(("Shared Clipboard: Unabled to connect to host service, rc=%Rrc\n", rc));
    }
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Connects to the Shared Clipboard service, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx            Command context.  This will be initialized by this
 *                          call.
 * @param   fGuestFeatures  The guest features supported by this client,
 *                          VBOX_SHCL_GF_0_XXX.
 */
VBGLR3DECL(int) VbglR3ClipboardConnectEx(PVBGLR3SHCLCMDCTX pCtx, uint64_t fGuestFeatures)
{
    /*
     * Intialize the context structure.
     */
    pCtx->idClient              = 0;
    pCtx->fGuestFeatures        = fGuestFeatures;
    pCtx->fHostFeatures         = 0;
    pCtx->fUseLegacyProtocol    = true;
    pCtx->idContext             = 0;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /* Init callback table. */
    RT_ZERO(pCtx->Transfers.Callbacks);
    /* Indicate that this guest supports Shared Clipboard file transfers. */
    pCtx->fGuestFeatures |= VBOX_SHCL_GF_0_TRANSFERS;
# ifdef RT_OS_WINDOWS
    /* Indicate that on Windows guest OSes we have our own IDataObject implementation which
     * integrates nicely into the guest's Windows Explorer showing / handling the Shared Clipboard file transfers. */
    pCtx->fGuestFeatures |= VBOX_SHCL_GF_0_TRANSFERS_FRONTEND;
# endif
    pCtx->Transfers.cbChunkSize    = VBOX_SHCL_DEFAULT_CHUNK_SIZE; /** @todo Make this configurable. */
    pCtx->Transfers.cbMaxChunkSize = VBOX_SHCL_MAX_CHUNK_SIZE;     /** @todo Ditto. */
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    /*
     * First step is connecting to the HGCM service.
     */
    int rc = VbglR3ClipboardConnect(&pCtx->idClient);
    if (RT_SUCCESS(rc))
    {
        /*
         * Next is reporting our features.  If this fails, assume older host.
         */
        rc = VbglR3ClipboardReportFeatures(pCtx->idClient, pCtx->fGuestFeatures, &pCtx->fHostFeatures);
        if (RT_SUCCESS(rc))
        {
            LogRel2(("Shared Clipboard: Guest features: %#RX64 - Host features: %#RX64\n",
                     pCtx->fGuestFeatures, pCtx->fHostFeatures));

            if (   (pCtx->fHostFeatures & VBOX_SHCL_HF_0_CONTEXT_ID)
                && (pCtx->fGuestFeatures & VBOX_SHCL_GF_0_CONTEXT_ID) )
            {
                pCtx->fUseLegacyProtocol = false;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                if (   (pCtx->fHostFeatures & VBOX_SHCL_HF_0_TRANSFERS)
                    && (pCtx->fGuestFeatures & VBOX_SHCL_GF_0_TRANSFERS) )
                {
                    VBoxShClParmNegotiateChunkSize MsgChunkSize;
                    RT_ZERO(MsgChunkSize);
                    do
                    {
                        VBGL_HGCM_HDR_INIT(&MsgChunkSize.hdr, pCtx->idClient, VBOX_SHCL_GUEST_FN_NEGOTIATE_CHUNK_SIZE,
                                           VBOX_SHCL_CPARMS_NEGOTIATE_CHUNK_SIZE);
                        MsgChunkSize.cb32MaxChunkSize.SetUInt32(pCtx->Transfers.cbMaxChunkSize);
                        MsgChunkSize.cb32ChunkSize.SetUInt32(0); /* If set to 0, let the host choose. */
                        rc = VbglR3HGCMCall(&MsgChunkSize.hdr, sizeof(MsgChunkSize));
                    } while (rc == VERR_INTERRUPTED);
                    if (RT_SUCCESS(rc))
                    {
                        Assert(MsgChunkSize.cb32ChunkSize.type == VMMDevHGCMParmType_32bit);
                        pCtx->Transfers.cbChunkSize    = RT_MIN(MsgChunkSize.cb32ChunkSize.u.value32,    pCtx->Transfers.cbChunkSize);
                        Assert(MsgChunkSize.cb32MaxChunkSize.type == VMMDevHGCMParmType_32bit);
                        pCtx->Transfers.cbMaxChunkSize = RT_MIN(MsgChunkSize.cb32MaxChunkSize.u.value32, pCtx->Transfers.cbMaxChunkSize);

                        LogRel2(("Shared Clipboard: Using chunk size %RU32 (maximum is %RU32)\n",
                                 pCtx->Transfers.cbChunkSize, pCtx->Transfers.cbMaxChunkSize));
                    }
                }
                else
                {
                    if (!(pCtx->fHostFeatures & VBOX_SHCL_HF_0_TRANSFERS))
                        LogRel(("Shared Clipboard: Host does not support transfers\n"));
                }
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
            }
            else
            {
                if (!(pCtx->fHostFeatures & VBOX_SHCL_HF_0_CONTEXT_ID))
                    LogRel(("Shared Clipboard: Host does not support context IDs, using legacy protocol\n"));

                pCtx->fUseLegacyProtocol = true;
            }
        }
        else
        {
            AssertLogRelMsg(rc == VERR_NOT_SUPPORTED || rc == VERR_NOT_IMPLEMENTED,
                            ("Reporting features failed: %Rrc\n", rc));
            pCtx->fUseLegacyProtocol = true;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Reports features to the host and retrieve host feature set.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3ClipboardConnect().
 * @param   fGuestFeatures  Features to report, VBOX_SHCL_GF_XXX.
 * @param   pfHostFeatures  Where to store the features VBOX_SHCL_HF_XXX.
 */
VBGLR3DECL(int) VbglR3ClipboardReportFeatures(uint32_t idClient, uint64_t fGuestFeatures, uint64_t *pfHostFeatures)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   f64Features0;
            HGCMFunctionParameter   f64Features1;
        } Msg;

        RT_ZERO(Msg);

        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES, 2);
        VbglHGCMParmUInt64Set(&Msg.f64Features0, fGuestFeatures);
        VbglHGCMParmUInt64Set(&Msg.f64Features1, VBOX_SHCL_GF_1_MUST_BE_ONE);

        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Assert(Msg.f64Features0.type == VMMDevHGCMParmType_64bit);
            Assert(Msg.f64Features1.type == VMMDevHGCMParmType_64bit);
            if (Msg.f64Features1.u.value64 & VBOX_SHCL_GF_1_MUST_BE_ONE)
                rc = VERR_NOT_SUPPORTED;
            else if (pfHostFeatures)
                *pfHostFeatures = Msg.f64Features0.u.value64;
            break;
        }
    } while (rc == VERR_INTERRUPTED);
    return rc;

}


/**
 * Disconnects from the Shared Clipboard service, legacy version, do not use anymore.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 */
VBGLR3DECL(int) VbglR3ClipboardDisconnect(HGCMCLIENTID idClient)
{
    return VbglR3HGCMDisconnect(idClient);
}


/**
 * Disconnects from the Shared Clipboard service, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 */
VBGLR3DECL(int) VbglR3ClipboardDisconnectEx(PVBGLR3SHCLCMDCTX pCtx)
{
    int rc = VbglR3ClipboardDisconnect(pCtx->idClient);
    if (RT_SUCCESS(rc))
    {
        pCtx->idClient = 0;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Receives reported formats from the host.
 *
 * @returns VBox status code.
 * @param   pCtx        Shared Clipboard command context to use for the
 *                      connection.
 * @param   pfFormats   Where to store the received formats from the host.
 */
static int vbglR3ClipboardFormatsReportRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLFORMATS pfFormats)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFormats, VERR_INVALID_POINTER);

    *pfFormats = 0;

    struct
    {
        VBGLIOCHGCMCALL         Hdr;
        HGCMFunctionParameter   id64Context;
        HGCMFunctionParameter   f32Formats;
    } Msg;

    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->idClient, VBOX_SHCL_GUEST_FN_MSG_GET, 2);
    Msg.id64Context.SetUInt32(VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    Msg.f32Formats.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.f32Formats.GetUInt32(pfFormats);
        AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Fetches a VBOX_SHCL_HOST_MSG_READ_DATA_CID message.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pfFormat            Where to return the requested format.
 */
static int vbglR3ClipboardFetchReadDataCid(PVBGLR3SHCLCMDCTX pCtx, PSHCLFORMAT pfFormat)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pfFormat, VERR_INVALID_POINTER);

    struct
    {
        VBGLIOCHGCMCALL         Hdr;
        HGCMFunctionParameter   id64Context;
        HGCMFunctionParameter   f32Format;
    } Msg;

    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->idClient, VBOX_SHCL_GUEST_FN_MSG_GET, 2);
    Msg.id64Context.SetUInt64(VBOX_SHCL_HOST_MSG_READ_DATA_CID);
    Msg.f32Format.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.id64Context.GetUInt64(&pCtx->idContext);
        AssertRC(rc);
        int rc2 = Msg.f32Format.GetUInt32(pfFormat);
        AssertRCStmt(rc2, rc = rc2);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Fetches a VBOX_SHCL_HOST_MSG_READ_DATA message.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pfFormat            Where to return the requested format.
 */
static int vbglR3ClipboardFetchReadData(PVBGLR3SHCLCMDCTX pCtx, PSHCLFORMAT pfFormat)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pfFormat, VERR_INVALID_POINTER);

    struct
    {
        VBGLIOCHGCMCALL         Hdr;
        HGCMFunctionParameter   id32Msg;
        HGCMFunctionParameter   f32Format;
    } Msg;

    VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->idClient, VBOX_SHCL_GUEST_FN_MSG_GET, 2);
    Msg.id32Msg.SetUInt32(VBOX_SHCL_HOST_MSG_READ_DATA);
    Msg.f32Format.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.f32Format.GetUInt32(pfFormat);
        AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Get a host message, legacy version (which does not have VBOX_SHCL_GUEST_FN_MSG_GET). Do not use anymore.
 *
 * Note: This is the old message which still is being used for the non-URI Shared Clipboard transfers,
 *       to not break compatibility with older additions / VBox versions.
 *
 * This will block until a message becomes available.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 * @param   pidMsg          Where to store the message id.
 * @param   pfFormats       Where to store the format(s) the message applies to.
 */
VBGLR3DECL(int) VbglR3ClipboardGetHostMsgOld(HGCMCLIENTID idClient, uint32_t *pidMsg, uint32_t *pfFormats)
{
    VBoxShClGetHostMsgOld Msg;

    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, VBOX_SHCL_CPARMS_GET_HOST_MSG_OLD);
    VbglHGCMParmUInt32Set(&Msg.msg, 0);
    VbglHGCMParmUInt32Set(&Msg.formats, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        int rc2 = VbglHGCMParmUInt32Get(&Msg.msg, pidMsg);
        if (RT_SUCCESS(rc))
        {
            rc2 = VbglHGCMParmUInt32Get(&Msg.formats, pfFormats);
            if (RT_SUCCESS(rc2))
                return rc;
        }
        rc = rc2;
    }
    *pidMsg    = UINT32_MAX - 1;
    *pfFormats = UINT32_MAX;
    return rc;
}


/**
 * Reads data from the host clipboard.
 *
 * @returns VBox status code.
 * @retval  VINF_BUFFER_OVERFLOW if there is more data available than the caller provided buffer space for.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 * @param   fFormat         The format we're requesting the data in.
 * @param   pvData          Where to store the data.
 * @param   cbData          The size of the buffer pointed to by \a pvData.
 *                          This also indicates the maximum size to read.
 * @param   pcbRead         The actual size of the host clipboard data. May be larger than \a cbData.
 */
VBGLR3DECL(int) VbglR3ClipboardReadData(HGCMCLIENTID idClient, uint32_t fFormat, void *pvData, uint32_t cbData,
                                        uint32_t *pcbRead)
{
    LogFlowFunc(("fFormat=%#x, pvData=%p, cbData=%RU32\n", fFormat, pvData, cbData));

    struct
    {
        VBGLIOCHGCMCALL      Hdr;
        VBoxShClParmDataRead Parms;
    } Msg;

    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, VBOX_SHCL_GUEST_FN_DATA_READ, VBOX_SHCL_CPARMS_DATA_READ);
    VbglHGCMParmUInt32Set(&Msg.Parms.f32Format,  fFormat);
    VbglHGCMParmPtrSet(   &Msg.Parms.pData,      pvData, cbData);
    VbglHGCMParmUInt32Set(&Msg.Parms.cb32Needed, 0);

    int rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        uint32_t cbRead;
        rc = VbglHGCMParmUInt32Get(&Msg.Parms.cb32Needed, &cbRead);
        if (RT_SUCCESS(rc))
        {
            LogFlowFunc(("cbRead=%RU32\n", cbRead));

            if (cbRead > cbData)
                rc = VINF_BUFFER_OVERFLOW;

            *pcbRead = cbRead;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Reads clipboard data from the host clipboard.
 *
 * @returns VBox status code.
 * @retval  VINF_BUFFER_OVERFLOW If there is more data available than the caller provided buffer space for.
 *
 * @param   pCtx                The command context returned by VbglR3ClipboardConnectEx().
 * @param   uFormat             Clipboard format of clipboard data to be read.
 * @param   ppvData             Buffer where return the newly allocated read clipboard data on success.
 *                              Needs to be free'd by the caller.
 * @param   pcbData             Size (in bytes) of clipboard data read on success.
 */
VBGLR3DECL(int) VbglR3ClipboardReadDataEx(PVBGLR3SHCLCMDCTX pCtx,
                                          SHCLFORMAT uFormat, void **ppvData, uint32_t *pcbData)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(ppvData, VERR_INVALID_POINTER);
    AssertReturn(pcbData, VERR_INVALID_PARAMETER);

    int rc;

    uint32_t cbRead = 0;
    uint32_t cbData = _4K;

    void *pvData = RTMemAlloc(cbData);
    if (pvData)
    {
        rc = VbglR3ClipboardReadData(pCtx->idClient, uFormat, pvData, cbData, &cbRead);

        /*
         * A return value of VINF_BUFFER_OVERFLOW tells us to try again with a
         * larger buffer.  The size of the buffer needed is placed in *pcb.
         * So we start all over again.
         */
        if (   rc == VINF_BUFFER_OVERFLOW
            && cbRead)
        {
            /* cbRead contains the size required. */
            pvData = RTMemReallocZ(pvData, cbData, cbRead);
            cbData = cbRead;
            if (pvData)
            {
                rc = VbglR3ClipboardReadData(pCtx->idClient, uFormat, pvData, cbData, &cbRead);
                if (RT_SUCCESS(rc))
                {
                    if (cbRead != cbData) /* The data size must match now. */
                        rc = VERR_MISMATCH;
                }
                else
                {
                    if (rc == VINF_BUFFER_OVERFLOW)
                        rc = VERR_BUFFER_OVERFLOW;
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }

        if (RT_SUCCESS(rc))
        {
            *pcbData = cbRead; /* Actual bytes read. */
            *ppvData = pvData;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    if (!cbRead)
        rc = VERR_SHCLPB_NO_DATA;

    if (RT_FAILURE(rc))
    {
        RTMemFree(pvData);
        LogRel(("Shared Clipboard: Reading clipboard data in format %#x from host failed with %Rrc\n", uFormat, rc));
    }

    return rc;
}


/**
 * Query the host features.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3ClipboardConnect().
 * @param   pfHostFeatures  Where to store the host feature, VBOX_SHCL_HF_XXX.
 */
VBGLR3DECL(int) VbglR3ClipboardQueryFeatures(uint32_t idClient, uint64_t *pfHostFeatures)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   f64Features0;
            HGCMFunctionParameter   f64Features1;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, VBOX_SHCL_GUEST_FN_QUERY_FEATURES, 2);
        VbglHGCMParmUInt64Set(&Msg.f64Features0, 0);
        VbglHGCMParmUInt64Set(&Msg.f64Features1, RT_BIT_64(63));

        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Assert(Msg.f64Features0.type == VMMDevHGCMParmType_64bit);
            Assert(Msg.f64Features1.type == VMMDevHGCMParmType_64bit);
            if (Msg.f64Features1.u.value64 & RT_BIT_64(63))
                rc = VERR_NOT_SUPPORTED;
            else if (pfHostFeatures)
                *pfHostFeatures = Msg.f64Features0.u.value64;
            break;
        }
    } while (rc == VERR_INTERRUPTED);
    return rc;

}

/**
 * Peeks at the next host message, extended version.
 *
 * This glosses over the difference between new (6.1) and old (1.3.2) host
 * service versions, however it does so by abusing @a pcParameters, so don't use
 * it directly when in legacy mode, always pass it on to
 * VbglR3ClipboardEventGetNext() or VbglR3ClipboardEventGetNextEx().
 *
 * @returns VBox status code.
 * @retval  VERR_INTERRUPTED if interrupted.  Does the necessary cleanup, so
 *          caller just have to repeat this call.
 * @retval  VERR_VM_RESTORED if the VM has been restored (idRestoreCheck).
 * @retval  VERR_TRY_AGAIN if no new message is available.
 *
 * @param   pCtx            Shared Clipboard command context to use for the connection.
 * @param   fWait           Wait for new messages to arrive if \c true, return immediately if \c false.
 * @param   pidMsg          Where to store the message id.
 * @param   pcParameters    Where to store the number  of parameters which will
 *                          be received in a second call to the host.
 * @param   pidRestoreCheck Pointer to the VbglR3GetSessionId() variable to use
 *                          for the VM restore check.  Optional.
 *
 * @note    Restore check is only performed optimally with a 6.0 host.
 */
static int vbglR3ClipboardMsgPeekEx(PVBGLR3SHCLCMDCTX pCtx, bool fWait, uint32_t *pidMsg,
                                    uint32_t *pcParameters, uint64_t *pidRestoreCheck)
{
    AssertPtrReturn(pidMsg, VERR_INVALID_POINTER);
    AssertPtrReturn(pcParameters, VERR_INVALID_POINTER);

    struct
    {
        VBGLIOCHGCMCALL Hdr;
        HGCMFunctionParameter idMsg;       /* Doubles as restore check on input. */
        HGCMFunctionParameter cParameters;
    } Msg;
    int rc;

    RT_ZERO(Msg);

    if (!pCtx->fUseLegacyProtocol)
    {
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->idClient,
                           fWait ? VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT
                                 : VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT, 2);
        VbglHGCMParmUInt64Set(&Msg.idMsg, pidRestoreCheck ? *pidRestoreCheck : 0);
        VbglHGCMParmUInt32Set(&Msg.cParameters, 0);
        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        Log4Func(("VbglR3HGCMCall -> %Rrc\n", rc));
        if (RT_SUCCESS(rc))
        {
            AssertMsgReturn(   Msg.idMsg.type       == VMMDevHGCMParmType_64bit
                            && Msg.cParameters.type == VMMDevHGCMParmType_32bit,
                            ("msg.type=%d num_parms.type=%d\n", Msg.idMsg.type, Msg.cParameters.type),
                            VERR_INTERNAL_ERROR_3);

            *pidMsg       = (uint32_t)Msg.idMsg.u.value64;
            *pcParameters = Msg.cParameters.u.value32;
            return rc;
        }

        /*
         * If restored, update pidRestoreCheck.
         */
        if (rc == VERR_VM_RESTORED && pidRestoreCheck)
            *pidRestoreCheck = Msg.idMsg.u.value64;
    }
    else
    {
        /*
         * We do some crude stuff here by putting the 2nd parameter (foramts) in the parameter count,
         * however it's supposed to be passed directly to VbglR3ClipboardEventGetNext or
         * VbglR3ClipboardEventGetNextEx, so that's fine...
         */
        rc = VbglR3ClipboardGetHostMsgOld(pCtx->idClient, pidMsg, pcParameters);
        if (RT_SUCCESS(rc))
            return rc;
    }

    /*
     * If interrupted we must cancel the call so it doesn't prevent us from making another one.
     */
    if (rc == VERR_INTERRUPTED)
    {
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->idClient, VBOX_SHCL_GUEST_FN_MSG_CANCEL, 0);
        int rc2 = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg.Hdr));
        AssertRC(rc2);
    }

    *pidMsg       = UINT32_MAX - 1;
    *pcParameters = UINT32_MAX - 2;
    return rc;
}

/**
 * Peeks at the next host message, waiting for one to turn up.
 *
 * This glosses over the difference between new (6.1) and old (1.3.2) host
 * service versions, however it does so by abusing @a pcParameters, so don't use
 * it directly when in legacy mode, always pass it on to
 * VbglR3ClipboardEventGetNext() or VbglR3ClipboardEventGetNextEx().
 *
 * @returns VBox status code.
 * @retval  VERR_INTERRUPTED if interrupted.  Does the necessary cleanup, so
 *          caller just have to repeat this call.
 * @retval  VERR_VM_RESTORED if the VM has been restored (idRestoreCheck).
 *
 * @param   pCtx            Shared Clipboard command context to use for the connection.
 * @param   pidMsg          Where to store the message id.
 * @param   pcParameters    Where to store the number  of parameters which will
 *                          be received in a second call to the host.
 * @param   pidRestoreCheck Pointer to the VbglR3GetSessionId() variable to use
 *                          for the VM restore check.  Optional.
 *
 * @note    Restore check is only performed optimally with a 6.0 host.
 */
VBGLR3DECL(int) VbglR3ClipboardMsgPeekWait(PVBGLR3SHCLCMDCTX pCtx, uint32_t *pidMsg,
                                           uint32_t *pcParameters, uint64_t *pidRestoreCheck)
{
    return vbglR3ClipboardMsgPeekEx(pCtx, true /* fWait */, pidMsg, pcParameters, pidRestoreCheck);
}

/**
 * Peeks at the next host message, returning immediately.
 *
 * This glosses over the difference between new (6.1) and old (1.3.2) host
 * service versions, however it does so by abusing @a pcParameters, so don't use
 * it directly when in legacy mode, always pass it on to
 * VbglR3ClipboardEventGetNext() or VbglR3ClipboardEventGetNextEx().
 *
 * @returns VBox status code.
 * @retval  VERR_INTERRUPTED if interrupted.  Does the necessary cleanup, so
 *          caller just have to repeat this call.
 * @retval  VERR_VM_RESTORED if the VM has been restored (idRestoreCheck).
 * @retval  VERR_TRY_AGAIN if no new message is available.
 *
 * @param   pCtx            Shared Clipboard command context to use for the connection.
 * @param   pidMsg          Where to store the message id.
 * @param   pcParameters    Where to store the number  of parameters which will
 *                          be received in a second call to the host.
 * @param   pidRestoreCheck Pointer to the VbglR3GetSessionId() variable to use
 *                          for the VM restore check.  Optional.
 *
 * @note    Restore check is only performed optimally with a 6.0 host.
 */
VBGLR3DECL(int) VbglR3ClipboardMsgPeek(PVBGLR3SHCLCMDCTX pCtx, uint32_t *pidMsg,
                                       uint32_t *pcParameters, uint64_t *pidRestoreCheck)
{
    return vbglR3ClipboardMsgPeekEx(pCtx, false /* fWait */, pidMsg, pcParameters, pidRestoreCheck);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS

/**
 * Reads a transfer root list header from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pRootListHdr        Where to store the received root list header.
 */
static int vbglR3ClipboardTransferRootListHdrRead(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHDR pRootListHdr)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pRootListHdr, VERR_INVALID_POINTER);

    VBoxShClRootListHdrMsg Msg;

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_READ, VBOX_SHCL_CPARMS_ROOT_LIST_HDR_READ);

    Msg.ReqParms.uContext.SetUInt64(pCtx->idContext);
    Msg.ReqParms.fRoots.SetUInt32(0);

    Msg.cRoots.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.fRoots.GetUInt32(&pRootListHdr->fFeatures); AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = Msg.cRoots.GetUInt64(&pRootListHdr->cEntries);
            AssertRC(rc);

            pRootListHdr->cbTotalSize = 0; /* Unused for the root list header. */
        }
    }
    else
        LogRel(("Shared Clipboard: Reading root list header failed: %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads a transfer root list entry from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   uIndex              Index of root list entry to read.
 * @param   pRootListEntry      Where to store the root list entry read from the host.
 */
static int vbglR3ClipboardTransferRootListEntryRead(PVBGLR3SHCLCMDCTX pCtx, uint64_t uIndex, PSHCLLISTENTRY pRootListEntry)
{
    AssertPtrReturn(pCtx,           VERR_INVALID_POINTER);
    AssertPtrReturn(pRootListEntry, VERR_INVALID_POINTER);

    VBoxShClRootListEntryMsg Msg;

    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_READ, VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_READ);

    uint32_t const fInfo = VBOX_SHCL_INFO_F_FSOBJINFO; /* For now the only info we have. */

    Msg.Parms.uContext.SetUInt64(pCtx->idContext);
    Msg.Parms.fInfo.SetUInt32(fInfo);
    Msg.Parms.uIndex.SetUInt64(uIndex);

    char szName[SHCLLISTENTRY_MAX_NAME];
    Msg.szName.SetPtr(szName, sizeof(szName));

    void    *pvInfo = NULL;
    uint32_t cbInfo = 0;

    int rc = VINF_SUCCESS;

    if ((fInfo & VBOX_SHCL_INFO_F_FSOBJINFO) == VBOX_SHCL_INFO_F_FSOBJINFO)
    {
        cbInfo = sizeof(SHCLFSOBJINFO);
        pvInfo = RTMemAlloc(cbInfo);
        if (!pvInfo)
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        Msg.cbInfo.SetUInt32(cbInfo);
        Msg.pvInfo.SetPtr(pvInfo, cbInfo);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            uint32_t fInfoRet;
            rc = Msg.Parms.fInfo.GetUInt32(&fInfoRet);
            if (RT_SUCCESS(rc))
            {
                AssertMsgStmt((fInfoRet & VBOX_SHCL_INFO_F_FSOBJINFO) == VBOX_SHCL_INFO_F_FSOBJINFO,
                              ("Host returned unknown root entry info flags (%#x)\n", fInfoRet), rc = VERR_INVALID_PARAMETER);
                if (RT_SUCCESS(rc))
                {
                    uint32_t cbInfoRet = 0;
                    rc = Msg.cbInfo.GetUInt32(&cbInfoRet);
                    if (RT_SUCCESS(rc))
                    {
                        AssertMsgStmt(cbInfo == cbInfoRet,
                                      ("Host reported cbInfo %RU32, expected %RU32\n", cbInfoRet, cbInfo), rc = VERR_INVALID_PARAMETER);
                        if (RT_SUCCESS(rc))
                            rc = ShClTransferListEntryInitEx(pRootListEntry, fInfo, szName, pvInfo, cbInfo);
                        if (RT_SUCCESS(rc))
                        {
                            pvInfo = NULL;  /* Entry took ownership of pvInfo now. */
                            cbInfo = 0;
                        }
                    }
                }
            }
        }
        else
            LogRel(("Shared Clipboard: Reading root list entry failed: %Rrc\n", rc));
    }

    RTMemFree(pvInfo);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads the transfer root list from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pTransfer           Transfer to read root list for.
 *                              Must be in INITIALIZED state.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferRootListRead(PVBGLR3SHCLCMDCTX pCtx, PSHCLTRANSFER pTransfer)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);

    SHCLTRANSFERSTATUS const enmSts = ShClTransferGetStatus(pTransfer);
    AssertMsgReturn(   enmSts == SHCLTRANSFERSTATUS_INITIALIZED
                    || enmSts == SHCLTRANSFERSTATUS_STARTED,
                    ("Can't read root list -- wrong transfer status (%#x)\n", enmSts), VERR_WRONG_ORDER);

    SHCLLISTHDR Hdr;
    int rc = vbglR3ClipboardTransferRootListHdrRead(pCtx, &Hdr);
    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("cEntries=%RU64, cTotalSize=%RU64\n", Hdr.cEntries, Hdr.cbTotalSize));

        if (!Hdr.cEntries) /* Should never happen (tm). */
        {
#ifdef DEBUG_andy
            AssertFailed();
#endif
            LogRel(("Shared Clipboard: Warning: Transfer %RU32 has no entries\n", ShClTransferGetID(pTransfer)));
        }

        for (uint64_t i = 0; i < Hdr.cEntries; i++)
        {
            PSHCLLISTENTRY pEntry = NULL;
            rc = ShClTransferListEntryAlloc(&pEntry);
            if (RT_SUCCESS(rc))
            {
                rc = vbglR3ClipboardTransferRootListEntryRead(pCtx, i, pEntry);
                if (RT_SUCCESS(rc))
                    rc = ShClTransferListAddEntry(&pTransfer->lstRoots, pEntry, true /* fAppend */);
            }

            if (RT_FAILURE(rc))
            {
                ShClTransferListEntryFree(pEntry);
                break;
            }
        }
    }
    else
        LogRel(("Shared Clipboard: Reading root list for transfer %RU16 failed: %Rrc\n", ShClTransferGetID(pTransfer), rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a transfer status from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pEnmDir             Where to store the transfer direction for the reported transfer.
 * @param   pReport             Where to store the transfer (status) report.
 */
VBGLR3DECL(int) VbglR3ClipboarTransferStatusRecv(PVBGLR3SHCLCMDCTX pCtx,
                                                 PSHCLTRANSFERDIR pEnmDir, PSHCLTRANSFERREPORT pReport)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(pReport, VERR_INVALID_POINTER);
    AssertPtrReturn(pEnmDir, VERR_INVALID_POINTER);

    VBoxShClTransferStatusMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_TRANSFER_STATUS);

    Msg.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    Msg.enmDir.SetUInt32(0);
    Msg.enmStatus.SetUInt32(0);
    Msg.rc.SetUInt32(0);
    Msg.fFlags.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt64(&pCtx->idContext); AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = Msg.enmDir.GetUInt32((uint32_t *)pEnmDir);
            AssertRC(rc);
        }
        if (RT_SUCCESS(rc))
        {
            rc = Msg.enmStatus.GetUInt32(&pReport->uStatus);
            AssertRC(rc);
        }
        if (RT_SUCCESS(rc))
        {
            rc = Msg.rc.GetUInt32((uint32_t *)&pReport->rc);
            AssertRC(rc);
        }
        if (RT_SUCCESS(rc))
        {
            rc = Msg.fFlags.GetUInt32(&pReport->fFlags);
            AssertRC(rc);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a transfer status to the host, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   uCID                Context ID to use.
 *                              The transfer ID is part of this.
 * @param   uStatus             Tranfer status to send.
 * @param   rcTransfer          Result code (rc) to send.
 */
static int vbglR3ClipboardTransferSendStatusEx(PVBGLR3SHCLCMDCTX pCtx, uint64_t uCID,
                                               SHCLTRANSFERSTATUS uStatus, int rcTransfer)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt64(uCID);
    Msg.enmType.SetUInt32(VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS);
    Msg.rc.SetUInt32((uint32_t )rcTransfer); /* int vs. uint32_t */
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.TransferStatus.enmStatus.SetUInt32((uint32_t)uStatus);

    LogFlowFunc(("%s\n", ShClTransferStatusToStr(uStatus)));

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a transfer status to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pTransfer           Transfer of report to reply to.
 * @param   uStatus             Tranfer status to reply.
 * @param   rcTransfer          Result code (rc) to reply.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferSendStatus(PVBGLR3SHCLCMDCTX pCtx, PSHCLTRANSFER pTransfer,
                                                  SHCLTRANSFERSTATUS uStatus, int rcTransfer)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    RT_NOREF(pTransfer); /* Currently not used (yet). */

    int rc = vbglR3ClipboardTransferSendStatusEx(pCtx, pCtx->idContext, uStatus, rcTransfer);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a transfer root list header from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pfRoots             Where to store the root list header flags to use, requested by the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferRootListHdrReadReq(PVBGLR3SHCLCMDCTX pCtx, uint32_t *pfRoots)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(pfRoots, VERR_INVALID_POINTER);

    VBoxShClRootListReadReqMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_ROOT_LIST_HDR_READ_REQ);

    Msg.ReqParms.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_HDR_READ);
    Msg.ReqParms.fRoots.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt64(&pCtx->idContext); AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = Msg.ReqParms.fRoots.GetUInt32(pfRoots);
            AssertRC(rc);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a transfer root list header request.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pRootListHdr        Root lsit header to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferRootListHdrReadReply(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHDR pRootListHdr)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pRootListHdr, VERR_INVALID_POINTER);

    VBoxShClRootListHdrMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_WRITE, VBOX_SHCL_CPARMS_ROOT_LIST_HDR_WRITE);

    Msg.ReqParms.uContext.SetUInt64(pCtx->idContext);
    Msg.ReqParms.fRoots.SetUInt32(pRootListHdr->fFeatures);

    Msg.cRoots.SetUInt64(pRootListHdr->cEntries);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a transfer root list entry from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   puIndex             Where to return the index of the root list entry the host wants to read.
 * @param   pfInfo              Where to return the read flags the host wants to use.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferRootListEntryReadReq(PVBGLR3SHCLCMDCTX pCtx, uint64_t *puIndex, uint32_t *pfInfo)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(puIndex, VERR_INVALID_POINTER);
    AssertPtrReturn(pfInfo,  VERR_INVALID_POINTER);

    VBoxShClRootListEntryReadReqMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_READ_REQ);

    Msg.Parms.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_ENTRY_READ);
    Msg.Parms.fInfo.SetUInt32(0);
    Msg.Parms.uIndex.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.Parms.uContext.GetUInt64(&pCtx->idContext); AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            rc = Msg.Parms.fInfo.GetUInt32(pfInfo);
            AssertRC(rc);
        }
        if (RT_SUCCESS(rc))
        {
            rc = Msg.Parms.uIndex.GetUInt64(puIndex);
            AssertRC(rc);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a transfer root list entry read request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   uIndex              Index of root list entry to reply.
 * @param   pEntry              Actual root list entry to reply.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferRootListEntryReadReply(PVBGLR3SHCLCMDCTX pCtx, uint32_t uIndex, PSHCLLISTENTRY pEntry)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pEntry, VERR_INVALID_POINTER);

    VBoxShClRootListEntryMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_WRITE, VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_WRITE);

    Msg.Parms.uContext.SetUInt64(pCtx->idContext);
    Msg.Parms.fInfo.SetUInt32(pEntry->fInfo);
    Msg.Parms.uIndex.SetUInt64(uIndex);

    Msg.szName.SetPtr(pEntry->pszName, pEntry->cbName);
    Msg.cbInfo.SetUInt32(pEntry->cbInfo);
    Msg.pvInfo.SetPtr(pEntry->pvInfo, pEntry->cbInfo);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to open a transfer list handle to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pOpenParms          List open parameters to use for the open request.
 * @param   phList              Where to return the list handle received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListOpenSend(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTOPENPARMS pOpenParms,
                                                    PSHCLLISTHANDLE phList)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pOpenParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phList,     VERR_INVALID_POINTER);

    int rc = ShClTransferTransformPath(pOpenParms->pszPath, pOpenParms->cbPath);
    AssertRCReturn(rc, rc);

    VBoxShClListOpenMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_LIST_OPEN, VBOX_SHCL_CPARMS_LIST_OPEN);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.fList.SetUInt32(0);
    Msg.pvFilter.SetPtr(pOpenParms->pszFilter, pOpenParms->cbFilter);
    Msg.pvPath.SetPtr(pOpenParms->pszPath, pOpenParms->cbPath);
    Msg.uHandle.SetUInt64(0);

    rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uHandle.GetUInt64(phList); AssertRC(rc);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to open a transfer list handle on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pOpenParms          Where to store the open parameters the host wants to use for opening the list handle.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListOpenRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTOPENPARMS pOpenParms)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pOpenParms, VERR_INVALID_POINTER);

    VBoxShClListOpenMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_OPEN);

    Msg.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_OPEN);
    Msg.fList.SetUInt32(0);
    Msg.pvPath.SetPtr(pOpenParms->pszPath, pOpenParms->cbPath);
    Msg.pvFilter.SetPtr(pOpenParms->pszFilter, pOpenParms->cbFilter);
    Msg.uHandle.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt64(&pCtx->idContext);
        if (RT_SUCCESS(rc))
            rc = Msg.fList.GetUInt32(&pOpenParms->fList);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a transfer list open request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hList               List handle of (guest) list to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListOpenReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLLISTHANDLE hList)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.enmType.SetUInt32(VBOX_SHCL_TX_REPLYMSGTYPE_LIST_OPEN);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ListOpen.uHandle.SetUInt64(hList);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to close a list handle on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phList              Where to store the list handle to close, received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListCloseRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHANDLE phList)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(phList, VERR_INVALID_POINTER);

    VBoxShClListCloseMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_CLOSE);

    Msg.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_CLOSE);
    Msg.uHandle.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt64(&pCtx->idContext);
        if (RT_SUCCESS(rc))
        {
            rc = Msg.uHandle.GetUInt64(phList);
            AssertRC(rc);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a transfer list handle close request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hList               List handle the send the close reply for.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListCloseReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLLISTHANDLE hList)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.enmType.SetUInt32(VBOX_SHCL_TX_REPLYMSGTYPE_LIST_CLOSE);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ListOpen.uHandle.SetUInt64(hList);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to close a transfer list handle to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to request for closing on the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListCloseSend(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClListCloseMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_LIST_CLOSE, VBOX_SHCL_CPARMS_LIST_CLOSE);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.uHandle.SetUInt64(hList);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to read a transfer list header to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to read list header for.
 * @param   fFlags              List header read flags to use.
 * @param   pListHdr            Where to return the list header received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListHdrRead(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList, uint32_t fFlags,
                                                   PSHCLLISTHDR pListHdr)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pListHdr, VERR_INVALID_POINTER);

    VBoxShClListHdrMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_LIST_HDR_READ, VBOX_SHCL_CPARMS_LIST_HDR);

    Msg.ReqParms.uContext.SetUInt64(pCtx->idContext);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fFlags.SetUInt32(fFlags);

    Msg.fFeatures.SetUInt32(0);
    Msg.cbTotalSize.SetUInt32(0);
    Msg.cTotalObjects.SetUInt64(0);
    Msg.cbTotalSize.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.fFeatures.GetUInt32(&pListHdr->fFeatures);
        if (RT_SUCCESS(rc))
            rc = Msg.cTotalObjects.GetUInt64(&pListHdr->cEntries);
        if (RT_SUCCESS(rc))
            rc = Msg.cbTotalSize.GetUInt64(&pListHdr->cbTotalSize);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a transfer list header on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phList              Where to return the list handle to read list header for.
 * @param   pfFlags             Where to return the List header read flags to use.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListHdrReadRecvReq(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHANDLE phList, uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(phList,  VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    VBoxShClListHdrReadReqMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_HDR_READ_REQ);

    Msg.ReqParms.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_HDR_READ);
    Msg.ReqParms.uHandle.SetUInt64(0);
    Msg.ReqParms.fFlags.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt64(&pCtx->idContext);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.uHandle.GetUInt64(phList);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.fFlags.GetUInt32(pfFlags);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends (writes) a transfer list header to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to write list header for.
 * @param   pListHdr            List header to write.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListHdrWrite(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList,
                                                    PSHCLLISTHDR pListHdr)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pListHdr, VERR_INVALID_POINTER);

    VBoxShClListHdrMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_LIST_HDR_WRITE, VBOX_SHCL_CPARMS_LIST_HDR);

    Msg.ReqParms.uContext.SetUInt64(pCtx->idContext);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fFlags.SetUInt32(0);

    Msg.fFeatures.SetUInt32(0);
    Msg.cbTotalSize.SetUInt32(pListHdr->fFeatures);
    Msg.cTotalObjects.SetUInt64(pListHdr->cEntries);
    Msg.cbTotalSize.SetUInt64(pListHdr->cbTotalSize);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to read a transfer list entry from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to request to read a list entry for.
 * @param   pListEntry          Where to return the list entry read from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListEntryRead(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList,
                                                     PSHCLLISTENTRY pListEntry)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pListEntry, VERR_INVALID_POINTER);

    VBoxShClListEntryMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_LIST_ENTRY_READ, VBOX_SHCL_CPARMS_LIST_ENTRY);

    uint32_t const fInfo = VBOX_SHCL_INFO_F_FSOBJINFO; /* For now the only info we have. */

    Msg.ReqParms.uContext.SetUInt64(pCtx->idContext);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fInfo.SetUInt32(fInfo);

    char szName[SHCLLISTENTRY_MAX_NAME];
    Msg.szName.SetPtr(szName, sizeof(szName));

    void    *pvInfo = NULL;
    uint32_t cbInfo = 0;

    int rc = VINF_SUCCESS;

    if ((fInfo & VBOX_SHCL_INFO_F_FSOBJINFO) == VBOX_SHCL_INFO_F_FSOBJINFO)
    {
        cbInfo = sizeof(SHCLFSOBJINFO);
        pvInfo = RTMemAlloc(cbInfo);
        if (!pvInfo)
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        Msg.cbInfo.SetUInt32(cbInfo);
        Msg.pvInfo.SetPtr(pvInfo, cbInfo);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            uint32_t cbInfoRet = 0;
            rc = Msg.cbInfo.GetUInt32(&cbInfoRet);
            if (RT_SUCCESS(rc))
            {
                AssertMsgStmt(cbInfo == cbInfoRet,
                              ("Host reported cbInfo %RU32, expected %RU32\n", cbInfoRet, cbInfo), rc = VERR_INVALID_PARAMETER);
                if (RT_SUCCESS(rc))
                    rc = ShClTransferListEntryInitEx(pListEntry, fInfo, szName, pvInfo, cbInfo);
                if (RT_SUCCESS(rc))
                {
                    pvInfo = NULL;  /* Entry took ownership of pvInfo now. */
                    cbInfo = 0;
                }
            }
        }
        else
            LogRel(("Shared Clipboard: Reading list entry failed: %Rrc\n", rc));
    }

    RTMemFree(pvInfo);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read a transfer list entry from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phList              Where to return the list handle to read a list entry for.
 * @param   pfInfo              Where to return the list read flags.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListEntryReadRecvReq(PVBGLR3SHCLCMDCTX pCtx, PSHCLLISTHANDLE phList, uint32_t *pfInfo)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(phList, VERR_INVALID_POINTER);
    AssertPtrReturn(pfInfo, VERR_INVALID_POINTER);

    VBoxShClListEntryReadReqMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_LIST_ENTRY_READ);

    Msg.ReqParms.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_LIST_ENTRY_READ);
    Msg.ReqParms.uHandle.SetUInt64(0);
    Msg.ReqParms.fInfo.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt64(&pCtx->idContext);
        if (RT_SUCCESS(rc))
        {
            rc = Msg.ReqParms.uHandle.GetUInt64(phList);
            AssertRC(rc);
        }
        if (RT_SUCCESS(rc))
        {
            rc = Msg.ReqParms.fInfo.GetUInt32(pfInfo);
            AssertRC(rc);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends (writes) a transfer list entry to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hList               List handle to write a list etnry for.
 * @param   pListEntry          List entry to write.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferListEntryWrite(PVBGLR3SHCLCMDCTX pCtx, SHCLLISTHANDLE hList,
                                                      PSHCLLISTENTRY pListEntry)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pListEntry, VERR_INVALID_POINTER);

    VBoxShClListEntryMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_LIST_ENTRY_WRITE, VBOX_SHCL_CPARMS_LIST_ENTRY);

    Msg.ReqParms.uContext.SetUInt64(pCtx->idContext);
    Msg.ReqParms.uHandle.SetUInt64(hList);
    Msg.ReqParms.fInfo.SetUInt32(pListEntry->fInfo);

    Msg.szName.SetPtr(pListEntry->pszName, pListEntry->cbName);
    Msg.cbInfo.SetUInt32(pListEntry->cbInfo);
    Msg.pvInfo.SetPtr(pListEntry->pvInfo, pListEntry->cbInfo);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to open a transfer object on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pCreateParms        Where to store the object open/create parameters received from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjOpenRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pCreateParms, VERR_INVALID_POINTER);

    VBoxShClObjOpenMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_OBJ_OPEN);

    Msg.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_OPEN);
    Msg.uHandle.SetUInt64(0);
    Msg.szPath.SetPtr(pCreateParms->pszPath, pCreateParms->cbPath);
    Msg.fCreate.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt64(&pCtx->idContext);
        if (RT_SUCCESS(rc))
            rc = Msg.fCreate.GetUInt32(&pCreateParms->fCreate);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies a host request to open a transfer object.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hObj                Object handle of opened object to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjOpenReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.enmType.SetUInt32(VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_OPEN);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ObjOpen.uHandle.SetUInt64(hObj);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a transfer object open request to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   pCreateParms        Object open/create parameters to use for opening the object on the host.
 * @param   phObj               Where to return the object handle from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjOpenSend(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms,
                                                   PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pCtx,         VERR_INVALID_POINTER);
    AssertPtrReturn(pCreateParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phObj,        VERR_INVALID_POINTER);

    int rc = ShClTransferTransformPath(pCreateParms->pszPath, pCreateParms->cbPath);
    AssertRCReturn(rc, rc);

    VBoxShClObjOpenMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_OBJ_OPEN, VBOX_SHCL_CPARMS_OBJ_OPEN);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.uHandle.SetUInt64(0);
    Msg.szPath.SetPtr((void *)pCreateParms->pszPath, pCreateParms->cbPath);
    Msg.fCreate.SetUInt32(pCreateParms->fCreate);

    rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
        Msg.uHandle.GetUInt64(phObj);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to close a transfer object on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phObj               Where to return the object handle to close from the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjCloseRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pCtx,  VERR_INVALID_POINTER);
    AssertPtrReturn(phObj, VERR_INVALID_POINTER);

    VBoxShClObjCloseMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_OBJ_CLOSE);

    Msg.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_CLOSE);
    Msg.uHandle.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.uContext.GetUInt64(&pCtx->idContext);
        if (RT_SUCCESS(rc))
            rc = Msg.uHandle.GetUInt64(phObj);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Replies to a transfer object open request from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   rcReply             Return code to reply to the host.
 * @param   hObj                Object handle to reply to the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjCloseReply(PVBGLR3SHCLCMDCTX pCtx, int rcReply, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClReplyMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN + 1);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.enmType.SetUInt32(VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_CLOSE);
    Msg.rc.SetUInt32((uint32_t)rcReply); /** int vs. uint32_t */
    Msg.pvPayload.SetPtr(NULL, 0);

    Msg.u.ObjClose.uHandle.SetUInt64(hObj);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to close a transfer object to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hObj                Object handle to close on the host.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjCloseSend(PVBGLR3SHCLCMDCTX pCtx, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    VBoxShClObjCloseMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_OBJ_CLOSE, VBOX_SHCL_CPARMS_OBJ_CLOSE);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.uHandle.SetUInt64(hObj);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Receives a host request to read from a transfer object on the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   phObj               Where to return the object handle to read from.
 * @param   pcbToRead           Where to return the amount (in bytes) to read.
 * @param   pfFlags             Where to return the read flags.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjReadRecv(PVBGLR3SHCLCMDCTX pCtx, PSHCLOBJHANDLE phObj, uint32_t *pcbToRead,
                                                   uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(phObj,     VERR_INVALID_POINTER);
    AssertPtrReturn(pcbToRead, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags,   VERR_INVALID_POINTER);

    VBoxShClObjReadReqMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_MSG_GET, VBOX_SHCL_CPARMS_OBJ_READ_REQ);

    Msg.ReqParms.uContext.SetUInt64(VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_READ);
    Msg.ReqParms.uHandle.SetUInt64(0);
    Msg.ReqParms.cbToRead.SetUInt32(0);
    Msg.ReqParms.fRead.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        rc = Msg.ReqParms.uContext.GetUInt64(&pCtx->idContext);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.uHandle.GetUInt64(phObj);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.cbToRead.GetUInt32(pcbToRead);
        if (RT_SUCCESS(rc))
            rc = Msg.ReqParms.fRead.GetUInt32(pfFlags);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to read from a transfer object to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hObj                Object handle of object to read from.
 * @param   pvData              Buffer where to store the read object data.
 * @param   cbData              Size (in bytes) of buffer.
 * @param   pcbRead             Where to store the amount (in bytes) read from the object.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjReadSend(PVBGLR3SHCLCMDCTX pCtx, SHCLOBJHANDLE hObj,
                                                   void *pvData, uint32_t cbData, uint32_t *pcbRead)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData,    VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    VBoxShClObjReadWriteMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_OBJ_READ, VBOX_SHCL_CPARMS_OBJ_READ);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.uHandle.SetUInt64(hObj);
    Msg.cbData.SetUInt32(cbData);
    Msg.pvData.SetPtr(pvData, cbData);
    Msg.cbChecksum.SetUInt32(0);
    Msg.pvChecksum.SetPtr(NULL, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Add checksum support. */

        if (pcbRead)
        {
            rc = Msg.cbData.GetUInt32(pcbRead); AssertRC(rc);
            AssertReturn(cbData >= *pcbRead, VERR_TOO_MUCH_DATA);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends a request to write to a transfer object to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Shared Clipboard command context to use for the connection.
 * @param   hObj                Object handle of object to write to.
 * @param   pvData              Buffer of data to write to object.
 * @param   cbData              Size (in bytes) of buffer.
 * @param   pcbWritten          Where to store the amount (in bytes) written to the object.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferObjWriteSend(PVBGLR3SHCLCMDCTX pCtx, SHCLOBJHANDLE hObj,
                                                    void *pvData, uint32_t cbData, uint32_t *pcbWritten)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    /* cbData can be 0. */
    /* pcbWritten is optional. */

    VBoxShClObjReadWriteMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->idClient,
                       VBOX_SHCL_GUEST_FN_OBJ_WRITE, VBOX_SHCL_CPARMS_OBJ_WRITE);

    Msg.uContext.SetUInt64(pCtx->idContext);
    Msg.uHandle.SetUInt64(hObj);
    Msg.cbData.SetUInt32(cbData);
    Msg.pvData.SetPtr(pvData, cbData);
    Msg.cbChecksum.SetUInt32(0);
    Msg.pvChecksum.SetPtr(NULL, 0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Add checksum support. */

        if (pcbWritten)
            *pcbWritten = cbData; /** @todo For now return all as being written. */
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/*********************************************************************************************************************************
*   Transfer interface implementations                                                                                           *
*********************************************************************************************************************************/

/** @copydoc SHCLTXPROVIDERIFACE::pfnRootListRead */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGRootListRead(PSHCLTXPROVIDERCTX pCtx)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardTransferRootListRead(pCmdCtx, pCtx->pTransfer);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListOpen */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGListOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLLISTOPENPARMS pOpenParms,
                                                                PSHCLLISTHANDLE phList)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardTransferListOpenSend(pCmdCtx, pOpenParms, phList);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListClose */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGListClose(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardTransferListCloseSend(pCmdCtx, hList);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListHdrRead */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGListHdrRead(PSHCLTXPROVIDERCTX pCtx,
                                                                  SHCLLISTHANDLE hList, PSHCLLISTHDR pListHdr)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = ShClTransferListHdrInit(pListHdr);
    if (RT_SUCCESS(rc))
    {
        if (RT_SUCCESS(rc))
        {
            rc = VbglR3ClipboardTransferListHdrRead(pCmdCtx, hList, 0 /* fFlags */, pListHdr);
        }
        else
            ShClTransferListHdrDestroy(pListHdr);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListEntryRead */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGListEntryRead(PSHCLTXPROVIDERCTX pCtx,
                                                                     SHCLLISTHANDLE hList, PSHCLLISTENTRY pListEntry)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardTransferListEntryRead(pCmdCtx, hList, pListEntry);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjOpen */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGObjOpen(PSHCLTXPROVIDERCTX pCtx,
                                                               PSHCLOBJOPENCREATEPARMS pCreateParms, PSHCLOBJHANDLE phObj)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardTransferObjOpenSend(pCmdCtx, pCreateParms, phObj);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjClose */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGObjClose(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    int rc = VbglR3ClipboardTransferObjCloseSend(pCmdCtx, hObj);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjRead */
static DECLCALLBACK(int) vbglR3ClipboardTransferIfaceHGObjRead(PSHCLTXPROVIDERCTX pCtx,
                                                               SHCLOBJHANDLE hObj, void *pvData, uint32_t cbData,
                                                               uint32_t fFlags, uint32_t *pcbRead)
{
    LogFlowFuncEnter();

    PVBGLR3SHCLCMDCTX pCmdCtx = (PVBGLR3SHCLCMDCTX)pCtx->pvUser;
    AssertPtr(pCmdCtx);

    RT_NOREF(fFlags); /* Not used yet. */

    int rc = VbglR3ClipboardTransferObjReadSend(pCmdCtx, hObj, pvData, cbData, pcbRead);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates (and registers) a transfer on the guest side.
 *
 * @returns VBox status code.
 * @param   pCmdCtx             Command context to use.
 * @param   pTransferCtx        Transfer context to init transfer for.
 * @param   enmDir              Specifies the transfer direction of this transfer.
 * @param   enmSource           Specifies the data source of the transfer.
 * @param   idTransfer          ID of transfer to create.
 * @param   ppTransfer          Where to return the transfer object on success. Optional.
 */
static int vbglR3ClipboardTransferCreate(PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFERCTX pTransferCtx,
                                         SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource, SHCLTRANSFERID idTransfer,
                                         PSHCLTRANSFER *ppTransfer)
{
    AssertReturn(idTransfer != NIL_SHCLTRANSFERID, VERR_WRONG_ORDER);

    RT_NOREF(pCmdCtx);

    PSHCLTRANSFER pTransfer;
    int rc = ShClTransferCreate(enmDir, enmSource, &pCmdCtx->Transfers.Callbacks, &pTransfer);
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferCtxRegisterById(pTransferCtx, pTransfer, idTransfer);
        if (   RT_SUCCESS(rc)
            && ppTransfer)
            *ppTransfer = pTransfer;
    }

    if (RT_SUCCESS(rc))
        LogRel2(("Shared Clipboard: Transfer %RU32 successfully created\n", idTransfer));
    else
        LogRel(("Shared Clipboard: Error creating transfer %RU16, rc=%Rrc\n", idTransfer, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Initializes a transfer on the guest side.
 *
 * @returns VBox status code.
 * @param   pCmdCtx             Command context to use.
 * @param   pTransfer           Transfer to init.
 */
static int vbglR3ClipboardTransferInit(PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFER pTransfer)
{
    LogFlowFuncEnter();

    SHCLTRANSFERDIR const enmDir = ShClTransferGetDir(pTransfer);

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);

    /* Assign local provider first and overwrite interface methods below if needed. */
    ShClTransferProviderLocalQueryInterface(&Provider);

    /* If this is a read transfer (reading data from host), set the interface to use
     * our VbglR3 routines here. */
    if (enmDir == SHCLTRANSFERDIR_FROM_REMOTE) /* Host -> Guest */
    {
        Provider.Interface.pfnRootListRead  = vbglR3ClipboardTransferIfaceHGRootListRead;

        Provider.Interface.pfnListOpen      = vbglR3ClipboardTransferIfaceHGListOpen;
        Provider.Interface.pfnListClose     = vbglR3ClipboardTransferIfaceHGListClose;
        Provider.Interface.pfnListHdrRead   = vbglR3ClipboardTransferIfaceHGListHdrRead;
        Provider.Interface.pfnListEntryRead = vbglR3ClipboardTransferIfaceHGListEntryRead;

        Provider.Interface.pfnObjOpen       = vbglR3ClipboardTransferIfaceHGObjOpen;
        Provider.Interface.pfnObjClose      = vbglR3ClipboardTransferIfaceHGObjClose;
        Provider.Interface.pfnObjRead       = vbglR3ClipboardTransferIfaceHGObjRead;
    }
    else if (enmDir == SHCLTRANSFERDIR_TO_REMOTE) /* Guest -> Host */
    {
        /* Uses the local provider assigned above. */
    }
    else
        AssertFailed();

    Provider.pvUser = pCmdCtx;

    /* Set the provider first before calling ShClTransferInit(), as the init callback might utilize some of the
     * provider functions. */
    int rc = ShClTransferSetProvider(pTransfer, &Provider);
    if (RT_SUCCESS(rc))
        rc = ShClTransferInit(pTransfer);

    SHCLTRANSFERID const idTransfer = ShClTransferGetID(pTransfer);

    if (RT_SUCCESS(rc))
    {
        LogRel2(("Shared Clipboard: Transfer %RU32 (%s) successfully initialized\n",
                 idTransfer, enmDir == SHCLTRANSFERDIR_FROM_REMOTE ? "host -> guest" : "guest -> host"));
    }
    else
        LogRel(("Shared Clipboard: Unable to initialize transfer %RU16, rc=%Rrc\n", idTransfer, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a transfer on the guest side.
 *
 * @returns VBox status code.
 * @param   pCmdCtx             Command context to use.
 * @param   pTransferCtx        Transfer context to uninit transfer for.
 * @param   idTransfer          ID of transfer to initialize.
 */
static int vbglR3ClipboardTransferDestroy(PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFERCTX pTransferCtx, SHCLTRANSFERID idTransfer)
{
    RT_NOREF(pCmdCtx);

    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx, idTransfer);
    if (pTransfer)
    {
        rc = ShClTransferCtxUnregisterById(pTransferCtx, idTransfer);
        if (RT_SUCCESS(rc))
            rc = ShClTransferDestroy(pTransfer);

        if (RT_SUCCESS(rc))
        {
            LogRel(("Shared Clipboard: Transfer %RU16 successfully uninitialized\n", idTransfer));
        }
        else
            LogRel(("Shared Clipboard: Unable to uninitialized transfer %RU16, rc=%Rrc\n", idTransfer, rc));
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Requests a new host -> guest transfer from the host.
 *
 * On success this will issue an INITIALIZED status reply from the host with a transfer ID set.
 * This ID will be used to initialize the transfer on the guest side then.
 *
 * @returns VBox status code.
 * @param   pCmdCtx             Command context to use.
 */
VBGLR3DECL(int) VbglR3ClipboardTransferRequest(PVBGLR3SHCLCMDCTX pCmdCtx)
{
    LogFlowFuncEnter();

    LogRel2(("Shared Clipboard: Requesting new host -> guest transfer from host\n"));

    int rc = vbglR3ClipboardTransferSendStatusEx(pCmdCtx, 0 /* Context ID not needed */,
                                                 SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Starts a transfer on the guest side.
 *
 * @returns VBox status code.
 * @param   pCmdCtx             Command context to use.
 * @param   pTransferCtx        Transfer context to start transfer for.
 * @param   uTransferID         ID to use for transfer to start.
 */
static int vbglR3ClipboardTransferStart(PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFERCTX pTransferCtx,
                                        SHCLTRANSFERID uTransferID)
{
    RT_NOREF(pCmdCtx);

    LogFlowFuncEnter();

    int rc;

    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx, uTransferID);
    if (pTransfer)
    {
        rc = ShClTransferStart(pTransfer);
        if (RT_SUCCESS(rc))
        {
            LogRel(("Shared Clipboard: Transfer %RU32 successfully started\n", uTransferID));
        }
        else
            LogRel(("Shared Clipboard: Unable to start transfer %RU16, rc=%Rrc\n", uTransferID, rc));
    }
    else
        rc = VERR_SHCLPB_TRANSFER_ID_NOT_FOUND;

    /* Send a reply in any case. */
    int rc2 = VbglR3ClipboardTransferSendStatus(pCmdCtx, pTransfer,
                                                  RT_SUCCESS(rc)
                                                ? SHCLTRANSFERSTATUS_STARTED : SHCLTRANSFERSTATUS_ERROR, rc);
    if (RT_SUCCESS(rc))
        rc = rc2;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

VBGLR3DECL(int) VbglR3ClipboardEventGetNextEx(uint32_t idMsg, uint32_t cParms,
                                              PVBGLR3SHCLCMDCTX pCmdCtx, PSHCLTRANSFERCTX pTransferCtx,
                                              PVBGLR3CLIPBOARDEVENT pEvent)
{
    AssertPtrReturn(pCmdCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(pTransferCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pEvent,       VERR_INVALID_POINTER);

    LogFunc(("Handling idMsg=%RU32 (%s), cParms=%RU32\n", idMsg, ShClHostMsgToStr(idMsg), cParms));

    int rc;
    if (!pCmdCtx->fUseLegacyProtocol)
    {
        bool fErrorSent = false; /* Whether an error has been reported back to the host already. */

        switch (idMsg)
        {
            case VBOX_SHCL_HOST_MSG_TRANSFER_STATUS:
            {
                SHCLTRANSFERDIR    enmDir;
                SHCLTRANSFERREPORT transferReport;
                rc = VbglR3ClipboarTransferStatusRecv(pCmdCtx, &enmDir, &transferReport);
                if (RT_SUCCESS(rc))
                {
                    const SHCLTRANSFERID idTransfer = VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext);

                    LogRel2(("Shared Clipboard: Received status %s (%Rrc) for transfer %RU16\n",
                             ShClTransferStatusToStr(transferReport.uStatus), transferReport.rc, idTransfer));

                    SHCLSOURCE enmSource = SHCLSOURCE_INVALID;

                    switch (transferReport.uStatus)
                    {
                        case SHCLTRANSFERSTATUS_REQUESTED: /* Only used for H->G transfers. */
                        {
                            enmDir    = SHCLTRANSFERDIR_FROM_REMOTE;
                            enmSource = SHCLSOURCE_REMOTE;

                            /* The host acknowledged our request to create a new transfer.
                             * So create a new transfer here with the transfer ID we just got from the host.
                             *
                             * Actual initialization will be done as soon as the host sends use the INITIALIZED status for it.
                             */
                            PSHCLTRANSFER pTransfer;
                            rc = vbglR3ClipboardTransferCreate(pCmdCtx, pTransferCtx, enmDir, enmSource, idTransfer, &pTransfer);

                            /* As soon as we've created our transfer locally, report back INITIALIZED to the host.
                             * This will initialize the transfer on the host, so that in turn reports INITIALIZED
                             * back to us (see case down below).*/
                            int rc2 = VbglR3ClipboardTransferSendStatus(pCmdCtx, pTransfer,
                                                                          RT_SUCCESS(rc)
                                                                        ? SHCLTRANSFERSTATUS_INITIALIZED : SHCLTRANSFERSTATUS_ERROR, rc);
                            if (RT_SUCCESS(rc))
                                rc = rc2;
                            break;
                        }

                        case SHCLTRANSFERSTATUS_INITIALIZED:
                        {
                            /* The host announces the transfer direction from its point of view, so inverse the direction here. */
                            if (enmDir == SHCLTRANSFERDIR_TO_REMOTE) /* H -> G */
                            {
                                enmDir    = SHCLTRANSFERDIR_FROM_REMOTE;
                                enmSource = SHCLSOURCE_REMOTE;
                            }
                            else if (enmDir == SHCLTRANSFERDIR_FROM_REMOTE) /* G -> H */
                            {
                                enmDir    = SHCLTRANSFERDIR_TO_REMOTE;
                                enmSource = SHCLSOURCE_LOCAL;
                            }
                            else
                                AssertFailedBreakStmt(rc = VERR_INVALID_PARAMETER);

                            if (enmDir == SHCLTRANSFERDIR_FROM_REMOTE) /* H->G */
                            {
                                /* The host reported INITIALIZED for the transfer.
                                 * So init our local transfer as well now. */
                                PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx, idTransfer);
                                if (pTransfer)
                                {
                                    rc = vbglR3ClipboardTransferInit(pCmdCtx, pTransfer);

                                    /* Only send back a reply on error -- we already reported INITIALIZED
                                     * in the case SHCLTRANSFERSTATUS_REQUESTED above. */
                                    if (RT_FAILURE(rc))
                                    {
                                        int rc2 = VbglR3ClipboardTransferSendStatus(pCmdCtx, pTransfer,
                                                                                    SHCLTRANSFERSTATUS_ERROR, rc);
                                        AssertRC(rc2);
                                    }
                                }
                                else
                                    rc = VERR_SHCLPB_TRANSFER_ID_NOT_FOUND;
                            }
                            else if (enmDir == SHCLTRANSFERDIR_TO_REMOTE) /* G->H */
                            {
                                /* The host reported the INITIALIZED status together with the transfer ID.
                                 * So create a local transfer here with that ID. */
                                PSHCLTRANSFER pTransfer;
                                rc = vbglR3ClipboardTransferCreate(pCmdCtx, pTransferCtx, enmDir, enmSource, idTransfer, &pTransfer);
                                if (RT_SUCCESS(rc))
                                    rc = vbglR3ClipboardTransferInit(pCmdCtx, pTransfer);

                                /* Send a reply in any case. */
                                int rc2 = VbglR3ClipboardTransferSendStatus(pCmdCtx, pTransfer,
                                                                              RT_SUCCESS(rc)
                                                                            ? SHCLTRANSFERSTATUS_INITIALIZED : SHCLTRANSFERSTATUS_ERROR, rc);
                                if (RT_SUCCESS(rc))
                                    rc = rc2;
                            }
                            else
                                AssertFailedBreakStmt(rc = VERR_INVALID_PARAMETER);

                            break;
                        }

                        case SHCLTRANSFERSTATUS_STARTED:
                        {
                            rc = vbglR3ClipboardTransferStart(pCmdCtx, pTransferCtx, idTransfer);
                            break;
                        }

                        case SHCLTRANSFERSTATUS_UNINITIALIZED:
                            RT_FALL_THROUGH();
                        case SHCLTRANSFERSTATUS_COMPLETED:
                            RT_FALL_THROUGH();
                        case SHCLTRANSFERSTATUS_CANCELED:
                            RT_FALL_THROUGH();
                        case SHCLTRANSFERSTATUS_KILLED:
                            RT_FALL_THROUGH();
                        case SHCLTRANSFERSTATUS_ERROR:
                        {
                            rc = vbglR3ClipboardTransferDestroy(pCmdCtx, pTransferCtx, idTransfer);
                            break;
                        }

                        default:
                            LogRel(("Shared Clipboard: Received unknown status %#x (%Rrc) for transfer %RU16\n",
                                    transferReport.uStatus, pEvent->u.TransferStatus.Report.rc, pEvent->u.TransferStatus.uID));
                            rc = VERR_NOT_SUPPORTED;
                            break;
                    }

                    if (RT_SUCCESS(rc))
                    {
                        pEvent->u.TransferStatus.enmDir = enmDir;
                        pEvent->u.TransferStatus.Report = transferReport;
                        pEvent->u.TransferStatus.uID    = VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext);

                        pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_TRANSFER_STATUS;
                    }
                }
                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_HDR_READ:
            {
                uint32_t fRoots;
                rc = VbglR3ClipboardTransferRootListHdrReadReq(pCmdCtx, &fRoots);

                /** @todo Validate / handle fRoots. */

                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                             VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    SHCLLISTHDR rootListHdr;
                    ShClTransferListHdrInit(&rootListHdr);

                    rootListHdr.cEntries = ShClTransferRootsCount(pTransfer);

                    LogFlowFunc(("cRoots=%RU32\n", rootListHdr.cEntries));

                    rc = VbglR3ClipboardTransferRootListHdrReadReply(pCmdCtx, &rootListHdr);
                }
                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_ROOT_LIST_ENTRY_READ:
            {
                uint64_t uIndex;
                uint32_t fInfo;
                rc = VbglR3ClipboardTransferRootListEntryReadReq(pCmdCtx, &uIndex, &fInfo);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                             VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    PCSHCLLISTENTRY pEntry = ShClTransferRootsEntryGet(pTransfer, uIndex);
                    if (pEntry)
                    {
                        rc = VbglR3ClipboardTransferRootListEntryReadReply(pCmdCtx, uIndex, pEntry);
                    }
                    else
                        rc = VERR_NOT_FOUND;
                }
                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_OPEN:
            {
                SHCLLISTOPENPARMS openParmsList;
                rc = ShClTransferListOpenParmsInit(&openParmsList);
                if (RT_SUCCESS(rc))
                {
                    rc = VbglR3ClipboardTransferListOpenRecv(pCmdCtx, &openParmsList);
                    if (RT_SUCCESS(rc))
                    {
                        PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                                 VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                        AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                        LogFlowFunc(("pszPath=%s\n", openParmsList.pszPath));

                        SHCLLISTHANDLE hList = NIL_SHCLLISTHANDLE;
                        rc = ShClTransferListOpen(pTransfer, &openParmsList, &hList);

                        /* Reply in any case. */
                        int rc2 = VbglR3ClipboardTransferListOpenReply(pCmdCtx, rc, hList);
                        AssertRC(rc2);
                    }

                    ShClTransferListOpenParmsDestroy(&openParmsList);
                }

                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_CLOSE:
            {
                SHCLLISTHANDLE hList;
                rc = VbglR3ClipboardTransferListCloseRecv(pCmdCtx, &hList);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                             VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    rc = ShClTransferListClose(pTransfer, hList);

                    /* Reply in any case. */
                    int rc2 = VbglR3ClipboardTransferListCloseReply(pCmdCtx, rc, hList);
                    AssertRC(rc2);
                }

                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_HDR_READ:
            {
                /** @todo Handle filter + list features. */

                SHCLLISTHANDLE hList  = NIL_SHCLLISTHANDLE;
                uint32_t       fFlags = 0;
                rc = VbglR3ClipboardTransferListHdrReadRecvReq(pCmdCtx, &hList, &fFlags);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                             VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    SHCLLISTHDR hdrList;
                    rc = ShClTransferListGetHeader(pTransfer, hList, &hdrList);
                    if (RT_SUCCESS(rc))
                    {
                        rc = VbglR3ClipboardTransferListHdrWrite(pCmdCtx, hList, &hdrList);

                        ShClTransferListHdrDestroy(&hdrList);
                    }
                }

                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_LIST_ENTRY_READ:
            {
                LogFlowFunc(("VBOX_SHCL_HOST_MSG_TRANSFER_LIST_ENTRY_READ\n"));

                SHCLLISTHANDLE hList;
                uint32_t       fInfo;
                rc = VbglR3ClipboardTransferListEntryReadRecvReq(pCmdCtx, &hList, &fInfo);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                             VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    SHCLLISTENTRY entryList;
                    rc = ShClTransferListEntryInit(&entryList);
                    if (RT_SUCCESS(rc))
                    {
                        rc = ShClTransferListRead(pTransfer, hList, &entryList);
                        if (RT_SUCCESS(rc))
                        {
                            PSHCLFSOBJINFO pObjInfo = (PSHCLFSOBJINFO)entryList.pvInfo;
                            Assert(entryList.cbInfo == sizeof(SHCLFSOBJINFO));

                            RT_NOREF(pObjInfo);

                            LogFlowFunc(("\t%s (%RU64 bytes)\n", entryList.pszName, pObjInfo->cbObject));

                            rc = VbglR3ClipboardTransferListEntryWrite(pCmdCtx, hList, &entryList);

                            ShClTransferListEntryDestroy(&entryList);
                        }
                    }
                }

                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_OPEN:
            {
                SHCLOBJOPENCREATEPARMS openParms;
                rc = ShClTransferObjOpenParmsInit(&openParms);
                if (RT_SUCCESS(rc))
                {
                    rc = VbglR3ClipboardTransferObjOpenRecv(pCmdCtx, &openParms);
                    if (RT_SUCCESS(rc))
                    {
                        PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                                 VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                        AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                        SHCLOBJHANDLE hObj;
                        rc = ShClTransferObjOpen(pTransfer, &openParms, &hObj);

                        /* Reply in any case. */
                        int rc2 = VbglR3ClipboardTransferObjOpenReply(pCmdCtx, rc, hObj);
                        AssertRC(rc2);
                    }

                    ShClTransferObjOpenParmsDestroy(&openParms);
                }

                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_CLOSE:
            {
                SHCLOBJHANDLE hObj;
                rc = VbglR3ClipboardTransferObjCloseRecv(pCmdCtx, &hObj);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                             VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    rc = ShClTransferObjClose(pTransfer, hObj);

                    /* Reply in any case. */
                    int rc2 = VbglR3ClipboardTransferObjCloseReply(pCmdCtx, rc, hObj);
                    AssertRC(rc2);
                }

                break;
            }

            case VBOX_SHCL_HOST_MSG_TRANSFER_OBJ_READ:
            {
                SHCLOBJHANDLE hObj;
                uint32_t      cbBuf;
                uint32_t      fFlags;
                rc = VbglR3ClipboardTransferObjReadRecv(pCmdCtx, &hObj, &cbBuf, &fFlags);
                if (RT_SUCCESS(rc))
                {
                    PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(pTransferCtx,
                                                                         VBOX_SHCL_CONTEXTID_GET_TRANSFER(pCmdCtx->idContext));
                    AssertPtrBreakStmt(pTransfer, rc = VERR_NOT_FOUND);

                    AssertBreakStmt(pCmdCtx->Transfers.cbChunkSize, rc = VERR_INVALID_PARAMETER);

                    const uint32_t cbToRead = RT_MIN(cbBuf, pCmdCtx->Transfers.cbChunkSize);

                    LogFlowFunc(("hObj=%RU64, cbBuf=%RU32, fFlags=0x%x -> cbChunkSize=%RU32, cbToRead=%RU32\n",
                                 hObj, cbBuf, fFlags, pCmdCtx->Transfers.cbChunkSize, cbToRead));

                    void *pvBuf = RTMemAlloc(cbToRead);
                    if (pvBuf)
                    {
                        uint32_t cbRead;
                        rc = ShClTransferObjRead(pTransfer, hObj, pvBuf, cbToRead, fFlags, &cbRead);
                        if (RT_SUCCESS(rc))
                            rc = VbglR3ClipboardTransferObjWriteSend(pCmdCtx, hObj, pvBuf, cbRead, NULL /* pcbWritten */);

                        RTMemFree(pvBuf);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }

                break;
            }

            default:
            {
                rc = VbglR3ClipboardEventGetNext(idMsg, cParms, pCmdCtx, pEvent);
                if (RT_FAILURE(rc))
                    fErrorSent = true;
                break;
            }
        }

        if (   !fErrorSent
            && RT_FAILURE(rc))
        {
            /* Report transfer-specific error back to the host. */
            int rc2 = vbglR3ClipboardTransferSendStatusEx(pCmdCtx, pCmdCtx->idContext, SHCLTRANSFERSTATUS_ERROR, rc);
            AssertRC(rc2);
        }
    }
    else
    {
        /*
         * This builds on what we did in VbglR3ClipboardMsgPeekWait, so
         * !HACK ALERT! cParms is the format flag or flags.
         */
        rc = VINF_SUCCESS;
        switch (idMsg)
        {
            case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS;
                pEvent->u.fReportedFormats = cParms;
                break;

            case VBOX_SHCL_HOST_MSG_READ_DATA:
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_READ_DATA;
                pEvent->u.fReadData = cParms;
                break;

            case VBOX_SHCL_HOST_MSG_QUIT:
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_QUIT;
                break;

            default:
                AssertMsgFailed(("%u (%#x)\n", idMsg, idMsg));
                rc = VERR_NOT_SUPPORTED;
                break;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

VBGLR3DECL(int) VbglR3ClipboardEventGetNext(uint32_t idMsg, uint32_t cParms, PVBGLR3SHCLCMDCTX pCtx, PVBGLR3CLIPBOARDEVENT pEvent)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    RT_NOREF(cParms);

    int rc;
    if (!pCtx->fUseLegacyProtocol)
    {
        LogFunc(("Handling idMsg=%RU32 (%s)\n", idMsg, ShClHostMsgToStr(idMsg)));
        switch (idMsg)
        {
            case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
            {
                rc = vbglR3ClipboardFormatsReportRecv(pCtx, &pEvent->u.fReportedFormats);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS;
                break;
            }

            case VBOX_SHCL_HOST_MSG_READ_DATA_CID:
            {
                rc = vbglR3ClipboardFetchReadDataCid(pCtx, &pEvent->u.fReadData);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_READ_DATA;
                break;
            }

            case VBOX_SHCL_HOST_MSG_READ_DATA:
            {
                rc = vbglR3ClipboardFetchReadData(pCtx, &pEvent->u.fReadData);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_READ_DATA;
                break;
            }

            case VBOX_SHCL_HOST_MSG_QUIT:
            {
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_QUIT;
                rc = VINF_SUCCESS;
                break;
            }

            default:
            {
                /** @todo r=bird: BUGBUG - need a skip command here! */
                rc = VERR_NOT_SUPPORTED;
                break;
            }
        }

        if (RT_SUCCESS(rc))
        {
            /* Copy over our command context to the event. */
            pEvent->cmdCtx = *pCtx;
        }
        else
        {
            /* Report error back to the host. */
            int rc2 = VbglR3ClipboardWriteError(pCtx->idClient, rc);
            AssertRC(rc2);
        }
    }
    else
    {
        /*
         * This builds on what we did in VbglR3ClipboardMsgPeekWait, so
         * !HACK ALERT! cParms is the format flag or flags.
         */
        rc = VINF_SUCCESS;
        switch (idMsg)
        {
            case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS;
                pEvent->u.fReportedFormats = cParms;
                break;

            case VBOX_SHCL_HOST_MSG_READ_DATA:
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_READ_DATA;
                pEvent->u.fReadData = cParms;
                break;

            case VBOX_SHCL_HOST_MSG_QUIT:
                pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_QUIT;
                break;

            default:
                AssertMsgFailed(("%u (%#x)\n", idMsg, idMsg));
                rc = VERR_NOT_SUPPORTED;
                break;
        }
        pEvent->cmdCtx = *pCtx;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Frees (destroys) a formerly allocated Shared Clipboard event.
 *
 * @returns IPRT status code.
 * @param   pEvent              Event to free (destroy).
 */
VBGLR3DECL(void) VbglR3ClipboardEventFree(PVBGLR3CLIPBOARDEVENT pEvent)
{
    if (!pEvent)
        return;

    /* Some messages require additional cleanup. */
    switch (pEvent->enmType)
    {
        default:
            break;
    }

    RTMemFree(pEvent);
    pEvent = NULL;
}

/**
 * Reports (advertises) guest clipboard formats to the host.
 *
 * Legacy function, do not use anymore.
 *
 * @returns VBox status code.
 * @param   idClient        The client id returned by VbglR3ClipboardConnect().
 * @param   fFormats        The formats to report.
 */
VBGLR3DECL(int) VbglR3ClipboardReportFormats(HGCMCLIENTID idClient, uint32_t fFormats)
{
    struct
    {
        VBGLIOCHGCMCALL             Hdr;
        VBoxShClParmReportFormats   Parms;
    } Msg;

    RT_ZERO(Msg);

    VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, VBOX_SHCL_GUEST_FN_REPORT_FORMATS, VBOX_SHCL_CPARMS_REPORT_FORMATS);
    VbglHGCMParmUInt32Set(&Msg.Parms.f32Formats, fFormats);

    int rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends guest clipboard data to the host.
 *
 * Legacy function kept for compatibility, do not use anymore.
 *
 * This is usually called in reply to a VBOX_SHCL_HOST_MSG_READ_DATA message
 * from the host.
 *
 * @returns VBox status code.
 * @param   idClient    The client id returned by VbglR3ClipboardConnect().
 * @param   fFormat     The format of the data.
 * @param   pvData      Pointer to the data to send.  Can be NULL if @a cbData
 *                      is zero.
 * @param   cbData      Number of bytes of data to send.  Zero is valid.
 */
VBGLR3DECL(int) VbglR3ClipboardWriteData(HGCMCLIENTID idClient, uint32_t fFormat, void *pv, uint32_t cb)
{
    LogFlowFuncEnter();

    struct
    {
        VBGLIOCHGCMCALL             Hdr;
        VBoxShClParmDataWriteOld    Parms;
    } Msg;

    VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, VBOX_SHCL_CPARMS_DATA_WRITE_OLD);
    VbglHGCMParmUInt32Set(&Msg.Parms.f32Format, fFormat);
    VbglHGCMParmPtrSet(&Msg.Parms.pData, pv, cb);

    int rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sends guest clipboard data to the host.
 *
 * This is usually called in reply to a VBOX_SHCL_HOST_MSG_READ_DATA message
 * from the host.
 *
 * @returns VBox status code.
 * @param   pCtx        The command context returned by VbglR3ClipboardConnectEx().
 * @param   fFormat     Clipboard format to send.
 * @param   pvData      Pointer to the data to send.  Can be NULL if @a cbData
 *                      is zero.
 * @param   cbData      Number of bytes of data to send.  Zero is valid.
 */
VBGLR3DECL(int) VbglR3ClipboardWriteDataEx(PVBGLR3SHCLCMDCTX pCtx, SHCLFORMAT fFormat, void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(cbData == 0 || RT_VALID_PTR(pvData), VERR_INVALID_PARAMETER);

    LogFlowFunc(("fFormat=%#x pvData=%p cbData=%#x\n", fFormat, pvData, cbData));

    int rc;
    if (pCtx->fUseLegacyProtocol)
        rc = VbglR3ClipboardWriteData(pCtx->idClient, fFormat, pvData, cbData);
    else
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            VBoxShClParmDataWrite   Parms;
        } Msg;

        RT_ZERO(Msg);

        VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->idClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, VBOX_SHCL_CPARMS_DATA_WRITE);
        Msg.Parms.id64Context.SetUInt64(pCtx->idContext);
        Msg.Parms.f32Format.SetUInt32(fFormat);
        Msg.Parms.pData.SetPtr(pvData, cbData);

        LogFlowFunc(("CID=%RU32\n", pCtx->idContext));

        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Writes an error to the host.
 *
 * @returns IPRT status code.
 * @param   idClient            The client id returned by VbglR3ClipboardConnect().
 * @param   rcErr               Error (IPRT-style) to send.
 */
VBGLR3DECL(int) VbglR3ClipboardWriteError(HGCMCLIENTID idClient, int rcErr)
{
    AssertReturn(idClient, VERR_INVALID_PARAMETER);

    VBoxShClWriteErrorMsg Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, VBOX_SHCL_GUEST_FN_ERROR, VBOX_SHCL_CPARMS_ERROR);

    /** @todo Context ID not used yet. */
    Msg.uContext.SetUInt64(0);
    Msg.rc.SetUInt32((uint32_t)rcErr); /* uint32_t vs. int. */

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    if (RT_FAILURE(rc))
        LogFlowFunc(("Sending error %Rrc failed with rc=%Rrc\n", rcErr, rc));
    if (rc == VERR_NOT_SUPPORTED)
        rc = VINF_SUCCESS;

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reporting error %Rrc to the host failed with %Rrc\n", rcErr, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

