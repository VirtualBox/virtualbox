/* $Id: PDMUsb.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Pluggable Device and Driver Manager, USB part.
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
#define LOG_GROUP LOG_GROUP_PDM_DRIVER
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vusb.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/sup.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/version.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/path.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal callback structure pointer.
 *
 * The main purpose is to define the extra data we associate
 * with PDMUSBREGCB so we can find the VM instance and so on.
 */
typedef struct PDMUSBREGCBINT
{
    /** The callback structure. */
    PDMUSBREGCB     Core;
    /** A bit of padding. */
    uint32_t        u32[4];
    /** VM Handle. */
    PVM             pVM;
} PDMUSBREGCBINT, *PPDMUSBREGCBINT;
typedef const PDMUSBREGCBINT *PCPDMUSBREGCBINT;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def PDMUSB_ASSERT_USBINS
 * Asserts the validity of the USB device instance.
 */
#ifdef VBOX_STRICT
# define PDMUSB_ASSERT_USBINS(pUsbIns) \
    do { \
        AssertPtr(pUsbIns); \
        Assert(pUsbIns->u32Version == PDM_USBINS_VERSION); \
        Assert(pUsbIns->pvInstanceDataR3 == (void *)&pUsbIns->achInstanceData[0]); \
    } while (0)
#else
# define PDMUSB_ASSERT_USBINS(pUsbIns)   do { } while (0)
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void pdmR3UsbDestroyDevice(PVM pVM, PPDMUSBINS pUsbIns);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern const PDMUSBHLP g_pdmR3UsbHlp;


AssertCompile(sizeof(PDMUSBINSINT) <= RT_SIZEOFMEMB(PDMUSBINS, Internal.padding));


/**
 * Registers a USB hub driver.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDrvIns         The driver instance of the hub.
 * @param   fVersions       Indicates the kinds of USB devices that can be attached to this HUB.
 * @param   cPorts          The number of ports.
 * @param   pUsbHubReg      The hub callback structure that PDMUsb uses to interact with it.
 * @param   ppUsbHubHlp     The helper callback structure that the hub uses to talk to PDMUsb.
 * @thread  EMT
 */
int pdmR3UsbRegisterHub(PVM pVM, PPDMDRVINS pDrvIns, uint32_t fVersions, uint32_t cPorts, PCPDMUSBHUBREG pUsbHubReg, PPCPDMUSBHUBHLP ppUsbHubHlp)
{
    /*
     * Validate input.
     */
    /* The driver must be in the USB class. */
    if (!(pDrvIns->pReg->fClass & PDM_DRVREG_CLASS_USB))
    {
        LogRel(("PDMUsb: pdmR3UsbRegisterHub: fClass=%#x expected %#x to be set\n", pDrvIns->pReg->fClass, PDM_DRVREG_CLASS_USB));
        return VERR_INVALID_PARAMETER;
    }
    AssertMsgReturn(!(fVersions & ~(VUSB_STDVER_11 | VUSB_STDVER_20 | VUSB_STDVER_30)), ("%#x\n", fVersions), VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppUsbHubHlp, VERR_INVALID_POINTER);
    AssertPtrReturn(pUsbHubReg, VERR_INVALID_POINTER);
    AssertReturn(pUsbHubReg->u32Version == PDM_USBHUBREG_VERSION, VERR_INVALID_MAGIC);
    AssertReturn(pUsbHubReg->u32TheEnd == PDM_USBHUBREG_VERSION, VERR_INVALID_MAGIC);
    AssertPtrReturn(pUsbHubReg->pfnAttachDevice, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pUsbHubReg->pfnDetachDevice, VERR_INVALID_PARAMETER);

    /*
     * Check for duplicate registration and find the last hub for FIFO registration.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    PPDMUSBHUB pPrev = NULL;
    for (PPDMUSBHUB pCur = pVM->pdm.s.pUsbHubs; pCur; pCur = pCur->pNext)
    {
        if (pCur->pDrvIns == pDrvIns)
        {
            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
            return VERR_PDM_USB_HUB_EXISTS;
        }
        pPrev = pCur;
    }

    /*
     * Create an internal USB hub structure.
     */
    PPDMUSBHUB pHub = (PPDMUSBHUB)MMR3HeapAlloc(pVM, MM_TAG_PDM_DRIVER, sizeof(*pHub));
    if (!pHub)
    {
        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
        return VERR_NO_MEMORY;
    }

    pHub->fVersions = fVersions;
    pHub->cPorts = cPorts;
    pHub->cAvailablePorts = cPorts;
    pHub->pDrvIns = pDrvIns;
    pHub->Reg = *pUsbHubReg;
    pHub->pNext = NULL;

    /* link it */
    if (pPrev)
        pPrev->pNext = pHub;
    else
        pVM->pdm.s.pUsbHubs = pHub;

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    Log(("PDM: Registered USB hub %p/%s\n", pDrvIns, pDrvIns->pReg->szName));
    return VINF_SUCCESS;
}


/**
 * Loads one device module and call the registration entry point.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pRegCB          The registration callback stuff.
 * @param   pszFilename     Module filename.
 * @param   pszName         Module name.
 */
static int pdmR3UsbLoad(PVM pVM, PCPDMUSBREGCBINT pRegCB, const char *pszFilename, const char *pszName)
{
    /*
     * Load it.
     */
    int rc = pdmR3LoadR3U(pVM->pUVM, pszFilename, pszName);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the registration export and call it.
         */
        FNPDMVBOXUSBREGISTER *pfnVBoxUsbRegister;
        rc = PDMR3LdrGetSymbolR3(pVM, pszName, "VBoxUsbRegister", (void **)&pfnVBoxUsbRegister);
        if (RT_SUCCESS(rc))
        {
            Log(("PDM: Calling VBoxUsbRegister (%p) of %s (%s)\n", pfnVBoxUsbRegister, pszName, pszFilename));
            rc = pfnVBoxUsbRegister(&pRegCB->Core, VBOX_VERSION);
            if (RT_SUCCESS(rc))
                Log(("PDM: Successfully loaded device module %s (%s).\n", pszName, pszFilename));
            else
                AssertMsgFailed(("VBoxDevicesRegister failed with rc=%Rrc for module %s (%s)\n", rc, pszName, pszFilename));
        }
        else
        {
            AssertMsgFailed(("Failed to locate 'VBoxUsbRegister' in %s (%s) rc=%Rrc\n", pszName, pszFilename, rc));
            if (rc == VERR_SYMBOL_NOT_FOUND)
                rc = VERR_PDM_NO_REGISTRATION_EXPORT;
        }
    }
    else
        AssertMsgFailed(("Failed to load VBoxDD!\n"));
    return rc;
}



/**
 * @interface_method_impl{PDMUSBREGCB,pfnRegister}
 */
static DECLCALLBACK(int) pdmR3UsbReg_Register(PCPDMUSBREGCB pCallbacks, PCPDMUSBREG pReg)
{
    /*
     * Validate the registration structure.
     */
    Assert(pReg);
    AssertMsgReturn(pReg->u32Version == PDM_USBREG_VERSION,
                    ("Unknown struct version %#x!\n", pReg->u32Version),
                    VERR_PDM_UNKNOWN_USBREG_VERSION);
    AssertMsgReturn(    pReg->szName[0]
                    &&  strlen(pReg->szName) < sizeof(pReg->szName)
                    &&  pdmR3IsValidName(pReg->szName),
                    ("Invalid name '%.*s'\n", sizeof(pReg->szName), pReg->szName),
                    VERR_PDM_INVALID_USB_REGISTRATION);
    AssertMsgReturn((pReg->fFlags & ~(PDM_USBREG_HIGHSPEED_CAPABLE | PDM_USBREG_SUPERSPEED_CAPABLE | PDM_USBREG_SAVED_STATE_SUPPORTED)) == 0,
                    ("fFlags=%#x\n", pReg->fFlags), VERR_PDM_INVALID_USB_REGISTRATION);
    AssertMsgReturn(pReg->cMaxInstances > 0,
                    ("Max instances %u! (USB Device %s)\n", pReg->cMaxInstances, pReg->szName),
                    VERR_PDM_INVALID_USB_REGISTRATION);
    AssertMsgReturn(pReg->cbInstance <= _1M,
                    ("Instance size %d bytes! (USB Device %s)\n", pReg->cbInstance, pReg->szName),
                    VERR_PDM_INVALID_USB_REGISTRATION);
    AssertMsgReturn(pReg->pfnConstruct, ("No constructor! (USB Device %s)\n", pReg->szName),
                    VERR_PDM_INVALID_USB_REGISTRATION);

    /*
     * Check for duplicate and find FIFO entry at the same time.
     */
    PCPDMUSBREGCBINT const pRegCB   = (PCPDMUSBREGCBINT)pCallbacks;
    PVM const              pVM      = pRegCB->pVM;
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    PPDMUSB pUsbPrev = NULL;
    PPDMUSB pUsb     = pVM->pdm.s.pUsbDevs;
    for (; pUsb; pUsbPrev = pUsb, pUsb = pUsb->pNext)
        AssertMsgReturnStmt(strcmp(pUsb->pReg->szName, pReg->szName),
                            ("USB Device '%s' already exists\n", pReg->szName),
                            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw),
                            VERR_PDM_USB_NAME_CLASH);

    /*
     * Allocate new device structure and insert it into the list.
     */
    pUsb = (PPDMUSB)MMR3HeapAlloc(pVM, MM_TAG_PDM_DEVICE, sizeof(*pUsb));
    if (pUsb)
    {
        pUsb->pNext = NULL;
        pUsb->iNextInstance = 0;
        pUsb->pInstances = NULL;
        pUsb->pReg = pReg;
        pUsb->cchName = (RTUINT)strlen(pReg->szName);

        if (pUsbPrev)
            pUsbPrev->pNext = pUsb;
        else
            pVM->pdm.s.pUsbDevs = pUsb;

        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
        Log(("PDM: Registered USB device '%s'\n", pReg->szName));
        return VINF_SUCCESS;
    }

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    return VERR_NO_MEMORY;
}


/**
 * Load USB Device modules.
 *
 * This is called by pdmR3DevInit() after it has loaded it's device modules.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
int pdmR3UsbLoadModules(PVM pVM)
{
    LogFlow(("pdmR3UsbLoadModules:\n"));

    AssertRelease(!(RT_UOFFSETOF(PDMUSBINS, achInstanceData) & 15));
    AssertRelease(sizeof(pVM->pdm.s.pUsbInstances->Internal.s) <= sizeof(pVM->pdm.s.pUsbInstances->Internal.padding));

    /*
     * Initialize the callback structure.
     */
    PDMUSBREGCBINT RegCB;
    RegCB.Core.u32Version = PDM_USBREG_CB_VERSION;
    RegCB.Core.pfnRegister = pdmR3UsbReg_Register;
    RegCB.pVM = pVM;

    /*
     * Load the builtin module
     */
    PCFGMNODE pUsbNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM/USB/");
    bool fLoadBuiltin;
    int rc = CFGMR3QueryBool(pUsbNode, "LoadBuiltin", &fLoadBuiltin);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
        fLoadBuiltin = true;
    else if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Querying boolean \"LoadBuiltin\" failed with %Rrc\n", rc));
        return rc;
    }
    if (fLoadBuiltin)
    {
        /* make filename */
        char *pszFilename = pdmR3FileR3("VBoxDD", true /*fShared*/);
        if (!pszFilename)
            return VERR_NO_TMP_MEMORY;
        rc = pdmR3UsbLoad(pVM, &RegCB, pszFilename, "VBoxDD");
        RTMemTmpFree(pszFilename);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Load additional device modules.
     */
    PCFGMNODE pCur;
    for (pCur = CFGMR3GetFirstChild(pUsbNode); pCur; pCur = CFGMR3GetNextChild(pCur))
    {
        /*
         * Get the name and path.
         */
        char szName[PDMMOD_NAME_LEN];
        rc = CFGMR3GetName(pCur, &szName[0], sizeof(szName));
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
        {
            AssertMsgFailed(("configuration error: The module name is too long, cchName=%zu.\n", CFGMR3GetNameLen(pCur)));
            return VERR_PDM_MODULE_NAME_TOO_LONG;
        }
        else if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("CFGMR3GetName -> %Rrc.\n", rc));
            return rc;
        }

        /* the path is optional, if no path the module name + path is used. */
        char szFilename[RTPATH_MAX];
        rc = CFGMR3QueryString(pCur, "Path", &szFilename[0], sizeof(szFilename));
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            strcpy(szFilename, szName);
        else if (RT_FAILURE(rc))
        {
            AssertMsgFailed(("configuration error: Failure to query the module path, rc=%Rrc.\n", rc));
            return rc;
        }

        /* prepend path? */
        if (!RTPathHavePath(szFilename))
        {
            char *psz = pdmR3FileR3(szFilename, false /*fShared*/);
            if (!psz)
                return VERR_NO_TMP_MEMORY;
            size_t cch = strlen(psz) + 1;
            if (cch > sizeof(szFilename))
            {
                RTMemTmpFree(psz);
                AssertMsgFailed(("Filename too long! cch=%d '%s'\n", cch, psz));
                return VERR_FILENAME_TOO_LONG;
            }
            memcpy(szFilename, psz, cch);
            RTMemTmpFree(psz);
        }

        /*
         * Load the module and register it's devices.
         */
        rc = pdmR3UsbLoad(pVM, &RegCB, szFilename, szName);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Send the init-complete notification to all the USB devices.
 *
 * This is called from pdmR3DevInit() after it has do its notification round.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
int pdmR3UsbVMInitComplete(PVM pVM)
{
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMUSBINS pUsbIns = pVM->pdm.s.pUsbInstances; pUsbIns; pUsbIns = pUsbIns->Internal.s.pNext)
    {
        if (pUsbIns->pReg->pfnVMInitComplete)
        {
            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            int rc = pUsbIns->pReg->pfnVMInitComplete(pUsbIns);
            if (RT_FAILURE(rc))
            {
                AssertMsgFailed(("InitComplete on USB device '%s'/%d failed with rc=%Rrc\n",
                                 pUsbIns->pReg->szName, pUsbIns->iInstance, rc));
                return rc;
            }
            RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
        }
    }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    return VINF_SUCCESS;
}


/**
 * Lookups a device structure by name.
 * @internal
 */
PPDMUSB pdmR3UsbLookup(PVM pVM, const char *pszName)
{
    size_t const cchName = strlen(pszName);

    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMUSB pUsb = pVM->pdm.s.pUsbDevs; pUsb; pUsb = pUsb->pNext)
        if (   pUsb->cchName == cchName
            && !strcmp(pUsb->pReg->szName, pszName))
        {
            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            return pUsb;
        }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    return NULL;
}


/**
 * Locates a suitable hub for the specified kind of device.
 *
 * @returns VINF_SUCCESS and *ppHub on success.
 *          VERR_PDM_NO_USB_HUBS or VERR_PDM_NO_USB_PORTS on failure.
 * @param   pVM             The cross context VM structure.
 * @param   iUsbVersion     The USB device version.
 * @param   ppHub           Where to store the pointer to the USB hub.
 */
static int pdmR3UsbFindHub(PVM pVM, uint32_t iUsbVersion, PPDMUSBHUB *ppHub)
{
    *ppHub = NULL;
    if (!pVM->pdm.s.pUsbHubs)
        return VERR_PDM_NO_USB_HUBS;

    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMUSBHUB pCur = pVM->pdm.s.pUsbHubs; pCur; pCur = pCur->pNext)
        if (pCur->cAvailablePorts > 0)
        {
            /* First check for an exact match. */
            if (pCur->fVersions & iUsbVersion)
            {
                *ppHub = pCur;
                break;
            }
            /* For high-speed USB 2.0 devices only, allow USB 1.1 fallback. */
            if ((iUsbVersion & VUSB_STDVER_20) && pCur->fVersions == VUSB_STDVER_11)
                *ppHub = pCur;
        }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    if (*ppHub)
        return VINF_SUCCESS;
    return VERR_PDM_NO_USB_PORTS;
}


/**
 * Translates a USB version (a bit-mask) to USB speed (enum). Picks
 * the highest available version.
 *
 * @returns VUSBSPEED enum
 *
 * @param   iUsbVersion     The USB version.
 *
 */
static VUSBSPEED pdmR3UsbVer2Spd(uint32_t iUsbVersion)
{
    VUSBSPEED   enmSpd = VUSB_SPEED_UNKNOWN;
    Assert(iUsbVersion);

    if (iUsbVersion & VUSB_STDVER_30)
        enmSpd = VUSB_SPEED_SUPER;
    else if (iUsbVersion & VUSB_STDVER_20)
        enmSpd = VUSB_SPEED_HIGH;
    else if (iUsbVersion & VUSB_STDVER_11)
        enmSpd = VUSB_SPEED_FULL;    /* Can't distinguish LS vs. FS. */

    return enmSpd;
}


/**
 * Translates a USB speed (enum) to USB version.
 *
 * @returns USB version mask
 *
 * @param   enmSpeed        The USB connection speed.
 *
 */
static uint32_t pdmR3UsbSpd2Ver(VUSBSPEED enmSpeed)
{
    uint32_t    iUsbVersion = 0;
    Assert(enmSpeed != VUSB_SPEED_UNKNOWN);

    switch (enmSpeed)
    {
    case VUSB_SPEED_LOW:
    case VUSB_SPEED_FULL:
        iUsbVersion = VUSB_STDVER_11;
        break;
    case VUSB_SPEED_HIGH:
        iUsbVersion = VUSB_STDVER_20;
        break;
    case VUSB_SPEED_SUPER:
    case VUSB_SPEED_SUPERPLUS:
    default:
        iUsbVersion = VUSB_STDVER_30;
        break;
    }

    return iUsbVersion;
}


/**
 * Creates the device.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pHub                The USB hub it'll be attached to.
 * @param   pUsbDev             The USB device emulation.
 * @param   iInstance           -1 if not called by pdmR3UsbInstantiateDevices().
 * @param   pUuid               The UUID for this device.
 * @param   ppInstanceNode      Pointer to the device instance pointer. This is set to NULL if inserted
 *                              into the tree or cleaned up.
 *
 *                              In the pdmR3UsbInstantiateDevices() case (iInstance != -1) this is
 *                              the actual instance node and will not be cleaned up.
 *
 * @param   enmSpeed            The speed the USB device is operating at.
 * @param   pszCaptureFilename  Path to the file for USB traffic capturing, optional.
 */
static int pdmR3UsbCreateDevice(PVM pVM, PPDMUSBHUB pHub, PPDMUSB pUsbDev, int iInstance, PCRTUUID pUuid,
                                PCFGMNODE *ppInstanceNode, VUSBSPEED enmSpeed, const char *pszCaptureFilename)
{
    int rc;

    AssertPtrReturn(ppInstanceNode, VERR_INVALID_POINTER);
    AssertPtrReturn(*ppInstanceNode, VERR_INVALID_POINTER);

    /*
     * If not called by pdmR3UsbInstantiateDevices(), we'll have to fix
     * the configuration now.
     */
    /* USB device node. */
    PCFGMNODE pDevNode = CFGMR3GetChildF(CFGMR3GetRoot(pVM), "USB/%s/", pUsbDev->pReg->szName);
    if (!pDevNode)
    {
        rc = CFGMR3InsertNodeF(CFGMR3GetRoot(pVM), &pDevNode, "USB/%s/", pUsbDev->pReg->szName);
        AssertRCReturn(rc, rc);
    }

    /* The instance node and number. */
    PCFGMNODE pInstanceToDelete = NULL;
    PCFGMNODE pInstanceNode = NULL;
    if (iInstance == -1)
    {
        /** @todo r=bird: This code is bogus as it ASSUMES that all USB devices are
         *        capable of infinite number of instances. */
        rc = VINF_SUCCESS; /* Shut up stupid incorrect uninitialized warning from Visual C++ 2010. */
        for (unsigned c = 0; c < _2M; c++)
        {
            iInstance = pUsbDev->iNextInstance++;
            rc = CFGMR3InsertNodeF(pDevNode, &pInstanceNode, "%d/", iInstance);
            if (rc != VERR_CFGM_NODE_EXISTS)
                break;
        }
        AssertRCReturn(rc, rc);

        rc = CFGMR3ReplaceSubTree(pInstanceNode, *ppInstanceNode);
        AssertRCReturn(rc, rc);
        *ppInstanceNode = NULL;
        pInstanceToDelete = pInstanceNode;
    }
    else
    {
        Assert(iInstance >= 0);
        if (iInstance >= (int)pUsbDev->iNextInstance)
            pUsbDev->iNextInstance = iInstance + 1;
        pInstanceNode = *ppInstanceNode;
    }

    /* Make sure the instance config node exists. */
    PCFGMNODE pConfig = CFGMR3GetChild(pInstanceNode, "Config");
    if (!pConfig)
    {
        rc = CFGMR3InsertNode(pInstanceNode, "Config", &pConfig);
        AssertRCReturn(rc, rc);
    }
    Assert(CFGMR3GetChild(pInstanceNode, "Config") == pConfig);

    /* The global device config node. */
    PCFGMNODE pGlobalConfig = CFGMR3GetChild(pDevNode, "GlobalConfig");
    if (!pGlobalConfig)
    {
        rc = CFGMR3InsertNode(pDevNode, "GlobalConfig", &pGlobalConfig);
        if (RT_FAILURE(rc))
        {
            CFGMR3RemoveNode(pInstanceToDelete);
            AssertRCReturn(rc, rc);
        }
    }

    /*
     * Allocate the device instance.
     */
    size_t cb = RT_UOFFSETOF_DYN(PDMUSBINS, achInstanceData[pUsbDev->pReg->cbInstance]);
    cb = RT_ALIGN_Z(cb, 16);
    PPDMUSBINS pUsbIns;
    rc = MMR3HeapAllocZEx(pVM, MM_TAG_PDM_USB, cb, (void **)&pUsbIns);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Failed to allocate %d bytes of instance data for USB device '%s'. rc=%Rrc\n",
                         cb, pUsbDev->pReg->szName, rc));
        CFGMR3RemoveNode(pInstanceToDelete);
        return rc;
    }

    /*
     * Initialize it.
     */
    pUsbIns->u32Version                     = PDM_USBINS_VERSION;
    //pUsbIns->Internal.s.pNext               = NULL;
    //pUsbIns->Internal.s.pPerDeviceNext      = NULL;
    pUsbIns->Internal.s.pUsbDev             = pUsbDev;
    pUsbIns->Internal.s.pVM                 = pVM;
    //pUsbIns->Internal.s.pLuns               = NULL;
    pUsbIns->Internal.s.pCfg                = pInstanceNode;
    pUsbIns->Internal.s.pCfgDelete          = pInstanceToDelete;
    pUsbIns->Internal.s.pCfgGlobal          = pGlobalConfig;
    pUsbIns->Internal.s.Uuid                = *pUuid;
    //pUsbIns->Internal.s.pHub                = NULL;
    pUsbIns->Internal.s.iPort               = UINT32_MAX; /* to be determined. */
    VMSTATE const enmVMState = VMR3GetState(pVM);
    pUsbIns->Internal.s.fVMSuspended        = !VMSTATE_IS_POWERED_ON(enmVMState);
    //pUsbIns->Internal.s.pfnAsyncNotify      = NULL;
    pUsbIns->pHlpR3                         = &g_pdmR3UsbHlp;
    pUsbIns->pReg                           = pUsbDev->pReg;
    pUsbIns->pCfg                           = pConfig;
    pUsbIns->pCfgGlobal                     = pGlobalConfig;
    pUsbIns->iInstance                      = iInstance;
    pUsbIns->pvInstanceDataR3               = &pUsbIns->achInstanceData[0];
    pUsbIns->pszName                        = RTStrDup(pUsbDev->pReg->szName);
    //pUsbIns->fTracing                       = 0;
    pUsbIns->idTracing                      = ++pVM->pdm.s.idTracingOther;
    pUsbIns->enmSpeed                       = enmSpeed;

    /*
     * Link it into all the lists.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /* The global instance FIFO. */
    PPDMUSBINS pPrev1 = pVM->pdm.s.pUsbInstances;
    if (!pPrev1)
        pVM->pdm.s.pUsbInstances = pUsbIns;
    else
    {
        while (pPrev1->Internal.s.pNext)
        {
            Assert(pPrev1->u32Version == PDM_USBINS_VERSION);
            pPrev1 = pPrev1->Internal.s.pNext;
        }
        pPrev1->Internal.s.pNext = pUsbIns;
    }

    /* The per device instance FIFO. */
    PPDMUSBINS pPrev2 = pUsbDev->pInstances;
    if (!pPrev2)
        pUsbDev->pInstances = pUsbIns;
    else
    {
        while (pPrev2->Internal.s.pPerDeviceNext)
        {
            Assert(pPrev2->u32Version == PDM_USBINS_VERSION);
            pPrev2 = pPrev2->Internal.s.pPerDeviceNext;
        }
        pPrev2->Internal.s.pPerDeviceNext = pUsbIns;
    }

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Call the constructor.
     */
    Log(("PDM: Constructing USB device '%s' instance %d...\n", pUsbIns->pReg->szName, pUsbIns->iInstance));
    rc = pUsbIns->pReg->pfnConstruct(pUsbIns, pUsbIns->iInstance, pUsbIns->pCfg, pUsbIns->pCfgGlobal);
    if (RT_SUCCESS(rc))
    {
        /*
         * Attach it to the hub.
         */
        Log(("PDM: Attaching it...\n"));
        rc = pHub->Reg.pfnAttachDevice(pHub->pDrvIns, pUsbIns, pszCaptureFilename, &pUsbIns->Internal.s.iPort);
        if (RT_SUCCESS(rc))
        {
            pHub->cAvailablePorts--;
            Assert((int32_t)pHub->cAvailablePorts >= 0 && pHub->cAvailablePorts < pHub->cPorts);
            pUsbIns->Internal.s.pHub = pHub;

            /* Send the hot-plugged notification if applicable. */
            if (VMSTATE_IS_POWERED_ON(enmVMState) && pUsbIns->pReg->pfnHotPlugged)
                pUsbIns->pReg->pfnHotPlugged(pUsbIns);

            Log(("PDM: Successfully attached USB device '%s' instance %d to hub %p\n",
                 pUsbIns->pReg->szName, pUsbIns->iInstance, pHub));
            return VINF_SUCCESS;
        }

        LogRel(("PDMUsb: Failed to attach USB device '%s' instance %d to hub %p: %Rrc\n",
                pUsbIns->pReg->szName, pUsbIns->iInstance, pHub, rc));
    }
    else
    {
        AssertMsgFailed(("Failed to construct '%s'/%d! %Rra\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc));
        if (rc == VERR_VERSION_MISMATCH)
            rc = VERR_PDM_USBDEV_VERSION_MISMATCH;
    }
    if (VMSTATE_IS_POWERED_ON(enmVMState))
        pdmR3UsbDestroyDevice(pVM, pUsbIns);
    /* else: destructors are invoked later. */
    return rc;
}


/**
 * Instantiate USB devices.
 *
 * This is called by pdmR3DevInit() after it has instantiated the
 * other devices and their drivers. If there aren't any hubs
 * around, we'll silently skip the USB devices.
 *
 * @returns VBox status code.
 * @param   pVM        The cross context VM structure.
 */
int pdmR3UsbInstantiateDevices(PVM pVM)
{
    /*
     * Any hubs?
     */
    if (!pVM->pdm.s.pUsbHubs)
    {
        Log(("PDM: No USB hubs, skipping USB device instantiation.\n"));
        return VINF_SUCCESS;
    }

    /*
     * Count the device instances.
     */
    PCFGMNODE pCur;
    PCFGMNODE pUsbNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "USB/");
    PCFGMNODE pInstanceNode;
    unsigned cUsbDevs = 0;
    for (pCur = CFGMR3GetFirstChild(pUsbNode); pCur; pCur = CFGMR3GetNextChild(pCur))
    {
        PCFGMNODE pGlobal = CFGMR3GetChild(pCur, "GlobalConfig/");
        for (pInstanceNode = CFGMR3GetFirstChild(pCur); pInstanceNode; pInstanceNode = CFGMR3GetNextChild(pInstanceNode))
            if (pInstanceNode != pGlobal)
                cUsbDevs++;
    }
    if (!cUsbDevs)
    {
        Log(("PDM: No USB devices were configured!\n"));
        return VINF_SUCCESS;
    }
    Log2(("PDM: cUsbDevs=%d!\n", cUsbDevs));

    /*
     * Collect info on each USB device instance.
     */
    struct USBDEVORDER
    {
        /** Configuration node. */
        PCFGMNODE   pNode;
        /** Pointer to the USB device. */
        PPDMUSB     pUsbDev;
        /** Init order. */
        uint32_t    u32Order;
        /** VBox instance number. */
        uint32_t    iInstance;
        /** Device UUID. */
        RTUUID      Uuid;
    } *paUsbDevs = (struct USBDEVORDER *)alloca(sizeof(paUsbDevs[0]) * (cUsbDevs + 1)); /* (One extra for swapping) */
    Assert(paUsbDevs);
    int rc;
    unsigned i = 0;
    for (pCur = CFGMR3GetFirstChild(pUsbNode); pCur; pCur = CFGMR3GetNextChild(pCur))
    {
        /* Get the device name. */
        char szName[sizeof(paUsbDevs[0].pUsbDev->pReg->szName)];
        rc = CFGMR3GetName(pCur, szName, sizeof(szName));
        AssertMsgRCReturn(rc, ("Configuration error: device name is too long (or something)! rc=%Rrc\n", rc), rc);

        /* Find the device. */
        PPDMUSB pUsbDev = pdmR3UsbLookup(pVM, szName);
        AssertMsgReturn(pUsbDev, ("Configuration error: device '%s' not found!\n", szName), VERR_PDM_DEVICE_NOT_FOUND);

        /* Configured priority or use default? */
        uint32_t u32Order;
        rc = CFGMR3QueryU32(pCur, "Priority", &u32Order);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
            u32Order = i << 4;
        else
            AssertMsgRCReturn(rc, ("Configuration error: reading \"Priority\" for the '%s' USB device failed rc=%Rrc!\n", szName, rc), rc);

        /* Global config. */
        PCFGMNODE pGlobal = CFGMR3GetChild(pCur, "GlobalConfig/");
        if (!pGlobal)
        {
            rc = CFGMR3InsertNode(pCur, "GlobalConfig/", &pGlobal);
            AssertMsgRCReturn(rc, ("Failed to create GlobalConfig node! rc=%Rrc\n", rc), rc);
            CFGMR3SetRestrictedRoot(pGlobal);
        }

        /* Enumerate the device instances. */
        for (pInstanceNode = CFGMR3GetFirstChild(pCur); pInstanceNode; pInstanceNode = CFGMR3GetNextChild(pInstanceNode))
        {
            if (pInstanceNode == pGlobal)
                continue;

            /* Use the configured UUID if present, create our own otherwise. */
            char *pszUuid = NULL;

            RTUuidClear(&paUsbDevs[i].Uuid);
            rc = CFGMR3QueryStringAlloc(pInstanceNode, "UUID", &pszUuid);
            if (RT_SUCCESS(rc))
            {
                AssertPtr(pszUuid);

                rc = RTUuidFromStr(&paUsbDevs[i].Uuid, pszUuid);
                AssertMsgRCReturn(rc, ("Failed to convert UUID from string! rc=%Rrc\n", rc), rc);
                MMR3HeapFree(pszUuid);
            }
            else if (rc == VERR_CFGM_VALUE_NOT_FOUND)
                rc = RTUuidCreate(&paUsbDevs[i].Uuid);

            AssertRCReturn(rc, rc);
            paUsbDevs[i].pNode = pInstanceNode;
            paUsbDevs[i].pUsbDev = pUsbDev;
            paUsbDevs[i].u32Order = u32Order;

            /* Get the instance number. */
            char szInstance[32];
            rc = CFGMR3GetName(pInstanceNode, szInstance, sizeof(szInstance));
            AssertMsgRCReturn(rc, ("Configuration error: instance name is too long (or something)! rc=%Rrc\n", rc), rc);
            char *pszNext = NULL;
            rc = RTStrToUInt32Ex(szInstance, &pszNext, 0, &paUsbDevs[i].iInstance);
            AssertMsgRCReturn(rc, ("Configuration error: RTStrToInt32Ex failed on the instance name '%s'! rc=%Rrc\n", szInstance, rc), rc);
            AssertMsgReturn(!*pszNext, ("Configuration error: the instance name '%s' isn't all digits. (%s)\n", szInstance, pszNext), VERR_INVALID_PARAMETER);

            /* next instance */
            i++;
        }
    } /* devices */
    Assert(i == cUsbDevs);

    /*
     * Sort the device array ascending on u32Order. (bubble)
     */
    unsigned c = cUsbDevs - 1;
    while (c)
    {
        unsigned j = 0;
        for (i = 0; i < c; i++)
            if (paUsbDevs[i].u32Order > paUsbDevs[i + 1].u32Order)
            {
                paUsbDevs[cUsbDevs] = paUsbDevs[i + 1];
                paUsbDevs[i + 1] = paUsbDevs[i];
                paUsbDevs[i] = paUsbDevs[cUsbDevs];
                j = i;
            }
        c = j;
    }

    /*
     * Instantiate the devices.
     */
    for (i = 0; i < cUsbDevs; i++)
    {
        /*
         * Make sure there is a config node and mark it as restricted.
         */
        PCFGMNODE pConfigNode = CFGMR3GetChild(paUsbDevs[i].pNode, "Config/");
        if (!pConfigNode)
        {
            rc = CFGMR3InsertNode(paUsbDevs[i].pNode, "Config", &pConfigNode);
            AssertMsgRCReturn(rc, ("Failed to create Config node! rc=%Rrc\n", rc), rc);
        }
        CFGMR3SetRestrictedRoot(pConfigNode);

        /*
         * Every emulated device must support USB 1.x hubs; optionally, high-speed USB 2.0 hubs
         * might be also supported. This determines where to attach the device.
         */
        uint32_t iUsbVersion = VUSB_STDVER_11;

        if (paUsbDevs[i].pUsbDev->pReg->fFlags & PDM_USBREG_HIGHSPEED_CAPABLE)
            iUsbVersion |= VUSB_STDVER_20;
        if (paUsbDevs[i].pUsbDev->pReg->fFlags & PDM_USBREG_SUPERSPEED_CAPABLE)
            iUsbVersion |= VUSB_STDVER_30;

        /*
         * Find a suitable hub with free ports.
         */
        PPDMUSBHUB pHub;
        rc = pdmR3UsbFindHub(pVM, iUsbVersion, &pHub);
        if (RT_FAILURE(rc))
        {
            Log(("pdmR3UsbFindHub failed %Rrc\n", rc));
            return rc;
        }

        /*
         * This is how we inform the device what speed it's communicating at, and hence
         * which descriptors it should present to the guest.
         */
        iUsbVersion &= pHub->fVersions;

        /*
         * Create and attach the device.
         */
        rc = pdmR3UsbCreateDevice(pVM, pHub, paUsbDevs[i].pUsbDev, paUsbDevs[i].iInstance, &paUsbDevs[i].Uuid,
                                  &paUsbDevs[i].pNode, pdmR3UsbVer2Spd(iUsbVersion), NULL);
        if (RT_FAILURE(rc))
            return rc;
    } /* for device instances */

    return VINF_SUCCESS;
}


/**
 * Creates an emulated USB device instance at runtime.
 *
 * This will find an appropriate HUB for the USB device
 * and try instantiate the emulated device.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pszDeviceName       The name of the PDM device to instantiate.
 * @param   pInstanceNode       The instance CFGM node.
 * @param   pUuid               The UUID to be associated with the device.
 * @param   pszCaptureFilename  Path to the file for USB traffic capturing, optional.
 *
 * @thread EMT
 */
VMMR3DECL(int) PDMR3UsbCreateEmulatedDevice(PUVM pUVM, const char *pszDeviceName, PCFGMNODE pInstanceNode, PCRTUUID pUuid,
                                            const char *pszCaptureFilename)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pszDeviceName, VERR_INVALID_POINTER);
    AssertPtrReturn(pInstanceNode, VERR_INVALID_POINTER);

    /*
     * Find the device.
     */
    PPDMUSB pUsbDev = pdmR3UsbLookup(pVM, pszDeviceName);
    if (!pUsbDev)
    {
        LogRel(("PDMUsb: PDMR3UsbCreateEmulatedDevice: The '%s' device wasn't found\n", pszDeviceName));
        return VERR_PDM_NO_USBPROXY;
    }

    /*
     * Every device must support USB 1.x hubs; optionally, high-speed USB 2.0 hubs
     * might be also supported. This determines where to attach the device.
     */
    uint32_t iUsbVersion = VUSB_STDVER_11;
    if (pUsbDev->pReg->fFlags & PDM_USBREG_HIGHSPEED_CAPABLE)
        iUsbVersion |= VUSB_STDVER_20;
    if (pUsbDev->pReg->fFlags & PDM_USBREG_SUPERSPEED_CAPABLE)
        iUsbVersion |= VUSB_STDVER_30;

    /*
     * Find a suitable hub with free ports.
     */
    PPDMUSBHUB pHub;
    int rc = pdmR3UsbFindHub(pVM, iUsbVersion, &pHub);
    if (RT_FAILURE(rc))
    {
        Log(("pdmR3UsbFindHub: failed %Rrc\n", rc));
        return rc;
    }

    /*
     * This is how we inform the device what speed it's communicating at, and hence
     * which descriptors it should present to the guest.
     */
    iUsbVersion &= pHub->fVersions;

    /*
     * Create and attach the device.
     */
    rc = pdmR3UsbCreateDevice(pVM, pHub, pUsbDev, -1, pUuid, &pInstanceNode,
                              pdmR3UsbVer2Spd(iUsbVersion), pszCaptureFilename);
    AssertRCReturn(rc, rc);

    return rc;
}


/**
 * Creates a USB proxy device instance.
 *
 * This will find an appropriate HUB for the USB device, create the necessary CFGM stuff
 * and try instantiate the proxy device.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pUuid               The UUID to be associated with the device.
 * @param   pszBackend          The proxy backend to use.
 * @param   pszAddress          The address string.
 * @param   pSubTree            The CFGM subtree to incorporate into the settings
 *                              (same restrictions as for CFGMR3InsertSubTree() apply),
 *                              optional.
 * @param   enmSpeed            The speed the USB device is operating at.
 * @param   fMaskedIfs          The interfaces to hide from the guest.
 * @param   pszCaptureFilename  Path to the file for USB traffic capturing, optional.
 */
VMMR3DECL(int) PDMR3UsbCreateProxyDevice(PUVM pUVM, PCRTUUID pUuid, const char *pszBackend, const char *pszAddress, PCFGMNODE pSubTree,
                                         VUSBSPEED enmSpeed, uint32_t fMaskedIfs, const char *pszCaptureFilename)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);
    AssertPtrReturn(pszAddress, VERR_INVALID_POINTER);
    AssertReturn(    enmSpeed == VUSB_SPEED_LOW
                 ||  enmSpeed == VUSB_SPEED_FULL
                 ||  enmSpeed == VUSB_SPEED_HIGH
                 ||  enmSpeed == VUSB_SPEED_SUPER
                 ||  enmSpeed == VUSB_SPEED_SUPERPLUS, VERR_INVALID_PARAMETER);

    /*
     * Find the USBProxy driver.
     */
    PPDMUSB pUsbDev = pdmR3UsbLookup(pVM, "USBProxy");
    if (!pUsbDev)
    {
        LogRel(("PDMUsb: PDMR3UsbCreateProxyDevice: The USBProxy device class wasn't found\n"));
        return VERR_PDM_NO_USBPROXY;
    }

    /*
     * Find a suitable hub with free ports.
     */
    PPDMUSBHUB pHub;
    uint32_t iUsbVersion = pdmR3UsbSpd2Ver(enmSpeed);
    int rc = pdmR3UsbFindHub(pVM, iUsbVersion, &pHub);
    if (RT_FAILURE(rc))
    {
        Log(("pdmR3UsbFindHub: failed %Rrc\n", rc));
        return rc;
    }

    /*
     * Create the CFGM instance node.
     */
    PCFGMNODE pInstance = CFGMR3CreateTree(pUVM);
    AssertReturn(pInstance, VERR_NO_MEMORY);
    do /* break loop */
    {
        PCFGMNODE pConfig;
        rc = CFGMR3InsertNode(pInstance, "Config", &pConfig);                   AssertRCBreak(rc);
        rc = CFGMR3InsertString(pConfig,  "Address", pszAddress);               AssertRCBreak(rc);
        char szUuid[RTUUID_STR_LENGTH];
        rc = RTUuidToStr(pUuid, &szUuid[0], sizeof(szUuid));                    AssertRCBreak(rc);
        rc = CFGMR3InsertString(pConfig,  "UUID", szUuid);                      AssertRCBreak(rc);
        rc = CFGMR3InsertString(pConfig, "Backend", pszBackend);                AssertRCBreak(rc);
        rc = CFGMR3InsertInteger(pConfig, "MaskedIfs", fMaskedIfs);             AssertRCBreak(rc);
        rc = CFGMR3InsertInteger(pConfig, "Force11Device", !(pHub->fVersions & iUsbVersion)); AssertRCBreak(rc);
        if (pSubTree)
        {
            rc = CFGMR3InsertSubTree(pConfig, "BackendCfg", pSubTree, NULL /*ppChild*/);
            AssertRCBreak(rc);
        }
    } while (0); /* break loop */
    if (RT_FAILURE(rc))
    {
        CFGMR3RemoveNode(pInstance);
        LogRel(("PDMUsb: PDMR3UsbCreateProxyDevice: failed to setup CFGM config, rc=%Rrc\n", rc));
        return rc;
    }

    if (enmSpeed == VUSB_SPEED_UNKNOWN)
        enmSpeed = pdmR3UsbVer2Spd(iUsbVersion);

    /*
     * Finally, try to create it.
     */
    rc = pdmR3UsbCreateDevice(pVM, pHub, pUsbDev, -1, pUuid, &pInstance, enmSpeed, pszCaptureFilename);
    if (RT_FAILURE(rc) && pInstance)
        CFGMR3RemoveNode(pInstance);
    return rc;
}


/**
 * Destroys a hot-plugged USB device.
 *
 * The device must be detached from the HUB at this point.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pUsbIns         The USB device instance to destroy.
 * @thread  EMT
 */
static void pdmR3UsbDestroyDevice(PVM pVM, PPDMUSBINS pUsbIns)
{
    Assert(!pUsbIns->Internal.s.pHub);

    /*
     * Do the unplug notification.
     */
    /** @todo what about the drivers? */
    if (pUsbIns->pReg->pfnHotUnplugged)
        pUsbIns->pReg->pfnHotUnplugged(pUsbIns);

    /*
     * Destroy the luns with their driver chains and call the device destructor.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    while (pUsbIns->Internal.s.pLuns)
    {
        PPDMLUN pLun = pUsbIns->Internal.s.pLuns;
        pUsbIns->Internal.s.pLuns = pLun->pNext;
        if (pLun->pTop)
            pdmR3DrvDestroyChain(pVM, pLun->pTop, PDM_TACH_FLAGS_NOT_HOT_PLUG); /* Hotplugging is handled differently here atm. */
        MMR3HeapFree(pLun);
    }
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    /* finally, the device. */
    if (pUsbIns->pReg->pfnDestruct)
    {
        Log(("PDM: Destructing USB device '%s' instance %d...\n", pUsbIns->pReg->szName, pUsbIns->iInstance));
        pUsbIns->pReg->pfnDestruct(pUsbIns);
    }
    TMR3TimerDestroyUsb(pVM, pUsbIns);
    SSMR3DeregisterUsb(pVM, pUsbIns, NULL, 0);
    pdmR3ThreadDestroyUsb(pVM, pUsbIns);
#ifdef VBOX_WITH_PDM_ASYNC_COMPLETION
    pdmR3AsyncCompletionTemplateDestroyUsb(pVM, pUsbIns);
#endif

    /*
     * Unlink it.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    /* The global instance FIFO. */
    if (pVM->pdm.s.pUsbInstances == pUsbIns)
        pVM->pdm.s.pUsbInstances = pUsbIns->Internal.s.pNext;
    else
    {
        PPDMUSBINS pPrev = pVM->pdm.s.pUsbInstances;
        while (pPrev && pPrev->Internal.s.pNext != pUsbIns)
        {
            Assert(pPrev->u32Version == PDM_USBINS_VERSION);
            pPrev = pPrev->Internal.s.pNext;
        }
        Assert(pPrev); Assert(pPrev != pUsbIns);
        if (pPrev)
            pPrev->Internal.s.pNext = pUsbIns->Internal.s.pNext;
    }

    /* The per device instance FIFO. */
    PPDMUSB pUsbDev = pUsbIns->Internal.s.pUsbDev;
    if (pUsbDev->pInstances == pUsbIns)
        pUsbDev->pInstances = pUsbIns->Internal.s.pPerDeviceNext;
    else
    {
        PPDMUSBINS pPrev = pUsbDev->pInstances;
        while (pPrev && pPrev->Internal.s.pPerDeviceNext != pUsbIns)
        {
            Assert(pPrev->u32Version == PDM_USBINS_VERSION);
            pPrev = pPrev->Internal.s.pPerDeviceNext;
        }
        Assert(pPrev); Assert(pPrev != pUsbIns);
        if (pPrev)
            pPrev->Internal.s.pPerDeviceNext = pUsbIns->Internal.s.pPerDeviceNext;
    }

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);

    /*
     * Trash it.
     */
    pUsbIns->u32Version = 0;
    pUsbIns->pReg = NULL;
    if (pUsbIns->pszName)
    {
        RTStrFree(pUsbIns->pszName);
        pUsbIns->pszName = NULL;
    }
    CFGMR3RemoveNode(pUsbIns->Internal.s.pCfgDelete);
    MMR3HeapFree(pUsbIns);
}


/**
 * Detaches and destroys a USB device.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pUuid           The UUID associated with the device to detach.
 * @thread  EMT
 */
VMMR3DECL(int) PDMR3UsbDetachDevice(PUVM pUVM, PCRTUUID pUuid)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM const pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT(pVM);
    AssertPtrReturn(pUuid, VERR_INVALID_POINTER);

    /*
     * Search the global list for it.
     */
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    PPDMUSBINS pUsbIns = pVM->pdm.s.pUsbInstances;
    for ( ; pUsbIns; pUsbIns = pUsbIns->Internal.s.pNext)
        if (!RTUuidCompare(&pUsbIns->Internal.s.Uuid, pUuid))
            break;
    if (!pUsbIns)
    {
        RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
        return VERR_PDM_DEVICE_INSTANCE_NOT_FOUND; /** @todo VERR_PDM_USB_INSTANCE_NOT_FOUND */
    }

    /*
     * Detach it from the HUB (if it's actually attached to one).
     */
    PPDMUSBHUB pHub = pUsbIns->Internal.s.pHub;
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw); /** @todo would be nice to call the pfnDetachDevice owning this.. */
    if (pHub)
    {
        int rc = pHub->Reg.pfnDetachDevice(pHub->pDrvIns, pUsbIns, pUsbIns->Internal.s.iPort);
        if (RT_FAILURE(rc))
        {
            LogRel(("PDMUsb: Failed to detach USB device '%s' instance %d from %p: %Rrc\n",
                    pUsbIns->pReg->szName, pUsbIns->iInstance, pHub, rc));
            return rc;
        }

        RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
        pHub->cAvailablePorts++;
        Assert(pHub->cAvailablePorts > 0 && pHub->cAvailablePorts <= pHub->cPorts);
        pUsbIns->Internal.s.pHub = NULL;
        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    }

    /*
     * Notify about unplugging and destroy the device with it's drivers.
     */
    pdmR3UsbDestroyDevice(pVM, pUsbIns);

    return VINF_SUCCESS;
}


/**
 * Checks if there are any USB hubs attached.
 *
 * @returns true / false accordingly.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) PDMR3UsbHasHub(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return pVM->pdm.s.pUsbHubs != NULL;
}


/**
 * Locates a LUN.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pszDevice       Device name.
 * @param   iInstance       Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   ppLun           Where to store the pointer to the LUN if found.
 */
static int pdmR3UsbFindLun(PVM pVM, const char *pszDevice, unsigned iInstance, unsigned iLun, PPPDMLUN ppLun)
{
    /*
     * Iterate registered devices looking for the device.
     */
    size_t const cchDevice = strlen(pszDevice);
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);
    for (PPDMUSB pUsbDev = pVM->pdm.s.pUsbDevs; pUsbDev; pUsbDev = pUsbDev->pNext)
    {
        if (   pUsbDev->cchName == cchDevice
            && !memcmp(pUsbDev->pReg->szName, pszDevice, cchDevice))
        {
            /*
             * Iterate device instances.
             */
            for (PPDMUSBINS pUsbIns = pUsbDev->pInstances; pUsbIns; pUsbIns = pUsbIns->Internal.s.pPerDeviceNext)
            {
                if (pUsbIns->iInstance == iInstance)
                {
                    /*
                     * Iterate luns.
                     */
                    for (PPDMLUN pLun = pUsbIns->Internal.s.pLuns; pLun; pLun = pLun->pNext)
                    {
                        if (pLun->iLun == iLun)
                        {
                            *ppLun = pLun;
                            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                            return VINF_SUCCESS;
                        }
                    }
                    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                    return VERR_PDM_LUN_NOT_FOUND;
                }
            }
            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            return VERR_PDM_DEVICE_INSTANCE_NOT_FOUND;
        }
    }
    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    return VERR_PDM_DEVICE_NOT_FOUND;
}


/**
 * Attaches a preconfigured driver to an existing device or driver instance.
 *
 * This is used to change drivers and suchlike at runtime.  The driver or device
 * at the end of the chain will be told to attach to whatever is configured
 * below it.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iDevIns         Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   fFlags          Flags, combination of the PDM_TACH_FLAGS_* \#defines.
 * @param   ppBase          Where to store the base interface pointer. Optional.
 *
 * @thread  EMT
 */
VMMR3DECL(int)  PDMR3UsbDriverAttach(PUVM pUVM, const char *pszDevice, unsigned iDevIns, unsigned iLun, uint32_t fFlags,
                                     PPPDMIBASE ppBase)
{
    LogFlow(("PDMR3UsbDriverAttach: pszDevice=%p:{%s} iDevIns=%d iLun=%d fFlags=%#x ppBase=%p\n",
             pszDevice, pszDevice, iDevIns, iLun, fFlags, ppBase));
    if (ppBase)
        *ppBase = NULL;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM const pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT(pVM);

    /*
     * Find the LUN in question.
     */
    PPDMLUN pLun;
    int rc = pdmR3UsbFindLun(pVM, pszDevice, iDevIns, iLun, &pLun);
    if (RT_SUCCESS(rc))
    {
        /*
         * Anything attached to the LUN?
         */
        PPDMDRVINS pDrvIns = pLun->pTop;
        if (!pDrvIns)
        {
            /* No, ask the device to attach to the new stuff. */
            PPDMUSBINS pUsbIns = pLun->pUsbIns;
            if (pUsbIns->pReg->pfnDriverAttach)
            {
                rc = pUsbIns->pReg->pfnDriverAttach(pUsbIns, iLun, fFlags);
                if (RT_SUCCESS(rc) && ppBase)
                    *ppBase = pLun->pTop ? &pLun->pTop->IBase : NULL;
            }
            else
                rc = VERR_PDM_DEVICE_NO_RT_ATTACH;
        }
        else
        {
            /* Yes, find the bottom most driver and ask it to attach to the new stuff. */
            while (pDrvIns->Internal.s.pDown)
                pDrvIns = pDrvIns->Internal.s.pDown;
            if (pDrvIns->pReg->pfnAttach)
            {
                rc = pDrvIns->pReg->pfnAttach(pDrvIns, fFlags);
                if (RT_SUCCESS(rc) && ppBase)
                    *ppBase = pDrvIns->Internal.s.pDown
                            ? &pDrvIns->Internal.s.pDown->IBase
                            : NULL;
            }
            else
                rc = VERR_PDM_DRIVER_NO_RT_ATTACH;
        }
    }

    if (ppBase)
        LogFlow(("PDMR3UsbDriverAttach: returns %Rrc *ppBase=%p\n", rc, *ppBase));
    else
        LogFlow(("PDMR3UsbDriverAttach: returns %Rrc\n", rc));
    return rc;
}


/**
 * Detaches the specified driver instance.
 *
 * This is used to replumb drivers at runtime for simulating hot plugging and
 * media changes.
 *
 * This method allows detaching drivers from
 * any driver or device by specifying the driver to start detaching at.  The
 * only prerequisite is that the driver or device above implements the
 * pfnDetach callback (PDMDRVREG / PDMUSBREG).
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iDevIns         Device instance.
 * @param   iLun            The Logical Unit in which to look for the driver.
 * @param   pszDriver       The name of the driver which to detach.  If NULL
 *                          then the entire driver chain is detatched.
 * @param   iOccurrence     The occurrence of that driver in the chain.  This is
 *                          usually 0.
 * @param   fFlags          Flags, combination of the PDM_TACH_FLAGS_* \#defines.
 * @thread  EMT
 */
VMMR3DECL(int)  PDMR3UsbDriverDetach(PUVM pUVM, const char *pszDevice, unsigned iDevIns, unsigned iLun,
                                     const char *pszDriver, unsigned iOccurrence, uint32_t fFlags)
{
    LogFlow(("PDMR3UsbDriverDetach: pszDevice=%p:{%s} iDevIns=%u iLun=%u pszDriver=%p:{%s} iOccurrence=%u fFlags=%#x\n",
             pszDevice, pszDevice, iDevIns, iLun, pszDriver, pszDriver, iOccurrence, fFlags));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_EMT(pVM);
    AssertPtr(pszDevice);
    AssertPtrNull(pszDriver);
    Assert(iOccurrence == 0 || pszDriver);
    Assert(!(fFlags & ~(PDM_TACH_FLAGS_NOT_HOT_PLUG)));

    /*
     * Find the LUN in question.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);

    PPDMLUN pLun;
    int rc = pdmR3UsbFindLun(pVM, pszDevice, iDevIns, iLun, &pLun);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the driver.
         */
        PPDMDRVINS pDrvIns = pLun->pTop;
        if (pDrvIns)
        {
            if (pszDriver)
            {
                while (pDrvIns)
                {
                    if (!strcmp(pDrvIns->pReg->szName, pszDriver))
                    {
                        if (iOccurrence == 0)
                            break;
                        iOccurrence--;
                    }
                    pDrvIns = pDrvIns->Internal.s.pDown;
                }
            }
            if (pDrvIns)
            {
                rc = pdmR3DrvDetach(pVM, pDrvIns, fFlags);

                RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
                LogFlow(("PDMR3UsbDriverDetach: returns %Rrc (pdmR3DrvDetach)\n", rc));
                return rc;
            }
            rc = VERR_PDM_DRIVER_INSTANCE_NOT_FOUND;
        }
        else
            rc = VINF_PDM_NO_DRIVER_ATTACHED_TO_LUN;
    }

    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
    LogFlow(("PDMR3UsbDriverDetach: returns %Rrc\n", rc));
    return rc;
}


/**
 * Query the interface of the top level driver on a LUN.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iInstance       Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   ppBase          Where to store the base interface pointer.
 * @remark  We're not doing any locking ATM, so don't try call this at times when the
 *          device chain is known to be updated.
 */
VMMR3DECL(int)  PDMR3UsbQueryLun(PUVM pUVM, const char *pszDevice, unsigned iInstance, unsigned iLun, PPDMIBASE *ppBase)
{
    LogFlow(("PDMR3UsbQueryLun: pszDevice=%p:{%s} iInstance=%u iLun=%u ppBase=%p\n",
             pszDevice, pszDevice, iInstance, iLun, ppBase));
    *ppBase = NULL;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Find the LUN.
     */
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);

    PPDMLUN pLun;
    int rc = pdmR3UsbFindLun(pVM, pszDevice, iInstance, iLun, &pLun);
    if (RT_SUCCESS(rc))
    {
        if (pLun->pTop)
        {
            *ppBase = &pLun->pTop->IBase;

            RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
            LogFlow(("PDMR3UsbQueryLun: return %Rrc and *ppBase=%p\n", VINF_SUCCESS, *ppBase));
            return VINF_SUCCESS;
        }
        rc = VERR_PDM_NO_DRIVER_ATTACHED_TO_LUN;
    }

    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    LogFlow(("PDMR3UsbQueryLun: returns %Rrc\n", rc));
    return rc;
}


/**
 * Query the interface of a named driver on a LUN.
 *
 * If the driver appears more than once in the driver chain, the first instance
 * is returned.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszDevice       Device name.
 * @param   iInstance       Device instance.
 * @param   iLun            The Logical Unit to obtain the interface of.
 * @param   pszDriver       The driver name.
 * @param   ppBase          Where to store the base interface pointer.
 *
 * @remark  We're not doing any locking ATM, so don't try call this at times when the
 *          device chain is known to be updated.
 */
VMMR3DECL(int) PDMR3UsbQueryDriverOnLun(PUVM pUVM, const char *pszDevice, unsigned iInstance,
                                        unsigned iLun, const char *pszDriver, PPPDMIBASE ppBase)
{
    LogFlow(("PDMR3QueryDriverOnLun: pszDevice=%p:{%s} iInstance=%u iLun=%u pszDriver=%p:{%s} ppBase=%p\n",
             pszDevice, pszDevice, iInstance, iLun, pszDriver, pszDriver, ppBase));
    *ppBase = NULL;
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Find the LUN.
     */
    RTCritSectRwEnterShared(&pVM->pdm.s.CoreListCritSectRw);

    PPDMLUN pLun;
    int rc = pdmR3UsbFindLun(pVM, pszDevice, iInstance, iLun, &pLun);
    if (RT_SUCCESS(rc))
    {
        if (pLun->pTop)
        {
            for (PPDMDRVINS pDrvIns = pLun->pTop; pDrvIns; pDrvIns = pDrvIns->Internal.s.pDown)
                if (!strcmp(pDrvIns->pReg->szName, pszDriver))
                {
                    *ppBase = &pDrvIns->IBase;

                    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
                    LogFlow(("PDMR3UsbQueryDriverOnLun: return %Rrc and *ppBase=%p\n", VINF_SUCCESS, *ppBase));
                    return VINF_SUCCESS;
                }
            rc = VERR_PDM_DRIVER_NOT_FOUND;
        }
        else
            rc = VERR_PDM_NO_DRIVER_ATTACHED_TO_LUN;
    }

    RTCritSectRwLeaveShared(&pVM->pdm.s.CoreListCritSectRw);
    LogFlow(("PDMR3UsbQueryDriverOnLun: returns %Rrc\n", rc));
    return rc;
}


/** @name USB Device Helpers
 * @{
 */

/** @interface_method_impl{PDMUSBHLP,pfnDriverAttach} */
static DECLCALLBACK(int) pdmR3UsbHlp_DriverAttach(PPDMUSBINS pUsbIns, RTUINT iLun, PPDMIBASE pBaseInterface,
                                                  PPDMIBASE *ppBaseInterface, const char *pszDesc)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3UsbHlp_DriverAttach: caller='%s'/%d: iLun=%d pBaseInterface=%p ppBaseInterface=%p pszDesc=%p:{%s}\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, iLun, pBaseInterface, ppBaseInterface, pszDesc, pszDesc));

    /*
     * Lookup the LUN, it might already be registered.
     */
    RTCritSectRwEnterExcl(&pVM->pdm.s.CoreListCritSectRw);
    PPDMLUN pLunPrev = NULL;
    PPDMLUN pLun = pUsbIns->Internal.s.pLuns;
    for (; pLun; pLunPrev = pLun, pLun = pLun->pNext)
        if (pLun->iLun == iLun)
            break;

    /*
     * Create the LUN if if wasn't found, else check if driver is already attached to it.
     */
    if (!pLun)
    {
        if (   !pBaseInterface
            || !pszDesc
            || !*pszDesc)
        {
            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
            Assert(pBaseInterface);
            Assert(pszDesc || *pszDesc);
            return VERR_INVALID_PARAMETER;
        }

        pLun = (PPDMLUN)MMR3HeapAlloc(pVM, MM_TAG_PDM_LUN, sizeof(*pLun));
        if (!pLun)
        {
            RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
            return VERR_NO_MEMORY;
        }

        pLun->iLun      = iLun;
        pLun->pNext     = pLunPrev ? pLunPrev->pNext : NULL;
        pLun->pTop      = NULL;
        pLun->pBottom   = NULL;
        pLun->pDevIns   = NULL;
        pLun->pUsbIns   = pUsbIns;
        pLun->pszDesc   = pszDesc;
        pLun->pBase     = pBaseInterface;
        if (!pLunPrev)
            pUsbIns->Internal.s.pLuns = pLun;
        else
            pLunPrev->pNext = pLun;
        Log(("pdmR3UsbHlp_DriverAttach: Registered LUN#%d '%s' with device '%s'/%d.\n",
             iLun, pszDesc, pUsbIns->pReg->szName, pUsbIns->iInstance));
    }
    else if (pLun->pTop)
    {
        RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);
        AssertMsgFailed(("Already attached! The device should keep track of such things!\n"));
        LogFlow(("pdmR3UsbHlp_DriverAttach: caller='%s'/%d: returns %Rrc\n", pUsbIns->pReg->szName, pUsbIns->iInstance, VERR_PDM_DRIVER_ALREADY_ATTACHED));
        return VERR_PDM_DRIVER_ALREADY_ATTACHED;
    }
    Assert(pLun->pBase == pBaseInterface);


    /*
     * Get the attached driver configuration.
     */
    int rc;
    PCFGMNODE pNode = CFGMR3GetChildF(pUsbIns->Internal.s.pCfg, "LUN#%u", iLun);
    if (pNode)
        rc = pdmR3DrvInstantiate(pVM, pNode, pBaseInterface, NULL /*pDrvAbove*/, pLun, ppBaseInterface);
    else
        rc = VERR_PDM_NO_ATTACHED_DRIVER;
    RTCritSectRwLeaveExcl(&pVM->pdm.s.CoreListCritSectRw);


    LogFlow(("pdmR3UsbHlp_DriverAttach: caller='%s'/%d: returns %Rrc\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnAssertEMT} */
static DECLCALLBACK(bool) pdmR3UsbHlp_AssertEMT(PPDMUSBINS pUsbIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    if (VM_IS_EMT(pUsbIns->Internal.s.pVM))
        return true;

    char szMsg[100];
    RTStrPrintf(szMsg, sizeof(szMsg), "AssertEMT '%s'/%d\n", pUsbIns->pReg->szName, pUsbIns->iInstance);
    RTAssertMsg1Weak(szMsg, iLine, pszFile, pszFunction);
    AssertBreakpoint();
    return false;
}


/** @interface_method_impl{PDMUSBHLP,pfnAssertOther} */
static DECLCALLBACK(bool) pdmR3UsbHlp_AssertOther(PPDMUSBINS pUsbIns, const char *pszFile, unsigned iLine, const char *pszFunction)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    if (!VM_IS_EMT(pUsbIns->Internal.s.pVM))
        return true;

    char szMsg[100];
    RTStrPrintf(szMsg, sizeof(szMsg), "AssertOther '%s'/%d\n", pUsbIns->pReg->szName, pUsbIns->iInstance);
    RTAssertMsg1Weak(szMsg, iLine, pszFile, pszFunction);
    AssertBreakpoint();
    return false;
}


/** @interface_method_impl{PDMUSBHLP,pfnDBGFStopV} */
static DECLCALLBACK(int) pdmR3UsbHlp_DBGFStopV(PPDMUSBINS pUsbIns, const char *pszFile, unsigned iLine, const char *pszFunction,
                                               const char *pszFormat, va_list va)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
#ifdef LOG_ENABLED
    va_list va2;
    va_copy(va2, va);
    LogFlow(("pdmR3UsbHlp_DBGFStopV: caller='%s'/%d: pszFile=%p:{%s} iLine=%d pszFunction=%p:{%s} pszFormat=%p:{%s} (%N)\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, pszFile, pszFile, iLine, pszFunction, pszFunction, pszFormat, pszFormat, pszFormat, &va2));
    va_end(va2);
#endif

    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);
    int rc = DBGFR3EventSrcV(pVM, DBGFEVENT_DEV_STOP, pszFile, iLine, pszFunction, pszFormat, va);
    if (rc == VERR_DBGF_NOT_ATTACHED)
        rc = VINF_SUCCESS;

    LogFlow(("pdmR3UsbHlp_DBGFStopV: caller='%s'/%d: returns %Rrc\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnDBGFInfoRegisterArgv} */
static DECLCALLBACK(int) pdmR3UsbHlp_DBGFInfoRegisterArgv(PPDMUSBINS pUsbIns, const char *pszName, const char *pszDesc,
                                                          PFNDBGFINFOARGVUSB pfnHandler)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    LogFlow(("pdmR3UsbHlp_DBGFInfoRegister: caller='%s'/%d: pszName=%p:{%s} pszDesc=%p:{%s} pfnHandler=%p\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, pszName, pszName, pszDesc, pszDesc, pfnHandler));

    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);
    int rc = DBGFR3InfoRegisterUsbArgv(pVM, pszName, pszDesc, pfnHandler, pUsbIns);

    LogFlow(("pdmR3UsbHlp_DBGFInfoRegister: caller='%s'/%d: returns %Rrc\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnMMHeapAlloc} */
static DECLCALLBACK(void *) pdmR3UsbHlp_MMHeapAlloc(PPDMUSBINS pUsbIns, size_t cb)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    LogFlow(("pdmR3UsbHlp_MMHeapAlloc: caller='%s'/%d: cb=%#x\n", pUsbIns->pReg->szName, pUsbIns->iInstance, cb));

    void *pv = MMR3HeapAlloc(pUsbIns->Internal.s.pVM, MM_TAG_PDM_USB_USER, cb);

    LogFlow(("pdmR3UsbHlp_MMHeapAlloc: caller='%s'/%d: returns %p\n", pUsbIns->pReg->szName, pUsbIns->iInstance, pv));
    return pv;
}


/** @interface_method_impl{PDMUSBHLP,pfnMMHeapAllocZ} */
static DECLCALLBACK(void *) pdmR3UsbHlp_MMHeapAllocZ(PPDMUSBINS pUsbIns, size_t cb)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    LogFlow(("pdmR3UsbHlp_MMHeapAllocZ: caller='%s'/%d: cb=%#x\n", pUsbIns->pReg->szName, pUsbIns->iInstance, cb));

    void *pv = MMR3HeapAllocZ(pUsbIns->Internal.s.pVM, MM_TAG_PDM_USB_USER, cb);

    LogFlow(("pdmR3UsbHlp_MMHeapAllocZ: caller='%s'/%d: returns %p\n", pUsbIns->pReg->szName, pUsbIns->iInstance, pv));
    return pv;
}


/** @interface_method_impl{PDMUSBHLP,pfnMMHeapFree} */
static DECLCALLBACK(void) pdmR3UsbHlp_MMHeapFree(PPDMUSBINS pUsbIns, void *pv)
{
    PDMUSB_ASSERT_USBINS(pUsbIns); RT_NOREF(pUsbIns);
    LogFlow(("pdmR3UsbHlp_MMHeapFree: caller='%s'/%d: pv=%p\n", pUsbIns->pReg->szName, pUsbIns->iInstance, pv));

    MMR3HeapFree(pv);

    LogFlow(("pdmR3UsbHlp_MMHeapFree: caller='%s'/%d: returns\n", pUsbIns->pReg->szName, pUsbIns->iInstance));
}


/** @interface_method_impl{PDMUSBHLP,pfnPDMQueueCreate} */
static DECLCALLBACK(int) pdmR3UsbHlp_PDMQueueCreate(PPDMUSBINS pUsbIns, RTUINT cbItem, RTUINT cItems, uint32_t cMilliesInterval,
                                                    PFNPDMQUEUEUSB pfnCallback, const char *pszName, PPDMQUEUE *ppQueue)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    LogFlow(("pdmR3UsbHlp_PDMQueueCreate: caller='%s'/%d: cbItem=%#x cItems=%#x cMilliesInterval=%u pfnCallback=%p pszName=%p:{%s} ppQueue=%p\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, cbItem, cItems, cMilliesInterval, pfnCallback, pszName, pszName, ppQueue));

    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);

    if (pUsbIns->iInstance > 0)
    {
        pszName = MMR3HeapAPrintf(pVM, MM_TAG_PDM_DEVICE_DESC, "%s_%u", pszName, pUsbIns->iInstance);
        AssertLogRelReturn(pszName, VERR_NO_MEMORY);
    }

    RT_NOREF5(cbItem, cItems, cMilliesInterval, pfnCallback, ppQueue);
    /** @todo int rc = PDMR3QueueCreateUsb(pVM, pUsbIns, cbItem, cItems, cMilliesInterval, pfnCallback, fGCEnabled, pszName, ppQueue); */
    int rc = VERR_NOT_IMPLEMENTED; AssertFailed();

    LogFlow(("pdmR3UsbHlp_PDMQueueCreate: caller='%s'/%d: returns %Rrc *ppQueue=%p\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc, *ppQueue));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnSSMRegister} */
static DECLCALLBACK(int) pdmR3UsbHlp_SSMRegister(PPDMUSBINS pUsbIns, uint32_t uVersion, size_t cbGuess,
                                                 PFNSSMUSBLIVEPREP pfnLivePrep, PFNSSMUSBLIVEEXEC pfnLiveExec, PFNSSMUSBLIVEVOTE pfnLiveVote,
                                                 PFNSSMUSBSAVEPREP pfnSavePrep, PFNSSMUSBSAVEEXEC pfnSaveExec, PFNSSMUSBSAVEDONE pfnSaveDone,
                                                 PFNSSMUSBLOADPREP pfnLoadPrep, PFNSSMUSBLOADEXEC pfnLoadExec, PFNSSMUSBLOADDONE pfnLoadDone)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    VM_ASSERT_EMT(pUsbIns->Internal.s.pVM);
    LogFlow(("pdmR3UsbHlp_SSMRegister: caller='%s'/%d: uVersion=%#x cbGuess=%#x\n"
             "    pfnLivePrep=%p pfnLiveExec=%p pfnLiveVote=%p pfnSavePrep=%p pfnSaveExec=%p pfnSaveDone=%p pszLoadPrep=%p pfnLoadExec=%p pfnLoadDone=%p\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, uVersion, cbGuess,
             pfnLivePrep, pfnLiveExec, pfnLiveVote,
             pfnSavePrep, pfnSaveExec, pfnSaveDone,
             pfnLoadPrep, pfnLoadExec, pfnLoadDone));

    int rc = SSMR3RegisterUsb(pUsbIns->Internal.s.pVM, pUsbIns, pUsbIns->pReg->szName, pUsbIns->iInstance,
                              uVersion, cbGuess,
                              pfnLivePrep, pfnLiveExec, pfnLiveVote,
                              pfnSavePrep, pfnSaveExec, pfnSaveDone,
                              pfnLoadPrep, pfnLoadExec, pfnLoadDone);

    LogFlow(("pdmR3UsbHlp_SSMRegister: caller='%s'/%d: returns %Rrc\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnSTAMRegisterV} */
static DECLCALLBACK(void) pdmR3UsbHlp_STAMRegisterV(PPDMUSBINS pUsbIns, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                                                    STAMUNIT enmUnit, const char *pszDesc, const char *pszName, va_list va)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);

    int rc = STAMR3RegisterV(pVM, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, va);
    AssertRC(rc);

    NOREF(pVM);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerCreate} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerCreate(PPDMUSBINS pUsbIns, TMCLOCK enmClock, PFNTMTIMERUSB pfnCallback, void *pvUser,
                                                 uint32_t fFlags, const char *pszDesc, PTMTIMERHANDLE phTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);
    LogFlow(("pdmR3UsbHlp_TMTimerCreate: caller='%s'/%d: enmClock=%d pfnCallback=%p pvUser=%p fFlags=%#x pszDesc=%p:{%s} phTimer=%p\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, enmClock, pfnCallback, pvUser, fFlags, pszDesc, pszDesc, phTimer));

    AssertReturn(!(fFlags & TMTIMER_FLAGS_RING0), VERR_INVALID_FLAGS);
    fFlags |= TMTIMER_FLAGS_NO_RING0;

    /* Mangle the timer name if there are more than one instance of this device. */
    char szName[32];
    AssertReturn(strlen(pszDesc) < sizeof(szName) - 8, VERR_INVALID_NAME);
    if (pUsbIns->iInstance > 0)
    {
        RTStrPrintf(szName, sizeof(szName), "%s[%u:%s]", pszDesc, pUsbIns->iInstance, pUsbIns->Internal.s.pUsbDev->pReg->szName);
        pszDesc = szName;
    }

    int rc = TMR3TimerCreateUsb(pVM, pUsbIns, enmClock, pfnCallback, pvUser, fFlags, pszDesc, phTimer);

    LogFlow(("pdmR3UsbHlp_TMTimerCreate: caller='%s'/%d: returns %Rrc *phTimer=%p\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc, *phTimer));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerFromMicro} */
static DECLCALLBACK(uint64_t) pdmR3UsbHlp_TimerFromMicro(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t cMicroSecs)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerFromMicro(pUsbIns->Internal.s.pVM, hTimer, cMicroSecs);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerFromMilli} */
static DECLCALLBACK(uint64_t) pdmR3UsbHlp_TimerFromMilli(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t cMilliSecs)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerFromMilli(pUsbIns->Internal.s.pVM, hTimer, cMilliSecs);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerFromNano} */
static DECLCALLBACK(uint64_t) pdmR3UsbHlp_TimerFromNano(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t cNanoSecs)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerFromNano(pUsbIns->Internal.s.pVM, hTimer, cNanoSecs);
}

/** @interface_method_impl{PDMUSBHLP,pfnTimerGet} */
static DECLCALLBACK(uint64_t) pdmR3UsbHlp_TimerGet(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerGet(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerGetFreq} */
static DECLCALLBACK(uint64_t) pdmR3UsbHlp_TimerGetFreq(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerGetFreq(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerGetNano} */
static DECLCALLBACK(uint64_t) pdmR3UsbHlp_TimerGetNano(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerGetNano(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerIsActive} */
static DECLCALLBACK(bool) pdmR3UsbHlp_TimerIsActive(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerIsActive(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerIsLockOwner} */
static DECLCALLBACK(bool) pdmR3UsbHlp_TimerIsLockOwner(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerIsLockOwner(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerLockClock} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerLockClock(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerLock(pUsbIns->Internal.s.pVM, hTimer, VERR_IGNORED);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerLockClock2} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerLockClock2(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM const pVM = pUsbIns->Internal.s.pVM;
    int rc = TMTimerLock(pVM, hTimer, VERR_IGNORED);
    if (rc == VINF_SUCCESS)
    {
        rc = PDMCritSectEnter(pVM, pCritSect, VERR_IGNORED);
        if (rc == VINF_SUCCESS)
            return rc;
        AssertRC(rc);
        TMTimerUnlock(pVM, hTimer);
    }
    else
        AssertRC(rc);
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSet} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSet(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t uExpire)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerSet(pUsbIns->Internal.s.pVM, hTimer, uExpire);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSetFrequencyHint} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSetFrequencyHint(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint32_t uHz)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerSetFrequencyHint(pUsbIns->Internal.s.pVM, hTimer, uHz);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSetMicro} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSetMicro(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerSetMicro(pUsbIns->Internal.s.pVM, hTimer, cMicrosToNext);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSetMillies} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSetMillies(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t cMilliesToNext)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerSetMillies(pUsbIns->Internal.s.pVM, hTimer, cMilliesToNext);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSetNano} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSetNano(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t cNanosToNext)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerSetNano(pUsbIns->Internal.s.pVM, hTimer, cNanosToNext);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSetRelative} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSetRelative(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerSetRelative(pUsbIns->Internal.s.pVM, hTimer, cTicksToNext, pu64Now);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerStop} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerStop(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMTimerStop(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerUnlockClock} */
static DECLCALLBACK(void) pdmR3UsbHlp_TimerUnlockClock(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    TMTimerUnlock(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerUnlockClock2} */
static DECLCALLBACK(void) pdmR3UsbHlp_TimerUnlockClock2(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM const pVM = pUsbIns->Internal.s.pVM;
    TMTimerUnlock(pVM, hTimer);
    int rc = PDMCritSectLeave(pVM, pCritSect);
    AssertRC(rc);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSetCritSect} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSetCritSect(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMR3TimerSetCritSect(pUsbIns->Internal.s.pVM, hTimer, pCritSect);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerSave} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerSave(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMR3TimerSave(pUsbIns->Internal.s.pVM, hTimer, pSSM);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerLoad} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerLoad(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMR3TimerLoad(pUsbIns->Internal.s.pVM, hTimer, pSSM);
}


/** @interface_method_impl{PDMUSBHLP,pfnTimerDestroy} */
static DECLCALLBACK(int) pdmR3UsbHlp_TimerDestroy(PPDMUSBINS pUsbIns, TMTIMERHANDLE hTimer)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    return TMR3TimerDestroy(pUsbIns->Internal.s.pVM, hTimer);
}


/** @interface_method_impl{PDMUSBHLP,pfnVMSetErrorV} */
static DECLCALLBACK(int) pdmR3UsbHlp_VMSetErrorV(PPDMUSBINS pUsbIns, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    int rc2 = VMSetErrorV(pUsbIns->Internal.s.pVM, rc, RT_SRC_POS_ARGS, pszFormat, va); Assert(rc2 == rc); NOREF(rc2);
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnVMSetRuntimeErrorV} */
static DECLCALLBACK(int) pdmR3UsbHlp_VMSetRuntimeErrorV(PPDMUSBINS pUsbIns, uint32_t fFlags, const char *pszErrorId, const char *pszFormat, va_list va)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    int rc = VMSetRuntimeErrorV(pUsbIns->Internal.s.pVM, fFlags, pszErrorId, pszFormat, va);
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnVMState} */
static DECLCALLBACK(VMSTATE) pdmR3UsbHlp_VMState(PPDMUSBINS pUsbIns)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);

    VMSTATE enmVMState = VMR3GetState(pUsbIns->Internal.s.pVM);

    LogFlow(("pdmR3UsbHlp_VMState: caller='%s'/%d: returns %d (%s)\n", pUsbIns->pReg->szName, pUsbIns->iInstance,
             enmVMState, VMR3GetStateName(enmVMState)));
    return enmVMState;
}

/** @interface_method_impl{PDMUSBHLP,pfnThreadCreate} */
static DECLCALLBACK(int) pdmR3UsbHlp_ThreadCreate(PPDMUSBINS pUsbIns, PPPDMTHREAD ppThread, void *pvUser, PFNPDMTHREADUSB pfnThread,
                                                  PFNPDMTHREADWAKEUPUSB pfnWakeup, size_t cbStack, RTTHREADTYPE enmType, const char *pszName)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    VM_ASSERT_EMT(pUsbIns->Internal.s.pVM);
    LogFlow(("pdmR3UsbHlp_ThreadCreate: caller='%s'/%d: ppThread=%p pvUser=%p pfnThread=%p pfnWakeup=%p cbStack=%#zx enmType=%d pszName=%p:{%s}\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName, pszName));

    int rc = pdmR3ThreadCreateUsb(pUsbIns->Internal.s.pVM, pUsbIns, ppThread, pvUser, pfnThread, pfnWakeup, cbStack, enmType, pszName);

    LogFlow(("pdmR3UsbHlp_ThreadCreate: caller='%s'/%d: returns %Rrc *ppThread=%RTthrd\n", pUsbIns->pReg->szName, pUsbIns->iInstance,
             rc, *ppThread));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnSetAsyncNotification} */
static DECLCALLBACK(int) pdmR3UsbHlp_SetAsyncNotification(PPDMUSBINS pUsbIns, PFNPDMUSBASYNCNOTIFY pfnAsyncNotify)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    VM_ASSERT_EMT0(pUsbIns->Internal.s.pVM);
    LogFlow(("pdmR3UsbHlp_SetAsyncNotification: caller='%s'/%d: pfnAsyncNotify=%p\n", pUsbIns->pReg->szName, pUsbIns->iInstance, pfnAsyncNotify));

    int rc = VINF_SUCCESS;
    AssertStmt(pfnAsyncNotify, rc = VERR_INVALID_PARAMETER);
    AssertStmt(!pUsbIns->Internal.s.pfnAsyncNotify, rc = VERR_WRONG_ORDER);
    AssertStmt(pUsbIns->Internal.s.fVMSuspended || pUsbIns->Internal.s.fVMReset, rc = VERR_WRONG_ORDER);
    VMSTATE enmVMState = VMR3GetState(pUsbIns->Internal.s.pVM);
    AssertStmt(   enmVMState == VMSTATE_SUSPENDING
               || enmVMState == VMSTATE_SUSPENDING_EXT_LS
               || enmVMState == VMSTATE_SUSPENDING_LS
               || enmVMState == VMSTATE_RESETTING
               || enmVMState == VMSTATE_RESETTING_LS
               || enmVMState == VMSTATE_POWERING_OFF
               || enmVMState == VMSTATE_POWERING_OFF_LS,
               rc = VERR_INVALID_STATE);

    if (RT_SUCCESS(rc))
        pUsbIns->Internal.s.pfnAsyncNotify = pfnAsyncNotify;

    LogFlow(("pdmR3UsbHlp_SetAsyncNotification: caller='%s'/%d: returns %Rrc\n", pUsbIns->pReg->szName, pUsbIns->iInstance, rc));
    return rc;
}


/** @interface_method_impl{PDMUSBHLP,pfnAsyncNotificationCompleted} */
static DECLCALLBACK(void) pdmR3UsbHlp_AsyncNotificationCompleted(PPDMUSBINS pUsbIns)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM pVM = pUsbIns->Internal.s.pVM;

    VMSTATE enmVMState = VMR3GetState(pVM);
    if (   enmVMState == VMSTATE_SUSPENDING
        || enmVMState == VMSTATE_SUSPENDING_EXT_LS
        || enmVMState == VMSTATE_SUSPENDING_LS
        || enmVMState == VMSTATE_RESETTING
        || enmVMState == VMSTATE_RESETTING_LS
        || enmVMState == VMSTATE_POWERING_OFF
        || enmVMState == VMSTATE_POWERING_OFF_LS)
    {
        LogFlow(("pdmR3UsbHlp_AsyncNotificationCompleted: caller='%s'/%d:\n", pUsbIns->pReg->szName, pUsbIns->iInstance));
        VMR3AsyncPdmNotificationWakeupU(pVM->pUVM);
    }
    else
        LogFlow(("pdmR3UsbHlp_AsyncNotificationCompleted: caller='%s'/%d: enmVMState=%d\n", pUsbIns->pReg->szName, pUsbIns->iInstance, enmVMState));
}


/** @interface_method_impl{PDMUSBHLP,pfnVMGetSuspendReason} */
static DECLCALLBACK(VMSUSPENDREASON) pdmR3UsbHlp_VMGetSuspendReason(PPDMUSBINS pUsbIns)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);
    VMSUSPENDREASON enmReason = VMR3GetSuspendReason(pVM->pUVM);
    LogFlow(("pdmR3UsbHlp_VMGetSuspendReason: caller='%s'/%d: returns %d\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMUSBHLP,pfnVMGetResumeReason} */
static DECLCALLBACK(VMRESUMEREASON) pdmR3UsbHlp_VMGetResumeReason(PPDMUSBINS pUsbIns)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM pVM = pUsbIns->Internal.s.pVM;
    VM_ASSERT_EMT(pVM);
    VMRESUMEREASON enmReason = VMR3GetResumeReason(pVM->pUVM);
    LogFlow(("pdmR3UsbHlp_VMGetResumeReason: caller='%s'/%d: returns %d\n",
             pUsbIns->pReg->szName, pUsbIns->iInstance, enmReason));
    return enmReason;
}


/** @interface_method_impl{PDMUSBHLP,pfnQueryGenericUserObject} */
static DECLCALLBACK(void *) pdmR3UsbHlp_QueryGenericUserObject(PPDMUSBINS pUsbIns, PCRTUUID pUuid)
{
    PDMUSB_ASSERT_USBINS(pUsbIns);
    PVM  pVM  = pUsbIns->Internal.s.pVM;
    PUVM pUVM = pVM->pUVM;

    void *pvRet;
    if (pUVM->pVmm2UserMethods->pfnQueryGenericObject)
        pvRet = pUVM->pVmm2UserMethods->pfnQueryGenericObject(pUVM->pVmm2UserMethods, pUVM, pUuid);
    else
        pvRet = NULL;

    Log(("pdmR3UsbHlp_QueryGenericUserObject: caller='%s'/%d: returns %#p for %RTuuid\n",
         pUsbIns->pReg->szName, pUsbIns->iInstance, pvRet, pUuid));
    return pvRet;
}


/**
 * The USB device helper structure.
 */
const PDMUSBHLP g_pdmR3UsbHlp =
{
    PDM_USBHLP_VERSION,
    pdmR3UsbHlp_DriverAttach,
    pdmR3UsbHlp_AssertEMT,
    pdmR3UsbHlp_AssertOther,
    pdmR3UsbHlp_DBGFStopV,
    pdmR3UsbHlp_DBGFInfoRegisterArgv,
    pdmR3UsbHlp_MMHeapAlloc,
    pdmR3UsbHlp_MMHeapAllocZ,
    pdmR3UsbHlp_MMHeapFree,
    pdmR3UsbHlp_PDMQueueCreate,
    pdmR3UsbHlp_SSMRegister,
    SSMR3PutStruct,
    SSMR3PutStructEx,
    SSMR3PutBool,
    SSMR3PutU8,
    SSMR3PutS8,
    SSMR3PutU16,
    SSMR3PutS16,
    SSMR3PutU32,
    SSMR3PutS32,
    SSMR3PutU64,
    SSMR3PutS64,
    SSMR3PutU128,
    SSMR3PutS128,
    SSMR3PutUInt,
    SSMR3PutSInt,
    SSMR3PutGCUInt,
    SSMR3PutGCUIntReg,
    SSMR3PutGCPhys32,
    SSMR3PutGCPhys64,
    SSMR3PutGCPhys,
    SSMR3PutGCPtr,
    SSMR3PutGCUIntPtr,
    SSMR3PutRCPtr,
    SSMR3PutIOPort,
    SSMR3PutSel,
    SSMR3PutMem,
    SSMR3PutStrZ,
    SSMR3GetStruct,
    SSMR3GetStructEx,
    SSMR3GetBool,
    SSMR3GetBoolV,
    SSMR3GetU8,
    SSMR3GetU8V,
    SSMR3GetS8,
    SSMR3GetS8V,
    SSMR3GetU16,
    SSMR3GetU16V,
    SSMR3GetS16,
    SSMR3GetS16V,
    SSMR3GetU32,
    SSMR3GetU32V,
    SSMR3GetS32,
    SSMR3GetS32V,
    SSMR3GetU64,
    SSMR3GetU64V,
    SSMR3GetS64,
    SSMR3GetS64V,
    SSMR3GetU128,
    SSMR3GetU128V,
    SSMR3GetS128,
    SSMR3GetS128V,
    SSMR3GetGCPhys32,
    SSMR3GetGCPhys32V,
    SSMR3GetGCPhys64,
    SSMR3GetGCPhys64V,
    SSMR3GetGCPhys,
    SSMR3GetGCPhysV,
    SSMR3GetUInt,
    SSMR3GetSInt,
    SSMR3GetGCUInt,
    SSMR3GetGCUIntReg,
    SSMR3GetGCPtr,
    SSMR3GetGCUIntPtr,
    SSMR3GetRCPtr,
    SSMR3GetIOPort,
    SSMR3GetSel,
    SSMR3GetMem,
    SSMR3GetStrZ,
    SSMR3GetStrZEx,
    SSMR3Skip,
    SSMR3SkipToEndOfUnit,
    SSMR3SetLoadError,
    SSMR3SetLoadErrorV,
    SSMR3SetCfgError,
    SSMR3SetCfgErrorV,
    SSMR3HandleGetStatus,
    SSMR3HandleGetAfter,
    SSMR3HandleIsLiveSave,
    SSMR3HandleMaxDowntime,
    SSMR3HandleHostBits,
    SSMR3HandleRevision,
    SSMR3HandleVersion,
    SSMR3HandleHostOSAndArch,
    CFGMR3Exists,
    CFGMR3QueryType,
    CFGMR3QuerySize,
    CFGMR3QueryInteger,
    CFGMR3QueryIntegerDef,
    CFGMR3QueryString,
    CFGMR3QueryStringDef,
    CFGMR3QueryBytes,
    CFGMR3QueryU64,
    CFGMR3QueryU64Def,
    CFGMR3QueryS64,
    CFGMR3QueryS64Def,
    CFGMR3QueryU32,
    CFGMR3QueryU32Def,
    CFGMR3QueryS32,
    CFGMR3QueryS32Def,
    CFGMR3QueryU16,
    CFGMR3QueryU16Def,
    CFGMR3QueryS16,
    CFGMR3QueryS16Def,
    CFGMR3QueryU8,
    CFGMR3QueryU8Def,
    CFGMR3QueryS8,
    CFGMR3QueryS8Def,
    CFGMR3QueryBool,
    CFGMR3QueryBoolDef,
    CFGMR3QueryPort,
    CFGMR3QueryPortDef,
    CFGMR3QueryUInt,
    CFGMR3QueryUIntDef,
    CFGMR3QuerySInt,
    CFGMR3QuerySIntDef,
    CFGMR3QueryGCPtr,
    CFGMR3QueryGCPtrDef,
    CFGMR3QueryGCPtrU,
    CFGMR3QueryGCPtrUDef,
    CFGMR3QueryGCPtrS,
    CFGMR3QueryGCPtrSDef,
    CFGMR3QueryStringAlloc,
    CFGMR3QueryStringAllocDef,
    CFGMR3GetParent,
    CFGMR3GetChild,
    CFGMR3GetChildF,
    CFGMR3GetChildFV,
    CFGMR3GetFirstChild,
    CFGMR3GetNextChild,
    CFGMR3GetName,
    CFGMR3GetNameLen,
    CFGMR3AreChildrenValid,
    CFGMR3GetFirstValue,
    CFGMR3GetNextValue,
    CFGMR3GetValueName,
    CFGMR3GetValueNameLen,
    CFGMR3GetValueType,
    CFGMR3AreValuesValid,
    CFGMR3ValidateConfig,
    pdmR3UsbHlp_STAMRegisterV,
    pdmR3UsbHlp_TimerCreate,
    pdmR3UsbHlp_TimerFromMicro,
    pdmR3UsbHlp_TimerFromMilli,
    pdmR3UsbHlp_TimerFromNano,
    pdmR3UsbHlp_TimerGet,
    pdmR3UsbHlp_TimerGetFreq,
    pdmR3UsbHlp_TimerGetNano,
    pdmR3UsbHlp_TimerIsActive,
    pdmR3UsbHlp_TimerIsLockOwner,
    pdmR3UsbHlp_TimerLockClock,
    pdmR3UsbHlp_TimerLockClock2,
    pdmR3UsbHlp_TimerSet,
    pdmR3UsbHlp_TimerSetFrequencyHint,
    pdmR3UsbHlp_TimerSetMicro,
    pdmR3UsbHlp_TimerSetMillies,
    pdmR3UsbHlp_TimerSetNano,
    pdmR3UsbHlp_TimerSetRelative,
    pdmR3UsbHlp_TimerStop,
    pdmR3UsbHlp_TimerUnlockClock,
    pdmR3UsbHlp_TimerUnlockClock2,
    pdmR3UsbHlp_TimerSetCritSect,
    pdmR3UsbHlp_TimerSave,
    pdmR3UsbHlp_TimerLoad,
    pdmR3UsbHlp_TimerDestroy,
    TMR3TimerSkip,
    pdmR3UsbHlp_VMSetErrorV,
    pdmR3UsbHlp_VMSetRuntimeErrorV,
    pdmR3UsbHlp_VMState,
    pdmR3UsbHlp_ThreadCreate,
    PDMR3ThreadDestroy,
    PDMR3ThreadIAmSuspending,
    PDMR3ThreadIAmRunning,
    PDMR3ThreadSleep,
    PDMR3ThreadSuspend,
    PDMR3ThreadResume,
    pdmR3UsbHlp_SetAsyncNotification,
    pdmR3UsbHlp_AsyncNotificationCompleted,
    pdmR3UsbHlp_VMGetSuspendReason,
    pdmR3UsbHlp_VMGetResumeReason,
    pdmR3UsbHlp_QueryGenericUserObject,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    PDM_USBHLP_VERSION
};

/** @}  */
