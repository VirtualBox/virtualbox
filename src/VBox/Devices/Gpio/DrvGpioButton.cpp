/* $Id: DrvGpioButton.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvGpioButton - Virtual GPIO driver for power/sleep button presses.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_GPIO

#include <VBox/vmm/pdmdrv.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The power button is currently pressed. */
#define DRV_GPIO_BUTTON_PRESSED_POWER RT_BIT_32(0)
/** The sleep button is currently pressed. */
#define DRV_GPIO_BUTTON_PRESSED_SLEEP RT_BIT_32(1)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * GPIO button driver instance data.
 *
 * @implements  PDMIEVENTBUTTONPORT
 * @implements  PDMIGPIOCONNECTOR
 */
typedef struct DRVGPIOBUTTON
{
    /** The button event interface for use by Main. */
    PDMIEVENTBUTTONPORT IEventButtonPort;
    /** The GPIO interface interface. */
    PDMIGPIOCONNECTOR   IGpioConnector;
    /** The GPIO port interface above. */
    PPDMIGPIOPORT       pGpioPort;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;

    /** Currently pressed button. */
    volatile uint32_t   fButtonsPressed;

    /** The power button GPIO line to trigger. */
    uint32_t            uPowerButtonGpio;
    /** The sleep button GPIO line to trigger. */
    uint32_t            uSleepButtonGpio;

    TMTIMERHANDLE       hTimerDepress;

} DRVGPIOBUTTON;
/** Pointer to a GPIO button driver instance. */
typedef DRVGPIOBUTTON *PDRVGPIOBUTTON;


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvGpioButton_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVGPIOBUTTON pThis = PDMINS_2_DATA(pDrvIns, PDRVGPIOBUTTON);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,            &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIGPIOCONNECTOR,   &pThis->IGpioConnector);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIEVENTBUTTONPORT, &pThis->IEventButtonPort);
    return NULL;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnQueryGuestCanHandleButtonEvents}
 */
static DECLCALLBACK(int) drvGpioButton_QueryGuestCanHandleButtonEvents(PPDMIEVENTBUTTONPORT pInterface, bool *pfCanHandleButtonEvents)
{
    PDRVGPIOBUTTON pThis = RT_FROM_MEMBER(pInterface, DRVGPIOBUTTON, IEventButtonPort);

    /** @todo Better interface for this. */
    *pfCanHandleButtonEvents =    pThis->pGpioPort->pfnGpioLineIsInput(pThis->pGpioPort, pThis->uPowerButtonGpio)
                               || pThis->pGpioPort->pfnGpioLineIsInput(pThis->pGpioPort, pThis->uSleepButtonGpio);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnPowerButtonPress}
 */
static DECLCALLBACK(int) drvGpioButton_PowerButtonPress(PPDMIEVENTBUTTONPORT pInterface)
{
    PDRVGPIOBUTTON pThis = RT_FROM_MEMBER(pInterface, DRVGPIOBUTTON, IEventButtonPort);

    ASMAtomicOrU32(&pThis->fButtonsPressed, DRV_GPIO_BUTTON_PRESSED_POWER);
    int rc = pThis->pGpioPort->pfnGpioLineChange(pThis->pGpioPort, pThis->uPowerButtonGpio, true /*fVal*/);
    if (RT_SUCCESS(rc))
        rc = PDMDrvHlpTimerSetMillies(pThis->pDrvIns, pThis->hTimerDepress, 250);
    return rc;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnSleepButtonPress}
 */
static DECLCALLBACK(int) drvGpioButton_SleepButtonPress(PPDMIEVENTBUTTONPORT pInterface)
{
    PDRVGPIOBUTTON pThis = RT_FROM_MEMBER(pInterface, DRVGPIOBUTTON, IEventButtonPort);

    ASMAtomicOrU32(&pThis->fButtonsPressed, DRV_GPIO_BUTTON_PRESSED_SLEEP);
    int rc = pThis->pGpioPort->pfnGpioLineChange(pThis->pGpioPort, pThis->uSleepButtonGpio, true /*fVal*/);
    if (RT_SUCCESS(rc))
        rc = PDMDrvHlpTimerSetMillies(pThis->pDrvIns, pThis->hTimerDepress, 250);
    return rc;
}


/**
 * @interface_method_impl{PDMIEVENTBUTTONPORT,pfnQueryPowerButtonHandled}
 */
static DECLCALLBACK(int) drvGpioButton_QueryPowerButtonHandled(PPDMIEVENTBUTTONPORT pInterface, bool *pfHandled)
{
    RT_NOREF(pInterface);

    /** @todo */
    *pfHandled = true;
    return VINF_SUCCESS;
}


/**
 * Timer callback that depresses a button.
 */
static DECLCALLBACK(void) drvGpioButtonTimerDepress(PPDMDRVINS pDrvIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PDRVGPIOBUTTON pThis = PDMINS_2_DATA(pDrvIns, PDRVGPIOBUTTON);
    Assert(hTimer == pThis->hTimerDepress); RT_NOREF(hTimer, pvUser);

    uint32_t fButtonsPressed = ASMAtomicXchgU32(&pThis->fButtonsPressed, 0);
    if (fButtonsPressed & DRV_GPIO_BUTTON_PRESSED_POWER)
        pThis->pGpioPort->pfnGpioLineChange(pThis->pGpioPort, pThis->uPowerButtonGpio, false /*fVal*/);
    if (fButtonsPressed & DRV_GPIO_BUTTON_PRESSED_SLEEP)
        pThis->pGpioPort->pfnGpioLineChange(pThis->pGpioPort, pThis->uSleepButtonGpio, false /*fVal*/);
}


/**
 * Construct an GPIO button driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvGpioButtonConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg, fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVGPIOBUTTON pThis = PDMINS_2_DATA(pDrvIns, PDRVGPIOBUTTON);
    PCPDMDRVHLPR3  pHlp  = pDrvIns->pHlpR3;
    int rc = VINF_SUCCESS;

    /*
     * Init the static parts.
     */
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface                            = drvGpioButton_QueryInterface;
    /* IEventButtonPort */
    pThis->IEventButtonPort.pfnQueryGuestCanHandleButtonEvents  = drvGpioButton_QueryGuestCanHandleButtonEvents;
    pThis->IEventButtonPort.pfnPowerButtonPress                 = drvGpioButton_PowerButtonPress;
    pThis->IEventButtonPort.pfnSleepButtonPress                 = drvGpioButton_SleepButtonPress;
    pThis->IEventButtonPort.pfnQueryPowerButtonHandled          = drvGpioButton_QueryPowerButtonHandled;

    pThis->pDrvIns         = pDrvIns;
    pThis->fButtonsPressed = 0;

    /*
     * Validate and read the configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "PowerButtonGpio"
                                           "|SleepButtonGpio",
                                           "");

    rc = pHlp->pfnCFGMQueryU32(pCfg, "PowerButtonGpio", &pThis->uPowerButtonGpio);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"PowerButtonGpio\" value"));

    rc = pHlp->pfnCFGMQueryU32(pCfg, "SleepButtonGpio", &pThis->uSleepButtonGpio);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Failed to get the \"SleepButtonGpio\" value"));

    /*
     * Check that no-one is attached to us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Query the GPIO port interface.
     */
    pThis->pGpioPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIGPIOPORT);
    if (!pThis->pGpioPort)
    {
        AssertMsgFailed(("Configuration error: the above device/driver didn't export the GPIO port interface!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

    rc = PDMDrvHlpTMTimerCreate(pDrvIns, TMCLOCK_VIRTUAL, drvGpioButtonTimerDepress, NULL,
                                TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0,
                                "Button depress timer", &pThis->hTimerDepress);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Failed to create button depress timer"));

    return VINF_SUCCESS;
}


/**
 * GPIO button driver registration record.
 */
const PDMDRVREG g_DrvGpioButton =
{
    /* .u32Version = */             PDM_DRVREG_VERSION,
    /* .szName = */                 "GpioButton",
    /* .szRCMod = */                "",
    /* .szR0Mod = */                "",
    /* .pszDescription = */         "GPIO Button Driver",
    /* .fFlags = */                 PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* .fClass = */                 PDM_DRVREG_CLASS_GPIO,
    /* .cMaxInstances = */          UINT32_MAX,
    /* .cbInstance = */             sizeof(DRVGPIOBUTTON),
    /* .pfnConstruct = */           drvGpioButtonConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            NULL,
    /* .pfnIOCtl = */               NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               NULL,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .u32EndVersion = */          PDM_DRVREG_VERSION
};
