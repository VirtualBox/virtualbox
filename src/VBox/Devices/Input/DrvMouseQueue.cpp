/* $Id: DrvMouseQueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox input devices: Mouse queue driver
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
#define LOG_GROUP LOG_GROUP_DRV_MOUSE_QUEUE
#include <VBox/vmm/pdmdrv.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Mouse queue driver instance data.
 *
 * @implements  PDMIMOUSECONNECTOR
 * @implements  PDMIMOUSEPORT
 */
typedef struct DRVMOUSEQUEUE
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the mouse port interface of the driver/device above us. */
    PPDMIMOUSEPORT              pUpPort;
    /** Pointer to the mouse port interface of the driver/device below us. */
    PPDMIMOUSECONNECTOR         pDownConnector;
    /** Our mouse connector interface. */
    PDMIMOUSECONNECTOR          IConnector;
    /** Our mouse port interface. */
    PDMIMOUSEPORT               IPort;
    /** The queue handle. */
    PDMQUEUEHANDLE              hQueue;
    /** Discard input when this flag is set.
     * We only accept input when the VM is running. */
    bool                        fInactive;
} DRVMOUSEQUEUE, *PDRVMOUSEQUEUE;


/**
 * Event type for @a DRVMOUSEQUEUEITEM
 */
enum EVENTTYPE { RELATIVE, ABSOLUTE };

/**
 * Mouse queue item.
 */
typedef struct DRVMOUSEQUEUEITEM
{
    /** The core part owned by the queue manager. */
    PDMQUEUEITEMCORE    Core;
    enum EVENTTYPE      enmType;
    union
    {
        uint32_t padding[5];
        struct
        {
            uint32_t    fButtons;
            int32_t     dx;
            int32_t     dy;
            int32_t     dz;
            int32_t     dw;
        } Relative;
        struct
        {
            uint32_t    fButtons;
            uint32_t    x;
            uint32_t    y;
            int32_t     dz;
            int32_t     dw;
        } Absolute;
    } u;
} DRVMOUSEQUEUEITEM, *PDRVMOUSEQUEUEITEM;



/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *)  drvMouseQueueQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMOUSEQUEUE  pThis   = PDMINS_2_DATA(pDrvIns, PDRVMOUSEQUEUE);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUSEPORT, &pThis->IPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUSECONNECTOR, &pThis->IConnector);
    return NULL;
}


/* -=-=-=-=- IMousePort -=-=-=-=- */

/** Converts a pointer to DRVMOUSEQUEUE::Port to a DRVMOUSEQUEUE pointer. */
#define IMOUSEPORT_2_DRVMOUSEQUEUE(pInterface) ( (PDRVMOUSEQUEUE)((char *)(pInterface) - RT_UOFFSETOF(DRVMOUSEQUEUE, IPort)) )


/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEvent}
 */
static DECLCALLBACK(int) drvMouseQueuePutEvent(PPDMIMOUSEPORT pInterface,
                                               int32_t dx, int32_t dy,
                                               int32_t dz, int32_t dw,
                                               uint32_t fButtons)
{
    PDRVMOUSEQUEUE pDrv = IMOUSEPORT_2_DRVMOUSEQUEUE(pInterface);
    if (pDrv->fInactive)
        return VINF_SUCCESS;

    PDRVMOUSEQUEUEITEM pItem = (PDRVMOUSEQUEUEITEM)PDMDrvHlpQueueAlloc(pDrv->pDrvIns, pDrv->hQueue);
    if (pItem)
    {
        RT_ZERO(pItem->u.padding);
        pItem->enmType             = RELATIVE;
        pItem->u.Relative.dx       = dx;
        pItem->u.Relative.dy       = dy;
        pItem->u.Relative.dz       = dz;
        pItem->u.Relative.dw       = dw;
        pItem->u.Relative.fButtons = fButtons;
        PDMDrvHlpQueueInsert(pDrv->pDrvIns, pDrv->hQueue, &pItem->Core);
        return VINF_SUCCESS;
    }
    return VERR_PDM_NO_QUEUE_ITEMS;
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventAbs}
 */
static DECLCALLBACK(int) drvMouseQueuePutEventAbs(PPDMIMOUSEPORT pInterface,
                                                  uint32_t x, uint32_t y,
                                                  int32_t dz, int32_t dw,
                                                  uint32_t fButtons)
{
    PDRVMOUSEQUEUE pDrv = IMOUSEPORT_2_DRVMOUSEQUEUE(pInterface);
    if (pDrv->fInactive)
        return VINF_SUCCESS;

    PDRVMOUSEQUEUEITEM pItem = (PDRVMOUSEQUEUEITEM)PDMDrvHlpQueueAlloc(pDrv->pDrvIns, pDrv->hQueue);
    if (pItem)
    {
        RT_ZERO(pItem->u.padding);
        pItem->enmType             = ABSOLUTE;
        pItem->u.Absolute.x        = x;
        pItem->u.Absolute.y        = y;
        pItem->u.Absolute.dz       = dz;
        pItem->u.Absolute.dw       = dw;
        pItem->u.Absolute.fButtons = fButtons;
        PDMDrvHlpQueueInsert(pDrv->pDrvIns, pDrv->hQueue, &pItem->Core);
        return VINF_SUCCESS;
    }
    return VERR_PDM_NO_QUEUE_ITEMS;
}


static DECLCALLBACK(int) drvMouseQueuePutEventMTAbs(PPDMIMOUSEPORT pInterface,
                                                         uint8_t cContacts,
                                                         const uint64_t *pau64Contacts,
                                                         uint32_t u32ScanTime)
{
    PDRVMOUSEQUEUE pThis = IMOUSEPORT_2_DRVMOUSEQUEUE(pInterface);
    return pThis->pUpPort->pfnPutEventTouchScreen(pThis->pUpPort, cContacts, pau64Contacts, u32ScanTime);
}

static DECLCALLBACK(int) drvMouseQueuePutEventMTRel(PPDMIMOUSEPORT pInterface,
                                                         uint8_t cContacts,
                                                         const uint64_t *pau64Contacts,
                                                         uint32_t u32ScanTime)
{
    PDRVMOUSEQUEUE pThis = IMOUSEPORT_2_DRVMOUSEQUEUE(pInterface);
    return pThis->pUpPort->pfnPutEventTouchPad(pThis->pUpPort, cContacts, pau64Contacts, u32ScanTime);
}

/* -=-=-=-=- IConnector -=-=-=-=- */

#define PPDMIMOUSECONNECTOR_2_DRVMOUSEQUEUE(pInterface) ( (PDRVMOUSEQUEUE)((char *)(pInterface) - RT_UOFFSETOF(DRVMOUSEQUEUE, IConnector)) )


/**
 * Pass absolute mode status changes from the guest through to the frontend
 * driver.
 *
 * @param   pInterface  Pointer to the mouse connector interface structure.
 * @param   fRel        Is relative reporting supported?
 * @param   fAbs        Is absolute reporting supported?
 * @param   fMTAbs      Is absolute multi-touch reporting supported?
 * @param   fMTRel         Is relative multi-touch reporting supported?
 */
static DECLCALLBACK(void) drvMousePassThruReportModes(PPDMIMOUSECONNECTOR pInterface, bool fRel, bool fAbs, bool fMTAbs, bool fMTRel)
{
    PDRVMOUSEQUEUE pDrv = PPDMIMOUSECONNECTOR_2_DRVMOUSEQUEUE(pInterface);
    pDrv->pDownConnector->pfnReportModes(pDrv->pDownConnector, fRel, fAbs, fMTAbs, fMTRel);
}


/**
 * Flush the mouse queue if there are pending events.
 *
 * @param   pInterface  Pointer to the mouse connector interface structure.
 */
static DECLCALLBACK(void) drvMouseFlushQueue(PPDMIMOUSECONNECTOR pInterface)
{
    PDRVMOUSEQUEUE pDrv = PPDMIMOUSECONNECTOR_2_DRVMOUSEQUEUE(pInterface);

    PDMDrvHlpQueueFlushIfNecessary(pDrv->pDrvIns, pDrv->hQueue);
}



/* -=-=-=-=- queue -=-=-=-=- */

/**
 * Queue callback for processing a queued item.
 *
 * @returns Success indicator.
 *          If false the item will not be removed and the flushing will stop.
 * @param   pDrvIns         The driver instance.
 * @param   pItemCore       Pointer to the queue item to process.
 */
static DECLCALLBACK(bool) drvMouseQueueConsumer(PPDMDRVINS pDrvIns, PPDMQUEUEITEMCORE pItemCore)
{
    PDRVMOUSEQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVMOUSEQUEUE);
    PDRVMOUSEQUEUEITEM    pItem = (PDRVMOUSEQUEUEITEM)pItemCore;
    int rc;
    if (pItem->enmType == RELATIVE)
        rc = pThis->pUpPort->pfnPutEvent(pThis->pUpPort,
                                         pItem->u.Relative.dx,
                                         pItem->u.Relative.dy,
                                         pItem->u.Relative.dz,
                                         pItem->u.Relative.dw,
                                         pItem->u.Relative.fButtons);
    else if (pItem->enmType == ABSOLUTE)
        rc = pThis->pUpPort->pfnPutEventAbs(pThis->pUpPort,
                                            pItem->u.Absolute.x,
                                            pItem->u.Absolute.y,
                                            pItem->u.Absolute.dz,
                                            pItem->u.Absolute.dw,
                                            pItem->u.Absolute.fButtons);
    else
        AssertMsgFailedReturn(("enmType=%d\n", pItem->enmType), true /* remove buggy data */);
    return rc != VERR_TRY_AGAIN;
}


/* -=-=-=-=- driver interface -=-=-=-=- */

/**
 * Power On notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void) drvMouseQueuePowerOn(PPDMDRVINS pDrvIns)
{
    PDRVMOUSEQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVMOUSEQUEUE);
    pThis->fInactive = false;
}


/**
 * Reset notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void)  drvMouseQueueReset(PPDMDRVINS pDrvIns)
{
    //PDRVKBDQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);
    /** @todo purge the queue on reset. */
    RT_NOREF(pDrvIns);
}


/**
 * Suspend notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void)  drvMouseQueueSuspend(PPDMDRVINS pDrvIns)
{
    PDRVMOUSEQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVMOUSEQUEUE);
    pThis->fInactive = true;
}


/**
 * Resume notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void)  drvMouseQueueResume(PPDMDRVINS pDrvIns)
{
    PDRVMOUSEQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVMOUSEQUEUE);
    pThis->fInactive = false;
}


/**
 * Power Off notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void) drvMouseQueuePowerOff(PPDMDRVINS pDrvIns)
{
    PDRVMOUSEQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVMOUSEQUEUE);
    pThis->fInactive = true;
}


/**
 * Construct a mouse driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvMouseQueueConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVMOUSEQUEUE  pDrv = PDMINS_2_DATA(pDrvIns, PDRVMOUSEQUEUE);
    PCPDMDRVHLPR3   pHlp = pDrvIns->pHlpR3;

    LogFlow(("drvMouseQueueConstruct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "QueueSize|Interval", "");

    /*
     * Init basic data members and interfaces.
     */
    pDrv->pDrvIns                           = pDrvIns;
    pDrv->fInactive                         = true;
    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface        = drvMouseQueueQueryInterface;
    /* IMouseConnector. */
    pDrv->IConnector.pfnReportModes         = drvMousePassThruReportModes;
    pDrv->IConnector.pfnFlushQueue          = drvMouseFlushQueue;
    /* IMousePort. */
    pDrv->IPort.pfnPutEvent                 = drvMouseQueuePutEvent;
    pDrv->IPort.pfnPutEventAbs              = drvMouseQueuePutEventAbs;
    pDrv->IPort.pfnPutEventTouchScreen      = drvMouseQueuePutEventMTAbs;
    pDrv->IPort.pfnPutEventTouchPad         = drvMouseQueuePutEventMTRel;

    /*
     * Get the IMousePort interface of the above driver/device.
     */
    pDrv->pUpPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIMOUSEPORT);
    AssertMsgReturn(pDrv->pUpPort, ("Configuration error: No mouse port interface above!\n"), VERR_PDM_MISSING_INTERFACE_ABOVE);

    /*
     * Attach driver below and query it's connector interface.
     */
    PPDMIBASE pDownBase;
    int rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pDownBase);
    AssertMsgRCReturn(rc, ("Failed to attach driver below us! rc=%Rra\n", rc), rc);

    pDrv->pDownConnector = PDMIBASE_QUERY_INTERFACE(pDownBase, PDMIMOUSECONNECTOR);
    AssertMsgReturn(pDrv->pDownConnector, ("Configuration error: No mouse connector interface below!\n"),
                    VERR_PDM_MISSING_INTERFACE_BELOW);

    /*
     * Create the queue.
     */
    uint32_t cMilliesInterval = 0;
    rc = pHlp->pfnCFGMQueryU32(pCfg, "Interval", &cMilliesInterval);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        cMilliesInterval = 0;
    else
        AssertMsgRCReturn(rc, ("Configuration error: 32-bit \"Interval\" -> rc=%Rrc\n", rc), rc);

    uint32_t cItems = 0;
    rc = pHlp->pfnCFGMQueryU32(pCfg, "QueueSize", &cItems);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        cItems = 128;
    else
        AssertMsgRCReturn(rc, ("Configuration error: 32-bit \"QueueSize\" -> rc=%Rrc\n", rc), rc);

    rc = PDMDrvHlpQueueCreate(pDrvIns, sizeof(DRVMOUSEQUEUEITEM), cItems, cMilliesInterval,
                              drvMouseQueueConsumer, "Mouse", &pDrv->hQueue);
    AssertMsgRCReturn(rc, ("Failed to create driver: cItems=%d cMilliesInterval=%d rc=%Rrc\n", cItems, cMilliesInterval, rc), rc);

    return VINF_SUCCESS;
}


/**
 * Mouse queue driver registration record.
 */
const PDMDRVREG g_DrvMouseQueue =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "MouseQueue",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Mouse queue driver to plug in between the key source and the device to do queueing and inter-thread transport.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_MOUSE,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMOUSEQUEUE),
    /* pfnConstruct */
    drvMouseQueueConstruct,
    /* pfnRelocate */
    NULL,
    /* pfnDestruct */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    drvMouseQueuePowerOn,
    /* pfnReset */
    drvMouseQueueReset,
    /* pfnSuspend */
    drvMouseQueueSuspend,
    /* pfnResume */
    drvMouseQueueResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvMouseQueuePowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

