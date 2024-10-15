/* $Id: VUSBDevice.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB - Device.
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
#define LOG_GROUP LOG_GROUP_DRV_VUSB
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include "VUSBInternal.h"

#include "VUSBSniffer.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Argument package of vusbDevResetThread().
 */
typedef struct vusb_reset_args
{
    /** Pointer to the device which is being reset. */
    PVUSBDEV            pDev;
    /** The reset return code. */
    int                 rc;
    /** Pointer to the completion callback. */
    PFNVUSBRESETDONE    pfnDone;
    /** User argument to pfnDone. */
    void               *pvUser;
} VUSBRESETARGS, *PVUSBRESETARGS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Default message pipe. */
const VUSBDESCENDPOINTEX g_Endpoint0 =
{
    {
        /* .bLength = */            VUSB_DT_ENDPOINT_MIN_LEN,
        /* .bDescriptorType = */    VUSB_DT_ENDPOINT,
        /* .bEndpointAddress = */   0,
        /* .bmAttributes = */       0,
        /* .wMaxPacketSize = */     64,
        /* .bInterval = */          0
    },
    NULL
};

/** Default configuration. */
const VUSBDESCCONFIGEX g_Config0 =
{
    {
        /* .bLength = */            VUSB_DT_CONFIG_MIN_LEN,
        /* .bDescriptorType = */    VUSB_DT_CONFIG,
        /* .WTotalLength = */       0, /* (auto-calculated) */
        /* .bNumInterfaces = */     0,
        /* .bConfigurationValue =*/ 0,
        /* .iConfiguration = */     0,
        /* .bmAttributes = */       0x80,
        /* .MaxPower = */           14
    },
    NULL,
    NULL
};



static PCVUSBDESCCONFIGEX vusbDevFindCfgDesc(PVUSBDEV pDev, int iCfg)
{
    if (iCfg == 0)
        return &g_Config0;

    for (unsigned i = 0; i < pDev->pDescCache->pDevice->bNumConfigurations; i++)
        if (pDev->pDescCache->paConfigs[i].Core.bConfigurationValue == iCfg)
            return &pDev->pDescCache->paConfigs[i];
    return NULL;
}

static PVUSBINTERFACESTATE vusbDevFindIfState(PVUSBDEV pDev, int iIf)
{
    for (unsigned i = 0; i < pDev->pCurCfgDesc->Core.bNumInterfaces; i++)
        if (pDev->paIfStates[i].pIf->paSettings[0].Core.bInterfaceNumber == iIf)
            return &pDev->paIfStates[i];
    return NULL;
}

static PCVUSBDESCINTERFACEEX vusbDevFindAltIfDesc(PCVUSBINTERFACESTATE pIfState, int iAlt)
{
    for (uint32_t i = 0; i < pIfState->pIf->cSettings; i++)
        if (pIfState->pIf->paSettings[i].Core.bAlternateSetting == iAlt)
            return &pIfState->pIf->paSettings[i];
    return NULL;
}

void vusbDevMapEndpoint(PVUSBDEV pDev, PCVUSBDESCENDPOINTEX pEndPtDesc)
{
    uint8_t i8Addr = pEndPtDesc->Core.bEndpointAddress & 0xF;
    PVUSBPIPE pPipe = &pDev->aPipes[i8Addr];
    LogFlow(("vusbDevMapEndpoint: pDev=%p[%s] pEndPtDesc=%p{.bEndpointAddress=%#x, .bmAttributes=%#x} p=%p stage %s->SETUP\n",
             pDev, pDev->pUsbIns->pszName, pEndPtDesc, pEndPtDesc->Core.bEndpointAddress, pEndPtDesc->Core.bmAttributes,
             pPipe, g_apszCtlStates[pPipe->pCtrl ? pPipe->pCtrl->enmStage : 3]));

    if ((pEndPtDesc->Core.bmAttributes & 0x3) == 0)
    {
        Log(("vusb: map message pipe on address %u\n", i8Addr));
        pPipe->in  = pEndPtDesc;
        pPipe->out = pEndPtDesc;
    }
    else if (pEndPtDesc->Core.bEndpointAddress & 0x80)
    {
        Log(("vusb: map input pipe on address %u\n", i8Addr));
        pPipe->in = pEndPtDesc;
    }
    else
    {
        Log(("vusb: map output pipe on address %u\n", i8Addr));
        pPipe->out = pEndPtDesc;
    }

    if (pPipe->pCtrl)
    {
        vusbMsgFreeExtraData(pPipe->pCtrl);
        pPipe->pCtrl = NULL;
    }
}

static void unmap_endpoint(PVUSBDEV pDev, PCVUSBDESCENDPOINTEX pEndPtDesc)
{
    uint8_t     EndPt = pEndPtDesc->Core.bEndpointAddress & 0xF;
    PVUSBPIPE   pPipe = &pDev->aPipes[EndPt];
    LogFlow(("unmap_endpoint: pDev=%p[%s] pEndPtDesc=%p{.bEndpointAddress=%#x, .bmAttributes=%#x} p=%p stage %s->SETUP\n",
             pDev, pDev->pUsbIns->pszName, pEndPtDesc, pEndPtDesc->Core.bEndpointAddress, pEndPtDesc->Core.bmAttributes,
             pPipe, g_apszCtlStates[pPipe->pCtrl ? pPipe->pCtrl->enmStage : 3]));

    if ((pEndPtDesc->Core.bmAttributes & 0x3) == 0)
    {
        Log(("vusb: unmap MSG pipe from address %u (%#x)\n", EndPt, pEndPtDesc->Core.bEndpointAddress));
        pPipe->in = NULL;
        pPipe->out = NULL;
    }
    else if (pEndPtDesc->Core.bEndpointAddress & 0x80)
    {
        Log(("vusb: unmap IN pipe from address %u (%#x)\n", EndPt, pEndPtDesc->Core.bEndpointAddress));
        pPipe->in = NULL;
    }
    else
    {
        Log(("vusb: unmap OUT pipe from address %u (%#x)\n", EndPt, pEndPtDesc->Core.bEndpointAddress));
        pPipe->out = NULL;
    }

    if (pPipe->pCtrl)
    {
        vusbMsgFreeExtraData(pPipe->pCtrl);
        pPipe->pCtrl = NULL;
    }
}

static void map_interface(PVUSBDEV pDev, PCVUSBDESCINTERFACEEX pIfDesc)
{
    LogFlow(("map_interface: pDev=%p[%s] pIfDesc=%p:{.iInterface=%d, .bAlternateSetting=%d}\n",
             pDev, pDev->pUsbIns->pszName, pIfDesc, pIfDesc->Core.iInterface, pIfDesc->Core.bAlternateSetting));

    for (unsigned i = 0; i < pIfDesc->Core.bNumEndpoints; i++)
    {
        if ((pIfDesc->paEndpoints[i].Core.bEndpointAddress & 0xF) == VUSB_PIPE_DEFAULT)
            Log(("vusb: Endpoint 0x%x on interface %u.%u tried to override the default message pipe!!!\n",
                pIfDesc->paEndpoints[i].Core.bEndpointAddress, pIfDesc->Core.bInterfaceNumber, pIfDesc->Core.bAlternateSetting));
        else
            vusbDevMapEndpoint(pDev, &pIfDesc->paEndpoints[i]);
    }
}


/**
 * Worker that resets the pipe data on select config and detach.
 *
 * This leaves the critical section unmolested
 *
 * @param   pPipe               The pipe which data should be reset.
 */
static void vusbDevResetPipeData(PVUSBPIPE pPipe)
{
    vusbMsgFreeExtraData(pPipe->pCtrl);
    pPipe->pCtrl = NULL;

    RT_ZERO(pPipe->in);
    RT_ZERO(pPipe->out);
    pPipe->async = 0;
}


bool vusbDevDoSelectConfig(PVUSBDEV pDev, PCVUSBDESCCONFIGEX pCfgDesc)
{
    LogFlow(("vusbDevDoSelectConfig: pDev=%p[%s] pCfgDesc=%p:{.iConfiguration=%d}\n",
             pDev, pDev->pUsbIns->pszName, pCfgDesc, pCfgDesc->Core.iConfiguration));

    /*
     * Clean up all pipes and interfaces.
     */
    unsigned i;
    for (i = 0; i < VUSB_PIPE_MAX; i++)
        if (i != VUSB_PIPE_DEFAULT)
            vusbDevResetPipeData(&pDev->aPipes[i]);
    memset(pDev->paIfStates, 0, pCfgDesc->Core.bNumInterfaces * sizeof(pDev->paIfStates[0]));

    /*
     * Map in the default setting for every interface.
     */
    for (i = 0; i < pCfgDesc->Core.bNumInterfaces; i++)
    {
        PCVUSBINTERFACE pIf;
        struct vusb_interface_state *pIfState;

        pIf = &pCfgDesc->paIfs[i];
        pIfState = &pDev->paIfStates[i];
        pIfState->pIf = pIf;

        /*
         * Find the 0 setting, if it is not present we just use
         * the lowest numbered one.
         */
        for (uint32_t j = 0; j < pIf->cSettings; j++)
        {
            if (    !pIfState->pCurIfDesc
                ||  pIf->paSettings[j].Core.bAlternateSetting < pIfState->pCurIfDesc->Core.bAlternateSetting)
                pIfState->pCurIfDesc = &pIf->paSettings[j];
            if (pIfState->pCurIfDesc->Core.bAlternateSetting == 0)
                break;
        }

        if (pIfState->pCurIfDesc)
            map_interface(pDev, pIfState->pCurIfDesc);
    }

    pDev->pCurCfgDesc = pCfgDesc;

    if (pCfgDesc->Core.bmAttributes & 0x40)
        pDev->u16Status |= (1 << VUSB_DEV_SELF_POWERED);
    else
        pDev->u16Status &= ~(1 << VUSB_DEV_SELF_POWERED);

    return true;
}

/**
 * Standard device request: SET_CONFIGURATION
 * @returns success indicator.
 */
static bool vusbDevStdReqSetConfig(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(EndPt, pbBuf, pcbBuf);
    unsigned iCfg = pSetup->wValue & 0xff;

    if ((pSetup->bmRequestType & VUSB_RECIP_MASK) != VUSB_TO_DEVICE)
    {
        Log(("vusb: error: %s: SET_CONFIGURATION - invalid request (dir) !!!\n", pDev->pUsbIns->pszName));
        return false;
    }

    /*
     * Check that the device is in a valid state.
     * (The caller has already checked that it's not being reset.)
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (enmState == VUSB_DEVICE_STATE_DEFAULT)
    {
        LogFlow(("vusbDevStdReqSetConfig: %s: default dev state !!?\n", pDev->pUsbIns->pszName));
        return false;
    }

    PCVUSBDESCCONFIGEX pNewCfgDesc = vusbDevFindCfgDesc(pDev, iCfg);
    if (!pNewCfgDesc)
    {
        Log(("vusb: error: %s: config %i not found !!!\n", pDev->pUsbIns->pszName, iCfg));
        return false;
    }

    if (iCfg == 0)
        vusbDevSetState(pDev, VUSB_DEVICE_STATE_ADDRESS);
    else
        vusbDevSetState(pDev, VUSB_DEVICE_STATE_CONFIGURED);
    if (pDev->pUsbIns->pReg->pfnUsbSetConfiguration)
    {
        RTCritSectEnter(&pDev->pHub->CritSectDevices);
        int rc = vusbDevIoThreadExecSync(pDev, (PFNRT)pDev->pUsbIns->pReg->pfnUsbSetConfiguration, 5,
                                         pDev->pUsbIns, pNewCfgDesc->Core.bConfigurationValue,
                                         pDev->pCurCfgDesc, pDev->paIfStates, pNewCfgDesc);
        RTCritSectLeave(&pDev->pHub->CritSectDevices);
        if (RT_FAILURE(rc))
        {
            Log(("vusb: error: %s: failed to set config %i (%Rrc) !!!\n", pDev->pUsbIns->pszName, iCfg, rc));
            return false;
        }
    }
    Log(("vusb: %p[%s]: SET_CONFIGURATION: Selected config %u\n", pDev, pDev->pUsbIns->pszName, iCfg));
    return vusbDevDoSelectConfig(pDev, pNewCfgDesc);
}


/**
 * Standard device request: GET_CONFIGURATION
 * @returns success indicator.
 */
static bool vusbDevStdReqGetConfig(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(EndPt);
    if ((pSetup->bmRequestType & VUSB_RECIP_MASK) != VUSB_TO_DEVICE)
    {
        Log(("vusb: error: %s: GET_CONFIGURATION - invalid request (dir) !!!\n", pDev->pUsbIns->pszName));
        return false;
    }

    /*
     * Check that the device is in a valid state.
     * (The caller has already checked that it's not being reset.)
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (    enmState != VUSB_DEVICE_STATE_CONFIGURED
        &&  enmState != VUSB_DEVICE_STATE_ADDRESS)
    {
        LogFlow(("vusbDevStdReqGetConfig: error: %s: invalid device state %d!!!\n", pDev->pUsbIns->pszName, enmState));
        return false;
    }

    if (*pcbBuf < 1)
    {
        LogFlow(("vusbDevStdReqGetConfig: %s: no space for data!\n", pDev->pUsbIns->pszName));
        return true;
    }

    uint8_t iCfg;
    if (enmState == VUSB_DEVICE_STATE_ADDRESS)
        iCfg = 0;
    else
        iCfg = pDev->pCurCfgDesc->Core.bConfigurationValue;

    *pbBuf = iCfg;
    *pcbBuf = 1;
    LogFlow(("vusbDevStdReqGetConfig: %s: returns iCfg=%d\n", pDev->pUsbIns->pszName, iCfg));
    return true;
}

/**
 * Standard device request: GET_INTERFACE
 * @returns success indicator.
 */
static bool vusbDevStdReqGetInterface(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(EndPt);
    if ((pSetup->bmRequestType & VUSB_RECIP_MASK) != VUSB_TO_INTERFACE)
    {
        Log(("vusb: error: %s: GET_INTERFACE - invalid request (dir) !!!\n", pDev->pUsbIns->pszName));
        return false;
    }

    /*
     * Check that the device is in a valid state.
     * (The caller has already checked that it's not being reset.)
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (enmState != VUSB_DEVICE_STATE_CONFIGURED)
    {
        LogFlow(("vusbDevStdReqGetInterface: error: %s: invalid device state %d!!!\n", pDev->pUsbIns->pszName, enmState));
        return false;
    }

    if (*pcbBuf < 1)
    {
        LogFlow(("vusbDevStdReqGetInterface: %s: no space for data!\n", pDev->pUsbIns->pszName));
        return true;
    }

    for (unsigned i = 0; i < pDev->pCurCfgDesc->Core.bNumInterfaces; i++)
    {
        PCVUSBDESCINTERFACEEX pIfDesc = pDev->paIfStates[i].pCurIfDesc;
        if (    pIfDesc
            &&  pSetup->wIndex == pIfDesc->Core.bInterfaceNumber)
        {
            *pbBuf = pIfDesc->Core.bAlternateSetting;
            *pcbBuf = 1;
            Log(("vusb: %s: GET_INTERFACE: %u.%u\n", pDev->pUsbIns->pszName, pIfDesc->Core.bInterfaceNumber, *pbBuf));
            return true;
        }
    }

    Log(("vusb: error: %s: GET_INTERFACE - unknown iface %u !!!\n", pDev->pUsbIns->pszName, pSetup->wIndex));
    return false;
}

/**
 * Standard device request: SET_INTERFACE
 * @returns success indicator.
 */
static bool vusbDevStdReqSetInterface(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(EndPt, pbBuf, pcbBuf);
    if ((pSetup->bmRequestType & VUSB_RECIP_MASK) != VUSB_TO_INTERFACE)
    {
        Log(("vusb: error: %s: SET_INTERFACE - invalid request (dir) !!!\n", pDev->pUsbIns->pszName));
        return false;
    }

    /*
     * Check that the device is in a valid state.
     * (The caller has already checked that it's not being reset.)
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (enmState != VUSB_DEVICE_STATE_CONFIGURED)
    {
        LogFlow(("vusbDevStdReqSetInterface: error: %s: invalid device state %d !!!\n", pDev->pUsbIns->pszName, enmState));
        return false;
    }

    /*
     * Find the interface.
     */
    uint8_t iIf = pSetup->wIndex;
    PVUSBINTERFACESTATE pIfState = vusbDevFindIfState(pDev, iIf);
    if (!pIfState)
    {
        LogFlow(("vusbDevStdReqSetInterface: error: %s: couldn't find interface %u !!!\n", pDev->pUsbIns->pszName, iIf));
        return false;
    }
    uint8_t iAlt = pSetup->wValue;
    PCVUSBDESCINTERFACEEX pIfDesc = vusbDevFindAltIfDesc(pIfState, iAlt);
    if (!pIfDesc)
    {
        LogFlow(("vusbDevStdReqSetInterface: error: %s: couldn't find alt interface %u.%u !!!\n", pDev->pUsbIns->pszName, iIf, iAlt));
        return false;
    }

    if (pDev->pUsbIns->pReg->pfnUsbSetInterface)
    {
        RTCritSectEnter(&pDev->pHub->CritSectDevices);
        int rc = vusbDevIoThreadExecSync(pDev, (PFNRT)pDev->pUsbIns->pReg->pfnUsbSetInterface, 3, pDev->pUsbIns, iIf, iAlt);
        RTCritSectLeave(&pDev->pHub->CritSectDevices);
        if (RT_FAILURE(rc))
        {
            LogFlow(("vusbDevStdReqSetInterface: error: %s: couldn't find alt interface %u.%u (%Rrc)\n", pDev->pUsbIns->pszName, iIf, iAlt, rc));
            return false;
        }
    }

    for (unsigned i = 0; i < pIfState->pCurIfDesc->Core.bNumEndpoints; i++)
        unmap_endpoint(pDev, &pIfState->pCurIfDesc->paEndpoints[i]);

    Log(("vusb: SET_INTERFACE: Selected %u.%u\n", iIf, iAlt));

    map_interface(pDev, pIfDesc);
    pIfState->pCurIfDesc = pIfDesc;

    return true;
}

/**
 * Standard device request: SET_ADDRESS
 * @returns success indicator.
 */
static bool vusbDevStdReqSetAddress(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(EndPt, pbBuf, pcbBuf);
    if ((pSetup->bmRequestType & VUSB_RECIP_MASK) != VUSB_TO_DEVICE)
    {
        Log(("vusb: error: %s: SET_ADDRESS - invalid request (dir) !!!\n", pDev->pUsbIns->pszName));
        return false;
    }

    /*
     * Check that the device is in a valid state.
     * (The caller has already checked that it's not being reset.)
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (    enmState != VUSB_DEVICE_STATE_DEFAULT
        &&  enmState != VUSB_DEVICE_STATE_ADDRESS)
    {
        LogFlow(("vusbDevStdReqSetAddress: error: %s: invalid device state %d !!!\n", pDev->pUsbIns->pszName, enmState));
        return false;
    }

    /*
     * If wValue has any bits set beyond 0-6, throw them away.
     */
    if ((pSetup->wValue & VUSB_ADDRESS_MASK) != pSetup->wValue) {
        LogRelMax(10, ("VUSB: %s: Warning: Ignoring high bits of requested address (wLength=0x%X), using only lower 7 bits.\n",
                       pDev->pUsbIns->pszName, pSetup->wValue));

        pSetup->wValue &= VUSB_ADDRESS_MASK;
    }

    pDev->u8NewAddress = pSetup->wValue;
    return true;
}

/**
 * Standard device request: CLEAR_FEATURE
 * @returns success indicator.
 *
 * @remark This is only called for VUSB_TO_ENDPOINT && ep == 0 && wValue == ENDPOINT_HALT.
 *         All other cases of CLEAR_FEATURE is handled in the normal async/sync manner.
 */
static bool vusbDevStdReqClearFeature(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(pbBuf, pcbBuf);
    switch (pSetup->bmRequestType & VUSB_RECIP_MASK)
    {
        case VUSB_TO_DEVICE:
            Log(("vusb: ClearFeature: dev(%u): selector=%u\n", pSetup->wIndex, pSetup->wValue));
            break;
        case VUSB_TO_INTERFACE:
            Log(("vusb: ClearFeature: iface(%u): selector=%u\n", pSetup->wIndex, pSetup->wValue));
            break;
        case VUSB_TO_ENDPOINT:
            Log(("vusb: ClearFeature: ep(%u): selector=%u\n", pSetup->wIndex, pSetup->wValue));
            if (    !EndPt /* Default control pipe only */
                &&  pSetup->wValue == 0 /* ENDPOINT_HALT */
                &&  pDev->pUsbIns->pReg->pfnUsbClearHaltedEndpoint)
            {
                RTCritSectEnter(&pDev->pHub->CritSectDevices);
                int rc = vusbDevIoThreadExecSync(pDev, (PFNRT)pDev->pUsbIns->pReg->pfnUsbClearHaltedEndpoint, 2,
                                                 pDev->pUsbIns, pSetup->wIndex);
                RTCritSectLeave(&pDev->pHub->CritSectDevices);
                return RT_SUCCESS(rc);
            }
            break;
        default:
            AssertMsgFailed(("VUSB_TO_OTHER!\n"));
            break;
    }

    AssertMsgFailed(("Invalid safe check !!!\n"));
    return false;
}

/**
 * Standard device request: SET_FEATURE
 * @returns success indicator.
 */
static bool vusbDevStdReqSetFeature(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(pDev, EndPt, pbBuf, pcbBuf);
    switch (pSetup->bmRequestType & VUSB_RECIP_MASK)
    {
        case VUSB_TO_DEVICE:
            Log(("vusb: SetFeature: dev(%u): selector=%u\n",
                pSetup->wIndex, pSetup->wValue));
            break;
        case VUSB_TO_INTERFACE:
            Log(("vusb: SetFeature: if(%u): selector=%u\n",
                pSetup->wIndex, pSetup->wValue));
            break;
        case VUSB_TO_ENDPOINT:
            Log(("vusb: SetFeature: ep(%u): selector=%u\n",
                pSetup->wIndex, pSetup->wValue));
            break;
        default:
            AssertMsgFailed(("VUSB_TO_OTHER!\n"));
            return false;
    }
    AssertMsgFailed(("This stuff is bogus\n"));
    return false;
}

static bool vusbDevStdReqGetStatus(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(EndPt);
    if (*pcbBuf != 2)
    {
        LogFlow(("vusbDevStdReqGetStatus: %s: buffer is too small! (%d)\n", pDev->pUsbIns->pszName, *pcbBuf));
        return false;
    }

    uint16_t u16Status;
    switch (pSetup->bmRequestType & VUSB_RECIP_MASK)
    {
        case VUSB_TO_DEVICE:
            u16Status = pDev->u16Status;
            LogFlow(("vusbDevStdReqGetStatus: %s: device status %#x (%d)\n", pDev->pUsbIns->pszName, u16Status, u16Status));
            break;
        case VUSB_TO_INTERFACE:
            u16Status = 0;
            LogFlow(("vusbDevStdReqGetStatus: %s: bogus interface status request!!\n", pDev->pUsbIns->pszName));
            break;
        case VUSB_TO_ENDPOINT:
            u16Status = 0;
            LogFlow(("vusbDevStdReqGetStatus: %s: bogus endpoint status request!!\n", pDev->pUsbIns->pszName));
            break;
        default:
            AssertMsgFailed(("VUSB_TO_OTHER!\n"));
            return false;
    }

    *(uint16_t *)pbBuf = u16Status;
    return true;
}


/**
 * Finds a cached string.
 *
 * @returns Pointer to the cached string if found.  NULL if not.
 * @param   paLanguages         The languages to search.
 * @param   cLanguages          The number of languages in the table.
 * @param   idLang              The language ID.
 * @param   iString             The string index.
 */
static PCPDMUSBDESCCACHESTRING FindCachedString(PCPDMUSBDESCCACHELANG paLanguages, unsigned cLanguages,
                                                uint16_t idLang, uint8_t iString)
{
    /** @todo binary lookups! */
    unsigned iCurLang = cLanguages;
    while (iCurLang-- > 0)
        if (paLanguages[iCurLang].idLang == idLang)
        {
            PCPDMUSBDESCCACHESTRING paStrings = paLanguages[iCurLang].paStrings;
            unsigned                iCurStr   = paLanguages[iCurLang].cStrings;
            while (iCurStr-- > 0)
                if (paStrings[iCurStr].idx == iString)
                    return &paStrings[iCurStr];
            break;
        }
    return NULL;
}


/** Macro for copying descriptor data. */
#define COPY_DATA(pbDst, cbLeft, pvSrc, cbSrc) \
    do { \
        uint32_t cbSrc_ = cbSrc; \
        uint32_t cbCopy = RT_MIN(cbLeft, cbSrc_); \
        if (cbCopy) \
            memcpy(pbBuf, pvSrc, cbCopy); \
        cbLeft -= cbCopy; \
        if (!cbLeft) \
            return; \
        pbBuf += cbCopy; \
    } while (0)

/**
 * Internal function for reading the language IDs.
 */
static void ReadCachedStringDesc(PCPDMUSBDESCCACHESTRING pString, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    uint32_t        cbLeft = *pcbBuf;

    RTUTF16         wsz[128];           /* 128-1 => bLength=0xff */
    PRTUTF16        pwsz = wsz;
    size_t          cwc;
    int rc = RTStrToUtf16Ex(pString->psz, RT_ELEMENTS(wsz) - 1, &pwsz, RT_ELEMENTS(wsz), &cwc);
    if (RT_FAILURE(rc))
    {
        AssertRC(rc);
        wsz[0] = 'e';
        wsz[1] = 'r';
        wsz[2] = 'r';
        cwc = 3;
    }

    VUSBDESCSTRING  StringDesc;
    StringDesc.bLength          = (uint8_t)(sizeof(StringDesc) + cwc * sizeof(RTUTF16));
    StringDesc.bDescriptorType  = VUSB_DT_STRING;
    COPY_DATA(pbBuf, cbLeft, &StringDesc, sizeof(StringDesc));
    COPY_DATA(pbBuf, cbLeft, wsz, (uint32_t)cwc * sizeof(RTUTF16));

    /* updated the size of the output buffer. */
    *pcbBuf -= cbLeft;
}


/**
 * Internal function for reading the language IDs.
 */
static void ReadCachedLangIdDesc(PCPDMUSBDESCCACHELANG paLanguages, unsigned cLanguages,
                                 uint8_t *pbBuf, uint32_t *pcbBuf)
{
    uint32_t        cbLeft      = *pcbBuf;

    VUSBDESCLANGID  LangIdDesc;
    size_t          cbDesc      = sizeof(LangIdDesc) + cLanguages * sizeof(paLanguages[0].idLang);
    LangIdDesc.bLength          = (uint8_t)RT_MIN(0xff, cbDesc);
    LangIdDesc.bDescriptorType  = VUSB_DT_STRING;
    COPY_DATA(pbBuf, cbLeft, &LangIdDesc, sizeof(LangIdDesc));

    unsigned iLanguage = cLanguages;
    while (iLanguage-- > 0)
        COPY_DATA(pbBuf, cbLeft, &paLanguages[iLanguage].idLang, sizeof(paLanguages[iLanguage].idLang));

    /* updated the size of the output buffer. */
    *pcbBuf -= cbLeft;
}


/**
 * Internal function which performs a descriptor read on the cached descriptors.
 */
static void ReadCachedConfigDesc(PCVUSBDESCCONFIGEX pCfgDesc, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    uint32_t cbLeft = *pcbBuf;

    /*
     * Make a copy of the config descriptor and calculate the wTotalLength field.
     */
    VUSBDESCCONFIG CfgDesc;
    memcpy(&CfgDesc, pCfgDesc, VUSB_DT_CONFIG_MIN_LEN);
    uint32_t cbTotal = 0;
    cbTotal += pCfgDesc->Core.bLength;
    cbTotal += pCfgDesc->cbClass;
    for (unsigned i = 0; i < pCfgDesc->Core.bNumInterfaces; i++)
    {
        PCVUSBINTERFACE pIf = &pCfgDesc->paIfs[i];
        for (uint32_t j = 0; j < pIf->cSettings; j++)
        {
            cbTotal += pIf->paSettings[j].cbIAD;
            cbTotal += pIf->paSettings[j].Core.bLength;
            cbTotal += pIf->paSettings[j].cbClass;
            for (unsigned k = 0; k < pIf->paSettings[j].Core.bNumEndpoints; k++)
            {
                cbTotal += pIf->paSettings[j].paEndpoints[k].Core.bLength;
                cbTotal += pIf->paSettings[j].paEndpoints[k].cbSsepc;
                cbTotal += pIf->paSettings[j].paEndpoints[k].cbClass;
            }
        }
    }
    CfgDesc.wTotalLength = RT_H2LE_U16(cbTotal);

    /*
     * Copy the config descriptor
     */
    COPY_DATA(pbBuf, cbLeft, &CfgDesc, VUSB_DT_CONFIG_MIN_LEN);
    COPY_DATA(pbBuf, cbLeft, pCfgDesc->pvMore, pCfgDesc->Core.bLength - VUSB_DT_CONFIG_MIN_LEN);
    COPY_DATA(pbBuf, cbLeft, pCfgDesc->pvClass, pCfgDesc->cbClass);

    /*
     * Copy out all the interfaces for this configuration
     */
    for (unsigned i = 0; i < pCfgDesc->Core.bNumInterfaces; i++)
    {
        PCVUSBINTERFACE pIf = &pCfgDesc->paIfs[i];
        for (uint32_t j = 0; j < pIf->cSettings; j++)
        {
            PCVUSBDESCINTERFACEEX pIfDesc = &pIf->paSettings[j];

            COPY_DATA(pbBuf, cbLeft, pIfDesc->pIAD, pIfDesc->cbIAD);
            COPY_DATA(pbBuf, cbLeft, pIfDesc, VUSB_DT_INTERFACE_MIN_LEN);
            COPY_DATA(pbBuf, cbLeft, pIfDesc->pvMore, pIfDesc->Core.bLength - VUSB_DT_INTERFACE_MIN_LEN);
            COPY_DATA(pbBuf, cbLeft, pIfDesc->pvClass, pIfDesc->cbClass);

            /*
             * Copy out all the endpoints for this interface
             */
            for (unsigned k = 0; k < pIfDesc->Core.bNumEndpoints; k++)
            {
                VUSBDESCENDPOINT EndPtDesc;
                memcpy(&EndPtDesc, &pIfDesc->paEndpoints[k], VUSB_DT_ENDPOINT_MIN_LEN);
                EndPtDesc.wMaxPacketSize = RT_H2LE_U16(EndPtDesc.wMaxPacketSize);

                COPY_DATA(pbBuf, cbLeft, &EndPtDesc, VUSB_DT_ENDPOINT_MIN_LEN);
                COPY_DATA(pbBuf, cbLeft, pIfDesc->paEndpoints[k].pvMore, EndPtDesc.bLength - VUSB_DT_ENDPOINT_MIN_LEN);
                COPY_DATA(pbBuf, cbLeft, pIfDesc->paEndpoints[k].pvSsepc, pIfDesc->paEndpoints[k].cbSsepc);
                COPY_DATA(pbBuf, cbLeft, pIfDesc->paEndpoints[k].pvClass, pIfDesc->paEndpoints[k].cbClass);
            }
        }
    }

    /* updated the size of the output buffer. */
    *pcbBuf -= cbLeft;
}

/**
 * Internal function which performs a descriptor read on the cached descriptors.
 */
static void ReadCachedDeviceDesc(PCVUSBDESCDEVICE pDevDesc, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    uint32_t cbLeft = *pcbBuf;

    /*
     * Duplicate the device description and update some fields we keep in cpu type.
     */
    Assert(sizeof(VUSBDESCDEVICE) == 18);
    VUSBDESCDEVICE DevDesc = *pDevDesc;
    DevDesc.bcdUSB    = RT_H2LE_U16(DevDesc.bcdUSB);
    DevDesc.idVendor  = RT_H2LE_U16(DevDesc.idVendor);
    DevDesc.idProduct = RT_H2LE_U16(DevDesc.idProduct);
    DevDesc.bcdDevice = RT_H2LE_U16(DevDesc.bcdDevice);

    COPY_DATA(pbBuf, cbLeft, &DevDesc, sizeof(DevDesc));
    COPY_DATA(pbBuf, cbLeft, pDevDesc + 1, pDevDesc->bLength - sizeof(DevDesc));

    /* updated the size of the output buffer. */
    *pcbBuf -= cbLeft;
}

#undef COPY_DATA

/**
 * Checks whether a descriptor read can be satisfied by reading from the
 * descriptor cache or has to be passed to the device.
 * If we have descriptors cached, it is generally safe to satisfy descriptor reads
 * from the cache. As usual, there is broken USB software and hardware out there
 * and guests might try to read a nonexistent desciptor (out of range index for
 * string or configuration descriptor) and rely on it not failing.
 * Since we cannot very well guess if such invalid requests should really succeed,
 * and what exactly should happen if they do, we pass such requests to the device.
 * If the descriptor was cached because it was edited, and the guest bypasses the
 * edited cache by reading a descriptor with an invalid index, it is probably
 * best to smash the USB device with a large hammer.
 *
 * See @bugref{10016}.
 *
 * @returns false if request must be passed to device.
 */
bool vusbDevIsDescriptorInCache(PVUSBDEV pDev, PCVUSBSETUP pSetup)
{
    unsigned int iIndex = (pSetup->wValue & 0xff);
    Assert(pSetup->bRequest == VUSB_REQ_GET_DESCRIPTOR);

    if ((pSetup->bmRequestType & VUSB_RECIP_MASK) == VUSB_TO_DEVICE)
    {
        if (pDev->pDescCache->fUseCachedDescriptors)
        {
            switch (pSetup->wValue >> 8)
            {
            case VUSB_DT_DEVICE:
                if (iIndex == 0)
                    return true;

                LogRelMax(10, ("VUSB: %s: Warning: Reading device descriptor with non-zero index %u (wLength=%u), passing request to device\n",
                               pDev->pUsbIns->pszName, iIndex, pSetup->wLength));
                break;

            case VUSB_DT_CONFIG:
                if (iIndex < pDev->pDescCache->pDevice->bNumConfigurations)
                    return true;

                LogRelMax(10, ("VUSB: %s: Warning: Reading configuration descriptor invalid index %u (bNumConfigurations=%u, wLength=%u), passing request to device\n",
                               pDev->pUsbIns->pszName, iIndex, pDev->pDescCache->pDevice->bNumConfigurations, pSetup->wLength));
                break;

            case VUSB_DT_STRING:
                if (pDev->pDescCache->fUseCachedStringsDescriptors)
                {
                    if (pSetup->wIndex == 0)    /* Language IDs. */
                        return true;

                    if (FindCachedString(pDev->pDescCache->paLanguages, pDev->pDescCache->cLanguages,
                                         pSetup->wIndex, iIndex))
                        return true;
                }
                break;

            default:
                break;
            }
            Log(("VUSB: %s: Descriptor not cached: type=%u descidx=%u lang=%u len=%u, passing request to device\n",
                 pDev->pUsbIns->pszName, pSetup->wValue >> 8, iIndex, pSetup->wIndex, pSetup->wLength));
        }
    }
    return false;
}


/**
 * Standard device request: GET_DESCRIPTOR
 * @returns success indicator.
 */
static bool vusbDevStdReqGetDescriptor(PVUSBDEV pDev, int EndPt, PVUSBSETUP pSetup, uint8_t *pbBuf, uint32_t *pcbBuf)
{
    RT_NOREF(EndPt);
    if ((pSetup->bmRequestType & VUSB_RECIP_MASK) == VUSB_TO_DEVICE)
    {
        switch (pSetup->wValue >> 8)
        {
            case VUSB_DT_DEVICE:
                ReadCachedDeviceDesc(pDev->pDescCache->pDevice, pbBuf, pcbBuf);
                LogFlow(("vusbDevStdReqGetDescriptor: %s: %u bytes of device descriptors\n", pDev->pUsbIns->pszName, *pcbBuf));
                return true;

            case VUSB_DT_CONFIG:
            {
                unsigned int iIndex = (pSetup->wValue & 0xff);
                if (iIndex >= pDev->pDescCache->pDevice->bNumConfigurations)
                {
                    LogFlow(("vusbDevStdReqGetDescriptor: %s: iIndex=%u >= bNumConfigurations=%d !!!\n",
                             pDev->pUsbIns->pszName, iIndex, pDev->pDescCache->pDevice->bNumConfigurations));
                    return false;
                }
                ReadCachedConfigDesc(&pDev->pDescCache->paConfigs[iIndex], pbBuf, pcbBuf);
                LogFlow(("vusbDevStdReqGetDescriptor: %s: %u bytes of config descriptors\n", pDev->pUsbIns->pszName, *pcbBuf));
                return true;
            }

            case VUSB_DT_STRING:
            {
                if (pSetup->wIndex == 0)
                {
                    ReadCachedLangIdDesc(pDev->pDescCache->paLanguages, pDev->pDescCache->cLanguages, pbBuf, pcbBuf);
                    LogFlow(("vusbDevStdReqGetDescriptor: %s: %u bytes of language ID (string) descriptors\n", pDev->pUsbIns->pszName, *pcbBuf));
                    return true;
                }
                PCPDMUSBDESCCACHESTRING pString;
                pString = FindCachedString(pDev->pDescCache->paLanguages, pDev->pDescCache->cLanguages,
                                           pSetup->wIndex, pSetup->wValue & 0xff);
                if (pString)
                {
                    ReadCachedStringDesc(pString, pbBuf, pcbBuf);
                    LogFlow(("vusbDevStdReqGetDescriptor: %s: %u bytes of string descriptors \"%s\"\n",
                             pDev->pUsbIns->pszName, *pcbBuf, pString->psz));
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
    Log(("vusb: %s: warning: unknown descriptor: type=%u descidx=%u lang=%u len=%u!!!\n",
         pDev->pUsbIns->pszName, pSetup->wValue >> 8, pSetup->wValue & 0xff, pSetup->wIndex, pSetup->wLength));
    return false;
}


/**
 * Service the standard USB requests.
 *
 * Devices may call this from controlmsg() if you want vusb core to handle your standard
 * request, it's not necessary - you could handle them manually
 *
 * @param   pDev        The device.
 * @param   EndPoint    The endpoint.
 * @param   pSetup      Pointer to the setup request structure.
 * @param   pvBuf       Buffer?
 * @param   pcbBuf      ?
 */
bool vusbDevStandardRequest(PVUSBDEV pDev, int EndPoint, PVUSBSETUP pSetup, void *pvBuf, uint32_t *pcbBuf)
{
    static bool (* const s_apfnStdReq[VUSB_REQ_MAX])(PVUSBDEV, int, PVUSBSETUP, uint8_t *, uint32_t *) =
    {
        vusbDevStdReqGetStatus,
        vusbDevStdReqClearFeature,
        NULL,
        vusbDevStdReqSetFeature,
        NULL,
        vusbDevStdReqSetAddress,
        vusbDevStdReqGetDescriptor,
        NULL,
        vusbDevStdReqGetConfig,
        vusbDevStdReqSetConfig,
        vusbDevStdReqGetInterface,
        vusbDevStdReqSetInterface,
        NULL /* for iso */
    };

    /*
     * Check that the device is in a valid state.
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (enmState == VUSB_DEVICE_STATE_RESET)
    {
        LogRel(("VUSB: %s: standard control message ignored, the device is resetting\n", pDev->pUsbIns->pszName));
        return false;
    }

    /*
     * Do the request if it's one we want to deal with.
     */
    if (    pSetup->bRequest >= VUSB_REQ_MAX
        ||  !s_apfnStdReq[pSetup->bRequest])
    {
        Log(("vusb: warning: standard req not implemented: message %u: val=%u idx=%u len=%u !!!\n",
             pSetup->bRequest, pSetup->wValue, pSetup->wIndex, pSetup->wLength));
        return false;
    }

    return s_apfnStdReq[pSetup->bRequest](pDev, EndPoint, pSetup, (uint8_t *)pvBuf, pcbBuf);
}


/**
 * Sets the address of a device.
 *
 * Called by status_completion() and vusbDevResetWorker().
 */
void vusbDevSetAddress(PVUSBDEV pDev, uint8_t u8Address)
{
    LogFlow(("vusbDevSetAddress: pDev=%p[%s]/%i u8Address=%#x\n",
             pDev, pDev->pUsbIns->pszName, pDev->i16Port, u8Address));

    /*
     * Check that the device is in a valid state.
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    VUSBDEV_ASSERT_VALID_STATE(enmState);
    if (    enmState == VUSB_DEVICE_STATE_ATTACHED
        ||  enmState == VUSB_DEVICE_STATE_DETACHED)
    {
        LogFlow(("vusbDevSetAddress: %s: fails because %d < POWERED\n", pDev->pUsbIns->pszName, pDev->enmState));
        return;
    }
    if (enmState == VUSB_DEVICE_STATE_RESET)
    {
        LogRel(("VUSB: %s: set address ignored, the device is resetting\n", pDev->pUsbIns->pszName));
        return;
    }

    /* Paranoia. */
    Assert((u8Address & VUSB_ADDRESS_MASK) == u8Address);
    u8Address &= VUSB_ADDRESS_MASK;

    /*
     * Ok, get on with it.
     */
    if (pDev->u8Address == u8Address)
        return;

    /** @todo The following logic belongs to the roothub and should actually be in that file. */
    PVUSBROOTHUB pRh = vusbDevGetRh(pDev);
    AssertPtrReturnVoid(pRh);

    RTCritSectEnter(&pRh->CritSectDevices);

    /* Remove the device from the current address. */
    if (pDev->u8Address != VUSB_INVALID_ADDRESS)
    {
        Assert(pRh->apDevByAddr[pDev->u8Address] == pDev);
        pRh->apDevByAddr[pDev->u8Address] = NULL;
    }

    if (u8Address == VUSB_DEFAULT_ADDRESS)
    {
        PVUSBDEV pDevDef = pRh->apDevByAddr[VUSB_DEFAULT_ADDRESS];

        if (pDevDef)
        {
            pDevDef->u8Address = VUSB_INVALID_ADDRESS;
            pDevDef->u8NewAddress = VUSB_INVALID_ADDRESS;
            vusbDevSetStateCmp(pDevDef, VUSB_DEVICE_STATE_POWERED, VUSB_DEVICE_STATE_DEFAULT);
            Log(("2 DEFAULT ADDRS\n"));
        }

        pRh->apDevByAddr[VUSB_DEFAULT_ADDRESS] = pDev;
        vusbDevSetState(pDev, VUSB_DEVICE_STATE_DEFAULT);
    }
    else
    {
        Assert(!pRh->apDevByAddr[u8Address]);
        pRh->apDevByAddr[u8Address] = pDev;
        vusbDevSetState(pDev, VUSB_DEVICE_STATE_ADDRESS);
    }

    pDev->u8Address = u8Address;
    RTCritSectLeave(&pRh->CritSectDevices);

    Log(("vusb: %p[%s]/%i: Assigned address %u\n",
         pDev, pDev->pUsbIns->pszName, pDev->i16Port, u8Address));
}


static DECLCALLBACK(int) vusbDevCancelAllUrbsWorker(PVUSBDEV pDev, bool fDetaching)
{
    /*
     * Iterate the URBs and cancel them.
     */
    PVUSBURBVUSB pVUsbUrb, pVUsbUrbNext;
    RTListForEachSafe(&pDev->LstAsyncUrbs, pVUsbUrb, pVUsbUrbNext, VUSBURBVUSBINT, NdLst)
    {
        PVUSBURB pUrb = pVUsbUrb->pUrb;

        Assert(pUrb->pVUsb->pDev == pDev);

        LogFlow(("%s: vusbDevCancelAllUrbs: CANCELING URB\n", pUrb->pszDesc));
        int rc = vusbUrbCancelWorker(pUrb, CANCELMODE_FAIL);
        AssertRC(rc);
    }

    /*
     * Reap any URBs which became ripe during cancel now.
     */
    RTCritSectEnter(&pDev->CritSectAsyncUrbs);
    unsigned cReaped;
    do
    {
        cReaped = 0;
        pVUsbUrb = RTListGetFirst(&pDev->LstAsyncUrbs, VUSBURBVUSBINT, NdLst);
        while (pVUsbUrb)
        {
            PVUSBURBVUSB pNext = RTListGetNext(&pDev->LstAsyncUrbs, pVUsbUrb, VUSBURBVUSBINT, NdLst);
            PVUSBURB pUrb = pVUsbUrb->pUrb;
            Assert(pUrb->pVUsb->pDev == pDev);

            PVUSBURB pRipe = NULL;
            if (pUrb->enmState == VUSBURBSTATE_REAPED)
                pRipe = pUrb;
            else if (pUrb->enmState == VUSBURBSTATE_CANCELLED)
#ifdef RT_OS_WINDOWS   /** @todo Windows doesn't do cancelling, thus this kludge to prevent really bad
                    * things from happening if we leave a pending URB behinds. */
                pRipe = pDev->pUsbIns->pReg->pfnUrbReap(pDev->pUsbIns, fDetaching ? 1500 : 0 /*ms*/);
#else
                pRipe = pDev->pUsbIns->pReg->pfnUrbReap(pDev->pUsbIns, fDetaching ? 10 : 0 /*ms*/);
#endif
            else
                AssertMsgFailed(("pUrb=%p enmState=%d\n", pUrb, pUrb->enmState));
            if (pRipe)
            {
                if (   pNext
                    && pRipe == pNext->pUrb)
                    pNext = RTListGetNext(&pDev->LstAsyncUrbs, pNext, VUSBURBVUSBINT, NdLst);
                vusbUrbRipe(pRipe);
                cReaped++;
            }

            pVUsbUrb = pNext;
        }
    } while (cReaped > 0);

    /*
     * If we're detaching, we'll have to orphan any leftover URBs.
     */
    if (fDetaching)
    {
        RTListForEachSafe(&pDev->LstAsyncUrbs, pVUsbUrb, pVUsbUrbNext, VUSBURBVUSBINT, NdLst)
        {
            PVUSBURB pUrb = pVUsbUrb->pUrb;
            Assert(pUrb->pVUsb->pDev == pDev);

            AssertMsgFailed(("%s: Leaking left over URB! state=%d pDev=%p[%s]\n",
                             pUrb->pszDesc, pUrb->enmState, pDev, pDev->pUsbIns->pszName));
            vusbUrbUnlink(pUrb);
            /* Unlink isn't enough, because boundary timer and detaching will try to reap it.
             * It was tested with MSD & iphone attachment to vSMP guest, if
             * it breaks anything, please add comment here, why we should unlink only.
             */
            pUrb->pVUsb->pfnFree(pUrb);
        }
    }
    RTCritSectLeave(&pDev->CritSectAsyncUrbs);
    return VINF_SUCCESS;
}

/**
 * Cancels and completes (with CRC failure) all async URBs pending
 * on a device. This is typically done as part of a reset and
 * before detaching a device.
 *
 * @param   pDev        The VUSB device instance.
 * @param   fDetaching  If set, we will unconditionally unlink (and leak)
 *                      any URBs which isn't reaped.
 */
DECLHIDDEN(void) vusbDevCancelAllUrbs(PVUSBDEV pDev, bool fDetaching)
{
    int rc = vusbDevIoThreadExecSync(pDev, (PFNRT)vusbDevCancelAllUrbsWorker, 2, pDev, fDetaching);
    AssertRC(rc);
}


static DECLCALLBACK(int) vusbDevUrbIoThread(RTTHREAD hThread, void *pvUser)
{
    PVUSBDEV pDev = (PVUSBDEV)pvUser;

    /* Notify the starter that we are up and running. */
    RTThreadUserSignal(hThread);

    LogFlowFunc(("Entering work loop\n"));

    while (!ASMAtomicReadBool(&pDev->fTerminate))
    {
        if (vusbDevGetState(pDev) != VUSB_DEVICE_STATE_RESET)
            vusbUrbDoReapAsyncDev(pDev, RT_INDEFINITE_WAIT);

        /* Process any URBs waiting to be cancelled first. */
        int rc = RTReqQueueProcess(pDev->hReqQueueSync, 0); /* Don't wait if there is nothing to do. */
        Assert(RT_SUCCESS(rc) || rc == VERR_TIMEOUT); NOREF(rc);
    }

    return VINF_SUCCESS;
}

int vusbDevUrbIoThreadWakeup(PVUSBDEV pDev)
{
    ASMAtomicXchgBool(&pDev->fWokenUp, true);
    return pDev->pUsbIns->pReg->pfnWakeup(pDev->pUsbIns);
}

/**
 * Create the URB I/O thread.
 *
 * @returns VBox status code.
 * @param   pDev    The VUSB device.
 */
int vusbDevUrbIoThreadCreate(PVUSBDEV pDev)
{
    int rc = VINF_SUCCESS;

    ASMAtomicXchgBool(&pDev->fTerminate, false);
    rc = RTThreadCreateF(&pDev->hUrbIoThread, vusbDevUrbIoThread, pDev, 0, RTTHREADTYPE_IO,
                         RTTHREADFLAGS_WAITABLE, "USBDevIo-%d", pDev->i16Port);
    if (RT_SUCCESS(rc))
    {
        /* Wait for it to become active. */
        rc = RTThreadUserWait(pDev->hUrbIoThread, RT_INDEFINITE_WAIT);
    }

    return rc;
}

/**
 * Destro the URB I/O thread.
 *
 * @returns VBox status code.
 * @param   pDev    The VUSB device.
 */
int vusbDevUrbIoThreadDestroy(PVUSBDEV pDev)
{
    int rc = VINF_SUCCESS;
    int rcThread = VINF_SUCCESS;

    ASMAtomicXchgBool(&pDev->fTerminate, true);
    vusbDevUrbIoThreadWakeup(pDev);

    rc = RTThreadWait(pDev->hUrbIoThread, RT_INDEFINITE_WAIT, &rcThread);
    if (RT_SUCCESS(rc))
        rc = rcThread;

    pDev->hUrbIoThread = NIL_RTTHREAD;

    return rc;
}


/**
 * Attaches a device to the given hub.
 *
 * @returns VBox status code.
 * @param   pDev        The device to attach.
 * @param   pHub        The roothub to attach to.
 */
int vusbDevAttach(PVUSBDEV pDev, PVUSBROOTHUB pHub)
{
    AssertMsg(pDev->enmState == VUSB_DEVICE_STATE_DETACHED, ("enmState=%d\n", pDev->enmState));

    pDev->pHub = pHub;
    pDev->enmState = VUSB_DEVICE_STATE_ATTACHED;

    /* noone else ever messes with the default pipe while we are attached */
    vusbDevMapEndpoint(pDev, &g_Endpoint0);
    vusbDevDoSelectConfig(pDev, &g_Config0);

    /* Create I/O thread and attach to the hub. */
    int rc = vusbDevUrbIoThreadCreate(pDev);
    if (RT_FAILURE(rc))
    {
        pDev->pHub = NULL;
        pDev->enmState = VUSB_DEVICE_STATE_DETACHED;
    }

    return rc;
}


/**
 * Detaches a device from the hub it's attached to.
 *
 * @returns VBox status code.
 * @param   pDev        The device to detach.
 *
 * @remark  This can be called in any state but reset.
 */
int vusbDevDetach(PVUSBDEV pDev)
{
    LogFlow(("vusbDevDetach: pDev=%p[%s] enmState=%#x\n", pDev, pDev->pUsbIns->pszName, pDev->enmState));
    VUSBDEV_ASSERT_VALID_STATE(pDev->enmState);
    Assert(pDev->enmState != VUSB_DEVICE_STATE_RESET);

    /*
     * Destroy I/O thread and request queue last because they might still be used
     * when cancelling URBs.
     */
    vusbDevUrbIoThreadDestroy(pDev);

    vusbDevSetState(pDev, VUSB_DEVICE_STATE_DETACHED);
    pDev->pHub = NULL;

    /* Remove the configuration */
    pDev->pCurCfgDesc = NULL;
    for (unsigned i = 0; i < RT_ELEMENTS(pDev->aPipes); i++)
        vusbDevResetPipeData(&pDev->aPipes[i]);
    return VINF_SUCCESS;
}


/**
 * Destroys a device, detaching it from the hub if necessary.
 *
 * @param   pDev    The device.
 * @thread any.
 */
void vusbDevDestroy(PVUSBDEV pDev)
{
    LogFlow(("vusbDevDestroy: pDev=%p[%s] enmState=%d\n", pDev, pDev->pUsbIns->pszName, pDev->enmState));

    RTMemFree(pDev->paIfStates);

    PDMUsbHlpTimerDestroy(pDev->pUsbIns, pDev->hResetTimer);
    pDev->hResetTimer = NIL_TMTIMERHANDLE;

    for (unsigned i = 0; i < RT_ELEMENTS(pDev->aPipes); i++)
    {
        Assert(pDev->aPipes[i].pCtrl == NULL);
        RTCritSectDelete(&pDev->aPipes[i].CritSectCtrl);
    }

    if (pDev->hSniffer != VUSBSNIFFER_NIL)
        VUSBSnifferDestroy(pDev->hSniffer);

    vusbUrbPoolDestroy(&pDev->UrbPool);

    int rc = RTReqQueueDestroy(pDev->hReqQueueSync);
    AssertRC(rc);
    pDev->hReqQueueSync = NIL_RTREQQUEUE;

    RTCritSectDelete(&pDev->CritSectAsyncUrbs);
    /* Not using vusbDevSetState() deliberately here because it would assert on the state. */
    pDev->enmState = VUSB_DEVICE_STATE_DESTROYED;
    pDev->pUsbIns->pvVUsbDev2 = NULL;
    RTMemFree(pDev);
}


/* -=-=-=-=-=- VUSBIDEVICE methods -=-=-=-=-=- */


/**
 * The actual reset has been done, do completion on EMT.
 *
 * There are several things we have to do now, like set default
 * config and address, and cleanup the state of control pipes.
 *
 * It's possible that the device has a delayed destroy request
 * pending when we get here. This can happen for async resetting.
 * We deal with it here, since we're now executing on the EMT
 * thread and the destruction will be properly serialized now.
 *
 * @param   pDev    The device that is being reset.
 * @param   rc      The vusbDevResetWorker return code.
 * @param   pfnDone The done callback specified by the caller of vusbDevReset().
 * @param   pvUser  The user argument for the callback.
 */
static void vusbDevResetDone(PVUSBDEV pDev, int rc, PFNVUSBRESETDONE pfnDone, void *pvUser)
{
    VUSBDEV_ASSERT_VALID_STATE(pDev->enmState);
    Assert(pDev->enmState == VUSB_DEVICE_STATE_RESET);

    /*
     * Do control pipe cleanup regardless of state and result.
     */
    for (unsigned i = 0; i < VUSB_PIPE_MAX; i++)
        if (pDev->aPipes[i].pCtrl)
            vusbMsgResetExtraData(pDev->aPipes[i].pCtrl);

    /*
     * Switch to the default state.
     */
    vusbDevSetState(pDev, VUSB_DEVICE_STATE_DEFAULT);
    pDev->u16Status = 0;
    vusbDevDoSelectConfig(pDev, &g_Config0);
    vusbDevSetAddress(pDev, VUSB_DEFAULT_ADDRESS);
    if (pfnDone)
        pfnDone(&pDev->IDevice, pDev->i16Port, rc, pvUser);
}


/**
 * @callback_method_impl{FNTMTIMERUSB,
 *          Timer callback for doing reset completion.}
 */
static DECLCALLBACK(void) vusbDevResetDoneTimer(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PVUSBDEV        pDev  = (PVUSBDEV)pvUser;
    PVUSBRESETARGS  pArgs = (PVUSBRESETARGS)pDev->pvArgs;
    Assert(pDev->pUsbIns == pUsbIns);
    RT_NOREF(pUsbIns, hTimer);

    AssertPtr(pArgs);

    /*
     * Reset-done processing and cleanup.
     */
    pDev->pvArgs = NULL;
    vusbDevResetDone(pDev, pArgs->rc, pArgs->pfnDone, pArgs->pvUser);
    RTMemFree(pArgs);
}


/**
 * Perform the actual reset.
 *
 * @thread EMT or a VUSB reset thread.
 */
static DECLCALLBACK(int) vusbDevResetWorker(PVUSBDEV pDev, bool fResetOnLinux, bool fUseTimer, PVUSBRESETARGS pArgs)
{
    uint64_t const uTimerDeadline = !fUseTimer ? 0
                                  :   PDMUsbHlpTimerGet(pDev->pUsbIns, pDev->hResetTimer)
                                    + PDMUsbHlpTimerFromMilli(pDev->pUsbIns, pDev->hResetTimer, 10);

    int rc = VINF_SUCCESS;
    if (pDev->pUsbIns->pReg->pfnUsbReset)
        rc = pDev->pUsbIns->pReg->pfnUsbReset(pDev->pUsbIns, fResetOnLinux);

    if (pArgs)
    {
        pArgs->rc = rc;
        rc = VINF_SUCCESS;
    }

    if (fUseTimer)
    {
        /*
         * We use a timer to communicate the result back to EMT.
         * This avoids suspend + poweroff issues, and it should give
         * us more accurate scheduling than making this thread sleep.
         */
        int rc2 = PDMUsbHlpTimerSet(pDev->pUsbIns, pDev->hResetTimer, uTimerDeadline);
        AssertReleaseRC(rc2);
    }

    LogFlow(("vusbDevResetWorker: %s: returns %Rrc\n", pDev->pUsbIns->pszName, rc));
    return rc;
}


/**
 * Resets a device.
 *
 * Since a device reset shall take at least 10ms from the guest point of view,
 * it must be performed asynchronously.  We create a thread which performs this
 * operation and ensures it will take at least 10ms.
 *
 * At times - like init - a synchronous reset is required, this can be done
 * by passing NULL for pfnDone.
 *
 * While the device is being reset it is in the VUSB_DEVICE_STATE_RESET state.
 * On completion it will be in the VUSB_DEVICE_STATE_DEFAULT state if successful,
 * or in the VUSB_DEVICE_STATE_DETACHED state if the rest failed.
 *
 * @returns VBox status code.
 *
 * @param   pDevice         Pointer to the VUSB device interface.
 * @param   fResetOnLinux   Whether it's safe to reset the device(s) on a linux
 *                          host system. See discussion of logical reconnects elsewhere.
 * @param   pfnDone         Pointer to the completion routine. If NULL a synchronous
 *                          reset is preformed not respecting the 10ms.
 * @param   pvUser          Opaque user data to pass to the done callback.
 * @param   pVM             Pointer to the VM handle for performing the done function
 *                          on the EMT thread.
 * @thread  EMT
 */
static DECLCALLBACK(int) vusbIDeviceReset(PVUSBIDEVICE pDevice, bool fResetOnLinux,
                                          PFNVUSBRESETDONE pfnDone, void *pvUser, PVM pVM)
{
    RT_NOREF(pVM);
    PVUSBDEV pDev = (PVUSBDEV)pDevice;
    Assert(!pfnDone || pVM);
    LogFlow(("vusb: reset: [%s]/%i\n", pDev->pUsbIns->pszName, pDev->i16Port));

    /*
     * Only one reset operation at a time.
     */
    const VUSBDEVICESTATE enmStateOld = vusbDevSetState(pDev, VUSB_DEVICE_STATE_RESET);
    if (enmStateOld == VUSB_DEVICE_STATE_RESET)
    {
        LogRel(("VUSB: %s: reset request is ignored, the device is already resetting!\n", pDev->pUsbIns->pszName));
        return VERR_VUSB_DEVICE_IS_RESETTING;
    }

    /*
     * First, cancel all async URBs.
     */
    vusbDevCancelAllUrbs(pDev, false);

    /* Async or sync? */
    if (pfnDone)
    {
        /*
         * Async fashion.
         */
        PVUSBRESETARGS pArgs = (PVUSBRESETARGS)RTMemTmpAlloc(sizeof(*pArgs));
        if (pArgs)
        {
            pArgs->pDev    = pDev;
            pArgs->pfnDone = pfnDone;
            pArgs->pvUser  = pvUser;
            pArgs->rc      = VINF_SUCCESS;
            AssertPtrNull(pDev->pvArgs);
            pDev->pvArgs   = pArgs;
            int rc = vusbDevIoThreadExec(pDev, 0 /* fFlags */, (PFNRT)vusbDevResetWorker, 4, pDev, fResetOnLinux, true, pArgs);
            if (RT_SUCCESS(rc))
                return rc;

            RTMemTmpFree(pArgs);
        }
        /* fall back to sync on failure */
    }

    /*
     * Sync fashion.
     */
    int rc = vusbDevResetWorker(pDev, fResetOnLinux, false, NULL);
    vusbDevResetDone(pDev, rc, pfnDone, pvUser);
    return rc;
}


/**
 * Powers on the device.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the device interface structure.
 */
static DECLCALLBACK(int) vusbIDevicePowerOn(PVUSBIDEVICE pInterface)
{
    PVUSBDEV pDev = (PVUSBDEV)pInterface;
    LogFlow(("vusbDevPowerOn: pDev=%p[%s]\n", pDev, pDev->pUsbIns->pszName));

    /*
     * Check that the device is in a valid state.
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (enmState == VUSB_DEVICE_STATE_DETACHED)
    {
        Log(("vusb: warning: attempt to power on detached device %p[%s]\n", pDev, pDev->pUsbIns->pszName));
        return VERR_VUSB_DEVICE_NOT_ATTACHED;
    }
    if (enmState == VUSB_DEVICE_STATE_RESET)
    {
        LogRel(("VUSB: %s: power on ignored, the device is resetting!\n", pDev->pUsbIns->pszName));
        return VERR_VUSB_DEVICE_IS_RESETTING;
    }

    /*
     * Do the job.
     */
    if (enmState == VUSB_DEVICE_STATE_ATTACHED)
        vusbDevSetState(pDev, VUSB_DEVICE_STATE_POWERED);

    return VINF_SUCCESS;
}


/**
 * Powers off the device.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the device interface structure.
 */
static DECLCALLBACK(int) vusbIDevicePowerOff(PVUSBIDEVICE pInterface)
{
    PVUSBDEV pDev = (PVUSBDEV)pInterface;
    LogFlow(("vusbDevPowerOff: pDev=%p[%s]\n", pDev, pDev->pUsbIns->pszName));

    /*
     * Check that the device is in a valid state.
     */
    const VUSBDEVICESTATE enmState = vusbDevGetState(pDev);
    if (enmState == VUSB_DEVICE_STATE_DETACHED)
    {
        Log(("vusb: warning: attempt to power off detached device %p[%s]\n", pDev, pDev->pUsbIns->pszName));
        return VERR_VUSB_DEVICE_NOT_ATTACHED;
    }
    if (enmState == VUSB_DEVICE_STATE_RESET)
    {
        LogRel(("VUSB: %s: power off ignored, the device is resetting!\n", pDev->pUsbIns->pszName));
        return VERR_VUSB_DEVICE_IS_RESETTING;
    }

    vusbDevSetState(pDev, VUSB_DEVICE_STATE_ATTACHED);
    return VINF_SUCCESS;
}


/**
 * Get the state of the device.
 *
 * @returns Device state.
 * @param   pInterface      Pointer to the device interface structure.
 */
static DECLCALLBACK(VUSBDEVICESTATE) vusbIDeviceGetState(PVUSBIDEVICE pInterface)
{
    return vusbDevGetState((PVUSBDEV)pInterface);
}


/**
 * @interface_method_impl{VUSBIDEVICE,pfnIsSavedStateSupported}
 */
static DECLCALLBACK(bool) vusbIDeviceIsSavedStateSupported(PVUSBIDEVICE pInterface)
{
    PVUSBDEV pDev = (PVUSBDEV)pInterface;
    bool fSavedStateSupported = RT_BOOL(pDev->pUsbIns->pReg->fFlags & PDM_USBREG_SAVED_STATE_SUPPORTED);

    LogFlowFunc(("pInterface=%p\n", pInterface));

    LogFlowFunc(("returns %RTbool\n", fSavedStateSupported));
    return fSavedStateSupported;
}


/**
 * @interface_method_impl{VUSBIDEVICE,pfnGetState}
 */
static DECLCALLBACK(VUSBSPEED) vusbIDeviceGetSpeed(PVUSBIDEVICE pInterface)
{
    PVUSBDEV pDev = (PVUSBDEV)pInterface;
    VUSBSPEED enmSpeed = pDev->pUsbIns->enmSpeed;

    LogFlowFunc(("pInterface=%p, returns %u\n", pInterface, enmSpeed));
    return enmSpeed;
}


/**
 * The maximum number of interfaces the device can have in all of it's configuration.
 *
 * @returns Number of interfaces.
 * @param   pDev        The device.
 */
size_t vusbDevMaxInterfaces(PVUSBDEV pDev)
{
    uint8_t cMax = 0;
    unsigned i = pDev->pDescCache->pDevice->bNumConfigurations;
    while (i-- > 0)
    {
        if (pDev->pDescCache->paConfigs[i].Core.bNumInterfaces > cMax)
            cMax = pDev->pDescCache->paConfigs[i].Core.bNumInterfaces;
    }

    return cMax;
}


/**
 * Executes a given function on the I/O thread.
 *
 * @returns IPRT status code.
 * @param   pDev           The USB device instance data.
 * @param   fFlags         Combination of VUSB_DEV_IO_THREAD_EXEC_FLAGS_*
 * @param   pfnFunction    The function to execute.
 * @param   cArgs          Number of arguments to the function.
 * @param   Args           The parameter list.
 *
 * @remarks See remarks on RTReqQueueCallV
 */
DECLHIDDEN(int) vusbDevIoThreadExecV(PVUSBDEV pDev, uint32_t fFlags, PFNRT pfnFunction, unsigned cArgs, va_list Args)
{
    int rc = VINF_SUCCESS;
    PRTREQ hReq = NULL;

    Assert(pDev->hUrbIoThread != NIL_RTTHREAD);
    if (RT_LIKELY(pDev->hUrbIoThread != NIL_RTTHREAD))
    {
        uint32_t fReqFlags = RTREQFLAGS_IPRT_STATUS;

        if (!(fFlags & VUSB_DEV_IO_THREAD_EXEC_FLAGS_SYNC))
            fReqFlags |= RTREQFLAGS_NO_WAIT;

        rc = RTReqQueueCallV(pDev->hReqQueueSync, &hReq, 0 /* cMillies */, fReqFlags, pfnFunction, cArgs, Args);
        Assert(RT_SUCCESS(rc) || rc == VERR_TIMEOUT);

        /* In case we are called on the I/O thread just process the request. */
        if (   pDev->hUrbIoThread == RTThreadSelf()
            && (fFlags & VUSB_DEV_IO_THREAD_EXEC_FLAGS_SYNC))
        {
            int rc2 = RTReqQueueProcess(pDev->hReqQueueSync, 0);
            Assert(RT_SUCCESS(rc2) || rc2 == VERR_TIMEOUT); NOREF(rc2);
        }
        else
            vusbDevUrbIoThreadWakeup(pDev);

        if (   rc == VERR_TIMEOUT
            && (fFlags & VUSB_DEV_IO_THREAD_EXEC_FLAGS_SYNC))
        {
            rc = RTReqWait(hReq, RT_INDEFINITE_WAIT);
            AssertRC(rc);
        }
        RTReqRelease(hReq);
    }
    else
        rc = VERR_INVALID_STATE;

    return rc;
}


/**
 * Executes a given function on the I/O thread.
 *
 * @returns IPRT status code.
 * @param   pDev           The USB device instance data.
 * @param   fFlags         Combination of VUSB_DEV_IO_THREAD_EXEC_FLAGS_*
 * @param   pfnFunction    The function to execute.
 * @param   cArgs          Number of arguments to the function.
 * @param   ...            The parameter list.
 *
 * @remarks See remarks on RTReqQueueCallV
 */
DECLHIDDEN(int) vusbDevIoThreadExec(PVUSBDEV pDev, uint32_t fFlags, PFNRT pfnFunction, unsigned cArgs, ...)
{
    int rc = VINF_SUCCESS;
    va_list va;

    va_start(va, cArgs);
    rc = vusbDevIoThreadExecV(pDev, fFlags, pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}


/**
 * Executes a given function synchronously on the I/O thread waiting for it to complete.
 *
 * @returns IPRT status code.
 * @param   pDev           The USB device instance data
 * @param   pfnFunction    The function to execute.
 * @param   cArgs          Number of arguments to the function.
 * @param   ...            The parameter list.
 *
 * @remarks See remarks on RTReqQueueCallV
 */
DECLHIDDEN(int) vusbDevIoThreadExecSync(PVUSBDEV pDev, PFNRT pfnFunction, unsigned cArgs, ...)
{
    int rc = VINF_SUCCESS;
    va_list va;

    va_start(va, cArgs);
    rc = vusbDevIoThreadExecV(pDev, VUSB_DEV_IO_THREAD_EXEC_FLAGS_SYNC, pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}


/**
 * Initialize a new VUSB device.
 *
 * @returns VBox status code.
 * @param   pDev                  The VUSB device to initialize.
 * @param   pUsbIns               Pointer to the PDM USB Device instance.
 * @param   pszCaptureFilename    Optional fileame to capture the traffic to.
 */
int vusbDevInit(PVUSBDEV pDev, PPDMUSBINS pUsbIns, const char *pszCaptureFilename)
{
    /*
     * Initialize the device data members.
     * (All that are Non-Zero at least.)
     */
    Assert(!pDev->IDevice.pfnReset);
    Assert(!pDev->IDevice.pfnPowerOn);
    Assert(!pDev->IDevice.pfnPowerOff);
    Assert(!pDev->IDevice.pfnGetState);
    Assert(!pDev->IDevice.pfnIsSavedStateSupported);

    pDev->IDevice.pfnReset = vusbIDeviceReset;
    pDev->IDevice.pfnPowerOn = vusbIDevicePowerOn;
    pDev->IDevice.pfnPowerOff = vusbIDevicePowerOff;
    pDev->IDevice.pfnGetState = vusbIDeviceGetState;
    pDev->IDevice.pfnIsSavedStateSupported = vusbIDeviceIsSavedStateSupported;
    pDev->IDevice.pfnGetSpeed = vusbIDeviceGetSpeed;
    pDev->pUsbIns = pUsbIns;
    pDev->pHub = NULL;
    pDev->enmState = VUSB_DEVICE_STATE_DETACHED;
    pDev->cRefs = 1;
    pDev->u8Address = VUSB_INVALID_ADDRESS;
    pDev->u8NewAddress = VUSB_INVALID_ADDRESS;
    pDev->i16Port = -1;
    pDev->u16Status = 0;
    pDev->pDescCache = NULL;
    pDev->pCurCfgDesc = NULL;
    pDev->paIfStates = NULL;
    RTListInit(&pDev->LstAsyncUrbs);
    memset(&pDev->aPipes[0], 0, sizeof(pDev->aPipes));
    for (unsigned i = 0; i < RT_ELEMENTS(pDev->aPipes); i++)
    {
        int rc = RTCritSectInit(&pDev->aPipes[i].CritSectCtrl);
        AssertRCReturn(rc, rc);
    }
    pDev->hResetTimer = NIL_TMTIMERHANDLE;
    pDev->hSniffer = VUSBSNIFFER_NIL;

    int rc = RTCritSectInit(&pDev->CritSectAsyncUrbs);
    AssertRCReturn(rc, rc);

    /* Create the URB pool. */
    rc = vusbUrbPoolInit(&pDev->UrbPool);
    AssertRCReturn(rc, rc);

    /* Setup request queue executing synchronous tasks on the I/O thread. */
    rc = RTReqQueueCreate(&pDev->hReqQueueSync);
    AssertRCReturn(rc, rc);

    /*
     * Create the reset timer.  Make sure the name is unique as we're generic code.
     */
    static uint32_t volatile s_iSeq;
    char                     szDesc[32];
    RTStrPrintf(szDesc, sizeof(szDesc), "VUSB Reset #%u", ASMAtomicIncU32(&s_iSeq));
    rc = PDMUsbHlpTimerCreate(pDev->pUsbIns, TMCLOCK_VIRTUAL, vusbDevResetDoneTimer, pDev, 0 /*fFlags*/,
                              szDesc, &pDev->hResetTimer);
    AssertRCReturn(rc, rc);

    if (pszCaptureFilename)
    {
        rc = VUSBSnifferCreate(&pDev->hSniffer, 0, pszCaptureFilename, NULL, NULL);
        AssertRCReturn(rc, rc);
    }

    /*
     * Get the descriptor cache from the device. (shall cannot fail)
     */
    pDev->pDescCache = pUsbIns->pReg->pfnUsbGetDescriptorCache(pUsbIns);
    AssertPtr(pDev->pDescCache);
#ifdef VBOX_STRICT
    if (pDev->pDescCache->fUseCachedStringsDescriptors)
    {
        int32_t iPrevId = -1;
        for (unsigned iLang = 0; iLang < pDev->pDescCache->cLanguages; iLang++)
        {
            Assert((int32_t)pDev->pDescCache->paLanguages[iLang].idLang > iPrevId);
            iPrevId = pDev->pDescCache->paLanguages[iLang].idLang;

            int32_t                 idxPrevStr = -1;
            PCPDMUSBDESCCACHESTRING paStrings  = pDev->pDescCache->paLanguages[iLang].paStrings;
            unsigned                cStrings   = pDev->pDescCache->paLanguages[iLang].cStrings;
            for (unsigned iStr = 0; iStr < cStrings; iStr++)
            {
                Assert((int32_t)paStrings[iStr].idx > idxPrevStr);
                idxPrevStr = paStrings[iStr].idx;
                size_t cch = strlen(paStrings[iStr].psz);
                Assert(cch <= 127);
            }
        }
    }
#endif

    /*
     * Allocate memory for the interface states.
     */
    size_t cbIface = vusbDevMaxInterfaces(pDev) * sizeof(*pDev->paIfStates);
    pDev->paIfStates = (PVUSBINTERFACESTATE)RTMemAllocZ(cbIface);
    AssertMsgReturn(pDev->paIfStates, ("RTMemAllocZ(%d) failed\n", cbIface), VERR_NO_MEMORY);

    return VINF_SUCCESS;
}

/*
 * Local Variables:
 *  mode: c
 *  c-file-style: "bsd"
 *  c-basic-offset: 4
 *  tab-width: 4
 *  indent-tabs-mode: s
 * End:
 */

