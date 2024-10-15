/* $Id: DrvKeyboardQueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox input devices: Keyboard queue driver
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
#define LOG_GROUP LOG_GROUP_DRV_KBD_QUEUE
#include <VBox/vmm/pdmdrv.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Keyboard usage page bits to be OR-ed into the code. */
#define HID_PG_KB_BITS  RT_MAKE_U32(0, USB_HID_KB_PAGE)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Scancode translator state.  */
typedef enum {
    SS_IDLE,    /**< Starting state. */
    SS_EXT,     /**< E0 byte was received. */
    SS_EXT1     /**< E1 byte was received. */
} scan_state_t;

/**
 * Keyboard queue driver instance data.
 *
 * @implements  PDMIKEYBOARDCONNECTOR
 * @implements  PDMIKEYBOARDPORT
 */
typedef struct DRVKBDQUEUE
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the keyboard port interface of the driver/device above us. */
    PPDMIKEYBOARDPORT           pUpPort;
    /** Pointer to the keyboard port interface of the driver/device below us. */
    PPDMIKEYBOARDCONNECTOR      pDownConnector;
    /** Our keyboard connector interface. */
    PDMIKEYBOARDCONNECTOR       IConnector;
    /** Our keyboard port interface. */
    PDMIKEYBOARDPORT            IPort;
    /** The queue handle. */
    PDMQUEUEHANDLE              hQueue;
    /** State of the scancode translation. */
    scan_state_t                XlatState;
    /** Discard input when this flag is set. */
    bool                        fInactive;
    /** When VM is suspended, queue full errors are not fatal. */
    bool                        fSuspended;
} DRVKBDQUEUE, *PDRVKBDQUEUE;


/**
 * Keyboard queue item.
 */
typedef struct DRVKBDQUEUEITEM
{
    /** The core part owned by the queue manager. */
    PDMQUEUEITEMCORE    Core;
    /** The keycode. */
    uint32_t            idUsage;
} DRVKBDQUEUEITEM, *PDRVKBDQUEUEITEM;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Lookup table for converting PC/XT scan codes to USB HID usage codes. */
static const uint8_t aScancode2Hid[] =
{
    0x00, 0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, /* 00-07 */
    0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x2b, /* 08-1F */
    0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, /* 10-17 */
    0x12, 0x13, 0x2f, 0x30, 0x28, 0xe0, 0x04, 0x16, /* 18-1F */
    0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, /* 20-27 */
    0x34, 0x35, 0xe1, 0x31, 0x1d, 0x1b, 0x06, 0x19, /* 28-2F */
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x55, /* 30-37 */
    0xe2, 0x2c, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, /* 38-3F */
    0x3f, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5f, /* 40-47 */
    0x60, 0x61, 0x56, 0x5c, 0x5d, 0x5e, 0x57, 0x59, /* 48-4F */
    0x5a, 0x5b, 0x62, 0x63, 0x46, 0x00, 0x64, 0x44, /* 50-57 */
    0x45, 0x67, 0x00, 0x00, 0x8c, 0x00, 0x00, 0x00, /* 58-5F */
    0x00, 0x00, 0x00, 0x00, 0x68, 0x69, 0x6a, 0x6b, /* 60-67 */
    0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x00, /* 68-6F */
    0x88, 0x91, 0x90, 0x87, 0x00, 0x00, 0x00, 0x00, /* 70-77 */
    0x00, 0x8a, 0x00, 0x8b, 0x00, 0x89, 0x85, 0x00  /* 78-7F */
};

/* Keyboard usage page (07h). */
#define KB(key)     (RT_MAKE_U32(0, USB_HID_KB_PAGE) | (uint16_t)key)

#ifndef VBOX_DISABLE_HID_CC_DC_PASSTHROUGH
 /* Consumer Control usage page (0Ch). */
 #define CC(key)     (RT_MAKE_U32(0, USB_HID_CC_PAGE) | (uint16_t)key)
 /* Generic Desktop Control usage page (01h). */
 #define DC(key)     (RT_MAKE_U32(0, USB_HID_DC_PAGE) | (uint16_t)key)
#else
 /* Do not pass through Consumer/Generic Desktop Control usage page. */
 #define CC(key)     0
 #define DC(key)     0
#endif

/* Untranslated/unused, shouldn't be encountered. */
#define XX(key)     0

/** Lookup table for extended scancodes (arrow keys etc.).
 *  Some of these keys use HID usage pages other than the
 *  standard (07). */
static const uint32_t aExtScan2Hid[] =
{
    XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), /* 00-07 */
    XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), /* 08-1F */
    CC(0x0B6), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), /* 10-17 */
    XX(0x000), CC(0x0B5), XX(0x000), XX(0x000), KB(0x058), KB(0x0e4), XX(0x000), XX(0x000), /* 18-1F */
    CC(0x0E2), CC(0x192), CC(0x0CD), XX(0x000), CC(0x0B7), XX(0x000), XX(0x000), XX(0x000), /* 20-27 */
    XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), CC(0x0EA), XX(0x000), /* 28-2F */
    CC(0x0E9), XX(0x000), CC(0x223), XX(0x000), XX(0x000), KB(0x054), XX(0x000), KB(0x046), /* 30-37 */
    /* Sun-specific keys.  Most of the XT codes are made up  */
    KB(0x0e6), XX(0x000), XX(0x000), KB(0x075), KB(0x076), KB(0x077), KB(0x0A3), KB(0x078), /* 38-3F */
    KB(0x080), KB(0x081), KB(0x082), KB(0x079), XX(0x000), XX(0x000), KB(0x048), KB(0x04a), /* 40-47 */
    KB(0x052), KB(0x04b), XX(0x000), KB(0x050), XX(0x000), KB(0x04f), XX(0x000), KB(0x04d), /* 48-4F */
    KB(0x051), KB(0x04e), KB(0x049), KB(0x04c), XX(0x000), XX(0x000), XX(0x000), XX(0x000), /* 50-57 */
    XX(0x000), XX(0x000), XX(0x000), KB(0x0e3), KB(0x0e7), KB(0x065), KB(0x066), DC(0x082), /* 58-5F */
    XX(0x000), XX(0x000), XX(0x000), DC(0x083), XX(0x000), CC(0x221), CC(0x22A), CC(0x227), /* 60-67 */
    CC(0x226), CC(0x225), CC(0x224), CC(0x194), CC(0x18A), CC(0x183), XX(0x000), XX(0x000), /* 68-6F */
    XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), /* 70-77 */
    XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000), XX(0x000)  /* 78-7F */
};

/**
 * Convert a PC scan code to a USB HID usage byte.
 *
 * @param state         Current state of the translator (scan_state_t).
 * @param scanCode      Incoming scan code.
 * @param pUsage        Pointer to usage; high bit set for key up events. The
 *                      contents are only valid if returned state is SS_IDLE.
 *
 * @return scan_state_t New state of the translator.
 */
static scan_state_t ScancodeToHidUsage(scan_state_t state, uint8_t scanCode, uint32_t *pUsage)
{
    uint32_t    keyUp;
    uint32_t    usagePg;
    uint8_t     usage;

    Assert(pUsage);

    /* Isolate the scan code and key break flag. */
    keyUp = (scanCode & 0x80) ? PDMIKBDPORT_KEY_UP : 0;

    switch (state) {
    case SS_IDLE:
        if (scanCode == 0xE0) {
            state = SS_EXT;
        } else if (scanCode == 0xE1) {
            state = SS_EXT1;
        } else {
            usage = aScancode2Hid[scanCode & 0x7F];
            AssertMsg(usage, ("SS_IDLE: scanCode=%02X\n", scanCode));
            *pUsage = usage | keyUp | HID_PG_KB_BITS;
            /* Remain in SS_IDLE state. */
        }
        break;
    case SS_EXT:
        usagePg = aExtScan2Hid[scanCode & 0x7F];
        AssertMsg(usagePg, ("SS_EXT: scanCode=%02X\n", scanCode));
        *pUsage = usagePg | keyUp;
        state = SS_IDLE;
        break;
    case SS_EXT1:
        /* The sequence is E1 1D 45 E1 9D C5. We take the easy way out and remain
         * in the SS_EXT1 state until 45 or C5 is received.
         */
        if ((scanCode & 0x7F) == 0x45) {
            *pUsage = 0x48 | HID_PG_KB_BITS;
            if (scanCode == 0xC5)
                *pUsage |= keyUp;
            state = SS_IDLE;
        }
        /* Else remain in SS_EXT1 state. */
        break;
    }
    return state;
}


/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *)  drvKbdQueueQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVKBDQUEUE    pThis   = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIKEYBOARDCONNECTOR, &pThis->IConnector);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIKEYBOARDPORT, &pThis->IPort);
    return NULL;
}


/* -=-=-=-=- IKeyboardPort -=-=-=-=- */

/** Converts a pointer to DRVKBDQUEUE::IPort to a DRVKBDQUEUE pointer. */
#define IKEYBOARDPORT_2_DRVKBDQUEUE(pInterface) ( (PDRVKBDQUEUE)((char *)(pInterface) - RT_UOFFSETOF(DRVKBDQUEUE, IPort)) )


/**
 * @interface_method_impl{PDMIKEYBOARDPORT,pfnPutEventScan}
 *
 * Because of the event queueing the EMT context requirement is lifted.
 * @thread  Any thread.
 */
static DECLCALLBACK(int) drvKbdQueuePutEventScan(PPDMIKEYBOARDPORT pInterface, uint8_t u8ScanCode)
{
    PDRVKBDQUEUE pDrv = IKEYBOARDPORT_2_DRVKBDQUEUE(pInterface);
    /* Ignore any attempt to send events if queue is inactive. */
    if (pDrv->fInactive)
        return VINF_SUCCESS;

    uint32_t idUsage = 0;
    pDrv->XlatState = ScancodeToHidUsage(pDrv->XlatState, u8ScanCode, &idUsage);

    if (pDrv->XlatState == SS_IDLE)
    {
        PDRVKBDQUEUEITEM pItem = (PDRVKBDQUEUEITEM)PDMDrvHlpQueueAlloc(pDrv->pDrvIns, pDrv->hQueue);
        if (pItem)
        {
            /*
             * Work around incredibly poorly desgined Korean keyboards which
             * only send break events for Hangul/Hanja keys -- convert a lone
             * key up into a key up/key down sequence.
             */
            if (   (idUsage == (PDMIKBDPORT_KEY_UP | HID_PG_KB_BITS | 0x90))
                || (idUsage == (PDMIKBDPORT_KEY_UP | HID_PG_KB_BITS | 0x91)))
            {
                PDRVKBDQUEUEITEM pItem2 = (PDRVKBDQUEUEITEM)PDMDrvHlpQueueAlloc(pDrv->pDrvIns, pDrv->hQueue);
                /*
                 * NB: If there's no room in the queue, we will drop the faked
                 * key down event. Probably less bad than the alternatives.
                 */
                if (pItem2)
                {
                    /* Manufacture a key down event. */
                    pItem2->idUsage = idUsage & ~PDMIKBDPORT_KEY_UP;
                    PDMDrvHlpQueueInsert(pDrv->pDrvIns, pDrv->hQueue, &pItem2->Core);
                }
            }

            pItem->idUsage = idUsage;
            PDMDrvHlpQueueInsert(pDrv->pDrvIns, pDrv->hQueue, &pItem->Core);

            return VINF_SUCCESS;
        }
        if (!pDrv->fSuspended)
            AssertMsgFailed(("drvKbdQueuePutEventScan: Queue is full!!!!\n"));
        return VERR_PDM_NO_QUEUE_ITEMS;
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIKEYBOARDPORT,pfnPutEventHid}
 *
 * Because of the event queueing the EMT context requirement is lifted.
 * @thread  Any thread.
 */
static DECLCALLBACK(int) drvKbdQueuePutEventHid(PPDMIKEYBOARDPORT pInterface, uint32_t idUsage)
{
    PDRVKBDQUEUE pDrv = IKEYBOARDPORT_2_DRVKBDQUEUE(pInterface);
    /* Ignore any attempt to send events if queue is inactive. */
    if (pDrv->fInactive)
        return VINF_SUCCESS;

    PDRVKBDQUEUEITEM pItem = (PDRVKBDQUEUEITEM)PDMDrvHlpQueueAlloc(pDrv->pDrvIns, pDrv->hQueue);
    if (pItem)
    {
        pItem->idUsage = idUsage;
        PDMDrvHlpQueueInsert(pDrv->pDrvIns, pDrv->hQueue, &pItem->Core);

        return VINF_SUCCESS;
    }
    AssertMsg(pDrv->fSuspended, ("drvKbdQueuePutEventHid: Queue is full!!!!\n"));
    return VERR_PDM_NO_QUEUE_ITEMS;
}


/**
 * @interface_method_impl{PDMIKEYBOARDPORT,pfnReleaseKeys}
 *
 * Because of the event queueing the EMT context requirement is lifted.
 * @thread  Any thread.
 */
static DECLCALLBACK(int) drvKbdQueueReleaseKeys(PPDMIKEYBOARDPORT pInterface)
{
    PDRVKBDQUEUE pDrv = IKEYBOARDPORT_2_DRVKBDQUEUE(pInterface);

    /* Ignore any attempt to send events if queue is inactive. */
    if (pDrv->fInactive)
        return VINF_SUCCESS;

    PDRVKBDQUEUEITEM pItem = (PDRVKBDQUEUEITEM)PDMDrvHlpQueueAlloc(pDrv->pDrvIns, pDrv->hQueue);
    if (pItem)
    {
        /* Send a special key event that forces all keys to be released.
         * Goes through the queue so that it would take effect only after
         * any key events that might already be queued up.
         */
        pItem->idUsage = PDMIKBDPORT_RELEASE_KEYS | HID_PG_KB_BITS;
        PDMDrvHlpQueueInsert(pDrv->pDrvIns, pDrv->hQueue, &pItem->Core);

        return VINF_SUCCESS;
    }
    AssertMsg(pDrv->fSuspended, ("drvKbdQueueReleaseKeys: Queue is full!!!!\n"));
    return VERR_PDM_NO_QUEUE_ITEMS;
}


/* -=-=-=-=- IConnector -=-=-=-=- */

#define PPDMIKEYBOARDCONNECTOR_2_DRVKBDQUEUE(pInterface) ( (PDRVKBDQUEUE)((char *)(pInterface) - RT_UOFFSETOF(DRVKBDQUEUE, IConnector)) )


/**
 * Pass LED status changes from the guest thru to the frontend driver.
 *
 * @param   pInterface  Pointer to the keyboard connector interface structure.
 * @param   enmLeds     The new LED mask.
 */
static DECLCALLBACK(void) drvKbdPassThruLedsChange(PPDMIKEYBOARDCONNECTOR pInterface, PDMKEYBLEDS enmLeds)
{
    PDRVKBDQUEUE pDrv = PPDMIKEYBOARDCONNECTOR_2_DRVKBDQUEUE(pInterface);
    pDrv->pDownConnector->pfnLedStatusChange(pDrv->pDownConnector, enmLeds);
}

/**
 * Pass keyboard state changes from the guest thru to the frontend driver.
 *
 * @param   pInterface  Pointer to the keyboard connector interface structure.
 * @param   fActive     The new active/inactive state.
 */
static DECLCALLBACK(void) drvKbdPassThruSetActive(PPDMIKEYBOARDCONNECTOR pInterface, bool fActive)
{
    PDRVKBDQUEUE pDrv = PPDMIKEYBOARDCONNECTOR_2_DRVKBDQUEUE(pInterface);

    AssertPtr(pDrv->pDownConnector->pfnSetActive);
    pDrv->pDownConnector->pfnSetActive(pDrv->pDownConnector, fActive);
}

/**
 * Flush the keyboard queue if there are pending events.
 *
 * @param   pInterface  Pointer to the keyboard connector interface structure.
 */
static DECLCALLBACK(void) drvKbdFlushQueue(PPDMIKEYBOARDCONNECTOR pInterface)
{
    PDRVKBDQUEUE pDrv = PPDMIKEYBOARDCONNECTOR_2_DRVKBDQUEUE(pInterface);

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
static DECLCALLBACK(bool) drvKbdQueueConsumer(PPDMDRVINS pDrvIns, PPDMQUEUEITEMCORE pItemCore)
{
    PDRVKBDQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);
    PDRVKBDQUEUEITEM    pItem = (PDRVKBDQUEUEITEM)pItemCore;
    int rc = pThis->pUpPort->pfnPutEventHid(pThis->pUpPort, pItem->idUsage);
    return rc != VERR_TRY_AGAIN;
}


/* -=-=-=-=- driver interface -=-=-=-=- */

/**
 * Power On notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void) drvKbdQueuePowerOn(PPDMDRVINS pDrvIns)
{
    PDRVKBDQUEUE    pThis = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);
    pThis->fInactive = false;
}


/**
 * Reset notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void)  drvKbdQueueReset(PPDMDRVINS pDrvIns)
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
static DECLCALLBACK(void)  drvKbdQueueSuspend(PPDMDRVINS pDrvIns)
{
    PDRVKBDQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);
    pThis->fSuspended = true;
}


/**
 * Resume notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void)  drvKbdQueueResume(PPDMDRVINS pDrvIns)
{
    PDRVKBDQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);
    pThis->fInactive = false;
    pThis->fSuspended = false;
}


/**
 * Power Off notification.
 *
 * @param   pDrvIns     The drive instance data.
 */
static DECLCALLBACK(void) drvKbdQueuePowerOff(PPDMDRVINS pDrvIns)
{
    PDRVKBDQUEUE        pThis = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);
    pThis->fInactive = true;
}


/**
 * Construct a keyboard driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvKbdQueueConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVKBDQUEUE    pDrv = PDMINS_2_DATA(pDrvIns, PDRVKBDQUEUE);
    PCPDMDRVHLPR3   pHlp = pDrvIns->pHlpR3;

    LogFlow(("drvKbdQueueConstruct: iInstance=%d\n", pDrvIns->iInstance));


    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "QueueSize|Interval", "");

    /*
     * Init basic data members and interfaces.
     */
    pDrv->pDrvIns                           = pDrvIns;
    pDrv->fInactive                         = true;
    pDrv->fSuspended                        = false;
    pDrv->XlatState                         = SS_IDLE;
    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface        = drvKbdQueueQueryInterface;
    /* IKeyboardConnector. */
    pDrv->IConnector.pfnLedStatusChange     = drvKbdPassThruLedsChange;
    pDrv->IConnector.pfnSetActive           = drvKbdPassThruSetActive;
    pDrv->IConnector.pfnFlushQueue          = drvKbdFlushQueue;
    /* IKeyboardPort. */
    pDrv->IPort.pfnPutEventScan             = drvKbdQueuePutEventScan;
    pDrv->IPort.pfnPutEventHid              = drvKbdQueuePutEventHid;
    pDrv->IPort.pfnReleaseKeys              = drvKbdQueueReleaseKeys;

    /*
     * Get the IKeyboardPort interface of the above driver/device.
     */
    pDrv->pUpPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIKEYBOARDPORT);
    AssertMsgReturn(pDrv->pUpPort, ("Configuration error: No keyboard port interface above!\n"), VERR_PDM_MISSING_INTERFACE_ABOVE);

    /*
     * Attach driver below and query it's connector interface.
     */
    PPDMIBASE pDownBase;
    int rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pDownBase);
    AssertMsgRCReturn(rc, ("Failed to attach driver below us! rc=%Rra\n", rc), rc);

    pDrv->pDownConnector = PDMIBASE_QUERY_INTERFACE(pDownBase, PDMIKEYBOARDCONNECTOR);
    AssertMsgReturn(pDrv->pDownConnector, ("Configuration error: No keyboard connector interface below!\n"),
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

    rc = PDMDrvHlpQueueCreate(pDrvIns, sizeof(DRVKBDQUEUEITEM), cItems, cMilliesInterval,
                              drvKbdQueueConsumer, "Keyboard", &pDrv->hQueue);
    AssertMsgRCReturn(rc, ("Failed to create driver: cItems=%d cMilliesInterval=%d rc=%Rrc\n", cItems, cMilliesInterval, rc), rc);

    return VINF_SUCCESS;
}


/**
 * Keyboard queue driver registration record.
 */
const PDMDRVREG g_DrvKeyboardQueue =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "KeyboardQueue",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Keyboard queue driver to plug in between the key source and the device to do queueing and inter-thread transport.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_KEYBOARD,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVKBDQUEUE),
    /* pfnConstruct */
    drvKbdQueueConstruct,
    /* pfnRelocate */
    NULL,
    /* pfnDestruct */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    drvKbdQueuePowerOn,
    /* pfnReset */
    drvKbdQueueReset,
    /* pfnSuspend */
    drvKbdQueueSuspend,
    /* pfnResume */
    drvKbdQueueResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvKbdQueuePowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

