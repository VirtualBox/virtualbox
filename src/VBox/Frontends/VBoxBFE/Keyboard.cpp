/* $Id: Keyboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox frontends: Basic Frontend (BFE):
 * Keyboard class implementation
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

#define LOG_GROUP LOG_GROUP_MAIN_KEYBOARD
#include "Keyboard.h"

#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/asm.h>
#include <iprt/time.h>
#include <iprt/cpp/utils.h>
#include <iprt/alloca.h>
#include <iprt/uuid.h>

#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/pdmdrv.h>

/**
 * Keyboard device capabilities bitfield.
 */
enum
{
    /** The keyboard device does not wish to receive keystrokes. */
    KEYBOARD_DEVCAP_DISABLED = 0,
    /** The keyboard device does wishes to receive keystrokes. */
    KEYBOARD_DEVCAP_ENABLED  = 1
};


/**
 * Keyboard driver instance data.
 *
 * @implements PDMIKEYBOARDCONNECTOR
 */
typedef struct DRVMAINKEYBOARD
{
    /** Pointer to the keyboard object. */
    Keyboard                   *pKeyboard;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the keyboard port interface of the driver/device above us. */
    PPDMIKEYBOARDPORT           pUpPort;
    /** Our keyboard connector interface. */
    PDMIKEYBOARDCONNECTOR       IConnector;
    /** The capabilities of this device. */
    uint32_t                    u32DevCaps;
} DRVMAINKEYBOARD, *PDRVMAINKEYBOARD;

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

Keyboard::Keyboard()
{
}

Keyboard::~Keyboard()
{
    if (mpDrv)
        mpDrv->pKeyboard = NULL;

    mpDrv = NULL;
}


// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////


// private methods
/////////////////////////////////////////////////////////////////////////////


/**
 * Sends a scancode to the keyboard.
 *
 * @returns COM status code
 * @param aScancode The scancode to send
 */
int Keyboard::PutScancode(long aScancode)
{
    return PutScancodes(&aScancode, 1, NULL);
}


/**
 * Sends a list of scancodes to the keyboard.
 *
 * @returns COM status code
 * @param aScancodes   Pointer to the first scancode
 * @param aCodesStored Address of variable to store the number
 *                     of scancodes that were sent to the keyboard.
                       This value can be NULL.
 */
int Keyboard::PutScancodes(const long *paScancodes,
                           uint32_t cScancodes,
                           unsigned long *aCodesStored)
{
    /* Send input to the last enabled device. */
    PPDMIKEYBOARDPORT pUpPort = NULL;
    if (mpDrv && (mpDrv->u32DevCaps & KEYBOARD_DEVCAP_ENABLED))
        pUpPort = mpDrv->pUpPort;

    /* No enabled keyboard - throw the input away. */
    if (!pUpPort)
    {
        if (aCodesStored)
            *aCodesStored = cScancodes;
        return VINF_SUCCESS;
    }

    int vrc = VINF_SUCCESS;

    uint32_t sent;
    for (sent = 0; (sent < cScancodes) && RT_SUCCESS(vrc); ++sent)
        vrc = pUpPort->pfnPutEventScan(pUpPort, (uint8_t)paScancodes[sent]);

    if (aCodesStored)
        *aCodesStored = sent;

    return vrc;
}

int Keyboard::PutUsageCode(long aUsageCode, long aUsagePage, bool fKeyRelease)
{
    /* Send input to the last enabled device. Relies on the fact that
     * the USB keyboard is always initialized after the PS/2 keyboard.
     */
    PPDMIKEYBOARDPORT pUpPort = NULL;
    if (mpDrv && (mpDrv->u32DevCaps & KEYBOARD_DEVCAP_ENABLED))
        pUpPort = mpDrv->pUpPort;

    /* No enabled keyboard - throw the input away. */
    if (!pUpPort)
        return VINF_SUCCESS;

    uint32_t idUsage = (uint16_t)aUsageCode | ((uint32_t)(uint8_t)aUsagePage << 16) | (fKeyRelease ? UINT32_C(0x80000000) : 0);
    return pUpPort->pfnPutEventHid(pUpPort, idUsage);
}


/**
 * Releases all currently held keys in the virtual keyboard.
 *
 * @returns COM status code
 *
 */
int Keyboard::releaseKeys()
{
    /* Release all keys on the active keyboard in order to start with a clean slate.
     * Note that this should mirror the logic in Keyboard::putScancodes() when choosing
     * which keyboard to send the release event to.
     */
    PPDMIKEYBOARDPORT pUpPort = NULL;
    if (mpDrv && (mpDrv->u32DevCaps & KEYBOARD_DEVCAP_ENABLED))
        pUpPort = mpDrv->pUpPort;

    if (pUpPort)
    {
        int vrc = pUpPort->pfnReleaseKeys(pUpPort);
        if (RT_FAILURE(vrc))
            AssertMsgFailed(("Failed to release keys on all keyboards! vrc=%Rrc\n", vrc));
    }

    return S_OK;
}

//
// private methods
//
/*static*/ DECLCALLBACK(void) Keyboard::i_keyboardLedStatusChange(PPDMIKEYBOARDCONNECTOR pInterface, PDMKEYBLEDS enmLeds)
{
    RT_NOREF(pInterface, enmLeds);
}

/**
 * @interface_method_impl{PDMIKEYBOARDCONNECTOR,pfnSetActive}
 */
DECLCALLBACK(void) Keyboard::i_keyboardSetActive(PPDMIKEYBOARDCONNECTOR pInterface, bool fActive)
{
    PDRVMAINKEYBOARD pDrv = RT_FROM_MEMBER(pInterface, DRVMAINKEYBOARD, IConnector);

    if (fActive)
        pDrv->u32DevCaps |= KEYBOARD_DEVCAP_ENABLED;
    else
        pDrv->u32DevCaps &= ~KEYBOARD_DEVCAP_ENABLED;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *) Keyboard::i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS          pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINKEYBOARD    pDrv    = PDMINS_2_DATA(pDrvIns, PDRVMAINKEYBOARD);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIKEYBOARDCONNECTOR, &pDrv->IConnector);
    return NULL;
}


/**
 * Destruct a display driver instance.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) Keyboard::i_drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINKEYBOARD pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINKEYBOARD);
    LogRelFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));

}


/**
 * Construct a display driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
DECLCALLBACK(int) Keyboard::i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    RT_NOREF(fFlags, pCfg);
    PDRVMAINKEYBOARD pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINKEYBOARD);
    LogFlow(("Keyboard::drvConstruct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "", "");
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * IBase.
     */
    pDrvIns->IBase.pfnQueryInterface      = Keyboard::i_drvQueryInterface;

    pThis->IConnector.pfnLedStatusChange  = i_keyboardLedStatusChange;
    pThis->IConnector.pfnSetActive        = Keyboard::i_keyboardSetActive;

    /*
     * Get the IKeyboardPort interface of the above driver/device.
     */
    pThis->pUpPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIKEYBOARDPORT);
    if (!pThis->pUpPort)
    {
        AssertMsgFailed(("Configuration error: No keyboard port interface above!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    /*
     * Get the Keyboard object pointer and update the mpDrv member.
     */
    RTUUID UuidDisp;
    RTUuidFromStr(&UuidDisp, KEYBOARD_OID);
    Keyboard *pKeyboard = (Keyboard *)PDMDrvHlpQueryGenericUserObject(pDrvIns, &UuidDisp);
    if (!pKeyboard)
    {
        AssertMsgFailed(("Configuration error: No/bad Keyboard object!\n"));
        return VERR_NOT_FOUND;
    }
    pThis->pKeyboard = pKeyboard;
    pThis->pKeyboard->mpDrv = pThis;

#if 0
    unsigned cDev;
    for (cDev = 0; cDev < KEYBOARD_MAX_DEVICES; ++cDev)
        if (!pThis->pKeyboard->mpDrv[cDev])
        {
            pThis->pKeyboard->mpDrv[cDev] = pThis;
            break;
        }
    if (cDev == KEYBOARD_MAX_DEVICES)
        return VERR_NO_MORE_HANDLES;
#endif

    return VINF_SUCCESS;
}


/**
 * Display driver registration record.
 */
const PDMDRVREG Keyboard::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "MainKeyboard",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main keyboard driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_KEYBOARD,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINKEYBOARD),
    /* pfnConstruct */
    Keyboard::i_drvConstruct,
    /* pfnDestruct */
    Keyboard::i_drvDestruct,
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

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
