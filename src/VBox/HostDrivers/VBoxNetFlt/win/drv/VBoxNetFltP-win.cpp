/* $Id: VBoxNetFltP-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFltP-win.cpp - Bridged Networking Driver, Windows Specific Code.
 * Protocol edge
 */
/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include "VBoxNetFltCmn-win.h"

#ifdef VBOXNETADP
# error "No protocol edge"
#endif

#define VBOXNETFLT_PT_STATUS_IS_FILTERED(_s) (\
       (_s) == NDIS_STATUS_MEDIA_CONNECT \
    || (_s) == NDIS_STATUS_MEDIA_DISCONNECT \
    )

/**
 * performs binding to the given adapter
 */
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtDoBinding(PVBOXNETFLTINS pThis, PNDIS_STRING pOurDeviceName, PNDIS_STRING pBindToDeviceName)
{
    Assert(pThis->u.s.WinIf.PtState.PowerState == NdisDeviceStateD3);
    Assert(pThis->u.s.WinIf.PtState.OpState == kVBoxNetDevOpState_Deinitialized);
    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    vboxNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kVBoxNetDevOpState_Initializing);

    NDIS_STATUS Status = vboxNetFltWinCopyString(&pThis->u.s.WinIf.MpDeviceName, pOurDeviceName);
    Assert (Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        vboxNetFltWinSetPowerState(&pThis->u.s.WinIf.PtState, NdisDeviceStateD0);
        pThis->u.s.WinIf.OpenCloseStatus = NDIS_STATUS_SUCCESS;

        UINT iMedium;
        NDIS_STATUS TmpStatus;
        NDIS_MEDIUM aenmNdisMedium[] =
        {
                /* Ethernet */
                NdisMedium802_3,
                /* Wan */
                NdisMediumWan
        };

        NdisResetEvent(&pThis->u.s.WinIf.OpenCloseEvent);

        NdisOpenAdapter(&Status, &TmpStatus, &pThis->u.s.WinIf.hBinding, &iMedium,
                aenmNdisMedium, RT_ELEMENTS(aenmNdisMedium),
                g_VBoxNetFltGlobalsWin.Pt.hProtocol,
                pThis,
                pBindToDeviceName,
                0, /* IN UINT OpenOptions, (reserved, should be NULL) */
                NULL /* IN PSTRING AddressingInformation  OPTIONAL */
                );
        Assert(Status == NDIS_STATUS_PENDING || Status == STATUS_SUCCESS);
        if (Status == NDIS_STATUS_PENDING)
        {
            NdisWaitEvent(&pThis->u.s.WinIf.OpenCloseEvent, 0);
            Status = pThis->u.s.WinIf.OpenCloseStatus;
        }

        Assert(Status == NDIS_STATUS_SUCCESS);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            Assert(pThis->u.s.WinIf.hBinding);
            pThis->u.s.WinIf.enmMedium = aenmNdisMedium[iMedium];
            vboxNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kVBoxNetDevOpState_Initialized);

            Status = vboxNetFltWinMpInitializeDevideInstance(pThis);
            Assert(Status == NDIS_STATUS_SUCCESS);
            if (Status == NDIS_STATUS_SUCCESS)
            {
                return NDIS_STATUS_SUCCESS;
            }
            else
            {
                LogRelFunc(("vboxNetFltWinMpInitializeDevideInstance failed, Status 0x%x\n", Status));
            }

            vboxNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kVBoxNetDevOpState_Deinitializing);
            vboxNetFltWinPtCloseInterface(pThis, &TmpStatus);
            Assert(TmpStatus == NDIS_STATUS_SUCCESS);
            vboxNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kVBoxNetDevOpState_Deinitialized);
        }
        else
        {
            LogRelFunc(("NdisOpenAdapter failed, Status (0x%x)", Status));
        }

        vboxNetFltWinSetOpState(&pThis->u.s.WinIf.PtState, kVBoxNetDevOpState_Deinitialized);
        pThis->u.s.WinIf.hBinding = NULL;
    }

    return Status;
}

static VOID vboxNetFltWinPtBindAdapter(OUT PNDIS_STATUS pStatus,
        IN NDIS_HANDLE hBindContext,
        IN PNDIS_STRING pDeviceNameStr,
        IN PVOID pvSystemSpecific1,
        IN PVOID pvSystemSpecific2)
{
    LogFlowFuncEnter();
    RT_NOREF2(hBindContext, pvSystemSpecific2);

    NDIS_STATUS Status;
    NDIS_HANDLE hConfig = NULL;

    NdisOpenProtocolConfiguration(&Status, &hConfig, (PNDIS_STRING)pvSystemSpecific1);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        PNDIS_CONFIGURATION_PARAMETER pParam;
        NDIS_STRING UppedBindStr = VBOX_NDIS_STRING_CONST("UpperBindings");
        NdisReadConfiguration(&Status, &pParam, hConfig, &UppedBindStr, NdisParameterString);
        Assert(Status == NDIS_STATUS_SUCCESS);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            PVBOXNETFLTINS pNetFlt;
            Status = vboxNetFltWinPtInitBind(&pNetFlt, &pParam->ParameterData.StringData, pDeviceNameStr);
            Assert(Status == NDIS_STATUS_SUCCESS);
        }

        NdisCloseConfiguration(hConfig);
    }

    *pStatus = Status;

    LogFlowFunc(("LEAVE: Status 0x%x\n", Status));
}

static VOID vboxNetFltWinPtOpenAdapterComplete(IN NDIS_HANDLE hProtocolBindingContext, IN NDIS_STATUS Status, IN NDIS_STATUS OpenErrorStatus)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;
    RT_NOREF1(OpenErrorStatus);

    LogFlowFunc(("ENTER: pNetFlt (0x%p), Status (0x%x), OpenErrorStatus(0x%x)\n", pNetFlt, Status, OpenErrorStatus));
    Assert(pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS)
    {
        pNetFlt->u.s.WinIf.OpenCloseStatus = Status;
        Assert(Status == NDIS_STATUS_SUCCESS);
        if (Status != NDIS_STATUS_SUCCESS)
            LogRelFunc(("Open Complete status is 0x%x", Status));
    }
    else
        LogRelFunc(("Adapter maintained status is 0x%x", pNetFlt->u.s.WinIf.OpenCloseStatus));
    NdisSetEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent);
    LogFlowFunc(("LEAVE: pNetFlt (0x%p), Status (0x%x), OpenErrorStatus(0x%x)\n", pNetFlt, Status, OpenErrorStatus));
}

static void vboxNetFltWinPtRequestsWaitComplete(PVBOXNETFLTINS pNetFlt)
{
    /* wait for request to complete */
    while (vboxNetFltWinAtomicUoReadWinState(pNetFlt->u.s.WinIf.StateFlags).fRequestInfo == VBOXNDISREQUEST_INPROGRESS)
    {
        vboxNetFltWinSleep(2);
    }

    /*
     * If the below miniport is going to low power state, complete the queued request
     */
    RTSpinlockAcquire(pNetFlt->hSpinlock);
    if (pNetFlt->u.s.WinIf.StateFlags.fRequestInfo & VBOXNDISREQUEST_QUEUED)
    {
        /* mark the request as InProgress before posting it to RequestComplete */
        pNetFlt->u.s.WinIf.StateFlags.fRequestInfo = VBOXNDISREQUEST_INPROGRESS;
        RTSpinlockRelease(pNetFlt->hSpinlock);
        vboxNetFltWinPtRequestComplete(pNetFlt, &pNetFlt->u.s.WinIf.PassDownRequest, NDIS_STATUS_FAILURE);
    }
    else
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
    }
}

DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtDoUnbinding(PVBOXNETFLTINS pNetFlt, bool bOnUnbind)
{
    NDIS_STATUS Status;
    uint64_t NanoTS = RTTimeSystemNanoTS();
    int cPPUsage;

    LogFlowFunc(("ENTER: pNetFlt 0x%p\n", pNetFlt));

    Assert(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Assert(vboxNetFltWinGetOpState(&pNetFlt->u.s.WinIf.PtState) == kVBoxNetDevOpState_Initialized);

    RTSpinlockAcquire(pNetFlt->hSpinlock);

    ASMAtomicUoWriteBool(&pNetFlt->fDisconnectedFromHost, true);
    ASMAtomicUoWriteBool(&pNetFlt->fRediscoveryPending, false);
    ASMAtomicUoWriteU64(&pNetFlt->NanoTSLastRediscovery, NanoTS);

    vboxNetFltWinSetOpState(&pNetFlt->u.s.WinIf.PtState, kVBoxNetDevOpState_Deinitializing);
    if (!bOnUnbind)
    {
        vboxNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kVBoxNetDevOpState_Deinitializing);
    }

    RTSpinlockRelease(pNetFlt->hSpinlock);

    vboxNetFltWinPtRequestsWaitComplete(pNetFlt);

    vboxNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.MpState);
    vboxNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.PtState);

    /* check packet pool is empty */
    cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hSendPacketPool);
    Assert(cPPUsage == 0);
    cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hRecvPacketPool);
    Assert(cPPUsage == 0);
    /* for debugging only, ignore the err in release */
    NOREF(cPPUsage);

    if (!bOnUnbind || !vboxNetFltWinMpDeInitializeDeviceInstance(pNetFlt, &Status))
    {
        vboxNetFltWinPtCloseInterface(pNetFlt, &Status);
        vboxNetFltWinSetOpState(&pNetFlt->u.s.WinIf.PtState, kVBoxNetDevOpState_Deinitialized);

        if (!bOnUnbind)
        {
            Assert(vboxNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kVBoxNetDevOpState_Deinitializing);
            vboxNetFltWinSetOpState(&pNetFlt->u.s.WinIf.MpState, kVBoxNetDevOpState_Deinitialized);
        }
        else
        {
            Assert(vboxNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kVBoxNetDevOpState_Deinitialized);
        }
    }
    else
    {
        Assert(vboxNetFltWinGetOpState(&pNetFlt->u.s.WinIf.MpState) == kVBoxNetDevOpState_Deinitialized);
    }

    LogFlowFunc(("LEAVE: pNetFlt 0x%p\n", pNetFlt));

    return Status;
}

static VOID vboxNetFltWinPtUnbindAdapter(OUT PNDIS_STATUS pStatus,
        IN NDIS_HANDLE hContext,
        IN NDIS_HANDLE hUnbindContext)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hContext;
    RT_NOREF1(hUnbindContext);

    LogFlowFunc(("ENTER: pNetFlt (0x%p)\n", pNetFlt));

    *pStatus = vboxNetFltWinDetachFromInterface(pNetFlt, true);
    Assert(*pStatus == NDIS_STATUS_SUCCESS);

    LogFlowFunc(("LEAVE: pNetFlt (0x%p)\n", pNetFlt));
}

static VOID vboxNetFltWinPtUnloadProtocol()
{
    LogFlowFuncEnter();
    NDIS_STATUS Status = vboxNetFltWinPtDeregister(&g_VBoxNetFltGlobalsWin.Pt);
    Assert(Status == NDIS_STATUS_SUCCESS); NOREF(Status);
    LogFlowFunc(("LEAVE: PtDeregister Status (0x%x)\n", Status));
}


static VOID vboxNetFltWinPtCloseAdapterComplete(IN NDIS_HANDLE ProtocolBindingContext, IN NDIS_STATUS Status)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)ProtocolBindingContext;

    LogFlowFunc(("ENTER: pNetFlt (0x%p), Status (0x%x)\n", pNetFlt, Status));
    Assert(pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS);
    Assert(Status == NDIS_STATUS_SUCCESS);
    Assert(pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS);
    if (pNetFlt->u.s.WinIf.OpenCloseStatus == NDIS_STATUS_SUCCESS)
    {
        pNetFlt->u.s.WinIf.OpenCloseStatus = Status;
    }
    NdisSetEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent);
    LogFlowFunc(("LEAVE: pNetFlt (0x%p), Status (0x%x)\n", pNetFlt, Status));
}

static VOID vboxNetFltWinPtResetComplete(IN NDIS_HANDLE hProtocolBindingContext, IN NDIS_STATUS Status)
{
    RT_NOREF2(hProtocolBindingContext, Status);
    LogFlowFunc(("ENTER: pNetFlt 0x%p, Status 0x%x\n", hProtocolBindingContext, Status));
    /*
     * should never be here
     */
    AssertFailed();
    LogFlowFunc(("LEAVE: pNetFlt 0x%p, Status 0x%x\n", hProtocolBindingContext, Status));
}

static NDIS_STATUS vboxNetFltWinPtHandleQueryInfoComplete(PVBOXNETFLTINS pNetFlt, NDIS_STATUS Status)
{
    PNDIS_REQUEST pRequest = &pNetFlt->u.s.WinIf.PassDownRequest;

    switch (pRequest->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_PNP_CAPABILITIES:
        {
            if (Status == NDIS_STATUS_SUCCESS)
            {
                if (pRequest->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof (NDIS_PNP_CAPABILITIES))
                {
                    PNDIS_PNP_CAPABILITIES pPnPCaps = (PNDIS_PNP_CAPABILITIES)(pRequest->DATA.QUERY_INFORMATION.InformationBuffer);
                    PNDIS_PM_WAKE_UP_CAPABILITIES pPmWuCaps = &pPnPCaps->WakeUpCapabilities;
                    pPmWuCaps->MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
                    pPmWuCaps->MinPatternWakeUp = NdisDeviceStateUnspecified;
                    pPmWuCaps->MinLinkChangeWakeUp = NdisDeviceStateUnspecified;
                    *pNetFlt->u.s.WinIf.pcPDRBytesRW = sizeof (NDIS_PNP_CAPABILITIES);
                    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = 0;
                    Status = NDIS_STATUS_SUCCESS;
                }
                else
                {
                    AssertFailed();
                    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof(NDIS_PNP_CAPABILITIES);
                    Status = NDIS_STATUS_RESOURCES;
                }
            }
            break;
        }

        case OID_GEN_MAC_OPTIONS:
        {
            if (Status == NDIS_STATUS_SUCCESS)
            {
                if (pRequest->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                {
                    pNetFlt->u.s.WinIf.fMacOptions = *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer;
#ifndef VBOX_LOOPBACK_USEFLAGS
                    /* clearing this flag tells ndis we'll handle loopback ourselves
                     * the ndis layer or nic driver below us would loopback packets as necessary */
                    *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer &= ~NDIS_MAC_OPTION_NO_LOOPBACK;
#else
                    /* we have to catch loopbacks from the underlying driver, so no duplications will occur,
                     * just indicate NDIS to handle loopbacks for the packets coming from the protocol */
                    *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer |= NDIS_MAC_OPTION_NO_LOOPBACK;
#endif
                }
                else
                {
                    AssertFailed();
                    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                    Status = NDIS_STATUS_RESOURCES;
                }
            }
            break;
        }

        case OID_GEN_CURRENT_PACKET_FILTER:
        {
            if (VBOXNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt))
            {
                /* we're here _ONLY_ in the passthru mode */
                Assert(pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter && !pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt);
                if (pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter && !pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt)
                {
                    Assert(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE);
                    vboxNetFltWinDereferenceModePassThru(pNetFlt);
                    vboxNetFltWinDereferenceWinIf(pNetFlt);
                }

                if (Status == NDIS_STATUS_SUCCESS)
                {
                    if (pRequest->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                    {
                        /* the filter request is issued below only in case netflt is not active,
                         * simply update the cache here */
                        /* cache the filter used by upper protocols */
                        pNetFlt->u.s.WinIf.fUpperProtocolSetFilter = *(PULONG)pRequest->DATA.QUERY_INFORMATION.InformationBuffer;
                        pNetFlt->u.s.WinIf.StateFlags.fUpperProtSetFilterInitialized = TRUE;
                    }
                    else
                    {
                        AssertFailed();
                        *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                        Status = NDIS_STATUS_RESOURCES;
                    }
                }
            }
            break;
        }

        default:
            Assert(pRequest->DATA.QUERY_INFORMATION.Oid != OID_PNP_QUERY_POWER);
            break;
    }

    *pNetFlt->u.s.WinIf.pcPDRBytesRW = pRequest->DATA.QUERY_INFORMATION.BytesWritten;
    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = pRequest->DATA.QUERY_INFORMATION.BytesNeeded;

    return Status;
}

static NDIS_STATUS vboxNetFltWinPtHandleSetInfoComplete(PVBOXNETFLTINS pNetFlt, NDIS_STATUS Status)
{
    PNDIS_REQUEST pRequest = &pNetFlt->u.s.WinIf.PassDownRequest;

    switch (pRequest->DATA.SET_INFORMATION.Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
        {
            if (VBOXNETFLT_PROMISCUOUS_SUPPORTED(pNetFlt))
            {
                Assert(Status == NDIS_STATUS_SUCCESS);
                if (pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter)
                {
                    if (pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt)
                    {
                        Assert(pNetFlt->enmTrunkState == INTNETTRUNKIFSTATE_ACTIVE);
                        pNetFlt->u.s.WinIf.StateFlags.fPPFNetFlt = 0;
                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            if (pRequest->DATA.SET_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                            {
                                pNetFlt->u.s.WinIf.fOurSetFilter = *((PULONG)pRequest->DATA.SET_INFORMATION.InformationBuffer);
                                Assert(pNetFlt->u.s.WinIf.fOurSetFilter == NDIS_PACKET_TYPE_PROMISCUOUS);
                            }
                            else
                            {
                                AssertFailed();
                                *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                                Status = NDIS_STATUS_RESOURCES;
                            }
                        }
                        vboxNetFltWinDereferenceNetFlt(pNetFlt);
                    }
                    else
                    {
                        Assert(pNetFlt->enmTrunkState != INTNETTRUNKIFSTATE_ACTIVE);

                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            if (pRequest->DATA.SET_INFORMATION.InformationBufferLength >= sizeof (ULONG))
                            {
                                /* the request was issued when the netflt was not active, simply update the cache here */
                                pNetFlt->u.s.WinIf.fUpperProtocolSetFilter = *((PULONG)pRequest->DATA.SET_INFORMATION.InformationBuffer);
                                pNetFlt->u.s.WinIf.StateFlags.fUpperProtSetFilterInitialized = TRUE;
                            }
                            else
                            {
                                AssertFailed();
                                *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = sizeof (ULONG);
                                Status = NDIS_STATUS_RESOURCES;
                            }
                        }
                        vboxNetFltWinDereferenceModePassThru(pNetFlt);
                    }

                    pNetFlt->u.s.WinIf.StateFlags.fProcessingPacketFilter = 0;
                    vboxNetFltWinDereferenceWinIf(pNetFlt);
                }
#ifdef DEBUG_misha
                else
                {
                    AssertFailed();
                }
#endif
            }
            break;
        }

        default:
            Assert(pRequest->DATA.SET_INFORMATION.Oid != OID_PNP_SET_POWER);
            break;
    }

    *pNetFlt->u.s.WinIf.pcPDRBytesRW = pRequest->DATA.SET_INFORMATION.BytesRead;
    *pNetFlt->u.s.WinIf.pcPDRBytesNeeded = pRequest->DATA.SET_INFORMATION.BytesNeeded;

    return Status;
}

DECLHIDDEN(VOID) vboxNetFltWinPtRequestComplete(NDIS_HANDLE hContext, PNDIS_REQUEST pNdisRequest, NDIS_STATUS Status)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hContext;
    PNDIS_REQUEST pSynchRequest = pNetFlt->u.s.WinIf.pSynchRequest;

    LogFlowFunc(("ENTER: pNetFlt (0x%p), pNdisRequest (0x%p), Status (0x%x)\n", pNetFlt, pNdisRequest, Status));

    if (pSynchRequest == pNdisRequest)
    {
        /* asynchronous completion of our sync request */
        /*1.set the status */
        pNetFlt->u.s.WinIf.SynchCompletionStatus = Status;
        /* 2. set event */
        KeSetEvent(&pNetFlt->u.s.WinIf.hSynchCompletionEvent, 0, FALSE);
        /* 3. return; */

        LogFlowFunc(("LEAVE: pNetFlt (0x%p), pNdisRequest (0x%p), Status (0x%x)\n", pNetFlt, pNdisRequest, Status));
        return;
    }

    Assert(&pNetFlt->u.s.WinIf.PassDownRequest == pNdisRequest);
    Assert(pNetFlt->u.s.WinIf.StateFlags.fRequestInfo == VBOXNDISREQUEST_INPROGRESS);
    vboxNetFltWinMpRequestStateComplete(pNetFlt);

    switch (pNdisRequest->RequestType)
    {
      case NdisRequestQueryInformation:
          Status = vboxNetFltWinPtHandleQueryInfoComplete(pNetFlt, Status);
          NdisMQueryInformationComplete(pNetFlt->u.s.WinIf.hMiniport, Status);
          break;

      case NdisRequestSetInformation:
          Status = vboxNetFltWinPtHandleSetInfoComplete(pNetFlt, Status);
          NdisMSetInformationComplete(pNetFlt->u.s.WinIf.hMiniport, Status);
          break;

      default:
          AssertFailed();
          break;
    }

    LogFlowFunc(("LEAVE: pNetFlt (0x%p), pNdisRequest (0x%p), Status (0x%x)\n", pNetFlt, pNdisRequest, Status));
}

static VOID vboxNetFltWinPtStatus(IN NDIS_HANDLE hProtocolBindingContext, IN NDIS_STATUS GeneralStatus, IN PVOID pvStatusBuffer, IN UINT cbStatusBuffer)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;

    LogFlowFunc(("ENTER: pNetFlt (0x%p), GeneralStatus (0x%x)\n", pNetFlt, GeneralStatus));

    if (vboxNetFltWinReferenceWinIf(pNetFlt))
    {
        Assert(pNetFlt->u.s.WinIf.hMiniport);

        if (VBOXNETFLT_PT_STATUS_IS_FILTERED(GeneralStatus))
        {
            pNetFlt->u.s.WinIf.MpIndicatedMediaStatus = GeneralStatus;
        }
        NdisMIndicateStatus(pNetFlt->u.s.WinIf.hMiniport,
                            GeneralStatus,
                            pvStatusBuffer,
                            cbStatusBuffer);

        vboxNetFltWinDereferenceWinIf(pNetFlt);
    }
    else
    {
        if (pNetFlt->u.s.WinIf.hMiniport != NULL
                && VBOXNETFLT_PT_STATUS_IS_FILTERED(GeneralStatus)
           )
        {
            pNetFlt->u.s.WinIf.MpUnindicatedMediaStatus = GeneralStatus;
        }
    }

    LogFlowFunc(("LEAVE: pNetFlt (0x%p), GeneralStatus (0x%x)\n", pNetFlt, GeneralStatus));
}


static VOID vboxNetFltWinPtStatusComplete(IN NDIS_HANDLE hProtocolBindingContext)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;

    LogFlowFunc(("ENTER: pNetFlt (0x%p)\n", pNetFlt));

    if (vboxNetFltWinReferenceWinIf(pNetFlt))
    {
        NdisMIndicateStatusComplete(pNetFlt->u.s.WinIf.hMiniport);

        vboxNetFltWinDereferenceWinIf(pNetFlt);
    }

    LogFlowFunc(("LEAVE: pNetFlt (0x%p)\n", pNetFlt));
}

static VOID vboxNetFltWinPtSendComplete(IN NDIS_HANDLE hProtocolBindingContext, IN PNDIS_PACKET pPacket, IN NDIS_STATUS Status)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;
    PVBOXNETFLT_PKTRSVD_PT pSendInfo = (PVBOXNETFLT_PKTRSVD_PT)pPacket->ProtocolReserved;
    PNDIS_PACKET pOrigPacket = pSendInfo->pOrigPacket;
    PVOID pBufToFree = pSendInfo->pBufToFree;
    LogFlowFunc(("ENTER: pNetFlt (0x%p), pPacket (0x%p), Status (0x%x)\n", pNetFlt, pPacket, Status));

#if defined(DEBUG_NETFLT_PACKETS) || !defined(VBOX_LOOPBACK_USEFLAGS)
    /** @todo for optimization we could check only for netflt-mode packets
     * do it for all for now */
     vboxNetFltWinLbRemoveSendPacket(pNetFlt, pPacket);
#endif

     if (pOrigPacket)
     {
         NdisIMCopySendCompletePerPacketInfo(pOrigPacket, pPacket);
         NdisFreePacket(pPacket);
         /* the ptk was posted from the upperlying protocol */
         NdisMSendComplete(pNetFlt->u.s.WinIf.hMiniport, pOrigPacket, Status);
     }
     else
     {
         /* if the pOrigPacket is zero - the ptk was originated by netFlt send/receive
          * need to free packet buffers */
         vboxNetFltWinFreeSGNdisPacket(pPacket, !pBufToFree);
     }

     if (pBufToFree)
     {
         vboxNetFltWinMemFree(pBufToFree);
     }

    vboxNetFltWinDereferenceWinIf(pNetFlt);

    LogFlowFunc(("LEAVE: pNetFlt (0x%p), pPacket (0x%p), Status (0x%x)\n", pNetFlt, pPacket, Status));
}

/**
 * removes searches for the packet in the list and removes it if found
 * @return true if the packet was found and removed, false - otherwise
 */
static bool vboxNetFltWinRemovePacketFromList(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList, PNDIS_PACKET pPacket)
{
    PVBOXNETFLT_PKTRSVD_TRANSFERDATA_PT pTDR = (PVBOXNETFLT_PKTRSVD_TRANSFERDATA_PT)pPacket->ProtocolReserved;
    return vboxNetFltWinInterlockedSearchListEntry(pList, &pTDR->ListEntry, true /* remove*/);
}

/**
 * puts the packet to the tail of the list
 */
static void vboxNetFltWinPutPacketToList(PVBOXNETFLT_INTERLOCKED_SINGLE_LIST pList, PNDIS_PACKET pPacket, PNDIS_BUFFER pOrigBuffer)
{
    PVBOXNETFLT_PKTRSVD_TRANSFERDATA_PT pTDR = (PVBOXNETFLT_PKTRSVD_TRANSFERDATA_PT)pPacket->ProtocolReserved;
    pTDR->pOrigBuffer = pOrigBuffer;
    vboxNetFltWinInterlockedPutTail(pList, &pTDR->ListEntry);
}

static bool vboxNetFltWinPtTransferDataCompleteActive(PVBOXNETFLTINS pNetFltIf, PNDIS_PACKET pPacket, NDIS_STATUS Status)
{
    PNDIS_BUFFER pBuffer;
    PVBOXNETFLT_PKTRSVD_TRANSFERDATA_PT pTDR;

    if (!vboxNetFltWinRemovePacketFromList(&pNetFltIf->u.s.WinIf.TransferDataList, pPacket))
        return false;

    pTDR = (PVBOXNETFLT_PKTRSVD_TRANSFERDATA_PT)pPacket->ProtocolReserved;
    Assert(pTDR);
    Assert(pTDR->pOrigBuffer);

    do
    {
        NdisUnchainBufferAtFront(pPacket, &pBuffer);

        Assert(pBuffer);

        NdisFreeBuffer(pBuffer);

        pBuffer = pTDR->pOrigBuffer;

        NdisChainBufferAtBack(pPacket, pBuffer);

        /* data transfer was initiated when the netFlt was active
         * the netFlt is still retained by us
         * 1. check if loopback
         * 2. enqueue packet
         * 3. release netFlt */

        if (Status == NDIS_STATUS_SUCCESS)
        {

#ifdef VBOX_LOOPBACK_USEFLAGS
            if (vboxNetFltWinIsLoopedBackPacket(pPacket))
            {
                /* should not be here */
                AssertFailed();
            }
#else
            PNDIS_PACKET pLb = vboxNetFltWinLbSearchLoopBack(pNetFltIf, pPacket, false);
            if (pLb)
            {
#ifndef DEBUG_NETFLT_RECV_TRANSFERDATA
                /* should not be here */
                AssertFailed();
#endif
                if (!vboxNetFltWinLbIsFromIntNet(pLb))
                {
                    /* the packet is not from int net, need to pass it up to the host */
                    NdisMIndicateReceivePacket(pNetFltIf->u.s.WinIf.hMiniport, &pPacket, 1);
                    /* dereference NetFlt, WinIf will be dereferenced on Packet return */
                    vboxNetFltWinDereferenceNetFlt(pNetFltIf);
                    break;
                }
            }
#endif
            else
            {
                /* 2. enqueue */
                /* use the same packet info to put the packet in the processing packet queue */
                PVBOXNETFLT_PKTRSVD_MP pRecvInfo = (PVBOXNETFLT_PKTRSVD_MP)pPacket->MiniportReserved;

                VBOXNETFLT_LBVERIFY(pNetFltIf, pPacket);

                pRecvInfo->pOrigPacket = NULL;
                pRecvInfo->pBufToFree = NULL;

                NdisGetPacketFlags(pPacket) = 0;
# ifdef VBOXNETFLT_NO_PACKET_QUEUE
                if (vboxNetFltWinPostIntnet(pNetFltIf, pPacket, 0))
                {
                    /* drop it */
                    vboxNetFltWinFreeSGNdisPacket(pPacket, true);
                    vboxNetFltWinDereferenceWinIf(pNetFltIf);
                }
                else
                {
                    NdisMIndicateReceivePacket(pNetFltIf->u.s.WinIf.hMiniport, &pPacket, 1);
                }
                vboxNetFltWinDereferenceNetFlt(pNetFltIf);
                break;
# else
                Status = vboxNetFltWinQuEnqueuePacket(pNetFltIf, pPacket, PACKET_MINE);
                if (Status == NDIS_STATUS_SUCCESS)
                {
                    break;
                }
                AssertFailed();
# endif
            }
        }
        else
        {
            AssertFailed();
        }
        /* we are here because of error either in data transfer or in enqueueing the packet */
        vboxNetFltWinFreeSGNdisPacket(pPacket, true);
        vboxNetFltWinDereferenceNetFlt(pNetFltIf);
        vboxNetFltWinDereferenceWinIf(pNetFltIf);
    } while (0);

    return true;
}

static VOID vboxNetFltWinPtTransferDataComplete(IN NDIS_HANDLE hProtocolBindingContext,
                    IN PNDIS_PACKET pPacket,
                    IN NDIS_STATUS Status,
                    IN UINT cbTransferred)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;
    LogFlowFunc(("ENTER: pNetFlt (0x%p), pPacket (0x%p), Status (0x%x), cbTransfered (%d)\n", pNetFlt, pPacket, Status, cbTransferred));
    if (!vboxNetFltWinPtTransferDataCompleteActive(pNetFlt, pPacket, Status))
    {
        if (pNetFlt->u.s.WinIf.hMiniport)
        {
            NdisMTransferDataComplete(pNetFlt->u.s.WinIf.hMiniport,
                                      pPacket,
                                      Status,
                                      cbTransferred);
        }

        vboxNetFltWinDereferenceWinIf(pNetFlt);
    }
    /* else - all processing is done with vboxNetFltWinPtTransferDataCompleteActive already */

    LogFlowFunc(("LEAVE: pNetFlt (0x%p), pPacket (0x%p), Status (0x%x), cbTransfered (%d)\n", pNetFlt, pPacket, Status, cbTransferred));
}

static INT vboxNetFltWinRecvPacketPassThru(PVBOXNETFLTINS pNetFlt, PNDIS_PACKET pPacket)
{
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PNDIS_PACKET pMyPacket;
    NDIS_STATUS Status = vboxNetFltWinPrepareRecvPacket(pNetFlt, pPacket, &pMyPacket, true);
    /* the Status holds the current packet status it will be checked for NDIS_STATUS_RESOURCES later
     * (see below) */
    Assert(pMyPacket);
    if (pMyPacket)
    {
        NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);
        if (Status == NDIS_STATUS_RESOURCES)
        {
            NdisDprFreePacket(pMyPacket);
            return 0;
        }

        return 1;
    }

    return 0;
}

/**
 * process the packet receive in a "passthru" mode
 */
static NDIS_STATUS vboxNetFltWinRecvPassThru(PVBOXNETFLTINS pNetFlt, PNDIS_PACKET pPacket)
{
    Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);

    NDIS_STATUS Status;
    PNDIS_PACKET pMyPacket;

    NdisDprAllocatePacket(&Status, &pMyPacket, pNetFlt->u.s.WinIf.hRecvPacketPool);
    Assert(Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        vboxNetFltWinCopyPacketInfoOnRecv(pMyPacket, pPacket, true /* force NDIS_STATUS_RESOURCES */);
        Assert(NDIS_GET_PACKET_STATUS(pMyPacket) == NDIS_STATUS_RESOURCES);

        NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);

        NdisDprFreePacket(pMyPacket);
    }
    return Status;
}

static VOID vboxNetFltWinRecvIndicatePassThru(PVBOXNETFLTINS pNetFlt, NDIS_HANDLE MacReceiveContext,
                PVOID pHeaderBuffer, UINT cbHeaderBuffer, PVOID pLookAheadBuffer, UINT cbLookAheadBuffer, UINT cbPacket)
{
    /* Note: we're using KeGetCurrentProcessorNumber, which is not entirely correct in case
    * we're running on 64bit win7+, which can handle > 64 CPUs, however since KeGetCurrentProcessorNumber
    * always returns the number < than the number of CPUs in the first group, we're guaranteed to have CPU index < 64
    * @todo: use KeGetCurrentProcessorNumberEx for Win7+ 64 and dynamically extended array */
    ULONG Proc = KeGetCurrentProcessorNumber();
    Assert(Proc < RT_ELEMENTS(pNetFlt->u.s.WinIf.abIndicateRxComplete));
    pNetFlt->u.s.WinIf.abIndicateRxComplete[Proc] = TRUE;
    switch (pNetFlt->u.s.WinIf.enmMedium)
    {
        case NdisMedium802_3:
        case NdisMediumWan:
            NdisMEthIndicateReceive(pNetFlt->u.s.WinIf.hMiniport,
                                         MacReceiveContext,
                                         (PCHAR)pHeaderBuffer,
                                         cbHeaderBuffer,
                                         pLookAheadBuffer,
                                         cbLookAheadBuffer,
                                         cbPacket);
            break;
        default:
            AssertFailed();
            break;
    }
}

/**
 * process the ProtocolReceive in an "active" mode
 *
 * @return NDIS_STATUS_SUCCESS - the packet is processed
 * NDIS_STATUS_PENDING - the packet is being processed, we are waiting for the ProtocolTransferDataComplete to be called
 * NDIS_STATUS_NOT_ACCEPTED - the packet is not needed - typically this is because this is a loopback packet
 * NDIS_STATUS_FAILURE - packet processing failed
 */
static NDIS_STATUS vboxNetFltWinPtReceiveActive(PVBOXNETFLTINS pNetFlt, NDIS_HANDLE MacReceiveContext, PVOID pHeaderBuffer, UINT cbHeaderBuffer,
                        PVOID pLookaheadBuffer, UINT cbLookaheadBuffer, UINT cbPacket)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    do
    {
        if (cbHeaderBuffer != VBOXNETFLT_PACKET_ETHEADER_SIZE)
        {
            Status = NDIS_STATUS_NOT_ACCEPTED;
            break;
        }

#ifndef DEBUG_NETFLT_RECV_TRANSFERDATA
        if (cbPacket == cbLookaheadBuffer)
        {
            PINTNETSG pSG;
            PUCHAR pRcvData;
#ifndef VBOX_LOOPBACK_USEFLAGS
            PNDIS_PACKET pLb;
#endif

            /* allocate SG buffer */
            Status = vboxNetFltWinAllocSG(cbPacket + cbHeaderBuffer, &pSG);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                break;
            }

            pRcvData = (PUCHAR)pSG->aSegs[0].pv;

            NdisMoveMappedMemory(pRcvData, pHeaderBuffer, cbHeaderBuffer);

            NdisCopyLookaheadData(pRcvData+cbHeaderBuffer,
                                                  pLookaheadBuffer,
                                                  cbLookaheadBuffer,
                                                  pNetFlt->u.s.WinIf.fMacOptions);
#ifndef VBOX_LOOPBACK_USEFLAGS
            pLb = vboxNetFltWinLbSearchLoopBackBySG(pNetFlt, pSG, false);
            if (pLb)
            {
#ifndef DEBUG_NETFLT_RECV_NOPACKET
                /* should not be here */
                AssertFailed();
#endif
                if (!vboxNetFltWinLbIsFromIntNet(pLb))
                {
                    PNDIS_PACKET pMyPacket;
                    pMyPacket = vboxNetFltWinNdisPacketFromSG(pNetFlt, /* PVBOXNETFLTINS */
                        pSG, /* PINTNETSG */
                        pSG, /* PVOID pBufToFree */
                        false, /* bool bToWire */
                        false); /* bool bCopyMemory */
                    if (pMyPacket)
                    {
                        NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);
                        /* dereference the NetFlt here & indicate SUCCESS, which would mean the caller would not do a dereference
                         * the WinIf dereference will be done on packet return */
                        vboxNetFltWinDereferenceNetFlt(pNetFlt);
                        Status = NDIS_STATUS_SUCCESS;
                    }
                    else
                    {
                        vboxNetFltWinMemFree(pSG);
                        Status = NDIS_STATUS_FAILURE;
                    }
                }
                else
                {
                    vboxNetFltWinMemFree(pSG);
                    Status = NDIS_STATUS_NOT_ACCEPTED;
                }
                break;
            }
#endif
            VBOXNETFLT_LBVERIFYSG(pNetFlt, pSG);

                /* enqueue SG */
# ifdef VBOXNETFLT_NO_PACKET_QUEUE
            if (vboxNetFltWinPostIntnet(pNetFlt, pSG, VBOXNETFLT_PACKET_SG))
            {
                /* drop it */
                vboxNetFltWinMemFree(pSG);
                vboxNetFltWinDereferenceWinIf(pNetFlt);
            }
            else
            {
                PNDIS_PACKET pMyPacket = vboxNetFltWinNdisPacketFromSG(pNetFlt, /* PVBOXNETFLTINS */
                        pSG, /* PINTNETSG */
                        pSG, /* PVOID pBufToFree */
                        false, /* bool bToWire */
                        false); /* bool bCopyMemory */
                Assert(pMyPacket);
                if (pMyPacket)
                {
                    NDIS_SET_PACKET_STATUS(pMyPacket, NDIS_STATUS_SUCCESS);

                    DBG_CHECK_PACKET_AND_SG(pMyPacket, pSG);

                    LogFlow(("non-ndis packet info, packet created (%p)\n", pMyPacket));
                    NdisMIndicateReceivePacket(pNetFlt->u.s.WinIf.hMiniport, &pMyPacket, 1);
                }
                else
                {
                    vboxNetFltWinDereferenceWinIf(pNetFlt);
                    Status = NDIS_STATUS_RESOURCES;
                }
            }
            vboxNetFltWinDereferenceNetFlt(pNetFlt);
# else
            Status = vboxNetFltWinQuEnqueuePacket(pNetFlt, pSG, PACKET_SG | PACKET_MINE);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                vboxNetFltWinMemFree(pSG);
                break;
            }
# endif
#endif
        }
        else
        {
            PNDIS_PACKET pPacket;
            PNDIS_BUFFER pTransferBuffer;
            PNDIS_BUFFER pOrigBuffer;
            PUCHAR pMemBuf;
            UINT cbBuf = cbPacket + cbHeaderBuffer;
            UINT cbTransferred;

            /* allocate NDIS Packet buffer */
            NdisAllocatePacket(&Status, &pPacket, pNetFlt->u.s.WinIf.hRecvPacketPool);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                break;
            }

            VBOXNETFLT_OOB_INIT(pPacket);

#ifdef VBOX_LOOPBACK_USEFLAGS
            /* set "don't loopback" flags */
            NdisGetPacketFlags(pPacket) = g_VBoxNetFltGlobalsWin.fPacketDontLoopBack;
#else
            NdisGetPacketFlags(pPacket) =  0;
#endif

            Status = vboxNetFltWinMemAlloc((PVOID*)(&pMemBuf), cbBuf);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                NdisFreePacket(pPacket);
                break;
            }
            NdisAllocateBuffer(&Status, &pTransferBuffer, pNetFlt->u.s.WinIf.hRecvBufferPool, pMemBuf + cbHeaderBuffer, cbPacket);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                Status = NDIS_STATUS_FAILURE;
                NdisFreePacket(pPacket);
                vboxNetFltWinMemFree(pMemBuf);
                break;
            }

            NdisAllocateBuffer(&Status, &pOrigBuffer, pNetFlt->u.s.WinIf.hRecvBufferPool, pMemBuf, cbBuf);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                AssertFailed();
                Status = NDIS_STATUS_FAILURE;
                NdisFreeBuffer(pTransferBuffer);
                NdisFreePacket(pPacket);
                vboxNetFltWinMemFree(pMemBuf);
                break;
            }

            NdisChainBufferAtBack(pPacket, pTransferBuffer);

            NdisMoveMappedMemory(pMemBuf, pHeaderBuffer, cbHeaderBuffer);

            vboxNetFltWinPutPacketToList(&pNetFlt->u.s.WinIf.TransferDataList, pPacket, pOrigBuffer);

#ifdef DEBUG_NETFLT_RECV_TRANSFERDATA
            if (cbPacket == cbLookaheadBuffer)
            {
                NdisCopyLookaheadData(pMemBuf+cbHeaderBuffer,
                                                  pLookaheadBuffer,
                                                  cbLookaheadBuffer,
                                                  pNetFlt->u.s.WinIf.fMacOptions);
            }
            else
#endif
            {
                Assert(cbPacket > cbLookaheadBuffer);

                NdisTransferData(&Status, pNetFlt->u.s.WinIf.hBinding, MacReceiveContext,
                        0,  /* ByteOffset */
                        cbPacket, pPacket, &cbTransferred);
            }

            if (Status != NDIS_STATUS_PENDING)
            {
                vboxNetFltWinPtTransferDataComplete(pNetFlt, pPacket, Status, cbTransferred);
            }
        }
    } while (0);

    return Status;
}

static NDIS_STATUS vboxNetFltWinPtReceive(IN NDIS_HANDLE hProtocolBindingContext,
                        IN NDIS_HANDLE MacReceiveContext,
                        IN PVOID pHeaderBuffer,
                        IN UINT cbHeaderBuffer,
                        IN PVOID pLookAheadBuffer,
                        IN UINT cbLookAheadBuffer,
                        IN UINT cbPacket)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;
    PNDIS_PACKET pPacket = NULL;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    bool bNetFltActive;
    bool fWinIfActive = vboxNetFltWinReferenceWinIfNetFlt(pNetFlt, &bNetFltActive);
    const bool bPassThruActive = !bNetFltActive;

    LogFlowFunc(("ENTER: pNetFlt (0x%p)\n", pNetFlt));

    if (fWinIfActive)
    {
        do
        {
#ifndef DEBUG_NETFLT_RECV_NOPACKET
            pPacket = NdisGetReceivedPacket(pNetFlt->u.s.WinIf.hBinding, MacReceiveContext);
            if (pPacket)
            {
# ifndef VBOX_LOOPBACK_USEFLAGS
                PNDIS_PACKET pLb = NULL;
# else
                if (vboxNetFltWinIsLoopedBackPacket(pPacket))
                {
                    AssertFailed();
                    /* nothing else to do here, just return the packet */
                    //NdisReturnPackets(&pPacket, 1);
                    Status = NDIS_STATUS_NOT_ACCEPTED;
                    break;
                }

                VBOXNETFLT_LBVERIFY(pNetFlt, pPacket);
# endif

                if (bNetFltActive)
                {
# ifndef VBOX_LOOPBACK_USEFLAGS
                    pLb = vboxNetFltWinLbSearchLoopBack(pNetFlt, pPacket, false);
                    if (!pLb)
# endif
                    {
                        VBOXNETFLT_LBVERIFY(pNetFlt, pPacket);

# ifdef VBOXNETFLT_NO_PACKET_QUEUE
                        if (vboxNetFltWinPostIntnet(pNetFlt, pPacket, 0))
                        {
                            /* drop it */
                            break;
                        }
# else
                        Status = vboxNetFltWinQuEnqueuePacket(pNetFlt, pPacket, PACKET_COPY);
                        Assert(Status == NDIS_STATUS_SUCCESS);
                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            //NdisReturnPackets(&pPacket, 1);
                            fWinIfActive = false;
                            bNetFltActive = false;
                            break;
                        }
# endif
                    }
# ifndef VBOX_LOOPBACK_USEFLAGS
                    else if (vboxNetFltWinLbIsFromIntNet(pLb))
                    {
                        /* nothing else to do here, just return the packet */
                        //NdisReturnPackets(&pPacket, 1);
                        Status = NDIS_STATUS_NOT_ACCEPTED;
                        break;
                    }
                    /* we are here because this is a looped back packet set not from intnet
                     * we will post it to the upper protocol */
# endif
                }

                Assert(Status == STATUS_SUCCESS);
                if (Status == STATUS_SUCCESS)
                {
# ifndef VBOX_LOOPBACK_USEFLAGS
                    Assert(!pLb || !vboxNetFltWinLbIsFromIntNet(pLb));
# endif
                    Status = vboxNetFltWinRecvPassThru(pNetFlt, pPacket);
                    Assert(Status == STATUS_SUCCESS);
                    /* we are done with packet processing, and we will
                     * not receive packet return event for this packet,
                     * fWinIfActive should be true to ensure we release WinIf*/
                    Assert(fWinIfActive);
                    if (Status == STATUS_SUCCESS)
                        break;
                }
                else
                {
                    /* intnet processing failed - fall back to no-packet mode */
                    Assert(bNetFltActive);
                    Assert(fWinIfActive);
                }

            }
#endif /* #ifndef DEBUG_NETFLT_RECV_NOPACKET */

            if (bNetFltActive)
            {
                Status = vboxNetFltWinPtReceiveActive(pNetFlt, MacReceiveContext, pHeaderBuffer, cbHeaderBuffer,
                        pLookAheadBuffer, cbLookAheadBuffer, cbPacket);
                if (NT_SUCCESS(Status))
                {
                    if (Status != NDIS_STATUS_NOT_ACCEPTED)
                    {
                        fWinIfActive = false;
                        bNetFltActive = false;
                    }
                    else
                    {
#ifndef VBOX_LOOPBACK_USEFLAGS
                        /* this is a loopback packet, nothing to do here */
#else
                        AssertFailed();
                        /* should not be here */
#endif
                    }
                    break;
                }
            }

            /* we are done with packet processing, and we will
             * not receive packet return event for this packet,
             * fWinIfActive should be true to ensure we release WinIf*/
            Assert(fWinIfActive);

            vboxNetFltWinRecvIndicatePassThru(pNetFlt, MacReceiveContext, pHeaderBuffer, cbHeaderBuffer, pLookAheadBuffer, cbLookAheadBuffer, cbPacket);
            /* the status could contain an error value here in case the IntNet recv failed,
             * ensure we return back success status */
            Status = NDIS_STATUS_SUCCESS;

        } while (0);

        if (bNetFltActive)
        {
            vboxNetFltWinDereferenceNetFlt(pNetFlt);
        }
        else if (bPassThruActive)
        {
            vboxNetFltWinDereferenceModePassThru(pNetFlt);
        }
        if (fWinIfActive)
        {
            vboxNetFltWinDereferenceWinIf(pNetFlt);
        }
    }
    else
    {
        Status = NDIS_STATUS_FAILURE;
    }

    LogFlowFunc(("LEAVE: pNetFlt (0x%p)\n", pNetFlt));

    return Status;

}

static VOID vboxNetFltWinPtReceiveComplete(NDIS_HANDLE hProtocolBindingContext)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;
    bool fNetFltActive;
    bool fWinIfActive = vboxNetFltWinReferenceWinIfNetFlt(pNetFlt, &fNetFltActive);
    NDIS_HANDLE hMiniport = pNetFlt->u.s.WinIf.hMiniport;
    /* Note: we're using KeGetCurrentProcessorNumber, which is not entirely correct in case
    * we're running on 64bit win7+, which can handle > 64 CPUs, however since KeGetCurrentProcessorNumber
    * always returns the number < than the number of CPUs in the first group, we're guaranteed to have CPU index < 64
    * @todo: use KeGetCurrentProcessorNumberEx for Win7+ 64 and dynamically extended array */
    ULONG iProc = KeGetCurrentProcessorNumber();
    Assert(iProc < RT_ELEMENTS(pNetFlt->u.s.WinIf.abIndicateRxComplete));

    LogFlowFunc(("ENTER: pNetFlt (0x%p)\n", pNetFlt));

    if (hMiniport != NULL && pNetFlt->u.s.WinIf.abIndicateRxComplete[iProc])
    {
        switch (pNetFlt->u.s.WinIf.enmMedium)
        {
            case NdisMedium802_3:
            case NdisMediumWan:
                NdisMEthIndicateReceiveComplete(hMiniport);
                break;
            default:
                AssertFailed();
                break;
        }
    }

    pNetFlt->u.s.WinIf.abIndicateRxComplete[iProc] = FALSE;

    if (fWinIfActive)
    {
        if (fNetFltActive)
            vboxNetFltWinDereferenceNetFlt(pNetFlt);
        else
            vboxNetFltWinDereferenceModePassThru(pNetFlt);
        vboxNetFltWinDereferenceWinIf(pNetFlt);
    }

    LogFlowFunc(("LEAVE: pNetFlt (0x%p)\n", pNetFlt));
}

static INT vboxNetFltWinPtReceivePacket(NDIS_HANDLE hProtocolBindingContext, PNDIS_PACKET pPacket)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;
    INT cRefCount = 0;
    bool bNetFltActive;
    bool fWinIfActive = vboxNetFltWinReferenceWinIfNetFlt(pNetFlt, &bNetFltActive);
    const bool bPassThruActive = !bNetFltActive;

    LogFlowFunc(("ENTER: pNetFlt (0x%p)\n", pNetFlt));

    if (fWinIfActive)
    {
        do
        {
#ifdef VBOX_LOOPBACK_USEFLAGS
            if (vboxNetFltWinIsLoopedBackPacket(pPacket))
            {
                AssertFailed();
                Log(("lb_rp"));

                /* nothing else to do here, just return the packet */
                cRefCount = 0;
                //NdisReturnPackets(&pPacket, 1);
                break;
            }

            VBOXNETFLT_LBVERIFY(pNetFlt, pPacket);
#endif

            if (bNetFltActive)
            {
#ifndef VBOX_LOOPBACK_USEFLAGS
                PNDIS_PACKET pLb = vboxNetFltWinLbSearchLoopBack(pNetFlt, pPacket, false);
                if (!pLb)
#endif
                {
#ifndef VBOXNETFLT_NO_PACKET_QUEUE
                    NDIS_STATUS fStatus;
#endif
                    bool fResources = NDIS_GET_PACKET_STATUS(pPacket) == NDIS_STATUS_RESOURCES; NOREF(fResources);

                    VBOXNETFLT_LBVERIFY(pNetFlt, pPacket);
#ifdef DEBUG_misha
                    /** @todo remove this assert.
                     * this is a temporary assert for debugging purposes:
                     * we're probably doing something wrong with the packets if the miniport reports NDIS_STATUS_RESOURCES */
                    Assert(!fResources);
#endif

#ifdef VBOXNETFLT_NO_PACKET_QUEUE
                    if (vboxNetFltWinPostIntnet(pNetFlt, pPacket, 0))
                    {
                        /* drop it */
                        cRefCount = 0;
                        break;
                    }

#else
                    fStatus = vboxNetFltWinQuEnqueuePacket(pNetFlt, pPacket, fResources ? PACKET_COPY : 0);
                    if (fStatus == NDIS_STATUS_SUCCESS)
                    {
                        bNetFltActive = false;
                        fWinIfActive = false;
                        if (fResources)
                        {
                            cRefCount = 0;
                            //NdisReturnPackets(&pPacket, 1);
                        }
                        else
                            cRefCount = 1;
                        break;
                    }
                    else
                    {
                        AssertFailed();
                    }
#endif
                }
#ifndef VBOX_LOOPBACK_USEFLAGS
                else if (vboxNetFltWinLbIsFromIntNet(pLb))
                {
                    /* the packet is from intnet, it has already been set to the host,
                     * no need for loopng it back to the host again */
                    /* nothing else to do here, just return the packet */
                    cRefCount = 0;
                    //NdisReturnPackets(&pPacket, 1);
                    break;
                }
#endif
            }

            cRefCount = vboxNetFltWinRecvPacketPassThru(pNetFlt, pPacket);
            if (cRefCount)
            {
                Assert(cRefCount == 1);
                fWinIfActive = false;
            }

        } while (FALSE);

        if (bNetFltActive)
        {
            vboxNetFltWinDereferenceNetFlt(pNetFlt);
        }
        else if (bPassThruActive)
        {
            vboxNetFltWinDereferenceModePassThru(pNetFlt);
        }
        if (fWinIfActive)
        {
            vboxNetFltWinDereferenceWinIf(pNetFlt);
        }
    }
    else
    {
        cRefCount = 0;
        //NdisReturnPackets(&pPacket, 1);
    }

    LogFlowFunc(("LEAVE: pNetFlt (0x%p), cRefCount (%d)\n", pNetFlt, cRefCount));

    return cRefCount;
}

DECLHIDDEN(bool) vboxNetFltWinPtCloseInterface(PVBOXNETFLTINS pNetFlt, PNDIS_STATUS pStatus)
{
    RTSpinlockAcquire(pNetFlt->hSpinlock);

    if (pNetFlt->u.s.WinIf.StateFlags.fInterfaceClosing)
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        AssertFailed();
        return false;
    }
    if (pNetFlt->u.s.WinIf.hBinding == NULL)
    {
        RTSpinlockRelease(pNetFlt->hSpinlock);
        AssertFailed();
        return false;
    }

    pNetFlt->u.s.WinIf.StateFlags.fInterfaceClosing = TRUE;
    RTSpinlockRelease(pNetFlt->hSpinlock);

    NdisResetEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent);
    NdisCloseAdapter(pStatus, pNetFlt->u.s.WinIf.hBinding);
    if (*pStatus == NDIS_STATUS_PENDING)
    {
        NdisWaitEvent(&pNetFlt->u.s.WinIf.OpenCloseEvent, 0);
        *pStatus = pNetFlt->u.s.WinIf.OpenCloseStatus;
    }

    Assert (*pStatus == NDIS_STATUS_SUCCESS);

    pNetFlt->u.s.WinIf.hBinding = NULL;

    return true;
}

static NDIS_STATUS vboxNetFltWinPtPnPSetPower(PVBOXNETFLTINS pNetFlt, NDIS_DEVICE_POWER_STATE enmPowerState)
{
    NDIS_DEVICE_POWER_STATE enmPrevPowerState = vboxNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.PtState);

    RTSpinlockAcquire(pNetFlt->hSpinlock);

    vboxNetFltWinSetPowerState(&pNetFlt->u.s.WinIf.PtState, enmPowerState);

    if (vboxNetFltWinGetPowerState(&pNetFlt->u.s.WinIf.PtState) > NdisDeviceStateD0)
    {
        if (enmPrevPowerState == NdisDeviceStateD0)
        {
            pNetFlt->u.s.WinIf.StateFlags.fStandBy = TRUE;
        }
        RTSpinlockRelease(pNetFlt->hSpinlock);
        vboxNetFltWinPtRequestsWaitComplete(pNetFlt);
        vboxNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.MpState);
        vboxNetFltWinWaitDereference(&pNetFlt->u.s.WinIf.PtState);

        /* check packet pool is empty */
        UINT cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hSendPacketPool);
        Assert(cPPUsage == 0);
        cPPUsage = NdisPacketPoolUsage(pNetFlt->u.s.WinIf.hRecvPacketPool);
        Assert(cPPUsage == 0);
        /* for debugging only, ignore the err in release */
        NOREF(cPPUsage);

        Assert(!pNetFlt->u.s.WinIf.StateFlags.fRequestInfo);
    }
    else
    {
        if (enmPrevPowerState > NdisDeviceStateD0)
        {
            pNetFlt->u.s.WinIf.StateFlags.fStandBy = FALSE;
        }

        if (pNetFlt->u.s.WinIf.StateFlags.fRequestInfo & VBOXNDISREQUEST_QUEUED)
        {
            pNetFlt->u.s.WinIf.StateFlags.fRequestInfo = VBOXNDISREQUEST_INPROGRESS;
            RTSpinlockRelease(pNetFlt->hSpinlock);

            vboxNetFltWinMpRequestPost(pNetFlt);
        }
        else
        {
            RTSpinlockRelease(pNetFlt->hSpinlock);
        }
    }

    return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS vboxNetFltWinPtPnPEvent(IN NDIS_HANDLE hProtocolBindingContext, IN PNET_PNP_EVENT pNetPnPEvent)
{
    PVBOXNETFLTINS pNetFlt = (PVBOXNETFLTINS)hProtocolBindingContext;

    LogFlowFunc(("ENTER: pNetFlt (0x%p), NetEvent (%d)\n", pNetFlt, pNetPnPEvent->NetEvent));

    switch (pNetPnPEvent->NetEvent)
    {
        case NetEventSetPower:
        {
            NDIS_DEVICE_POWER_STATE enmPowerState = *((PNDIS_DEVICE_POWER_STATE)pNetPnPEvent->Buffer);
            NDIS_STATUS rcNdis = vboxNetFltWinPtPnPSetPower(pNetFlt, enmPowerState);
            LogFlowFunc(("LEAVE: pNetFlt (0x%p), NetEvent (%d), rcNdis=%#x\n", pNetFlt, pNetPnPEvent->NetEvent, rcNdis));
            return rcNdis;
        }

        case NetEventReconfigure:
        {
            if (!pNetFlt)
            {
                NdisReEnumerateProtocolBindings(g_VBoxNetFltGlobalsWin.Pt.hProtocol);
            }
        }
        /** @todo r=bird: Is the fall thru intentional?? */
        default:
            LogFlowFunc(("LEAVE: pNetFlt (0x%p), NetEvent (%d)\n", pNetFlt, pNetPnPEvent->NetEvent));
            return NDIS_STATUS_SUCCESS;
    }

}

#ifdef __cplusplus
# define PTCHARS_40(_p) ((_p).Ndis40Chars)
#else
# define PTCHARS_40(_p) (_p)
#endif

/**
 * register the protocol edge
 */
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtRegister(PVBOXNETFLTGLOBALS_PT pGlobalsPt, PDRIVER_OBJECT pDriverObject,
                                                PUNICODE_STRING pRegistryPathStr)
{
    RT_NOREF2(pDriverObject, pRegistryPathStr);
    NDIS_PROTOCOL_CHARACTERISTICS PtChars;
    NDIS_STRING NameStr;

    NdisInitUnicodeString(&NameStr, VBOXNETFLT_NAME_PROTOCOL);

    NdisZeroMemory(&PtChars, sizeof (PtChars));
    PTCHARS_40(PtChars).MajorNdisVersion = VBOXNETFLT_VERSION_PT_NDIS_MAJOR;
    PTCHARS_40(PtChars).MinorNdisVersion = VBOXNETFLT_VERSION_PT_NDIS_MINOR;

    PTCHARS_40(PtChars).Name = NameStr;
    PTCHARS_40(PtChars).OpenAdapterCompleteHandler = vboxNetFltWinPtOpenAdapterComplete;
    PTCHARS_40(PtChars).CloseAdapterCompleteHandler = vboxNetFltWinPtCloseAdapterComplete;
    PTCHARS_40(PtChars).SendCompleteHandler = vboxNetFltWinPtSendComplete;
    PTCHARS_40(PtChars).TransferDataCompleteHandler = vboxNetFltWinPtTransferDataComplete;
    PTCHARS_40(PtChars).ResetCompleteHandler = vboxNetFltWinPtResetComplete;
    PTCHARS_40(PtChars).RequestCompleteHandler = vboxNetFltWinPtRequestComplete;
    PTCHARS_40(PtChars).ReceiveHandler = vboxNetFltWinPtReceive;
    PTCHARS_40(PtChars).ReceiveCompleteHandler = vboxNetFltWinPtReceiveComplete;
    PTCHARS_40(PtChars).StatusHandler = vboxNetFltWinPtStatus;
    PTCHARS_40(PtChars).StatusCompleteHandler = vboxNetFltWinPtStatusComplete;
    PTCHARS_40(PtChars).BindAdapterHandler = vboxNetFltWinPtBindAdapter;
    PTCHARS_40(PtChars).UnbindAdapterHandler = vboxNetFltWinPtUnbindAdapter;
    PTCHARS_40(PtChars).UnloadHandler = vboxNetFltWinPtUnloadProtocol;
#if !defined(DEBUG_NETFLT_RECV)
    PTCHARS_40(PtChars).ReceivePacketHandler = vboxNetFltWinPtReceivePacket;
#endif
    PTCHARS_40(PtChars).PnPEventHandler = vboxNetFltWinPtPnPEvent;

    NDIS_STATUS Status;
    NdisRegisterProtocol(&Status, &pGlobalsPt->hProtocol, &PtChars, sizeof (PtChars));
    Assert(Status == STATUS_SUCCESS);
    return Status;
}

/**
 * deregister the protocol edge
 */
DECLHIDDEN(NDIS_STATUS) vboxNetFltWinPtDeregister(PVBOXNETFLTGLOBALS_PT pGlobalsPt)
{
    if (!pGlobalsPt->hProtocol)
        return NDIS_STATUS_SUCCESS;

    NDIS_STATUS Status;

    NdisDeregisterProtocol(&Status, pGlobalsPt->hProtocol);
    Assert (Status == NDIS_STATUS_SUCCESS);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        NdisZeroMemory(pGlobalsPt, sizeof (*pGlobalsPt));
    }
    return Status;
}
