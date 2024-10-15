/* $Id: DrvVMNet.m 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvVMNet - Network filter driver that uses MAC OS VMNET API.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_VMNET
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/intnet.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <VBox/param.h>

#include "Pcap.h"
#include "VBoxDD.h"

#include <sys/uio.h>
#import <vmnet/vmnet.h>

#define VMNET_MAX_HOST_INTERFACE_NAME_LENGTH 16
#define VMNET_MAX_IP_ADDRESS_STRING_LENGTH   48

/* Force release logging for debug builds */
#if 0
# undef Log
# undef LogFlow
# undef Log2
# undef Log3
# define Log     LogRel
# define LogFlow LogRel
# define Log2    LogRel
# define Log3    LogRel
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * VMNET driver states.
 */
 typedef enum VMNETSTATE
{
    /** The driver is suspended. */
    VMNETSTATE_SUSPENDED = 1,
    /** The driver is running. */
    VMNETSTATE_RUNNING,
    /** The usual 32-bit type blowup. */
    VMNETSTATE_32BIT_HACK = 0x7fffffff
} VMNETSTATE;

/**
 * VMNET driver instance data.
 *
 * @implements  PDMINETWORKUP
 * @implements  PDMINETWORKCONFIG
 */
typedef struct DRVVMNET
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The port we're attached to. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** The config port interface we're attached to. */
    PPDMINETWORKCONFIG      pIAboveConfig;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** For when we're the leaf driver. */
    RTCRITSECT              XmitLock;
    /** VMNET interface queue handle. */
    dispatch_queue_t        InterfaceQueue;
    /** VMNET interface handle. */
    interface_ref           Interface;
    /** The unique id for this network. */
    uuid_t                  uuid;
    /** The operation mode: bridged or host. */
    uint32_t                uMode;
    /** The operational state: suspended or running. */
    VMNETSTATE volatile     enmState;
    /** The host interface name for bridge mode. */
    char                    szHostInterface[VMNET_MAX_HOST_INTERFACE_NAME_LENGTH];
    /** The network mask for host mode. */
    char                    szNetworkMask[VMNET_MAX_IP_ADDRESS_STRING_LENGTH];
    /** The lower IP address of DHCP range for host mode. */
    char                    szLowerIP[VMNET_MAX_IP_ADDRESS_STRING_LENGTH];
    /** The upper IP address of DHCP range for host mode. */
    char                    szUpperIP[VMNET_MAX_IP_ADDRESS_STRING_LENGTH];
} DRVVMNET, *PDRVVMNET;



/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
static DECLCALLBACK(int) drvVMNetUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVVMNET pThis = RT_FROM_MEMBER(pInterface, DRVVMNET, INetworkUp);
    LogFlow(("drvVMNetUp_BeginXmit:\n"));
    int rc = RTCritSectTryEnter(&pThis->XmitLock);
    if (RT_UNLIKELY(rc == VERR_SEM_BUSY))
        rc = VERR_TRY_AGAIN;
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnAllocBuf}
 */
static DECLCALLBACK(int) drvVMNetUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                  PCPDMNETWORKGSO pGso, PPPDMSCATTERGATHER ppSgBuf)
{
    RT_NOREF(pInterface);
    //PDRVVMNET pThis = RT_FROM_MEMBER(pInterface, DRVVMNET, INetworkUp);
    LogFlow(("drvVMNetUp_AllocBuf: cb=%llu%s\n", cbMin, pGso == NULL ? "" : " GSO"));
    /*
     * Allocate a scatter / gather buffer descriptor that is immediately
     * followed by the buffer space of its single segment.  The GSO context
     * comes after that again.
     */
    PPDMSCATTERGATHER pSgBuf = (PPDMSCATTERGATHER)RTMemAlloc(  RT_ALIGN_Z(sizeof(*pSgBuf), 16)
                                                             + RT_ALIGN_Z(cbMin, 16)
                                                             + (pGso ? RT_ALIGN_Z(sizeof(*pGso), 16) : 0));
    if (!pSgBuf)
        return VERR_NO_MEMORY;

    /*
     * Initialize the S/G buffer and return.
     */
    pSgBuf->fFlags         = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1;
    pSgBuf->cbUsed         = 0;
    pSgBuf->cbAvailable    = RT_ALIGN_Z(cbMin, 16);
    pSgBuf->pvAllocator    = NULL;
    if (!pGso)
        pSgBuf->pvUser     = NULL;
    else
    {
        pSgBuf->pvUser     = (uint8_t *)(pSgBuf + 1) + pSgBuf->cbAvailable;
        *(PPDMNETWORKGSO)pSgBuf->pvUser = *pGso;
    }
    pSgBuf->cSegs          = 1;
    pSgBuf->aSegs[0].cbSeg = pSgBuf->cbAvailable;
    pSgBuf->aSegs[0].pvSeg = pSgBuf + 1;

    LogFlow(("drvVMNetUp_AllocBuf: successful %p\n", pSgBuf));
    *ppSgBuf = pSgBuf;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnFreeBuf}
 */
static DECLCALLBACK(int) drvVMNetUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    PDRVVMNET pThis = RT_FROM_MEMBER(pInterface, DRVVMNET, INetworkUp);
    LogFlow(("drvVMNetUp_FreeBuf: %p\n", pSgBuf));
    Assert(RTCritSectIsOwner(&pThis->XmitLock));
    RT_NOREF(pThis);
    if (pSgBuf)
    {
        Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
        pSgBuf->fFlags = 0;
        RTMemFree(pSgBuf);
    }
    return VINF_SUCCESS;
}


static int drvVMNetReceive(PDRVVMNET pThis, const uint8_t *pbFrame, uint32_t cbFrame)
{
    if (pThis->enmState != VMNETSTATE_RUNNING)
    {
        Log(("drvVMNetReceive: Ignoring incoming packet (%d bytes) in suspended state\n", cbFrame));
        return VINF_SUCCESS;
    }

    Log(("drvVMNetReceive: Incoming packet: %RTmac <= %RTmac (%d bytes)\n", pbFrame, pbFrame + 6, cbFrame));
    Log2(("%.*Rhxd\n", cbFrame, pbFrame));
    if (pThis->pIAboveNet && pThis->pIAboveNet->pfnReceive)
        return pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, pbFrame, cbFrame);
    return VERR_TRY_AGAIN;
}


static int drvVMNetSend(PDRVVMNET pThis, const uint8_t *pbFrame, uint32_t cbFrame)
{
    if (pThis->enmState != VMNETSTATE_RUNNING)
    {
        Log(("drvVMNetReceive: Ignoring outgoing packet (%d bytes) in suspended state\n", cbFrame));
        return VINF_SUCCESS;
    }

    Log(("drvVMNetSend: Outgoing packet (%d bytes)\n", cbFrame));
    Log2(("%.*Rhxd\n", cbFrame, pbFrame));

    struct iovec io;
    struct vmpktdesc packets;
    int    packet_count = 1;

    io.iov_base = (void*)pbFrame;
    io.iov_len = cbFrame;
    packets.vm_pkt_size = cbFrame;
    packets.vm_pkt_iov = &io;
    packets.vm_pkt_iovcnt = 1;
    packets.vm_flags = 0;

    vmnet_return_t rc = vmnet_write(pThis->Interface, &packets, &packet_count);
    if (rc != VMNET_SUCCESS)
        Log(("drvVMNetSend: Failed to send a packet with error code %d\n", rc));
    return (rc == VMNET_SUCCESS) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
}

/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
static DECLCALLBACK(int) drvVMNetUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVVMNET pThis = RT_FROM_MEMBER(pInterface, DRVVMNET, INetworkUp);

    LogFlow(("drvVMNetUp_SendBuf: %p\n", pSgBuf));
    Assert(RTCritSectIsOwner(&pThis->XmitLock));

    int rc;
    if (!pSgBuf->pvUser)
    {
        rc = drvVMNetSend(pThis, pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed);
    }
    else
    {
        uint8_t         abHdrScratch[256];
        uint8_t const  *pbFrame = (uint8_t const *)pSgBuf->aSegs[0].pvSeg;
        PCPDMNETWORKGSO pGso    = (PCPDMNETWORKGSO)pSgBuf->pvUser;
        uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, pSgBuf->cbUsed);  Assert(cSegs > 1);
        rc = VINF_SUCCESS;
        for (uint32_t iSeg = 0; iSeg < cSegs && RT_SUCCESS(rc); iSeg++)
        {
            uint32_t cbSegFrame;
            void *pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)pbFrame, pSgBuf->cbUsed, abHdrScratch,
                                                       iSeg, cSegs, &cbSegFrame);
            rc = drvVMNetSend(pThis, pvSegFrame, cbSegFrame);
        }
    }

    LogFlow(("drvVMNetUp_SendBuf: free %p\n", pSgBuf));
    pSgBuf->fFlags = 0;
    RTMemFree(pSgBuf);
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
static DECLCALLBACK(void) drvVMNetUp_EndXmit(PPDMINETWORKUP pInterface)
{
    LogFlow(("drvVMNetUp_EndXmit:\n"));
    PDRVVMNET pThis = RT_FROM_MEMBER(pInterface, DRVVMNET, INetworkUp);
    RTCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
static DECLCALLBACK(void) drvVMNetUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    RT_NOREF(pInterface, fPromiscuous);
    LogFlow(("drvVMNetUp_SetPromiscuousMode: fPromiscuous=%d\n", fPromiscuous));
    // PDRVVMNET pThis = RT_FROM_MEMBER(pInterface, DRVVMNET, INetworkUp);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnNotifyLinkChanged}
 */
static DECLCALLBACK(void) drvVMNetUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    RT_NOREF(pInterface, enmLinkState);
    LogFlow(("drvVMNetUp_NotifyLinkChanged: enmLinkState=%d\n", enmLinkState));
    // PDRVVMNET pThis = RT_FROM_MEMBER(pInterface, DRVVMNET, INetworkUp);
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvVMNetQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVVMNET  pThis   = PDMINS_2_DATA(pDrvIns, PDRVVMNET);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUp);
    //PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKVMNETCONFIG, &pThis->INetworkVmnetConfig);
    return NULL;
}


static vmnet_return_t drvVMNetAttach(PDRVVMNET pThis)
{
    xpc_object_t interface_desc;
    dispatch_semaphore_t operation_done;
    __block vmnet_return_t vmnet_status = VMNET_FAILURE;
    __block size_t max_packet_size = 0;
    //__block RTMAC MacAddress;

    pThis->InterfaceQueue = dispatch_queue_create("VMNET", DISPATCH_QUEUE_SERIAL);
    operation_done = dispatch_semaphore_create(0);
    interface_desc = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uuid(interface_desc, vmnet_interface_id_key, pThis->uuid);
    xpc_dictionary_set_bool(interface_desc, vmnet_allocate_mac_address_key, false);
    xpc_dictionary_set_uint64(interface_desc, vmnet_operation_mode_key, pThis->uMode);
    if (pThis->uMode == VMNET_BRIDGED_MODE)
    {
        LogFlow(("drvVMNetAttach: mode=briged hostInterface='%s'\n", pThis->szHostInterface));
        xpc_dictionary_set_string(interface_desc, vmnet_shared_interface_name_key, pThis->szHostInterface);
    }
    else
    {
#ifdef LOG_ENABLED
        char szUUID[40];
        uuid_unparse(pThis->uuid, szUUID);
        LogFlow(("drvVMNetAttach: mode=host id='%s' netmask='%s' start='%s' end='%s'\n", szUUID, pThis->szNetworkMask, pThis->szLowerIP, pThis->szUpperIP));
#endif
        xpc_dictionary_set_string(interface_desc, vmnet_subnet_mask_key, pThis->szNetworkMask);
        xpc_dictionary_set_string(interface_desc, vmnet_start_address_key, pThis->szLowerIP);
        xpc_dictionary_set_string(interface_desc, vmnet_end_address_key, pThis->szUpperIP);
    }
    pThis->Interface = vmnet_start_interface(interface_desc, pThis->InterfaceQueue,
        ^(vmnet_return_t status, xpc_object_t interface_param)
    {
        // Log(("Callback reached!\n"));
        vmnet_status = status;
        if (status != VMNET_SUCCESS)
            Log(("Failed to start VMNET interface. Status = %d.\n", status));
        else if (interface_param == NULL)
            Log(("No interface parameters provided!\n"));
        else
        {
            Log(("VMNET interface has been started. Status = %d.\n", status));
#if 0
            const char *pcszMacAddress = xpc_dictionary_get_string(interface_param, vmnet_mac_address_key);
            int rc = VERR_NOT_FOUND;
            if (pcszMacAddress)
                rc = RTNetStrToMacAddr(pcszMacAddress, &pThis->MacAddress);
            if (RT_FAILURE(rc))
                Log(("drvVMNetAttachBridged: Failed to convert '%s' to MAC address (%Rrc)\n", pcszMacAddress ? pcszMacAddress : "(null)", rc));
#endif
            max_packet_size = xpc_dictionary_get_uint64(interface_param, vmnet_max_packet_size_key);
            if (max_packet_size == 0)
            {
                max_packet_size = 1518;
                LogRel(("VMNet: Failed to retrieve max packet size, assuming %d bytes.\n", max_packet_size));
                LogRel(("VMNet: Avaliable interface parameter keys:\n"));
                xpc_dictionary_apply(interface_param, ^bool(const char * _Nonnull key, xpc_object_t  _Nonnull value) {
                    RT_NOREF(value);
                    LogRel(("VMNet:   %s\n", key));
                    return true;
                });
            }
#ifdef LOG_ENABLED
            // Log(("MAC address: %s\n", xpc_dictionary_get_string(interface_param, vmnet_mac_address_key)));
            Log(("Max packet size: %zu\n", max_packet_size));
            Log(("MTU size: %llu\n", xpc_dictionary_get_uint64(interface_param, vmnet_mtu_key)));
            Log(("Avaliable keys:\n"));
            xpc_dictionary_apply(interface_param, ^bool(const char * _Nonnull key, xpc_object_t  _Nonnull value) {
                RT_NOREF(value);
                Log(("%s\n", key));
                return true;
            });
#endif /* LOG_ENABLED */
        }
        dispatch_semaphore_signal(operation_done);
    });
    if (dispatch_semaphore_wait(operation_done, dispatch_time(DISPATCH_TIME_NOW, RT_NS_1MIN)))
    {
        LogRel(("VMNet: Failed to start VMNET interface due to time out!\n"));
        return VMNET_FAILURE;
    }

    if (vmnet_status != VMNET_SUCCESS)
        return vmnet_status;

    if (pThis->Interface == NULL)
    {
        Log(("Failed to start VMNET interface with unknown status!\n"));
        return VMNET_FAILURE;
    }

    LogRel(("VMNet: Max packet size is %zu\n", max_packet_size));

    vmnet_interface_set_event_callback(pThis->Interface, VMNET_INTERFACE_PACKETS_AVAILABLE, pThis->InterfaceQueue, ^(interface_event_t event_mask, xpc_object_t  _Nonnull event) {
        if (event_mask & VMNET_INTERFACE_PACKETS_AVAILABLE)
        {
            int rc;
            struct vmpktdesc packets;
            struct iovec io;
            int packet_count = (int)xpc_dictionary_get_uint64(event, vmnet_estimated_packets_available_key);
            if (packet_count == 1)
                Log3(("Incoming packets available: %d\n", packet_count));
            else
                Log(("WARNING! %d incoming packets available, but we will fetch just one.\n", packet_count));
            packet_count = 1;
            io.iov_base = malloc(max_packet_size);
            io.iov_len = max_packet_size;
            packets.vm_pkt_iov = &io;
            packets.vm_pkt_iovcnt = 1;
            packets.vm_pkt_size = max_packet_size;
            packets.vm_flags = 0;
            rc = vmnet_read(pThis->Interface, &packets, &packet_count);
            if (rc != VMNET_SUCCESS)
                Log(("Failed to read packets with error code %d\n", rc));
            else
            {
                Log3(("Successfully read %d packets:\n", packet_count));
                for (int i = 0; i < packet_count; ++i)
                {
                    rc = drvVMNetReceive(pThis, io.iov_base, packets.vm_pkt_size);
                }
            }
            free(io.iov_base);
        }
    });

    return vmnet_status;
}

static int drvVMNetDetach(PDRVVMNET pThis)
{
    if (pThis->Interface)
    {
        vmnet_stop_interface(pThis->Interface, pThis->InterfaceQueue, ^(vmnet_return_t status){
            RT_NOREF(status);
            Log(("VMNET interface has been stopped. Status = %d.\n", status));
        });
        pThis->Interface = 0;
    }
    if (pThis->InterfaceQueue)
    {
        dispatch_release(pThis->InterfaceQueue);
        pThis->InterfaceQueue = 0;
    }

    return 0;
}


/**
 * @interface_method_impl{PDMDRVREG,pfnDestruct}
 */
static DECLCALLBACK(void) drvVMNetDestruct(PPDMDRVINS pDrvIns)
{
    PDRVVMNET pThis = PDMINS_2_DATA(pDrvIns, PDRVVMNET);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    LogFlow(("drvVMNetDestruct: %p\n", pDrvIns));
    drvVMNetDetach(pThis);
    if (RTCritSectIsInitialized(&pThis->XmitLock))
        RTCritSectDelete(&pThis->XmitLock);
}


/**
 * @interface_method_impl{Construct a NAT network transport driver instance,
 *                       PDMDRVREG,pfnDestruct}
 */
static DECLCALLBACK(int) drvVMNetConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVVMNET       pThis = PDMINS_2_DATA(pDrvIns, PDRVVMNET);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    LogFlow(("drvVMNetConstruct: %p\n", pDrvIns));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                                  = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface                = drvVMNetQueryInterface;
    /* INetworkUp */
    pThis->INetworkUp.pfnBeginXmit                  = drvVMNetUp_BeginXmit;
    pThis->INetworkUp.pfnAllocBuf                   = drvVMNetUp_AllocBuf;
    pThis->INetworkUp.pfnFreeBuf                    = drvVMNetUp_FreeBuf;
    pThis->INetworkUp.pfnSendBuf                    = drvVMNetUp_SendBuf;
    pThis->INetworkUp.pfnEndXmit                    = drvVMNetUp_EndXmit;
    pThis->INetworkUp.pfnSetPromiscuousMode         = drvVMNetUp_SetPromiscuousMode;
    pThis->INetworkUp.pfnNotifyLinkChanged          = drvVMNetUp_NotifyLinkChanged;

    /* Initialize the state. */
    pThis->enmState = VMNETSTATE_SUSPENDED;

    /*
     * Create the locks.
     */
    int rc = RTCritSectInit(&pThis->XmitLock);
    AssertRCReturn(rc, rc);

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,
                                  "Network"
                                  "|Id"
                                  "|Trunk"
                                  "|TrunkType"
                                  "|NetworkMask"
                                  "|LowerIP"
                                  "|UpperIP",
                                  "");

    /** @cfgm{GUID, string}
     * The unique id of the VMNET interface.
     */
    char szUUID[40];
    rc = pHlp->pfnCFGMQueryString(pCfg, "Id", szUUID, sizeof(szUUID));
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        uuid_generate_random(pThis->uuid);
    else if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"Id\" value"));
    else if (uuid_parse(szUUID, pThis->uuid))
        return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Configuration error: Invalid \"Id\" value: %s"), szUUID);

    /** @cfgm{TrunkType, uint32_t}
     * The trunk connection type see INTNETTRUNKTYPE.
     */
    uint32_t u32TrunkType;
    rc = pHlp->pfnCFGMQueryU32(pCfg, "TrunkType", &u32TrunkType);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"TrunkType\" value"));

    switch ((INTNETTRUNKTYPE)u32TrunkType)
    {
        case kIntNetTrunkType_NetAdp:
            /*
            * Get the network mask.
            */
            rc = pHlp->pfnCFGMQueryString(pCfg, "NetworkMask", pThis->szNetworkMask, sizeof(pThis->szNetworkMask));
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc,
                                        N_("Configuration error: Failed to get the \"NetworkMask\" value"));

            /*
            * Get the network mask.
            */
            rc = pHlp->pfnCFGMQueryString(pCfg, "LowerIP", pThis->szLowerIP, sizeof(pThis->szLowerIP));
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc,
                                        N_("Configuration error: Failed to get the \"LowerIP\" value"));

            /*
            * Get the network mask.
            */
            rc = pHlp->pfnCFGMQueryString(pCfg, "UpperIP", pThis->szUpperIP, sizeof(pThis->szUpperIP));
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc,
                                        N_("Configuration error: Failed to get the \"UpperIP\" value"));

            pThis->uMode = VMNET_HOST_MODE;
            LogRel(("VMNet: Host network with mask %s (%s to %s)\n", pThis->szNetworkMask, pThis->szLowerIP, pThis->szUpperIP));
            break;

        case kIntNetTrunkType_NetFlt:
            /** @cfgm{Trunk, string}
            * The name of the host interface to use for bridging.
            */
            rc = pHlp->pfnCFGMQueryString(pCfg, "Trunk", pThis->szHostInterface, sizeof(pThis->szHostInterface));
            if (RT_FAILURE(rc))
                return PDMDRV_SET_ERROR(pDrvIns, rc,
                                        N_("Configuration error: Failed to get the \"Trunk\" value"));
            pThis->uMode = VMNET_BRIDGED_MODE;
            LogRel(("VMNet: Bridge to %s\n", pThis->szHostInterface));
            break;

        default:
            return PDMDRV_SET_ERROR(pDrvIns, rc,
                                    N_("Configuration error: Unsupported \"TrunkType\" value"));
    }

    /*
     * Check that no-one is attached to us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Query the network port interface.
     */
    pThis->pIAboveNet = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKDOWN);
    if (!pThis->pIAboveNet)
    {
        AssertMsgFailed(("Configuration error: the above device/driver didn't export the network port interface!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    /*
     * Query the network config interface.
     */
    pThis->pIAboveConfig = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKCONFIG);
    if (!pThis->pIAboveConfig)
    {
        AssertMsgFailed(("Configuration error: the above device/driver didn't export the network config interface!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    vmnet_return_t status = drvVMNetAttach(pThis);
    if (status != VMNET_SUCCESS)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("Error: vmnet_start_interface returned %d"), status);

    return VINF_SUCCESS;
}


/**
 * Power On notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvVMNetPowerOn(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvVMNetPowerOn\n"));
    PDRVVMNET pThis = PDMINS_2_DATA(pDrvIns, PDRVVMNET);
    ASMAtomicXchgSize(&pThis->enmState, VMNETSTATE_RUNNING);
}


/**
 * Suspend notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvVMNetSuspend(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvVMNetSuspend\n"));
    PDRVVMNET pThis = PDMINS_2_DATA(pDrvIns, PDRVVMNET);
    ASMAtomicXchgSize(&pThis->enmState, VMNETSTATE_SUSPENDED);
}


/**
 * Resume notification.
 *
 * @param   pDrvIns     The driver instance.
 */
static DECLCALLBACK(void) drvVMNetResume(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvVMNetResume\n"));
    PDRVVMNET pThis = PDMINS_2_DATA(pDrvIns, PDRVVMNET);
    ASMAtomicXchgSize(&pThis->enmState, VMNETSTATE_RUNNING);
}



/**
 * Network sniffer filter driver registration record.
 */
const PDMDRVREG g_DrvVMNet =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "VMNet",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "VMNET Filter Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    UINT32_MAX,
    /* cbInstance */
    sizeof(DRVVMNET),
    /* pfnConstruct */
    drvVMNetConstruct,
    /* pfnDestruct */
    drvVMNetDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    drvVMNetPowerOn,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    drvVMNetSuspend,
    /* pfnResume */
    drvVMNetResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

