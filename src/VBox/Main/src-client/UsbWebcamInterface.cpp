/* $Id: UsbWebcamInterface.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbWebcamInterface - Driver Interface for USB Webcam emulation.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


#define LOG_GROUP LOG_GROUP_USB_WEBCAM
#include "LoggingNew.h"

#include "UsbWebcamInterface.h"
#include "ConsoleImpl.h"
#include "ConsoleVRDPServer.h"
#include "EmulatedUSBImpl.h"

#include <VBox/vmm/pdmwebcaminfs.h>
#include <VBox/err.h>


typedef struct EMWEBCAMREMOTE
{
    EmWebcam *pEmWebcam;

    VRDEVIDEOINDEVICEHANDLE deviceHandle; /* The remote identifier. */

    /* Received from the remote client. */
    uint32_t u32Version;                  /* VRDE_VIDEOIN_NEGOTIATE_VERSION */
    uint32_t fu32Capabilities;            /* VRDE_VIDEOIN_NEGOTIATE_CAP_* */
    VRDEVIDEOINDEVICEDESC *pDeviceDesc;
    uint32_t cbDeviceDesc;

    /* The device identifier for the PDM device.*/
    uint64_t u64DeviceId;
} EMWEBCAMREMOTE;

typedef struct EMWEBCAMDRV
{
    EMWEBCAMREMOTE *pRemote;
    PPDMIWEBCAMDEV  pIWebcamUp;
    PDMIWEBCAMDRV   IWebcamDrv;
} EMWEBCAMDRV, *PEMWEBCAMDRV;

typedef struct EMWEBCAMREQCTX
{
    EMWEBCAMREMOTE *pRemote;
    void *pvUser;
} EMWEBCAMREQCTX;


static DECLCALLBACK(void) drvEmWebcamReady(PPDMIWEBCAMDRV pInterface,
                                           bool fReady)
{
    NOREF(fReady);

    PEMWEBCAMDRV pThis = RT_FROM_MEMBER(pInterface, EMWEBCAMDRV, IWebcamDrv);
    EMWEBCAMREMOTE *pRemote = pThis->pRemote;

    LogFlowFunc(("pRemote:%p\n", pThis->pRemote));

    if (pThis->pIWebcamUp)
    {
        pThis->pIWebcamUp->pfnAttached(pThis->pIWebcamUp,
                                       pRemote->u64DeviceId,
                                       pRemote->pDeviceDesc,
                                       pRemote->cbDeviceDesc,
                                       pRemote->u32Version,
                                       pRemote->fu32Capabilities);
    }
}

static DECLCALLBACK(int) drvEmWebcamControl(PPDMIWEBCAMDRV pInterface,
                                            void *pvUser,
                                            uint64_t u64DeviceId,
                                            const struct VRDEVIDEOINCTRLHDR *pCtrl,
                                            uint32_t cbCtrl)
{
    PEMWEBCAMDRV pThis = RT_FROM_MEMBER(pInterface, EMWEBCAMDRV, IWebcamDrv);
    EMWEBCAMREMOTE *pRemote = pThis->pRemote;

    LogFlowFunc(("pRemote:%p, u64DeviceId %lld\n", pRemote, u64DeviceId));

    return pRemote->pEmWebcam->SendControl(pThis, pvUser, u64DeviceId, pCtrl, cbCtrl);
}


EmWebcam::EmWebcam(ConsoleVRDPServer *pServer)
    :
    mParent(pServer),
    mpDrv(NULL),
    mpRemote(NULL),
    mu64DeviceIdSrc(0)
{
}

EmWebcam::~EmWebcam()
{
    if (mpDrv)
    {
        mpDrv->pRemote = NULL;
        mpDrv = NULL;
    }
}

void EmWebcam::EmWebcamConstruct(EMWEBCAMDRV *pDrv)
{
    AssertReturnVoid(mpDrv == NULL);

    mpDrv = pDrv;
}

void EmWebcam::EmWebcamDestruct(EMWEBCAMDRV *pDrv)
{
    AssertReturnVoid(pDrv == mpDrv);

    if (mpRemote)
    {
        mParent->VideoInDeviceDetach(&mpRemote->deviceHandle);

        RTMemFree(mpRemote->pDeviceDesc);
        mpRemote->pDeviceDesc = NULL;
        mpRemote->cbDeviceDesc = 0;

        RTMemFree(mpRemote);
        mpRemote = NULL;
    }

    mpDrv->pRemote = NULL;
    mpDrv = NULL;
}

void EmWebcam::EmWebcamCbNotify(uint32_t u32Id, const void *pvData, uint32_t cbData)
{
    int vrc = VINF_SUCCESS;

    switch (u32Id)
    {
        case VRDE_VIDEOIN_NOTIFY_ID_ATTACH:
        {
            VRDEVIDEOINNOTIFYATTACH *p = (VRDEVIDEOINNOTIFYATTACH *)pvData;

            /* Older versions did not report u32Version and fu32Capabilities. */
            uint32_t u32Version = 1;
            uint32_t fu32Capabilities = VRDE_VIDEOIN_NEGOTIATE_CAP_VOID;

            if (cbData >= RT_UOFFSETOF(VRDEVIDEOINNOTIFYATTACH, u32Version) + sizeof(p->u32Version))
                u32Version = p->u32Version;

            if (cbData >= RT_UOFFSETOF(VRDEVIDEOINNOTIFYATTACH, fu32Capabilities) + sizeof(p->fu32Capabilities))
                fu32Capabilities = p->fu32Capabilities;

            LogFlowFunc(("ATTACH[%d,%d] version %d, caps 0x%08X\n",
                         p->deviceHandle.u32ClientId, p->deviceHandle.u32DeviceId,
                         u32Version, fu32Capabilities));

            /* Currently only one device is allowed. */
            if (mpRemote)
            {
                AssertFailed();
                vrc = VERR_NOT_SUPPORTED;
                break;
            }

            EMWEBCAMREMOTE *pRemote = (EMWEBCAMREMOTE *)RTMemAllocZ(sizeof(EMWEBCAMREMOTE));
            if (pRemote == NULL)
            {
                vrc = VERR_NO_MEMORY;
                break;
            }

            pRemote->pEmWebcam        = this;
            pRemote->deviceHandle     = p->deviceHandle;
            pRemote->u32Version       = u32Version;
            pRemote->fu32Capabilities = fu32Capabilities;
            pRemote->pDeviceDesc      = NULL;
            pRemote->cbDeviceDesc     = 0;
            pRemote->u64DeviceId      = ASMAtomicIncU64(&mu64DeviceIdSrc);

            mpRemote = pRemote;

            /* Tell the server that this webcam will be used. */
            vrc = mParent->VideoInDeviceAttach(&mpRemote->deviceHandle, mpRemote);
            if (RT_FAILURE(vrc))
            {
                RTMemFree(mpRemote);
                mpRemote = NULL;
                break;
            }

            /* Get the device description. */
            vrc = mParent->VideoInGetDeviceDesc(NULL, &mpRemote->deviceHandle);

            if (RT_FAILURE(vrc))
            {
                mParent->VideoInDeviceDetach(&mpRemote->deviceHandle);
                RTMemFree(mpRemote);
                mpRemote = NULL;
                break;
            }

            LogFlowFunc(("sent DeviceDesc\n"));
        } break;

        case VRDE_VIDEOIN_NOTIFY_ID_DETACH:
        {
            VRDEVIDEOINNOTIFYDETACH *p = (VRDEVIDEOINNOTIFYDETACH *)pvData; NOREF(p);
            Assert(cbData == sizeof(VRDEVIDEOINNOTIFYDETACH));

            LogFlowFunc(("DETACH[%d,%d]\n", p->deviceHandle.u32ClientId, p->deviceHandle.u32DeviceId));

            /** @todo */
            if (mpRemote)
            {
                if (mpDrv && mpDrv->pIWebcamUp)
                    mpDrv->pIWebcamUp->pfnDetached(mpDrv->pIWebcamUp, mpRemote->u64DeviceId);
                /* mpRemote is deallocated in EmWebcamDestruct */
            }
        } break;

        default:
            vrc = VERR_INVALID_PARAMETER;
            AssertFailed();
            break;
    }

    return;
}

void EmWebcam::EmWebcamCbDeviceDesc(int rcRequest, void *pDeviceCtx, void *pvUser,
                                    const VRDEVIDEOINDEVICEDESC *pDeviceDesc, uint32_t cbDeviceDesc)
{
    RT_NOREF(pvUser);
    EMWEBCAMREMOTE *pRemote = (EMWEBCAMREMOTE *)pDeviceCtx;
    Assert(pRemote == mpRemote);

    LogFlowFunc(("mpDrv %p, rcRequest %Rrc %p %p %p %d\n",
                 mpDrv, rcRequest, pDeviceCtx, pvUser, pDeviceDesc, cbDeviceDesc));

    if (RT_SUCCESS(rcRequest))
    {
        /* Save device description. */
        Assert(pRemote->pDeviceDesc == NULL);
        pRemote->pDeviceDesc = (VRDEVIDEOINDEVICEDESC *)RTMemDup(pDeviceDesc, cbDeviceDesc);
        pRemote->cbDeviceDesc = cbDeviceDesc;

        /* Try to attach the device. */
        EmulatedUSB *pEUSB = mParent->getConsole()->i_getEmulatedUSB();
        pEUSB->i_webcamAttachInternal("", "", "EmWebcam", pRemote);
    }
    else
    {
        mParent->VideoInDeviceDetach(&mpRemote->deviceHandle);
        RTMemFree(mpRemote);
        mpRemote = NULL;
    }
}

void EmWebcam::EmWebcamCbControl(int rcRequest, void *pDeviceCtx, void *pvUser,
                                 const VRDEVIDEOINCTRLHDR *pControl, uint32_t cbControl)
{
    RT_NOREF(rcRequest);
    EMWEBCAMREMOTE *pRemote = (EMWEBCAMREMOTE *)pDeviceCtx; NOREF(pRemote);
    Assert(pRemote == mpRemote);

    LogFlowFunc(("rcRequest %Rrc %p %p %p %d\n",
                 rcRequest, pDeviceCtx, pvUser, pControl, cbControl));

    bool fResponse = (pvUser != NULL);

    if (mpDrv && mpDrv->pIWebcamUp)
    {
        mpDrv->pIWebcamUp->pfnControl(mpDrv->pIWebcamUp,
                                      fResponse,
                                      pvUser,
                                      mpRemote->u64DeviceId,
                                      pControl,
                                      cbControl);
    }

    RTMemFree(pvUser);
}

void EmWebcam::EmWebcamCbFrame(int rcRequest, void *pDeviceCtx,
                               const VRDEVIDEOINPAYLOADHDR *pFrame, uint32_t cbFrame)
{
    RT_NOREF(rcRequest, pDeviceCtx);
    LogFlowFunc(("rcRequest %Rrc %p %p %d\n",
                 rcRequest, pDeviceCtx, pFrame, cbFrame));

    if (mpDrv && mpDrv->pIWebcamUp)
    {
        if (   cbFrame >= sizeof(VRDEVIDEOINPAYLOADHDR)
            && cbFrame >= pFrame->u8HeaderLength)
        {
            uint32_t cbImage = cbFrame - pFrame->u8HeaderLength;
            const uint8_t *pu8Image = cbImage > 0? (const uint8_t *)pFrame + pFrame->u8HeaderLength: NULL;

            mpDrv->pIWebcamUp->pfnFrame(mpDrv->pIWebcamUp,
                                        mpRemote->u64DeviceId,
                                        pFrame,
                                        pFrame->u8HeaderLength,
                                        pu8Image,
                                        cbImage);
        }
    }
}

int EmWebcam::SendControl(EMWEBCAMDRV *pDrv, void *pvUser, uint64_t u64DeviceId,
                          const VRDEVIDEOINCTRLHDR *pControl, uint32_t cbControl)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc = VINF_SUCCESS;

    EMWEBCAMREQCTX *pCtx = NULL;

    /* Verify that there is a remote device. */
    if (   !mpRemote
        || mpRemote->u64DeviceId != u64DeviceId)
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    if (RT_SUCCESS(vrc))
    {
        pCtx = (EMWEBCAMREQCTX *)RTMemAlloc(sizeof(EMWEBCAMREQCTX));
        if (!pCtx)
        {
            vrc = VERR_NO_MEMORY;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        pCtx->pRemote = mpRemote;
        pCtx->pvUser = pvUser;

        vrc = mParent->VideoInControl(pCtx, &mpRemote->deviceHandle, pControl, cbControl);

        if (RT_FAILURE(vrc))
        {
            RTMemFree(pCtx);
        }
    }

    return vrc;
}

/* static */ DECLCALLBACK(void *) EmWebcam::drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PEMWEBCAMDRV pThis = PDMINS_2_DATA(pDrvIns, PEMWEBCAMDRV);

    LogFlowFunc(("pszIID:%s\n", pszIID));

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIWEBCAMDRV, &pThis->IWebcamDrv);
    return NULL;
}

/* static */ DECLCALLBACK(void) EmWebcam::drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PEMWEBCAMDRV pThis = PDMINS_2_DATA(pDrvIns, PEMWEBCAMDRV);
    EMWEBCAMREMOTE *pRemote = pThis->pRemote;

    LogFlowFunc(("iInstance %d, pRemote %p, pIWebcamUp %p\n",
                 pDrvIns->iInstance, pRemote, pThis->pIWebcamUp));

    if (pRemote && pRemote->pEmWebcam)
    {
        pRemote->pEmWebcam->EmWebcamDestruct(pThis);
    }
}

/* static */ DECLCALLBACK(int) EmWebcam::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    LogFlowFunc(("iInstance:%d, pCfg:%p, fFlags:%x\n", pDrvIns->iInstance, pCfg, fFlags));

    PEMWEBCAMDRV pThis = PDMINS_2_DATA(pDrvIns, PEMWEBCAMDRV);

    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /* Check early that there is a device. No need to init anything if there is no device. */
    pThis->pIWebcamUp = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIWEBCAMDEV);
    if (pThis->pIWebcamUp == NULL)
    {
        LogRel(("USBWEBCAM: Emulated webcam device does not exist.\n"));
        return VERR_PDM_MISSING_INTERFACE;
    }

    char *pszId = NULL;
    int vrc = pDrvIns->pHlpR3->pfnCFGMQueryStringAlloc(pCfg, "Id", &pszId);
    if (RT_SUCCESS(vrc))
    {
        RTUUID UuidEmulatedUsbIf;
        vrc = RTUuidFromStr(&UuidEmulatedUsbIf, EMULATEDUSBIF_OID); AssertRC(vrc);

        PEMULATEDUSBIF pEmulatedUsbIf = (PEMULATEDUSBIF)PDMDrvHlpQueryGenericUserObject(pDrvIns, &UuidEmulatedUsbIf);
        AssertPtrReturn(pEmulatedUsbIf, VERR_INVALID_PARAMETER);

        vrc = pEmulatedUsbIf->pfnQueryEmulatedUsbDataById(pEmulatedUsbIf->pvUser, pszId,
                                                          NULL /*ppvEmUsbCb*/, NULL /*ppvEmUsbCbData*/, (void **)&pThis->pRemote);
        pDrvIns->pHlpR3->pfnMMHeapFree(pDrvIns, pszId);
        AssertRCReturn(vrc, vrc);
    }
    else
        return vrc;

    /* Everything ok. Initialize. */
    pThis->pRemote->pEmWebcam->EmWebcamConstruct(pThis);

    pDrvIns->IBase.pfnQueryInterface = drvQueryInterface;

    pThis->IWebcamDrv.pfnReady   = drvEmWebcamReady;
    pThis->IWebcamDrv.pfnControl = drvEmWebcamControl;

    return VINF_SUCCESS;
}

/* static */ const PDMDRVREG EmWebcam::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName[32] */
    "EmWebcam",
    /* szRCMod[32] */
    "",
    /* szR0Mod[32] */
    "",
    /* pszDescription */
    "Main Driver communicating with VRDE",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass */
    PDM_DRVREG_CLASS_USB,
    /* cMaxInstances */
    1,
    /* cbInstance */
    sizeof(EMWEBCAMDRV),
    /* pfnConstruct */
    EmWebcam::drvConstruct,
    /* pfnDestruct */
    EmWebcam::drvDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32VersionEnd */
    PDM_DRVREG_VERSION
};
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
