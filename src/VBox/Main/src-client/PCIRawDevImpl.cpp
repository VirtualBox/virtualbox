/* $Id: PCIRawDevImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Driver Interface to raw PCI device.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_DEV_PCI_RAW
#include "LoggingNew.h"

#include "PCIRawDevImpl.h"
#include "PCIDeviceAttachmentImpl.h"
#include "ConsoleImpl.h"

// generated header for events
#include "VBoxEvents.h"

#include <VBox/err.h>


/**
 * PCI raw driver instance data.
 */
typedef struct DRVMAINPCIRAWDEV
{
    /** Pointer to the real PCI raw object. */
    PCIRawDev                   *pPCIRawDev;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Our PCI device connector interface. */
    PDMIPCIRAWCONNECTOR         IConnector;

} DRVMAINPCIRAWDEV, *PDRVMAINPCIRAWDEV;

//
// constructor / destructor
//
PCIRawDev::PCIRawDev(Console *console)
  : mParent(console),
    mpDrv(NULL)
{
}

PCIRawDev::~PCIRawDev()
{
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *) PCIRawDev::drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS         pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINPCIRAWDEV  pThis   = PDMINS_2_DATA(pDrvIns, PDRVMAINPCIRAWDEV);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,            &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIPCIRAWCONNECTOR, &pThis->IConnector);

    return NULL;
}


/**
 * @interface_method_impl{PDMIPCIRAWCONNECTOR,pfnDeviceConstructComplete}
 */
DECLCALLBACK(int) PCIRawDev::drvDeviceConstructComplete(PPDMIPCIRAWCONNECTOR pInterface, const char *pcszName,
                                                        uint32_t uHostPCIAddress, uint32_t uGuestPCIAddress,
                                                        int vrc)
{
    PDRVMAINPCIRAWDEV pThis = RT_FROM_CPP_MEMBER(pInterface, DRVMAINPCIRAWDEV, IConnector);
    Console *pConsole = pThis->pPCIRawDev->getParent();
    const ComPtr<IMachine>& machine = pConsole->i_machine();
    ComPtr<IVirtualBox> vbox;

    HRESULT hrc = machine->COMGETTER(Parent)(vbox.asOutParam());
    Assert(SUCCEEDED(hrc)); NOREF(hrc);

    ComPtr<IEventSource> es;
    hrc = vbox->COMGETTER(EventSource)(es.asOutParam());
    Assert(SUCCEEDED(hrc));

    Bstr bstrId;
    hrc = machine->COMGETTER(Id)(bstrId.asOutParam());
    Assert(SUCCEEDED(hrc));

    ComObjPtr<PCIDeviceAttachment> pda;
    BstrFmt bstrName(pcszName);
    pda.createObject();
    pda->init(machine, bstrName, uHostPCIAddress, uGuestPCIAddress, TRUE);

    Bstr msg("");
    if (RT_FAILURE(vrc))
        msg.printf("runtime error %Rrc", vrc);

    ::FireHostPCIDevicePlugEvent(es, bstrId.raw(), true /* plugged */, RT_SUCCESS_NP(vrc) /* success */, pda, msg.raw());

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDRVREG,pfnReset}
 */
DECLCALLBACK(void) PCIRawDev::drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINPCIRAWDEV pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINPCIRAWDEV);

    if (pThis->pPCIRawDev)
        pThis->pPCIRawDev->mpDrv = NULL;
}


/**
 * @interface_method_impl{PDMDRVREG,pfnConstruct}
 */
DECLCALLBACK(int) PCIRawDev::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVMAINPCIRAWDEV pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINPCIRAWDEV);

    /*
     * Validate configuration.
     */
    if (!CFGMR3AreValuesValid(pCfgHandle, "Object\0"))
        return VERR_PDM_DRVINS_UNKNOWN_CFG_VALUES;

    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * IBase.
     */
    pDrvIns->IBase.pfnQueryInterface = PCIRawDev::drvQueryInterface;

    /*
     * IConnector.
     */
    pThis->IConnector.pfnDeviceConstructComplete = PCIRawDev::drvDeviceConstructComplete;

    /*
     * Get the object pointer and update the mpDrv member.
     */
    void *pv;
    int vrc = CFGMR3QueryPtr(pCfgHandle, "Object", &pv);
    if (RT_FAILURE(vrc))
    {
        AssertMsgFailed(("Configuration error: No \"Object\" value! vrc=%Rrc\n", vrc));
        return vrc;
    }

    pThis->pPCIRawDev = (PCIRawDev *)pv;
    pThis->pPCIRawDev->mpDrv = pThis;

    return VINF_SUCCESS;
}

/**
 * Main raw PCI driver registration record.
 */
const PDMDRVREG PCIRawDev::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "MainPciRaw",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main PCI raw driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_PCIRAW,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINPCIRAWDEV),
    /* pfnConstruct */
    PCIRawDev::drvConstruct,
    /* pfnDestruct */
    PCIRawDev::drvDestruct,
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
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

