/* $Id: VBoxDisplay.cpp 108105 2025-01-28 16:08:20Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxSeamless - Display notifications.
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
#include "VBoxTray.h"
#include "VBoxHelpers.h"
#include "VBoxSeamless.h"

#include <iprt/alloca.h>
#include <iprt/assert.h>
#ifdef VBOX_WITH_WDDM
# include <iprt/asm.h>
#endif
#include <iprt/log.h>
#include <iprt/system.h>

#include <VBoxDisplay.h>
#include <VBoxHook.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct _VBOXDISPLAYCONTEXT
{
    const VBOXTRAYSVCENV *pEnv;
    BOOL fAnyX;
    /** ChangeDisplaySettingsEx does not exist in NT. ResizeDisplayDevice uses the function. */
    DECLCALLBACKMEMBER_EX(LONG,WINAPI, pfnChangeDisplaySettingsEx,(LPCTSTR lpszDeviceName, LPDEVMODE lpDevMode, HWND hwnd,
                                                                   DWORD dwflags, LPVOID lParam));
    /** EnumDisplayDevices does not exist in NT. */
    DECLCALLBACKMEMBER_EX(BOOL, WINAPI, pfnEnumDisplayDevices,(IN LPCSTR lpDevice, IN DWORD iDevNum,
                                                               OUT PDISPLAY_DEVICEA lpDisplayDevice, IN DWORD dwFlags));
    /** Display driver interface, XPDM - WDDM abstraction see VBOXDISPIF** definitions above */
    VBOXDISPIF dispIf;
} VBOXDISPLAYCONTEXT, *PVBOXDISPLAYCONTEXT;

typedef enum
{
    VBOXDISPLAY_DRIVER_TYPE_UNKNOWN = 0,
    VBOXDISPLAY_DRIVER_TYPE_XPDM    = 1,
    VBOXDISPLAY_DRIVER_TYPE_WDDM    = 2
} VBOXDISPLAY_DRIVER_TYPE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static VBOXDISPLAYCONTEXT g_Ctx = { 0 };


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static VBOXDISPLAY_DRIVER_TYPE getVBoxDisplayDriverType(VBOXDISPLAYCONTEXT *pCtx);


/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnPreInit}
 */
static DECLCALLBACK(int) vbtrDispPreInit(void)
{
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnOption}
 */
static DECLCALLBACK(int) vbtrDispOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    RT_NOREF(ppszShort, argc, argv, pi);

    return -1;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
static DECLCALLBACK(int) vbtrDisplayInit(const PVBOXTRAYSVCENV pEnv, void **ppvInstance)
{
    LogFlowFuncEnter();

    PVBOXDISPLAYCONTEXT pCtx = &g_Ctx; /** @todo r=andy Use instance data via service lookup (add void *pvInstance). */
    AssertPtr(pCtx);

    int rc;
    HMODULE hUser = GetModuleHandle("user32.dll"); /** @todo r=andy Use RTLdrXXX and friends. */

    pCtx->pEnv = pEnv;

    uint64_t const uNtVersion = RTSystemGetNtVersion();

    if (NULL == hUser)
    {
        LogFlowFunc(("Could not get module handle of USER32.DLL!\n"));
        rc = VERR_NOT_IMPLEMENTED;
    }
    else if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(5, 0, 0)) /* APIs available only on W2K and up. */
    {
        /** @todo r=andy Use RTLdrXXX and friends. */
        /** @todo r=andy No unicode version available? */
        *(uintptr_t *)&pCtx->pfnChangeDisplaySettingsEx = (uintptr_t)GetProcAddress(hUser, "ChangeDisplaySettingsExA");
        LogFlowFunc(("pfnChangeDisplaySettingsEx = %p\n", pCtx->pfnChangeDisplaySettingsEx));

        *(uintptr_t *)&pCtx->pfnEnumDisplayDevices = (uintptr_t)GetProcAddress(hUser, "EnumDisplayDevicesA");
        LogFlowFunc(("pfnEnumDisplayDevices = %p\n", pCtx->pfnEnumDisplayDevices));

#ifdef VBOX_WITH_WDDM
        if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
        {
            /* This is Vista and up, check if we need to switch the display driver if to WDDM mode. */
            LogFlowFunc(("this is Windows Vista and up\n"));
            VBOXDISPLAY_DRIVER_TYPE enmType = getVBoxDisplayDriverType(pCtx);
            if (enmType == VBOXDISPLAY_DRIVER_TYPE_WDDM)
            {
                LogFlowFunc(("WDDM driver is installed, switching display driver if to WDDM mode\n"));
                /* This is hacky, but the most easiest way. */
                VBOXDISPIF_MODE enmMode = uNtVersion < RTSYSTEM_MAKE_NT_VERSION(6, 1, 0)
                                        ? VBOXDISPIF_MODE_WDDM : VBOXDISPIF_MODE_WDDM_W7;
                DWORD dwErr = VBoxDispIfSwitchMode(const_cast<PVBOXDISPIF>(&pEnv->dispIf), enmMode, NULL /* old mode, we don't care about it */);
                if (dwErr == NO_ERROR)
                {
                    LogFlowFunc(("DispIf successfully switched to WDDM mode\n"));
                    rc = VINF_SUCCESS;
                }
                else
                {
                    LogFlowFunc(("Failed to switch DispIf to WDDM mode, error (%d)\n", dwErr));
                    rc = RTErrConvertFromWin32(dwErr);
                }
            }
            else
                rc = VINF_SUCCESS;
        }
        else
            rc = VINF_SUCCESS;
#endif
    }
    else if (uNtVersion < RTSYSTEM_MAKE_NT_VERSION(5, 0, 0)) /* Windows NT 4.0. */
    {
        /* Nothing to do here yet. */
        /** @todo r=andy Has this been tested? */
        rc = VINF_SUCCESS;
    }
    else                                 /* Unsupported platform. */
    {
        LogFlowFunc(("Warning: Display for platform not handled yet!\n"));
        rc = VERR_NOT_IMPLEMENTED;
    }

    if (RT_SUCCESS(rc))
    {
        VBOXDISPIFESCAPE_ISANYX IsAnyX = { {0} };
        IsAnyX.EscapeHdr.escapeCode = VBOXESC_ISANYX;
        DWORD err = VBoxDispIfEscapeInOut(&pEnv->dispIf, &IsAnyX.EscapeHdr, sizeof(uint32_t));
        if (err == NO_ERROR)
            pCtx->fAnyX = !!IsAnyX.u32IsAnyX;
        else
            pCtx->fAnyX = TRUE;

        *ppvInstance = pCtx;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnDestroy}
 */
static DECLCALLBACK(void) vbtrDisplayDestroy(void *pvInstance)
{
    RT_NOREF(pvInstance);
    return;
}

static VBOXDISPLAY_DRIVER_TYPE getVBoxDisplayDriverType(PVBOXDISPLAYCONTEXT pCtx)
{
    VBOXDISPLAY_DRIVER_TYPE enmType = VBOXDISPLAY_DRIVER_TYPE_UNKNOWN;

    if (pCtx->pfnEnumDisplayDevices)
    {
        INT devNum = 0;
        DISPLAY_DEVICE dispDevice;
        FillMemory(&dispDevice, sizeof(DISPLAY_DEVICE), 0);
        dispDevice.cb = sizeof(DISPLAY_DEVICE);

        LogFlowFunc(("getVBoxDisplayDriverType: Checking for active VBox display driver (W2K+) ...\n"));

        while (EnumDisplayDevices(NULL,
                                  devNum,
                                  &dispDevice,
                                  0))
        {
            LogFlowFunc(("getVBoxDisplayDriverType: DevNum:%d\nName:%s\nString:%s\nID:%s\nKey:%s\nFlags=%08X\n\n",
                          devNum,
                          &dispDevice.DeviceName[0],
                          &dispDevice.DeviceString[0],
                          &dispDevice.DeviceID[0],
                          &dispDevice.DeviceKey[0],
                          dispDevice.StateFlags));

            if (dispDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            {
                LogFlowFunc(("getVBoxDisplayDriverType: Primary device\n"));

                /* WDDM driver can now have multiple incarnations,
                * if the driver name contains VirtualBox, and does NOT match the XPDM name,
                * assume it to be WDDM */
                if (strcmp(&dispDevice.DeviceString[0], "VirtualBox Graphics Adapter") == 0)
                    enmType = VBOXDISPLAY_DRIVER_TYPE_XPDM;
                else if (strstr(&dispDevice.DeviceString[0], "VirtualBox"))
                    enmType = VBOXDISPLAY_DRIVER_TYPE_WDDM;

                break;
            }

            FillMemory(&dispDevice, sizeof(DISPLAY_DEVICE), 0);

            dispDevice.cb = sizeof(DISPLAY_DEVICE);

            devNum++;
        }
    }
    else    /* This must be NT 4 or something really old, so don't use EnumDisplayDevices() here  ... */
    {
        LogFlowFunc(("getVBoxDisplayDriverType: Checking for active VBox display driver (NT or older) ...\n"));

        DEVMODE tempDevMode;
        ZeroMemory (&tempDevMode, sizeof (tempDevMode));
        tempDevMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &tempDevMode);     /* Get current display device settings */

        /* Check for the short name, because all long stuff would be truncated */
        if (strcmp((char*)&tempDevMode.dmDeviceName[0], "VBoxDisp") == 0)
            enmType = VBOXDISPLAY_DRIVER_TYPE_XPDM;
    }

    return enmType;
}

/** @todo r=andy The "display", "seamless" (and VBoxCaps facility in VBoxTray.cpp indirectly) is using this.
 *               Add a PVBOXDISPLAYCONTEXT here for properly getting the display (XPDM/WDDM) abstraction interfaces. */
DWORD EnableAndResizeDispDev(DEVMODE *paDeviceModes, DISPLAY_DEVICE *paDisplayDevices,
                             DWORD totalDispNum, UINT Id, DWORD aWidth, DWORD aHeight,
                             DWORD aBitsPerPixel, LONG aPosX, LONG aPosY, BOOL fEnabled, BOOL fExtDispSup)
{
    DISPLAY_DEVICE displayDeviceTmp;
    DISPLAY_DEVICE displayDevice;
    DEVMODE deviceMode;
    DWORD dwStatus = DISP_CHANGE_SUCCESSFUL;
    DWORD iter ;

    PVBOXDISPLAYCONTEXT pCtx = &g_Ctx; /* See todo above. */

    deviceMode = paDeviceModes[Id];
    displayDevice = paDisplayDevices[Id];

    for (iter = 0; iter < totalDispNum; iter++)
    {
        if (iter != 0 && iter != Id && !(paDisplayDevices[iter].StateFlags & DISPLAY_DEVICE_ACTIVE))
        {
            LogRel(("Display: Initially disabling monitor with ID=%ld; total monitor count is %ld\n", iter, totalDispNum));
            DEVMODE deviceModeTmp;
            ZeroMemory(&deviceModeTmp, sizeof(DEVMODE));
            deviceModeTmp.dmSize = sizeof(DEVMODE);
            deviceModeTmp.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_POSITION
                                     | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS ;
            displayDeviceTmp = paDisplayDevices[iter];
            pCtx->pfnChangeDisplaySettingsEx(displayDeviceTmp.DeviceName, &deviceModeTmp, NULL,
                                             (CDS_UPDATEREGISTRY | CDS_NORESET), NULL);
        }
    }

    if (fExtDispSup) /* Extended Display Support possible*/
    {
        if (fEnabled)
        {
            /* Special case for enabling the secondary monitor. */
            if(!(displayDevice.StateFlags & DISPLAY_DEVICE_ACTIVE))
            {
                LogRel(("Display [ID=%ld, name='%s']: Is a secondary monitor and disabled -- enabling it\n", Id, displayDevice.DeviceName));
                deviceMode.dmPosition.x = paDeviceModes[0].dmPelsWidth;
                deviceMode.dmPosition.y = 0;
                deviceMode.dmBitsPerPel = 32;

                uint64_t const uNtVersion = RTSystemGetNtVersion();
                if (uNtVersion < RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
                    /* dont any more flags here as, only DM_POISITON is used to enable the secondary display */
                    deviceMode.dmFields = DM_POSITION;
                else /* for win 7 and above */
                    /* for vista and above DM_BITSPERPEL is necessary */
                    deviceMode.dmFields = DM_BITSPERPEL | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY  | DM_POSITION;

                dwStatus = pCtx->pfnChangeDisplaySettingsEx((LPSTR)displayDevice.DeviceName,&deviceMode, NULL, (CDS_UPDATEREGISTRY | CDS_NORESET), NULL);
                /* A second call to ChangeDisplaySettings updates the monitor.*/
                pCtx->pfnChangeDisplaySettingsEx(NULL, NULL, NULL,0, NULL);
            }
            else /* secondary monitor already enabled. Request to change the resolution or position. */
            {
                if (aWidth !=0 && aHeight != 0)
                {
                    LogRel(("Display [ID=%ld, name='%s']: Changing resolution to %ldx%ld\n", Id, displayDevice.DeviceName, aWidth, aHeight));
                    deviceMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL
                                          | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;
                    deviceMode.dmPelsWidth = aWidth;
                    deviceMode.dmPelsHeight = aHeight;
                    deviceMode.dmBitsPerPel = aBitsPerPixel;
                }
                if (aPosX != 0 || aPosY != 0)
                {
                    LogRel(("Display [ID=%ld, name='%s']: Changing position to %ld,%ld\n", Id, displayDevice.DeviceName, aPosX, aPosY));
                    deviceMode.dmFields |=  DM_POSITION;
                    deviceMode.dmPosition.x = aPosX;
                    deviceMode.dmPosition.y = aPosY;
                }
                dwStatus = pCtx->pfnChangeDisplaySettingsEx((LPSTR)displayDevice.DeviceName,
                                                            &deviceMode, NULL, CDS_NORESET|CDS_UPDATEREGISTRY, NULL);
                /* A second call to ChangeDisplaySettings updates the monitor. */
                pCtx->pfnChangeDisplaySettingsEx(NULL, NULL, NULL,0, NULL);
            }
        }
        else /* Request is there to disable the monitor with ID = Id*/
        {
            LogRel(("Display [ID=%ld, name='%s']: Disalbing\n", Id, displayDevice.DeviceName));

            DEVMODE deviceModeTmp;
            ZeroMemory(&deviceModeTmp, sizeof(DEVMODE));
            deviceModeTmp.dmSize = sizeof(DEVMODE);
            deviceModeTmp.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_POSITION
                                     | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS ;
            displayDeviceTmp = paDisplayDevices[Id];
            dwStatus = pCtx->pfnChangeDisplaySettingsEx(displayDeviceTmp.DeviceName, &deviceModeTmp, NULL,
                                                        (CDS_UPDATEREGISTRY | CDS_NORESET), NULL);
            pCtx->pfnChangeDisplaySettingsEx(NULL, NULL, NULL,0, NULL);
        }
    }
    return dwStatus;
}

DWORD VBoxDisplayGetCount(void)
{
    DISPLAY_DEVICE DisplayDevice;

    ZeroMemory(&DisplayDevice, sizeof(DISPLAY_DEVICE));
    DisplayDevice.cb = sizeof(DISPLAY_DEVICE);

    /* Find out how many display devices the system has */
    DWORD NumDevices = 0;
    DWORD i = 0;
    while (EnumDisplayDevices (NULL, i, &DisplayDevice, 0))
    {
        LogFlowFunc(("ResizeDisplayDevice: [%d] %s\n", i, DisplayDevice.DeviceName));

        if (DisplayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            LogFlowFunc(("ResizeDisplayDevice: Found primary device. err %d\n", GetLastError ()));
            NumDevices++;
        }
        else if (!(DisplayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
        {

            LogFlowFunc(("ResizeDisplayDevice: Found secondary device. err %d\n", GetLastError ()));
            NumDevices++;
        }

        ZeroMemory(&DisplayDevice, sizeof(DisplayDevice));
        DisplayDevice.cb = sizeof(DisplayDevice);
        i++;
    }

    return NumDevices;
}

DWORD VBoxDisplayGetConfig(const DWORD NumDevices, DWORD *pDevPrimaryNum, DWORD *pNumDevices,
                           DISPLAY_DEVICE *paDisplayDevices, DEVMODE *paDeviceModes)
{
    /* Fetch information about current devices and modes. */
    DWORD DevNum = 0;
    DWORD DevPrimaryNum = 0;

    DISPLAY_DEVICE DisplayDevice;

    ZeroMemory(&DisplayDevice, sizeof(DISPLAY_DEVICE));
    DisplayDevice.cb = sizeof(DISPLAY_DEVICE);

    DWORD i = 0;
    while (EnumDisplayDevices (NULL, i, &DisplayDevice, 0))
    {
        LogFlowFunc(("ResizeDisplayDevice: [%d(%d)] %s\n", i, DevNum, DisplayDevice.DeviceName));

        BOOL bFetchDevice = FALSE;

        if (DisplayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            LogFlowFunc(("ResizeDisplayDevice: Found primary device. err %d\n", GetLastError ()));
            DevPrimaryNum = DevNum;
            bFetchDevice = TRUE;
        }
        else if (!(DisplayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
        {

            LogFlowFunc(("ResizeDisplayDevice: Found secondary device. err %d\n", GetLastError ()));
            bFetchDevice = TRUE;
        }

        if (bFetchDevice)
        {
            if (DevNum >= NumDevices)
            {
                LogFlowFunc(("ResizeDisplayDevice: %d >= %d\n", NumDevices, DevNum));
                return ERROR_BUFFER_OVERFLOW;
            }

            paDisplayDevices[DevNum] = DisplayDevice;

            /* First try to get the video mode stored in registry (ENUM_REGISTRY_SETTINGS).
             * A secondary display could be not active at the moment and would not have
             * a current video mode (ENUM_CURRENT_SETTINGS).
             */
            ZeroMemory(&paDeviceModes[DevNum], sizeof(DEVMODE));
            paDeviceModes[DevNum].dmSize = sizeof(DEVMODE);
            if (!EnumDisplaySettings((LPSTR)DisplayDevice.DeviceName,
                 ENUM_REGISTRY_SETTINGS, &paDeviceModes[DevNum]))
            {
                LogFlowFunc(("ResizeDisplayDevice: EnumDisplaySettings error %d\n", GetLastError ()));
            }

            if (   paDeviceModes[DevNum].dmPelsWidth == 0
                || paDeviceModes[DevNum].dmPelsHeight == 0)
            {
                /* No ENUM_REGISTRY_SETTINGS yet. Seen on Vista after installation.
                 * Get the current video mode then.
                 */
                ZeroMemory(&paDeviceModes[DevNum], sizeof(DEVMODE));
                paDeviceModes[DevNum].dmSize = sizeof(DEVMODE);
                if (!EnumDisplaySettings((LPSTR)DisplayDevice.DeviceName,
                     ENUM_CURRENT_SETTINGS, &paDeviceModes[DevNum]))
                {
                    /* ENUM_CURRENT_SETTINGS returns FALSE when the display is not active:
                     * for example a disabled secondary display.
                     * Do not return here, ignore the error and set the display info to 0x0x0.
                     */
                    LogFlowFunc(("ResizeDisplayDevice: EnumDisplaySettings(ENUM_CURRENT_SETTINGS) error %d\n", GetLastError ()));
                }
            }


            DevNum++;
        }

        ZeroMemory(&DisplayDevice, sizeof(DISPLAY_DEVICE));
        DisplayDevice.cb = sizeof(DISPLAY_DEVICE);
        i++;
    }

    *pNumDevices = DevNum;
    *pDevPrimaryNum = DevPrimaryNum;

    return NO_ERROR;
}

static void ResizeDisplayDeviceNT4(DWORD dwNewXRes, DWORD dwNewYRes, DWORD dwNewBpp)
{
    DEVMODE devMode;

    RT_ZERO(devMode);
    devMode.dmSize = sizeof(DEVMODE);

    /* get the current screen setup */
    if (!EnumDisplaySettings(NULL, ENUM_REGISTRY_SETTINGS, &devMode))
    {
        LogFlowFunc(("error from EnumDisplaySettings: %d\n", GetLastError()));
        return;
    }

    LogFlowFunc(("Current mode: %d x %d x %d at %d,%d\n",
        devMode.dmPelsWidth, devMode.dmPelsHeight, devMode.dmBitsPerPel, devMode.dmPosition.x, devMode.dmPosition.y));

    /* Check whether a mode reset or a change is requested. */
    if (dwNewXRes || dwNewYRes || dwNewBpp)
    {
        /* A change is requested.
        * Set values which are not to be changed to the current values.
        */
        if (!dwNewXRes)
            dwNewXRes = devMode.dmPelsWidth;
        if (!dwNewYRes)
            dwNewYRes = devMode.dmPelsHeight;
        if (!dwNewBpp)
            dwNewBpp = devMode.dmBitsPerPel;
    }
    else
    {
        /* All zero values means a forced mode reset. Do nothing. */
        LogFlowFunc(("Forced mode reset\n"));
    }

    /* Verify that the mode is indeed changed. */
    if (devMode.dmPelsWidth == dwNewXRes
        && devMode.dmPelsHeight == dwNewYRes
        && devMode.dmBitsPerPel == dwNewBpp)
    {
        LogFlowFunc(("already at desired resolution\n"));
        return;
    }

    // without this, Windows will not ask the miniport for its
    // mode table but uses an internal cache instead
    DEVMODE tempDevMode = { 0 };
    tempDevMode.dmSize = sizeof(DEVMODE);
    EnumDisplaySettings(NULL, 0xffffff, &tempDevMode);

    /* adjust the values that are supposed to change */
    if (dwNewXRes)
        devMode.dmPelsWidth = dwNewXRes;
    if (dwNewYRes)
        devMode.dmPelsHeight = dwNewYRes;
    if (dwNewBpp)
        devMode.dmBitsPerPel = dwNewBpp;

    LogFlowFunc(("setting new mode %d x %d, %d BPP\n",
        devMode.dmPelsWidth, devMode.dmPelsHeight, devMode.dmBitsPerPel));

    /* set the new mode */
    LONG status = ChangeDisplaySettings(&devMode, CDS_UPDATEREGISTRY);
    if (status != DISP_CHANGE_SUCCESSFUL)
    {
        LogFlowFunc(("error from ChangeDisplaySettings: %d\n", status));

        if (status == DISP_CHANGE_BADMODE)
        {
            /* Our driver can not set the requested mode. Stop trying. */
            return;
        }
    }
}

/* Returns TRUE to try again. */
/** @todo r=andy Why not using the VMMDevDisplayChangeRequestEx structure for all those parameters here? */
static BOOL ResizeDisplayDevice(PVBOXDISPLAYCONTEXT pCtx,
                                UINT Id, DWORD Width, DWORD Height, DWORD BitsPerPixel,
                                BOOL fEnabled, LONG dwNewPosX, LONG dwNewPosY, bool fChangeOrigin,
                                BOOL fExtDispSup)
{
    BOOL fDispAlreadyEnabled = false; /* check whether the monitor with ID is already enabled. */
    BOOL fModeReset = (   Width == 0 && Height == 0 && BitsPerPixel == 0
                       && dwNewPosX == 0 && dwNewPosY == 0 && !fChangeOrigin);
    DWORD dmFields = 0;
    VBOXDISPLAY_DRIVER_TYPE enmDriverType = getVBoxDisplayDriverType(pCtx);

    LogFlowFunc(("[%d] %dx%d at %d,%d fChangeOrigin %d fEnabled %d fExtDisSup %d\n",
                 Id, Width, Height, dwNewPosX, dwNewPosY, fChangeOrigin, fEnabled, fExtDispSup));

    if (!pCtx->fAnyX)
        Width &= 0xFFF8;

    VBoxDispIfCancelPendingResize(&pCtx->pEnv->dispIf);

    DWORD NumDevices = VBoxDisplayGetCount();

    if (NumDevices == 0 || Id >= NumDevices)
    {
        LogFlowFunc(("ResizeDisplayDevice: Requested identifier %d is invalid. err %d\n", Id, GetLastError ()));
        return FALSE;
    }

    LogFlowFunc(("ResizeDisplayDevice: Found total %d devices. err %d\n", NumDevices, GetLastError ()));

    DISPLAY_DEVICE *paDisplayDevices = (DISPLAY_DEVICE *)alloca(sizeof (DISPLAY_DEVICE) * NumDevices);
    DEVMODE *paDeviceModes = (DEVMODE *)alloca(sizeof (DEVMODE) * NumDevices);
    RECTL *paRects = (RECTL *)alloca (sizeof (RECTL) * NumDevices);
    DWORD DevNum = 0;
    DWORD DevPrimaryNum = 0;
    DWORD dwStatus = VBoxDisplayGetConfig(NumDevices, &DevPrimaryNum, &DevNum, paDisplayDevices, paDeviceModes);
    if (dwStatus != NO_ERROR)
    {
        LogFlowFunc(("ResizeDisplayDevice: VBoxGetDisplayConfig failed, %d\n", dwStatus));
        return dwStatus;
    }

    if (NumDevices != DevNum)
        LogFlowFunc(("ResizeDisplayDevice: NumDevices(%d) != DevNum(%d)\n", NumDevices, DevNum));

    DWORD i;
    for (i = 0; i < DevNum; ++i)
    {
        if (fExtDispSup)
        {
            LogRel(("Extended Display Support.\n"));
            LogFlowFunc(("[%d] %dx%dx%d at %d,%d, dmFields 0x%x\n",
                  i,
                  paDeviceModes[i].dmPelsWidth,
                  paDeviceModes[i].dmPelsHeight,
                  paDeviceModes[i].dmBitsPerPel,
                  paDeviceModes[i].dmPosition.x,
                  paDeviceModes[i].dmPosition.y,
                  paDeviceModes[i].dmFields));
        }
        else
        {
            LogRel(("NO Ext Display Support \n"));
        }

        paRects[i].left   = paDeviceModes[i].dmPosition.x;
        paRects[i].top    = paDeviceModes[i].dmPosition.y;
        paRects[i].right  = paDeviceModes[i].dmPosition.x + paDeviceModes[i].dmPelsWidth;
        paRects[i].bottom = paDeviceModes[i].dmPosition.y + paDeviceModes[i].dmPelsHeight;
    }

    /* Keep a record if the display with ID is already active or not. */
    if (paDisplayDevices[Id].StateFlags & DISPLAY_DEVICE_ACTIVE)
    {
        LogRel(("Display with ID=%d already enabled\n", Id));
        fDispAlreadyEnabled = TRUE;
    }

    /* Width, height equal to 0 means that this value must be not changed.
     * Update input parameters if necessary.
     * Note: BitsPerPixel is taken into account later, when new rectangles
     *       are assigned to displays.
     */
    if (Width == 0)
        Width = paRects[Id].right - paRects[Id].left;
    else
        dmFields |= DM_PELSWIDTH;

    if (Height == 0)
        Height = paRects[Id].bottom - paRects[Id].top;
    else
        dmFields |= DM_PELSHEIGHT;

    if (BitsPerPixel == 0)
        BitsPerPixel = paDeviceModes[Id].dmBitsPerPel;
    else
        dmFields |= DM_BITSPERPEL;

    if (!fChangeOrigin)
    {
        /* Use existing position. */
        dwNewPosX = paRects[Id].left;
        dwNewPosY = paRects[Id].top;
        LogFlowFunc(("existing dwNewPosX %d, dwNewPosY %d\n", dwNewPosX, dwNewPosY));
    }

    /* Always update the position.
     * It is either explicitly requested or must be set to the existing position.
     */
    dmFields |= DM_POSITION;

    /* Check whether a mode reset or a change is requested.
     * Rectangle position is recalculated only if fEnabled is 1.
     * For non extended supported modes (old Host VMs), fEnabled
     * is always 1.
     */
    /* Handled the case where previouseresolution of secondary monitor
     * was for eg. 1024*768*32 and monitor was in disabled state.
     * User gives the command
     * setvideomode 1024 768 32 1 yes.
     * Now in this case the resolution request is same as previous one but
     * monitor is going from disabled to enabled state so the below condition
     * shour return false
     * The below condition will only return true , if no mode reset has
     * been requested AND fEnabled is 1 and fDispAlreadyEnabled is also 1 AND
     * all rect conditions are true. Thus in this case nothing has to be done.
     */
    if (   !fModeReset
        && (!fEnabled == !fDispAlreadyEnabled)
        && paRects[Id].left                      == dwNewPosX
        && paRects[Id].top                       == dwNewPosY
        && paRects[Id].right  - paRects[Id].left == (LONG)Width
        && paRects[Id].bottom - paRects[Id].top  == (LONG)Height
        && paDeviceModes[Id].dmBitsPerPel == BitsPerPixel)
    {
        LogRel(("Already at desired resolution. No Change.\n"));
        return FALSE;
    }

    hlpResizeRect(paRects, NumDevices, DevPrimaryNum, Id,
            fEnabled ? Width : 0, fEnabled ? Height : 0, dwNewPosX, dwNewPosY);

    for (i = 0; i < NumDevices; i++)
    {
        LogFlowFunc(("ResizeDisplayDevice: [%d]: %d,%d %dx%d\n",
                i, paRects[i].left, paRects[i].top,
                paRects[i].right - paRects[i].left,
                paRects[i].bottom - paRects[i].top));
    }

    /* Assign the new rectangles to displays. */
    for (i = 0; i < NumDevices; i++)
    {
        paDeviceModes[i].dmPosition.x = paRects[i].left;
        paDeviceModes[i].dmPosition.y = paRects[i].top;
        paDeviceModes[i].dmPelsWidth  = paRects[i].right - paRects[i].left;
        paDeviceModes[i].dmPelsHeight = paRects[i].bottom - paRects[i].top;

        if (i == Id)
            paDeviceModes[i].dmBitsPerPel = BitsPerPixel;

        if (enmDriverType >= VBOXDISPLAY_DRIVER_TYPE_WDDM)
        {
            paDeviceModes[i].dmFields |= dmFields;

            /* On Vista one must specify DM_BITSPERPEL.
            * Note that the current mode dmBitsPerPel is already in the DEVMODE structure.
            */
            if (!(paDeviceModes[i].dmFields & DM_BITSPERPEL))
            {
                LogFlowFunc(("no DM_BITSPERPEL\n"));
                paDeviceModes[i].dmFields |= DM_BITSPERPEL;
                paDeviceModes[i].dmBitsPerPel = 32;
            }
        }
        else
        {
            paDeviceModes[i].dmFields = DM_POSITION | DM_PELSHEIGHT | DM_PELSWIDTH | DM_BITSPERPEL;
        }

        LogFlowFunc(("ResizeDisplayDevice: Going to resize display %d to %dx%dx%d at %d,%d fields 0x%X\n",
            i,
            paDeviceModes[i].dmPelsWidth,
            paDeviceModes[i].dmPelsHeight,
            paDeviceModes[i].dmBitsPerPel,
            paDeviceModes[i].dmPosition.x,
            paDeviceModes[i].dmPosition.y,
            paDeviceModes[i].dmFields));
    }

    if (enmDriverType == VBOXDISPLAY_DRIVER_TYPE_WDDM)
    {
        DWORD err = VBoxDispIfResizeModes(&pCtx->pEnv->dispIf, Id, fEnabled, fExtDispSup, paDisplayDevices, paDeviceModes, DevNum);

        return (err == ERROR_RETRY);
    }

    /* The XPDM code path goes below.
     * Re-requesting modes with EnumDisplaySettings forces Windows to again ask the miniport for its mode table.
     */
    for (i = 0; i < NumDevices; i++)
    {
        DEVMODE tempDevMode;
        ZeroMemory (&tempDevMode, sizeof (tempDevMode));
        tempDevMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettings((LPSTR)paDisplayDevices[i].DeviceName, 0xffffff, &tempDevMode);
        LogFlowFunc(("ResizeDisplayDevice: EnumDisplaySettings last error %d\n", GetLastError ()));
    }

    /* Assign the new rectangles to displays. */
    for (i = 0; i < NumDevices; i++)
    {
        LONG status = pCtx->pfnChangeDisplaySettingsEx((LPSTR)paDisplayDevices[i].DeviceName,
                                                       &paDeviceModes[i], NULL, CDS_NORESET | CDS_UPDATEREGISTRY, NULL);
        LogFlowFunc(("ResizeDisplayDevice: ChangeDisplaySettingsEx position status %d, err %d\n", status, GetLastError()));
        NOREF(status);
    }

    LogFlowFunc(("Enable And Resize Device. Id = %d, Width=%d Height=%d, dwNewPosX = %d, dwNewPosY = %d fEnabled=%d & fExtDispSupport = %d \n",
                 Id, Width, Height, dwNewPosX, dwNewPosY, fEnabled, fExtDispSup));
    dwStatus = EnableAndResizeDispDev(paDeviceModes, paDisplayDevices, DevNum, Id, Width, Height, BitsPerPixel,
                                      dwNewPosX, dwNewPosY, fEnabled, fExtDispSup);
    if (dwStatus == DISP_CHANGE_SUCCESSFUL || dwStatus == DISP_CHANGE_BADMODE)
    {
        /* Successfully set new video mode or our driver can not set
         * the requested mode. Stop trying.
         */
        return FALSE;
    }
    /* Retry the request. */
    return TRUE;
}

static void doResize(PVBOXDISPLAYCONTEXT pCtx,
                     uint32_t iDisplay,
                     uint32_t cx,
                     uint32_t cy,
                     uint32_t cBits,
                     bool     fEnabled,
                     uint32_t cxOrigin,
                     uint32_t cyOrigin,
                     bool     fChangeOrigin)
{
    for (;;)
    {
        VBOXDISPLAY_DRIVER_TYPE enmDriverType = getVBoxDisplayDriverType(pCtx);
        if (enmDriverType == VBOXDISPLAY_DRIVER_TYPE_UNKNOWN)
        {
            LogFlowFunc(("vboxDisplayDriver is not active\n"));
            break;
        }

        if (pCtx->pfnChangeDisplaySettingsEx != 0)
        {
            LogFlowFunc(("Detected W2K or later\n"));
            if (!ResizeDisplayDevice(pCtx,
                                     iDisplay,
                                     cx,
                                     cy,
                                     cBits,
                                     fEnabled,
                                     cxOrigin,
                                     cyOrigin,
                                     fChangeOrigin,
                                     true /*fExtDispSup*/ ))
            {
                LogFlowFunc(("ResizeDipspalyDevice return 0\n"));
                break;
            }

        }
        else
        {
            LogFlowFunc(("Detected NT\n"));
            ResizeDisplayDeviceNT4(cx, cy, cBits);
            break;
        }

        /* Retry the change a bit later. */
        RTThreadSleep(1000);
    }
}

static BOOL DisplayChangeRequestHandler(PVBOXDISPLAYCONTEXT pCtx)
{
    VMMDevDisplayDef aDisplays[64];
    uint32_t cDisplays = RT_ELEMENTS(aDisplays);
    int rc = VINF_SUCCESS;

    /* Multidisplay resize is still implemented only for Win7 and newer guests. */
    if (pCtx->pEnv->dispIf.enmMode >= VBOXDISPIF_MODE_WDDM_W7 &&
        RT_SUCCESS(rc = VbglR3GetDisplayChangeRequestMulti(cDisplays, &cDisplays, &aDisplays[0], true /* fAck */)))
    {
        uint32_t i;

        LogRel(("Got multi resize request %d displays\n", cDisplays));

        for (i = 0; i < cDisplays; ++i)
        {
            LogRel(("[%d]: %d 0x%02X %d,%d %dx%d %d\n",
                i, aDisplays[i].idDisplay,
                aDisplays[i].fDisplayFlags,
                aDisplays[i].xOrigin,
                aDisplays[i].yOrigin,
                aDisplays[i].cx,
                aDisplays[i].cy,
                aDisplays[i].cBitsPerPixel));
        }

        return VBoxDispIfResizeDisplayWin7(&pCtx->pEnv->dispIf, cDisplays, &aDisplays[0]);
    }

    /* Fall back to the single monitor resize request. */

    /*
    * We got at least one event. (bird: What does that mean actually?  The driver wakes us up immediately upon
    * receiving the event.  Or are we refering to mouse & display?  In the latter case it's misleading.)
    *
    * Read the requested resolution and try to set it until success.
    * New events will not be seen but a new resolution will be read in
    * this poll loop.
    *
    * Note! The interface we're using here was added in VBox 4.2.4.  As of 2017-08-16, this
    *       version has been unsupported for a long time and we therefore don't bother
    *       implementing fallbacks using VMMDevDisplayChangeRequest2 and VMMDevDisplayChangeRequest.
    */
    uint32_t cx = 0;
    uint32_t cy = 0;
    uint32_t cBits = 0;
    uint32_t iDisplay = 0;
    uint32_t cxOrigin = 0;
    uint32_t cyOrigin = 0;
    bool     fChangeOrigin = false;
    bool     fEnabled = false;
    rc = VbglR3GetDisplayChangeRequest(&cx, &cy, &cBits, &iDisplay, &cxOrigin, &cyOrigin, &fEnabled, &fChangeOrigin,
        true /*fAck*/);
    if (RT_SUCCESS(rc))
    {
        /* Try to set the requested video mode. Repeat until it is successful or is rejected by the driver. */
        LogFlowFunc(("DisplayChangeReqEx parameters  iDisplay=%d x cx=%d x cy=%d x cBits=%d x SecondayMonEnb=%d x NewOriginX=%d x NewOriginY=%d x ChangeOrigin=%d\n",
            iDisplay, cx, cy, cBits, fEnabled, cxOrigin, cyOrigin, fChangeOrigin));

        doResize(pCtx,
            iDisplay,
            cx,
            cy,
            cBits,
            fEnabled,
            cxOrigin,
            cyOrigin,
            fChangeOrigin);
    }

    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnWorker}
 */
static DECLCALLBACK(int) vbtrDisplayWorker(void *pvInstance, bool volatile *pfShutdown)
{
    AssertPtr(pvInstance);
    PVBOXDISPLAYCONTEXT pCtx = (PVBOXDISPLAYCONTEXT)pvInstance;
    AssertPtr(pCtx->pEnv);
    LogFlowFunc(("pvInstance=%p\n", pvInstance));

    /*
     * Tell the control thread that it can continue
     * spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    int rc = VbglR3CtlFilterMask(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST | VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED, 0 /*fNot*/);
    if (RT_FAILURE(rc))
    {
        LogFlowFunc(("VbglR3CtlFilterMask(mask,0): %Rrc\n", rc));
        return rc;
    }

    PostMessage(g_hwndToolWindow, WM_VBOX_GRAPHICS_SUPPORTED, 0, 0);

    VBoxDispIfResizeStarted(&pCtx->pEnv->dispIf);

    for (;;)
    {
        /*
         * Wait for a display change event, checking for shutdown both before and after.
         */
        if (*pfShutdown)
        {
            rc = VINF_SUCCESS;
            break;
        }

        uint32_t fEvents = 0;
        rc = VbglR3WaitEvent(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST | VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED, 1000 /*ms*/, &fEvents);

        if (*pfShutdown)
        {
            rc = VINF_SUCCESS;
            break;
        }

        if (RT_SUCCESS(rc))
        {
            if (fEvents & VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST)
                DisplayChangeRequestHandler(pCtx);

            if (fEvents & VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED)
                hlpReloadCursor();
        }
        else
        {
            // Checking once a second whether or not WM_DISPLAYCHANGED happened.
            if (ASMAtomicXchgU32(&g_fGuestDisplaysChanged, 0))
            {
                // XPDM driver has VBoxDispDrvNotify to receive such a notifications
                if (pCtx->pEnv->dispIf.enmMode >= VBOXDISPIF_MODE_WDDM)
                {
                    VBOXDISPIFESCAPE EscapeHdr = { 0 };
                    EscapeHdr.escapeCode = VBOXESC_GUEST_DISPLAYCHANGED;

                    DWORD err = VBoxDispIfEscapeInOut(&pCtx->pEnv->dispIf, &EscapeHdr, 0);
                    LogFlowFunc(("VBoxDispIfEscapeInOut returned %d\n", err)); NOREF(err);
                }
            }

            /* sleep a bit to not eat too much CPU in case the above call always fails */
            if (rc != VERR_TIMEOUT)
                RTThreadSleep(10);
        }
    }

    /*
     * Remove event filter and graphics capability report.
     */
    int rc2 = VbglR3CtlFilterMask(0 /*fOr*/, VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST | VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED /*fNot*/);
    if (RT_FAILURE(rc2))
        LogFlowFunc(("VbglR3CtlFilterMask failed: %Rrc\n", rc2));
    PostMessage(g_hwndToolWindow, WM_VBOX_GRAPHICS_UNSUPPORTED, 0, 0);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * The service description.
 */
VBOXTRAYSVCDESC g_SvcDescDisplay =
{
    /* pszName. */
    "display",
    /* pszDescription. */
    "Display Notifications",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    vbtrDispPreInit,
    vbtrDispOption,
    vbtrDisplayInit,
    vbtrDisplayWorker,
    NULL /* pfnStop */,
    vbtrDisplayDestroy
};

