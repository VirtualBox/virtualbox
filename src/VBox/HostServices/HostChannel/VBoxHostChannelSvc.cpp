/* $Id: VBoxHostChannelSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/* @file
 * Host Channel: Host service entry points.
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


/*
 * The HostChannel host service provides a generic proxy between a host's
 * channel provider and a client running in the guest.
 *
 * Host providers must register via a HostCall.
 *
 * A guest client can connect to a host provider and send/receive data.
 *
 * GuestCalls:
 *    * Attach      - attach to a host channel
 *    * Detach      - completely detach from a channel
 *    * Send        - send data from the guest to the channel
 *    * Recv        - non blocking read of available data from the channel
 *    * Control     - generic channel specific command exchange
 *    * EventWait   - wait for a host event
 *    * EventCancel - make the blocking EventWait call to return
 * HostCalls:
 *    * Register    - register a host channel
 *    * Unregister  - unregister it
 *
 * The guest HGCM client connects to the service. The client can attach multiple channels.
 *
 */

#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <VBox/vmm/ssm.h>

#include "HostChannel.h"


static void VBoxHGCMParmUInt32Set(VBOXHGCMSVCPARM *pParm, uint32_t u32)
{
    pParm->type = VBOX_HGCM_SVC_PARM_32BIT;
    pParm->u.uint32 = u32;
}

static int VBoxHGCMParmUInt32Get(VBOXHGCMSVCPARM *pParm, uint32_t *pu32)
{
    if (pParm->type == VBOX_HGCM_SVC_PARM_32BIT)
    {
        *pu32 = pParm->u.uint32;
        return VINF_SUCCESS;
    }

    AssertFailed();
    return VERR_INVALID_PARAMETER;
}

#if 0 /* unused */
static void VBoxHGCMParmPtrSet(VBOXHGCMSVCPARM *pParm, void *pv, uint32_t cb)
{
    pParm->type             = VBOX_HGCM_SVC_PARM_PTR;
    pParm->u.pointer.size   = cb;
    pParm->u.pointer.addr   = pv;
}
#endif

static int VBoxHGCMParmPtrGet(VBOXHGCMSVCPARM *pParm, void **ppv, uint32_t *pcb)
{
    if (pParm->type == VBOX_HGCM_SVC_PARM_PTR)
    {
        *ppv = pParm->u.pointer.addr;
        *pcb = pParm->u.pointer.size;
        return VINF_SUCCESS;
    }

    AssertFailed();
    return VERR_INVALID_PARAMETER;
}


static PVBOXHGCMSVCHELPERS g_pHelpers = NULL;

static RTCRITSECT g_critsect;

/*
 * Helpers.
 */

int vboxHostChannelLock(void)
{
    return RTCritSectEnter(&g_critsect);
}

void vboxHostChannelUnlock(void)
{
    RTCritSectLeave(&g_critsect);
}

void vboxHostChannelEventParmsSet(VBOXHGCMSVCPARM *paParms,
                                  uint32_t u32ChannelHandle,
                                  uint32_t u32Id,
                                  const void *pvEvent,
                                  uint32_t cbEvent)
{
    if (cbEvent > 0)
    {
        void *pvParm = NULL;
        uint32_t cbParm = 0;

        VBoxHGCMParmPtrGet(&paParms[2], &pvParm, &cbParm);

        uint32_t cbToCopy = RT_MIN(cbParm, cbEvent);
        if (cbToCopy > 0)
        {
            Assert(pvParm);
            memcpy(pvParm, pvEvent, cbToCopy);
        }
    }

    VBoxHGCMParmUInt32Set(&paParms[0], u32ChannelHandle);
    VBoxHGCMParmUInt32Set(&paParms[1], u32Id);
    VBoxHGCMParmUInt32Set(&paParms[3], cbEvent);
}

/* This is called under the lock. */
void vboxHostChannelReportAsync(VBOXHOSTCHCLIENT *pClient,
                                uint32_t u32ChannelHandle,
                                uint32_t u32Id,
                                const void *pvEvent,
                                uint32_t cbEvent)
{
    Assert(RTCritSectIsOwner(&g_critsect));

    vboxHostChannelEventParmsSet(pClient->async.paParms,
                                 u32ChannelHandle,
                                 u32Id,
                                 pvEvent,
                                 cbEvent);

    LogRelFlow(("svcCall: CallComplete for pending\n"));

    g_pHelpers->pfnCallComplete (pClient->async.callHandle, VINF_SUCCESS);
}


/*
 *  Service entry points.
 */

static DECLCALLBACK(int) svcUnload(void *pvService)
{
    NOREF(pvService);
    vboxHostChannelDestroy();
    RTCritSectDelete(&g_critsect);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) svcDisconnect(void *pvService, uint32_t u32ClientID, void *pvClient)
{
    RT_NOREF2(pvService, u32ClientID);

    VBOXHOSTCHCLIENT *pClient = (VBOXHOSTCHCLIENT *)pvClient;

    vboxHostChannelClientDisconnect(pClient);

    memset(pClient, 0, sizeof(VBOXHOSTCHCLIENT));

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) svcConnect(void *pvService, uint32_t u32ClientID, void *pvClient, uint32_t fRequestor, bool fRestoring)
{
    RT_NOREF(pvService, fRequestor, fRestoring);
    VBOXHOSTCHCLIENT *pClient = (VBOXHOSTCHCLIENT *)pvClient;

    /* Register the client. */
    memset(pClient, 0, sizeof(VBOXHOSTCHCLIENT));

    pClient->u32ClientID = u32ClientID;

    int rc = vboxHostChannelClientConnect(pClient);

    LogRel2(("svcConnect: rc = %Rrc\n", rc));

    return rc;
}

static DECLCALLBACK(void) svcCall(void *pvService,
                                  VBOXHGCMCALLHANDLE callHandle,
                                  uint32_t u32ClientID,
                                  void *pvClient,
                                  uint32_t u32Function,
                                  uint32_t cParms,
                                  VBOXHGCMSVCPARM paParms[],
                                  uint64_t tsArrival)
{
    RT_NOREF(pvService, tsArrival);

    int rc = VINF_SUCCESS;

    LogRel2(("svcCall: u32ClientID = %d, fn = %d, cParms = %d, pparms = %d\n",
             u32ClientID, u32Function, cParms, paParms));

    VBOXHOSTCHCLIENT *pClient = (VBOXHOSTCHCLIENT *)pvClient;

    bool fAsynchronousProcessing = false;

#ifdef DEBUG
    uint32_t i;

    for (i = 0; i < cParms; i++)
    {
        /** @todo parameters other than 32 bit */
        LogRel2(("    pparms[%d]: type %d value %d\n", i, paParms[i].type, paParms[i].u.uint32));
    }
#endif

    switch (u32Function)
    {
        case VBOX_HOST_CHANNEL_FN_ATTACH:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_ATTACH\n"));

            if (cParms != 3)
                rc = VERR_INVALID_PARAMETER;
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR     /* name */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_32BIT   /* flags */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_32BIT   /* handle */
                    )
                rc = VERR_INVALID_PARAMETER;
            else
            {
                uint32_t u32Flags = 0; /* Shut up msvc*/
                const char *pszName;
                uint32_t cbName;

                rc = VBoxHGCMParmPtrGet(&paParms[0], (void **)&pszName, &cbName);
                if (   RT_SUCCESS(rc)
                    && pszName[cbName - 1] != '\0')
                    rc = VERR_INVALID_PARAMETER;
                if (RT_SUCCESS(rc))
                    rc = VBoxHGCMParmUInt32Get(&paParms[1], &u32Flags);
                if (RT_SUCCESS(rc))
                {
                    uint32_t u32Handle = 0;
                    rc = vboxHostChannelAttach(pClient, &u32Handle, pszName, u32Flags);
                    if (RT_SUCCESS(rc))
                        VBoxHGCMParmUInt32Set(&paParms[2], u32Handle);
                }
            }
        } break;

        case VBOX_HOST_CHANNEL_FN_DETACH:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_DETACH\n"));

            if (cParms != 1)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* handle */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                uint32_t u32Handle;

                rc = VBoxHGCMParmUInt32Get(&paParms[0], &u32Handle);

                if (RT_SUCCESS(rc))
                {
                    rc = vboxHostChannelDetach(pClient, u32Handle);
                }
            }
        } break;

        case VBOX_HOST_CHANNEL_FN_SEND:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_SEND\n"));

            if (cParms != 2)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* handle */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR     /* data */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                uint32_t u32Handle;
                void *pvData;
                uint32_t cbData;

                rc = VBoxHGCMParmUInt32Get (&paParms[0], &u32Handle);

                if (RT_SUCCESS (rc))
                {
                    rc = VBoxHGCMParmPtrGet (&paParms[1], &pvData, &cbData);

                    if (RT_SUCCESS (rc))
                    {
                        rc = vboxHostChannelSend(pClient, u32Handle, pvData, cbData);
                    }
                }
            }
        } break;

        case VBOX_HOST_CHANNEL_FN_RECV:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_RECV\n"));

            if (cParms != 4)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* handle */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR     /* data */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_32BIT   /* sizeReceived */
                     || paParms[3].type != VBOX_HGCM_SVC_PARM_32BIT   /* sizeRemaining */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                uint32_t u32Handle;
                void *pvData;
                uint32_t cbData;

                rc = VBoxHGCMParmUInt32Get (&paParms[0], &u32Handle);

                if (RT_SUCCESS (rc))
                {
                    rc = VBoxHGCMParmPtrGet (&paParms[1], &pvData, &cbData);

                    if (RT_SUCCESS (rc))
                    {
                        uint32_t u32SizeReceived = 0;
                        uint32_t u32SizeRemaining = 0;

                        rc = vboxHostChannelRecv(pClient, u32Handle,
                                                 pvData, cbData,
                                                 &u32SizeReceived, &u32SizeRemaining);

                        if (RT_SUCCESS(rc))
                        {
                            VBoxHGCMParmUInt32Set(&paParms[2], u32SizeReceived);
                            VBoxHGCMParmUInt32Set(&paParms[3], u32SizeRemaining);
                        }
                    }
                }
            }
        } break;

        case VBOX_HOST_CHANNEL_FN_CONTROL:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_CONTROL\n"));

            if (cParms != 5)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* handle */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_32BIT   /* code */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR     /* parm */
                     || paParms[3].type != VBOX_HGCM_SVC_PARM_PTR     /* data */
                     || paParms[4].type != VBOX_HGCM_SVC_PARM_32BIT   /* sizeDataReturned */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                uint32_t u32Handle;
                uint32_t u32Code;
                void *pvParm;
                uint32_t cbParm;
                void *pvData;
                uint32_t cbData;

                rc = VBoxHGCMParmUInt32Get (&paParms[0], &u32Handle);

                if (RT_SUCCESS (rc))
                {
                    rc = VBoxHGCMParmUInt32Get (&paParms[1], &u32Code);

                    if (RT_SUCCESS (rc))
                    {
                        rc = VBoxHGCMParmPtrGet (&paParms[2], &pvParm, &cbParm);

                        if (RT_SUCCESS (rc))
                        {
                            rc = VBoxHGCMParmPtrGet (&paParms[3], &pvData, &cbData);

                            if (RT_SUCCESS (rc))
                            {
                                uint32_t u32SizeDataReturned = 0;

                                rc = vboxHostChannelControl(pClient, u32Handle, u32Code,
                                                            pvParm, cbParm,
                                                            pvData, cbData, &u32SizeDataReturned);
                                if (RT_SUCCESS(rc))
                                {
                                    VBoxHGCMParmUInt32Set(&paParms[4], u32SizeDataReturned);
                                }
                            }
                        }
                    }
                }
            }
        } break;

        case VBOX_HOST_CHANNEL_FN_EVENT_WAIT:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_EVENT_WAIT\n"));

            if (cParms != 4)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* handle */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_32BIT   /* id */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR     /* parm */
                     || paParms[3].type != VBOX_HGCM_SVC_PARM_32BIT   /* sizeReturned */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                bool fEvent = false;

                rc = vboxHostChannelEventWait(pClient, &fEvent, callHandle, paParms);

                if (RT_SUCCESS(rc))
                {
                    if (!fEvent)
                    {
                        /* No event available at the time. Process asynchronously. */
                        fAsynchronousProcessing = true;

                        LogRel2(("svcCall: async.\n"));
                    }
                }
            }
        } break;

        case VBOX_HOST_CHANNEL_FN_EVENT_CANCEL:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_EVENT_CANCEL\n"));

            if (cParms != 0)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                rc = vboxHostChannelEventCancel(pClient);
            }
        } break;

        case VBOX_HOST_CHANNEL_FN_QUERY:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_FN_QUERY\n"));

            if (cParms != 5)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR     /* channel name */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_32BIT   /* code */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR     /* parm */
                     || paParms[3].type != VBOX_HGCM_SVC_PARM_PTR     /* data */
                     || paParms[4].type != VBOX_HGCM_SVC_PARM_32BIT   /* sizeDataReturned */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                const char *pszName;
                uint32_t cbName;
                uint32_t u32Code = 0; /* Shut up msvc*/
                void *pvParm = NULL;  /* Shut up msvc*/
                uint32_t cbParm = 0;  /* Shut up msvc*/
                void *pvData = NULL;  /* Shut up msvc*/
                uint32_t cbData = 0;  /* Shut up msvc*/

                rc = VBoxHGCMParmPtrGet(&paParms[0], (void **)&pszName, &cbName);
                if (   RT_SUCCESS(rc)
                    && pszName[cbName - 1] != '\0')
                    rc = VERR_INVALID_PARAMETER;

                if (RT_SUCCESS(rc))
                    rc = VBoxHGCMParmUInt32Get(&paParms[1], &u32Code);
                if (RT_SUCCESS (rc))
                    rc = VBoxHGCMParmPtrGet(&paParms[2], &pvParm, &cbParm);
                if (RT_SUCCESS (rc))
                    rc = VBoxHGCMParmPtrGet(&paParms[3], &pvData, &cbData);
                if (RT_SUCCESS (rc))
                {
                    uint32_t u32SizeDataReturned = 0;
                    rc = vboxHostChannelQuery(pClient, pszName, u32Code,
                                              pvParm, cbParm,
                                              pvData, cbData, &u32SizeDataReturned);
                    if (RT_SUCCESS(rc))
                        VBoxHGCMParmUInt32Set(&paParms[4], u32SizeDataReturned);
                }
            }
        } break;

        default:
        {
            rc = VERR_NOT_IMPLEMENTED;
        }
    }

    LogRelFlow(("svcCall: rc = %Rrc, async %d\n", rc, fAsynchronousProcessing));

    if (!fAsynchronousProcessing)
    {
        g_pHelpers->pfnCallComplete(callHandle, rc);
    }
}

static DECLCALLBACK(int) svcHostCall(void *pvService,
                                     uint32_t u32Function,
                                     uint32_t cParms,
                                     VBOXHGCMSVCPARM paParms[])
{
    NOREF(pvService);

    int rc = VINF_SUCCESS;

    LogRel2(("svcHostCall: fn = %d, cParms = %d, pparms = %d\n",
             u32Function, cParms, paParms));

    switch (u32Function)
    {
        case VBOX_HOST_CHANNEL_HOST_FN_REGISTER:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_HOST_FN_REGISTER\n"));

            if (cParms != 2)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR /* name */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR /* iface */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                void *pvName;
                uint32_t cbName;
                void *pvInterface;
                uint32_t cbInterface;

                rc = VBoxHGCMParmPtrGet(&paParms[0], &pvName, &cbName);

                if (RT_SUCCESS(rc))
                {
                    rc = VBoxHGCMParmPtrGet(&paParms[1], &pvInterface, &cbInterface);

                    if (RT_SUCCESS(rc))
                    {
                        rc = vboxHostChannelRegister((const char *)pvName,
                                                     (VBOXHOSTCHANNELINTERFACE *)pvInterface, cbInterface);
                    }
                }
            }
        } break;

        case VBOX_HOST_CHANNEL_HOST_FN_UNREGISTER:
        {
            LogRel2(("svcCall: VBOX_HOST_CHANNEL_HOST_FN_UNREGISTER\n"));

            if (cParms != 1)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR /* name */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                void *pvName;
                uint32_t cbName;

                rc = VBoxHGCMParmPtrGet(&paParms[0], &pvName, &cbName);

                if (RT_SUCCESS(rc))
                {
                    rc = vboxHostChannelUnregister((const char *)pvName);
                }
            }
        } break;

        default:
            break;
    }

    LogRelFlow(("svcHostCall: rc = %Rrc\n", rc));
    return rc;
}

#if 0
/** If the client in the guest is waiting for a read operation to complete
 * then complete it, otherwise return.  See the protocol description in the
 * shared clipboard module description. */
void vboxSvcClipboardCompleteReadData(VBOXHOSTCHCLIENT *pClient, int rc, uint32_t cbActual)
{
    VBOXHGCMCALLHANDLE callHandle = NULL;
    VBOXHGCMSVCPARM *paParms = NULL;
    bool fReadPending = false;
    if (vboxSvcClipboardLock())  /* if not can we do anything useful? */
    {
        callHandle   = pClient->asyncRead.callHandle;
        paParms      = pClient->asyncRead.paParms;
        fReadPending = pClient->fReadPending;
        pClient->fReadPending = false;
        vboxSvcClipboardUnlock();
    }
    if (fReadPending)
    {
        VBoxHGCMParmUInt32Set (&paParms[2], cbActual);
        g_pHelpers->pfnCallComplete (callHandle, rc);
    }
}

/**
 * SSM descriptor table for the VBOXHOSTCHCLIENT structure.
 */
static SSMFIELD const g_aClipboardClientDataFields[] =
{
    SSMFIELD_ENTRY(VBOXHOSTCHCLIENT, u32ClientID),  /* for validation purposes */
    SSMFIELD_ENTRY(VBOXHOSTCHCLIENT, fMsgQuit),
    SSMFIELD_ENTRY(VBOXHOSTCHCLIENT, fMsgReadData),
    SSMFIELD_ENTRY(VBOXHOSTCHCLIENT, fMsgFormats),
    SSMFIELD_ENTRY(VBOXHOSTCHCLIENT, u32RequestedFormat),
    SSMFIELD_ENTRY_TERM()
};

static DECLCALLBACK(int) svcSaveState(void *pvService, uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM)
{
    NOREF(pvService);

    /* If there are any pending requests, they must be completed here. Since
     * the service is single threaded, there could be only requests
     * which the service itself has postponed.
     *
     * HGCM knows that the state is being saved and that the pfnComplete
     * calls are just clean ups. These requests are saved by the VMMDev.
     *
     * When the state will be restored, these requests will be reissued
     * by VMMDev. The service therefore must save state as if there were no
     * pending request.
     */
    LogRel2 (("svcSaveState: u32ClientID = %d\n", u32ClientID));

    VBOXHOSTCHCLIENT *pClient = (VBOXHOSTCHCLIENT *)pvClient;

    /* This field used to be the length. We're using it as a version field
       with the high bit set. */
    SSMR3PutU32 (pSSM, UINT32_C (0x80000002));
    int rc = SSMR3PutStructEx (pSSM, pClient, sizeof(*pClient), 0 /*fFlags*/, &g_aClipboardClientDataFields[0], NULL);
    AssertRCReturn (rc, rc);

    if (pClient->fAsync)
    {
        g_pHelpers->pfnCallComplete (pClient->async.callHandle, VINF_SUCCESS /* error code is not important here. */);
        pClient->fAsync = false;
    }

    vboxSvcClipboardCompleteReadData (pClient, VINF_SUCCESS, 0);

    return VINF_SUCCESS;
}

/**
 * This structure corresponds to the original layout of the
 * VBOXHOSTCHCLIENT structure.  As the structure was saved as a whole
 * when saving state, we need to remember it forever in order to preserve
 * compatibility.
 *
 * (Starting with 3.1 this is no longer used.)
 *
 * @remarks Putting this outside svcLoadState to avoid visibility warning caused
 *          by -Wattributes.
 */
typedef struct CLIPSAVEDSTATEDATA
{
    struct CLIPSAVEDSTATEDATA *pNext;
    struct CLIPSAVEDSTATEDATA *pPrev;

    VBOXCLIPBOARDCONTEXT *pCtx;

    uint32_t u32ClientID;

    bool fAsync: 1; /* Guest is waiting for a message. */

    bool fMsgQuit: 1;
    bool fMsgReadData: 1;
    bool fMsgFormats: 1;

    struct {
        VBOXHGCMCALLHANDLE callHandle;
        VBOXHGCMSVCPARM *paParms;
    } async;

    struct {
         void *pv;
         uint32_t cb;
         uint32_t u32Format;
    } data;

    uint32_t u32AvailableFormats;
    uint32_t u32RequestedFormat;

} CLIPSAVEDSTATEDATA;

static DECLCALLBACK(int) svcLoadState(void *, uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM)
{
    LogRel2 (("svcLoadState: u32ClientID = %d\n", u32ClientID));

    VBOXHOSTCHCLIENT *pClient = (VBOXHOSTCHCLIENT *)pvClient;

    /* Existing client can not be in async state yet. */
    Assert (!pClient->fAsync);

    /* Save the client ID for data validation. */
    /** @todo isn't this the same as u32ClientID? Playing safe for now... */
    uint32_t const u32ClientIDOld = pClient->u32ClientID;

    /* Restore the client data. */
    uint32_t lenOrVer;
    int rc = SSMR3GetU32 (pSSM, &lenOrVer);
    AssertRCReturn (rc, rc);
    if (lenOrVer == UINT32_C (0x80000002))
    {
        rc = SSMR3GetStructEx (pSSM, pClient, sizeof(*pClient), 0 /*fFlags*/, &g_aClipboardClientDataFields[0], NULL);
        AssertRCReturn (rc, rc);
    }
    else if (lenOrVer == (SSMR3HandleHostBits (pSSM) == 64 ? 72 : 48))
    {
        /**
         * SSM descriptor table for the CLIPSAVEDSTATEDATA structure.
         */
        static SSMFIELD const s_aClipSavedStateDataFields30[] =
        {
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, pNext),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, pPrev),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, pCtx),
            SSMFIELD_ENTRY(                 CLIPSAVEDSTATEDATA, u32ClientID),
            SSMFIELD_ENTRY_CUSTOM(fMsgQuit+fMsgReadData+fMsgFormats, RT_OFFSETOF(CLIPSAVEDSTATEDATA, u32ClientID) + 4, 4),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, async.callHandle),
            SSMFIELD_ENTRY_IGN_HCPTR(       CLIPSAVEDSTATEDATA, async.paParms),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, data.pv),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, data.cb),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, data.u32Format),
            SSMFIELD_ENTRY_IGNORE(          CLIPSAVEDSTATEDATA, u32AvailableFormats),
            SSMFIELD_ENTRY(                 CLIPSAVEDSTATEDATA, u32RequestedFormat),
            SSMFIELD_ENTRY_TERM()
        };

        CLIPSAVEDSTATEDATA savedState;
        RT_ZERO (savedState);
        rc = SSMR3GetStructEx (pSSM, &savedState, sizeof(savedState), SSMSTRUCT_FLAGS_MEM_BAND_AID,
                               &s_aClipSavedStateDataFields30[0], NULL);
        AssertRCReturn (rc, rc);

        pClient->fMsgQuit           = savedState.fMsgQuit;
        pClient->fMsgReadData       = savedState.fMsgReadData;
        pClient->fMsgFormats        = savedState.fMsgFormats;
        pClient->u32RequestedFormat = savedState.u32RequestedFormat;
    }
    else
    {
        LogRel (("Client data size mismatch: got %#x\n", lenOrVer));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }

    /* Verify the client ID. */
    if (pClient->u32ClientID != u32ClientIDOld)
    {
        LogRel (("Client ID mismatch: expected %d, got %d\n", u32ClientIDOld, pClient->u32ClientID));
        pClient->u32ClientID = u32ClientIDOld;
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }

    /* Actual host data are to be reported to guest (SYNC). */
    vboxClipboardSync (pClient);

    return VINF_SUCCESS;
}
#endif

static int svcInit(void)
{
    int rc = RTCritSectInit(&g_critsect);

    if (RT_SUCCESS (rc))
    {
        rc = vboxHostChannelInit();

        /* Clean up on failure, because 'svnUnload' will not be called
         * if the 'svcInit' returns an error.
         */
        if (RT_FAILURE(rc))
        {
            RTCritSectDelete(&g_critsect);
        }
    }

    return rc;
}

extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad(VBOXHGCMSVCFNTABLE *pTable)
{
    int rc = VINF_SUCCESS;

    LogRelFlowFunc(("pTable = %p\n", pTable));

    if (!pTable)
    {
        rc = VERR_INVALID_PARAMETER;
    }
    else
    {
        LogRel2(("VBoxHGCMSvcLoad: pTable->cbSize = %d, pTable->u32Version = 0x%08X\n",
                  pTable->cbSize, pTable->u32Version));

        if (   pTable->cbSize != sizeof (VBOXHGCMSVCFNTABLE)
            || pTable->u32Version != VBOX_HGCM_SVC_VERSION)
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            g_pHelpers = pTable->pHelpers;

            pTable->cbClient = sizeof(VBOXHOSTCHCLIENT);

            pTable->pfnUnload     = svcUnload;
            pTable->pfnConnect    = svcConnect;
            pTable->pfnDisconnect = svcDisconnect;
            pTable->pfnCall       = svcCall;
            pTable->pfnHostCall   = svcHostCall;
            pTable->pfnSaveState  = NULL; // svcSaveState;
            pTable->pfnLoadState  = NULL; // svcLoadState;
            pTable->pfnRegisterExtension  = NULL;
            pTable->pvService     = NULL;

            /* Service specific initialization. */
            rc = svcInit();
        }
    }

    return rc;
}
