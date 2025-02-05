/* $Id: VBoxControl.cpp 108215 2025-02-05 12:05:02Z vadim.galitsyn@oracle.com $ */
/** @file
 * VBoxControl - Guest Additions Command Line Management Interface.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/alloca.h>
#include <iprt/cpp/autores.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/zip.h>
#include <VBox/log.h>
#include <VBox/version.h>
#include <VBox/VBoxGuestLib.h>
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif
#ifdef VBOX_WITH_GUEST_PROPS
# include <VBox/HostServices/GuestPropertySvc.h>
#endif
#ifdef VBOX_WITH_SHARED_FOLDERS
# include <VBox/shflsvc.h>
# ifdef RT_OS_OS2
#  define OS2EMX_PLAIN_CHAR
#  define INCL_ERRORS
#  define INCL_DOSFILEMGR
#  include <os2emx.h>
# endif
#endif
#ifdef VBOX_WITH_DPC_LATENCY_CHECKER
# include <VBox/VBoxGuest.h>
# include "../VBoxGuest/lib/VBoxGuestR3LibInternal.h" /* HACK ALERT! Using vbglR3DoIOCtl directly!! */
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The program name (derived from argv[0]). */
char const *g_pszProgName = "";
/** The current verbosity level. */
int g_cVerbosity = 0;


/** @name Displays the program usage message.
 * @{
 */

/**
 * Helper function that does indentation.
 *
 * @param   pszLine     Text.
 * @param   pszName     Program name.
 * @param   pszCommand  Command/option syntax.
 */
static void doUsage(char const *pszLine, char const *pszName = "", char const *pszCommand = "")
{
    /* Allow for up to 15 characters command name length (VBoxControl.exe) with
     * perfect column alignment. Beyond that there's at least one space between
     * the command if there are command line parameters. */
    RTPrintf("%s %-*s%s%s\n",
             pszName,
             *pszLine ? 35 - strlen(pszName) : 1, pszCommand,
             *pszLine ? " " : "", pszLine);
}

/** Enumerate the different parts of the usage we might want to print out */
enum VBoxControlUsage
{
#ifdef RT_OS_WINDOWS
    GET_VIDEO_ACCEL,
    SET_VIDEO_ACCEL,
    VIDEO_FLAGS,
    LIST_CUST_MODES,
    ADD_CUST_MODE,
    REMOVE_CUST_MODE,
    SET_VIDEO_MODE,
#endif
#ifdef VBOX_WITH_GUEST_PROPS
    GUEST_PROP,
#endif
#ifdef VBOX_WITH_SHARED_FOLDERS
    GUEST_SHAREDFOLDERS,
#endif
#if !defined(VBOX_CONTROL_TEST)
    WRITE_CORE_DUMP,
#endif
    WRITE_LOG,
    TAKE_SNAPSHOT,
    SAVE_STATE,
    SUSPEND,
    POWER_OFF,
    VERSION,
    HELP,
    USAGE_ALL = UINT32_MAX
};

static RTEXITCODE usage(enum VBoxControlUsage eWhich = USAGE_ALL)
{
    RTPrintf("Usage:\n\n");
    doUsage("print version number and exit", g_pszProgName, "[-V|--version]");
    doUsage("suppress the logo", g_pszProgName, "--nologo ...");
    RTPrintf("\n");

    /* Exclude the Windows bits from the test version.  Anyone who needs to
       test them can fix this. */
#if defined(RT_OS_WINDOWS) && !defined(VBOX_CONTROL_TEST)
    if (eWhich  == GET_VIDEO_ACCEL || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "getvideoacceleration");
    if (eWhich  == SET_VIDEO_ACCEL || eWhich == USAGE_ALL)
        doUsage("<on|off>", g_pszProgName, "setvideoacceleration");
    if (eWhich  == VIDEO_FLAGS || eWhich == USAGE_ALL)
        doUsage("<get|set|clear|delete> [hex mask]", g_pszProgName, "videoflags");
    if (eWhich  == LIST_CUST_MODES || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "listcustommodes");
    if (eWhich  == ADD_CUST_MODE || eWhich == USAGE_ALL)
        doUsage("<width> <height> <bpp>", g_pszProgName, "addcustommode");
    if (eWhich  == REMOVE_CUST_MODE || eWhich == USAGE_ALL)
        doUsage("<width> <height> <bpp>", g_pszProgName, "removecustommode");
    if (eWhich  == SET_VIDEO_MODE || eWhich == USAGE_ALL)
        doUsage("<width> <height> <bpp> <screen>", g_pszProgName, "setvideomode");
#endif
#ifdef VBOX_WITH_GUEST_PROPS
    if (eWhich == GUEST_PROP || eWhich == USAGE_ALL)
    {
        doUsage("get <property> [--verbose]", g_pszProgName, "guestproperty");
        doUsage("set <property> [<value> [--flags <flags>]]", g_pszProgName, "guestproperty");
        doUsage("delete|unset <property>", g_pszProgName, "guestproperty");
        doUsage("enumerate [--patterns <patterns>]", g_pszProgName, "guestproperty");
        doUsage("wait <patterns>", g_pszProgName, "guestproperty");
        doUsage("[--timestamp <last timestamp>]");
        doUsage("[--timeout <timeout in ms>");
    }
#endif
#ifdef VBOX_WITH_SHARED_FOLDERS
    if (eWhich == GUEST_SHAREDFOLDERS || eWhich == USAGE_ALL)
    {
        doUsage("list [--automount]", g_pszProgName, "sharedfolder");
# ifdef RT_OS_OS2
        doUsage("use <drive> <folder>", g_pszProgName, "sharedfolder");
        doUsage("unuse <drive>", g_pszProgName, "sharedfolder");
# endif
    }
#endif

#if !defined(VBOX_CONTROL_TEST)
    if (eWhich == WRITE_CORE_DUMP || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "writecoredump");
#endif
    if (eWhich == WRITE_LOG || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "writelog [-n|--no-newline] [--] <msg>");
    if (eWhich == TAKE_SNAPSHOT || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "takesnapshot");
    if (eWhich == SAVE_STATE || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "savestate");
    if (eWhich == SUSPEND   || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "suspend");
    if (eWhich == POWER_OFF  || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "poweroff");
    if (eWhich == HELP      || eWhich == USAGE_ALL)
        doUsage("[command]", g_pszProgName, "help");
    if (eWhich == VERSION   || eWhich == USAGE_ALL)
        doUsage("", g_pszProgName, "version");

    return RTEXITCODE_SUCCESS;
}

/** @} */


/**
 * Implementation of the '--version' option.
 *
 * @returns RTEXITCODE_SUCCESS
 */
static RTEXITCODE printVersion(void)
{
    RTPrintf("%sr%u\n", VBOX_VERSION_STRING, RTBldCfgRevision());
    return RTEXITCODE_SUCCESS;
}


/**
 * Displays an error message.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pszFormat   The message text. No newline.
 * @param   ...         Format arguments.
 */
static RTEXITCODE VBoxControlError(const char *pszFormat, ...)
{
    /** @todo prefix with current command. */
    va_list va;
    va_start(va, pszFormat);
    RTMsgErrorV(pszFormat, va);
    va_end(va);
    return RTEXITCODE_FAILURE;
}


/**
 * Displays a getopt error.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   ch          The RTGetOpt return value.
 * @param   pValueUnion The RTGetOpt return data.
 */
static RTEXITCODE VBoxCtrlGetOptError(int ch, PCRTGETOPTUNION pValueUnion)
{
    /** @todo prefix with current command. */
    return RTGetOptPrintError(ch, pValueUnion);
}


/**
 * Displays an syntax error message.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pszFormat   The message text. No newline.
 * @param   ...         Format arguments.
 */
static RTEXITCODE VBoxControlSyntaxError(const char *pszFormat, ...)
{
    /** @todo prefix with current command. */
    va_list va;
    va_start(va, pszFormat);
    RTMsgErrorV(pszFormat, va);
    va_end(va);
    return RTEXITCODE_SYNTAX;
}

#if defined(RT_OS_WINDOWS) && !defined(VBOX_CONTROL_TEST)

decltype(ChangeDisplaySettingsExA) *g_pfnChangeDisplaySettingsExA;
decltype(ChangeDisplaySettings)    *g_pfnChangeDisplaySettingsA;
decltype(EnumDisplaySettingsA)     *g_pfnEnumDisplaySettingsA;

static unsigned nextAdjacentRectXP(RECTL const *paRects, unsigned cRects, unsigned iRect)
{
    for (unsigned i = 0; i < cRects; i++)
        if (paRects[iRect].right == paRects[i].left)
            return i;
    return ~0U;
}

static unsigned nextAdjacentRectXN(RECTL const *paRects, unsigned cRects, unsigned iRect)
{
    for (unsigned i = 0; i < cRects; i++)
        if (paRects[iRect].left == paRects[i].right)
            return i;
    return ~0U;
}

static unsigned nextAdjacentRectYP(RECTL const *paRects, unsigned cRects, unsigned iRect)
{
    for (unsigned i = 0; i < cRects; i++)
        if (paRects[iRect].bottom == paRects[i].top)
            return i;
    return ~0U;
}

unsigned nextAdjacentRectYN(RECTL const *paRects, unsigned cRects, unsigned iRect)
{
    for (unsigned i = 0; i < cRects; i++)
        if (paRects[iRect].top == paRects[i].bottom)
            return i;
    return ~0U;
}

void resizeRect(RECTL *paRects, unsigned cRects, unsigned iPrimary, unsigned iResized, int NewWidth, int NewHeight)
{
    RECTL *paNewRects = (RECTL *)alloca(sizeof (RECTL) * cRects);
    memcpy (paNewRects, paRects, sizeof(RECTL) * cRects);
    paNewRects[iResized].right  += NewWidth - (paNewRects[iResized].right - paNewRects[iResized].left);
    paNewRects[iResized].bottom += NewHeight - (paNewRects[iResized].bottom - paNewRects[iResized].top);

    /* Verify all pairs of originally adjacent rectangles for all 4 directions.
     * If the pair has a "good" delta (that is the first rectangle intersects the second)
     * at a direction and the second rectangle is not primary one (which can not be moved),
     * move the second rectangle to make it adjacent to the first one.
     */

    /* X positive. */
    unsigned iRect;
    for (iRect = 0; iRect < cRects; iRect++)
    {
        /* Find the next adjacent original rect in x positive direction. */
        unsigned iNextRect = nextAdjacentRectXP (paRects, cRects, iRect);
        Log(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == iPrimary)
        {
            continue;
        }

        /* Check whether there is an X intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].right - paNewRects[iNextRect].left;

        if (delta > 0)
        {
            Log(("XP intersection right %d left %d, diff %d\n",
                     paNewRects[iRect].right, paNewRects[iNextRect].left,
                     delta));

            paNewRects[iNextRect].left += delta;
            paNewRects[iNextRect].right += delta;
        }
    }

    /* X negative. */
    for (iRect = 0; iRect < cRects; iRect++)
    {
        /* Find the next adjacent original rect in x negative direction. */
        unsigned iNextRect = nextAdjacentRectXN (paRects, cRects, iRect);
        Log(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == iPrimary)
        {
            continue;
        }

        /* Check whether there is an X intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].left - paNewRects[iNextRect].right;

        if (delta < 0)
        {
            Log(("XN intersection left %d right %d, diff %d\n",
                     paNewRects[iRect].left, paNewRects[iNextRect].right,
                     delta));

            paNewRects[iNextRect].left += delta;
            paNewRects[iNextRect].right += delta;
        }
    }

    /* Y positive (in the computer sense, top->down). */
    for (iRect = 0; iRect < cRects; iRect++)
    {
        /* Find the next adjacent original rect in y positive direction. */
        unsigned iNextRect = nextAdjacentRectYP (paRects, cRects, iRect);
        Log(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == iPrimary)
        {
            continue;
        }

        /* Check whether there is an Y intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].bottom - paNewRects[iNextRect].top;

        if (delta > 0)
        {
            Log(("YP intersection bottom %d top %d, diff %d\n",
                     paNewRects[iRect].bottom, paNewRects[iNextRect].top,
                     delta));

            paNewRects[iNextRect].top += delta;
            paNewRects[iNextRect].bottom += delta;
        }
    }

    /* Y negative (in the computer sense, down->top). */
    for (iRect = 0; iRect < cRects; iRect++)
    {
        /* Find the next adjacent original rect in x negative direction. */
        unsigned iNextRect = nextAdjacentRectYN (paRects, cRects, iRect);
        Log(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == iPrimary)
        {
            continue;
        }

        /* Check whether there is an Y intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].top - paNewRects[iNextRect].bottom;

        if (delta < 0)
        {
            Log(("YN intersection top %d bottom %d, diff %d\n",
                     paNewRects[iRect].top, paNewRects[iNextRect].bottom,
                     delta));

            paNewRects[iNextRect].top += delta;
            paNewRects[iNextRect].bottom += delta;
        }
    }

    memcpy (paRects, paNewRects, sizeof (RECTL) * cRects);
    return;
}

/* Returns TRUE to try again. */
static BOOL ResizeDisplayDevice(ULONG Id, DWORD Width, DWORD Height, DWORD BitsPerPixel)
{
    BOOL fModeReset = (Width == 0 && Height == 0 && BitsPerPixel == 0);

    DISPLAY_DEVICE DisplayDevice;
    RT_ZERO(DisplayDevice);
    DisplayDevice.cb = sizeof(DisplayDevice);

    /* Find out how many display devices the system has */
    DWORD NumDevices = 0;
    DWORD i = 0;
    while (EnumDisplayDevices(NULL, i, &DisplayDevice, 0))
    {
        Log(("[%d] %s\n", i, DisplayDevice.DeviceName));

        if (DisplayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            Log(("Found primary device. err %d\n", GetLastError()));
            NumDevices++;
        }
        else if (!(DisplayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
        {

            Log(("Found secondary device. err %d\n", GetLastError()));
            NumDevices++;
        }

        RT_ZERO(DisplayDevice);
        DisplayDevice.cb = sizeof(DisplayDevice);
        i++;
    }

    Log(("Found total %d devices. err %d\n", NumDevices, GetLastError()));

    if (NumDevices == 0 || Id >= NumDevices)
    {
        Log(("Requested identifier %d is invalid. err %d\n", Id, GetLastError()));
        return FALSE;
    }

    DISPLAY_DEVICE *paDisplayDevices = (DISPLAY_DEVICE *)alloca(sizeof (DISPLAY_DEVICE) * NumDevices);
    DEVMODE *paDeviceModes = (DEVMODE *)alloca(sizeof (DEVMODE) * NumDevices);
    RECTL *paRects = (RECTL *)alloca(sizeof (RECTL) * NumDevices);

    /* Fetch information about current devices and modes. */
    DWORD DevNum = 0;
    DWORD DevPrimaryNum = 0;

    RT_ZERO(DisplayDevice);
    DisplayDevice.cb = sizeof(DISPLAY_DEVICE);

    i = 0;
    while (EnumDisplayDevices (NULL, i, &DisplayDevice, 0))
    {
        Log(("[%d(%d)] %s\n", i, DevNum, DisplayDevice.DeviceName));

        BOOL fFetchDevice = FALSE;

        if (DisplayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            Log(("Found primary device. err %d\n", GetLastError()));
            DevPrimaryNum = DevNum;
            fFetchDevice = TRUE;
        }
        else if (!(DisplayDevice.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
        {

            Log(("Found secondary device. err %d\n", GetLastError()));
            fFetchDevice = TRUE;
        }

        if (fFetchDevice)
        {
            if (DevNum >= NumDevices)
            {
                Log(("%d >= %d\n", NumDevices, DevNum));
                return FALSE;
            }

            paDisplayDevices[DevNum] = DisplayDevice;

            RT_BZERO(&paDeviceModes[DevNum], sizeof(DEVMODE));
            paDeviceModes[DevNum].dmSize = sizeof(DEVMODE);
            if (!g_pfnEnumDisplaySettingsA((LPSTR)DisplayDevice.DeviceName, ENUM_REGISTRY_SETTINGS, &paDeviceModes[DevNum]))
            {
                Log(("EnumDisplaySettings err %d\n", GetLastError()));
                return FALSE;
            }

            Log(("%dx%d at %d,%d\n",
                    paDeviceModes[DevNum].dmPelsWidth,
                    paDeviceModes[DevNum].dmPelsHeight,
                    paDeviceModes[DevNum].dmPosition.x,
                    paDeviceModes[DevNum].dmPosition.y));

            paRects[DevNum].left   = paDeviceModes[DevNum].dmPosition.x;
            paRects[DevNum].top    = paDeviceModes[DevNum].dmPosition.y;
            paRects[DevNum].right  = paDeviceModes[DevNum].dmPosition.x + paDeviceModes[DevNum].dmPelsWidth;
            paRects[DevNum].bottom = paDeviceModes[DevNum].dmPosition.y + paDeviceModes[DevNum].dmPelsHeight;
            DevNum++;
        }

        RT_ZERO(DisplayDevice);
        DisplayDevice.cb = sizeof(DISPLAY_DEVICE);
        i++;
    }

    if (Width == 0)
        Width = paRects[Id].right - paRects[Id].left;

    if (Height == 0)
        Height = paRects[Id].bottom - paRects[Id].top;

    /* Check whether a mode reset or a change is requested. */
    if (   !fModeReset
        && paRects[Id].right - paRects[Id].left == (LONG)Width
        && paRects[Id].bottom - paRects[Id].top == (LONG)Height
        && paDeviceModes[Id].dmBitsPerPel == BitsPerPixel)
    {
        Log(("VBoxDisplayThread : already at desired resolution.\n"));
        return FALSE;
    }

    resizeRect(paRects, NumDevices, DevPrimaryNum, Id, Width, Height);
#ifdef LOG_ENABLED
    for (i = 0; i < NumDevices; i++)
        Log(("[%d]: %d,%d %dx%d\n",
             i, paRects[i].left, paRects[i].top,
             paRects[i].right - paRects[i].left,
             paRects[i].bottom - paRects[i].top));
#endif /* Log */

    /* Without this, Windows will not ask the miniport for its
     * mode table but uses an internal cache instead.
     */
    DEVMODE tempDevMode;
    RT_ZERO(tempDevMode);
    tempDevMode.dmSize = sizeof(DEVMODE);
    g_pfnEnumDisplaySettingsA(NULL, 0xffffff, &tempDevMode);

    /* Assign the new rectangles to displays. */
    for (i = 0; i < NumDevices; i++)
    {
        paDeviceModes[i].dmPosition.x = paRects[i].left;
        paDeviceModes[i].dmPosition.y = paRects[i].top;
        paDeviceModes[i].dmPelsWidth  = paRects[i].right - paRects[i].left;
        paDeviceModes[i].dmPelsHeight = paRects[i].bottom - paRects[i].top;

        paDeviceModes[i].dmFields = DM_POSITION | DM_PELSHEIGHT | DM_PELSWIDTH;

        if (   i == Id
            && BitsPerPixel != 0)
        {
            paDeviceModes[i].dmFields |= DM_BITSPERPEL;
            paDeviceModes[i].dmBitsPerPel = BitsPerPixel;
        }
        Log(("calling pfnChangeDisplaySettingsEx %p\n", RT_CB_LOG_CAST(g_pfnChangeDisplaySettingsExA)));
        g_pfnChangeDisplaySettingsExA((LPSTR)paDisplayDevices[i].DeviceName,
                                      &paDeviceModes[i], NULL, CDS_NORESET | CDS_UPDATEREGISTRY, NULL);
        Log(("ChangeDisplaySettingsEx position err %d\n", GetLastError()));
    }

    /* A second call to ChangeDisplaySettings updates the monitor. */
    LONG status = g_pfnChangeDisplaySettingsA(NULL, 0);
    Log(("ChangeDisplaySettings update status %d\n", status));
    if (status == DISP_CHANGE_SUCCESSFUL || status == DISP_CHANGE_BADMODE)
    {
        /* Successfully set new video mode or our driver can not set the requested mode. Stop trying. */
        return FALSE;
    }

    /* Retry the request. */
    return TRUE;
}

static DECLCALLBACK(RTEXITCODE) handleSetVideoMode(int argc, char *argv[])
{
    if (argc != 3 && argc != 4)
    {
        usage(SET_VIDEO_MODE);
        return RTEXITCODE_FAILURE;
    }

    DWORD xres = RTStrToUInt32(argv[0]);
    DWORD yres = RTStrToUInt32(argv[1]);
    DWORD bpp  = RTStrToUInt32(argv[2]);
    DWORD scr  = 0;
    if (argc == 4)
        scr = RTStrToUInt32(argv[3]);

    HMODULE hmodUser = GetModuleHandle("user32.dll");
    if (hmodUser)
    {
        /* ChangeDisplaySettingsExA was probably added in W2K, whereas ChangeDisplaySettingsA
           and EnumDisplaySettingsA was added in NT 3.51. */
        g_pfnChangeDisplaySettingsExA = (decltype(g_pfnChangeDisplaySettingsExA))GetProcAddress(hmodUser, "ChangeDisplaySettingsExA");
        g_pfnChangeDisplaySettingsA   = (decltype(g_pfnChangeDisplaySettingsA))  GetProcAddress(hmodUser, "ChangeDisplaySettingsA");
        g_pfnEnumDisplaySettingsA     = (decltype(g_pfnEnumDisplaySettingsA))    GetProcAddress(hmodUser, "EnumDisplaySettingsA");

        Log(("VBoxService: g_pfnChangeDisplaySettingsExA=%p g_pfnChangeDisplaySettingsA=%p g_pfnEnumDisplaySettingsA=%p\n",
             RT_CB_LOG_CAST(g_pfnChangeDisplaySettingsExA), RT_CB_LOG_CAST(g_pfnChangeDisplaySettingsA),
             RT_CB_LOG_CAST(g_pfnEnumDisplaySettingsA)));

        if (   g_pfnChangeDisplaySettingsExA
            && g_pfnChangeDisplaySettingsA
            && g_pfnEnumDisplaySettingsA)
        {
            /* The screen index is 0 based in the ResizeDisplayDevice call. */
            scr = scr > 0 ? scr - 1 : 0;

            /* Horizontal resolution must be a multiple of 8, round down. */
            xres &= ~0x7;

            RTPrintf("Setting resolution of display %d to %dx%dx%d ...", scr, xres, yres, bpp);
            ResizeDisplayDevice(scr, xres, yres, bpp);
            RTPrintf("done.\n");
        }
        else
            VBoxControlError("Error retrieving API for display change!");
    }
    else
        VBoxControlError("Error retrieving handle to user32.dll!");

    return RTEXITCODE_SUCCESS;
}

static int checkVBoxVideoKey(HKEY hkeyVideo)
{
    RTUTF16 wszValue[128];
    DWORD   cbValue = sizeof(wszValue);
    DWORD   dwKeyType;
    LONG status = RegQueryValueExW(hkeyVideo, L"Device Description", NULL, &dwKeyType, (LPBYTE)wszValue, &cbValue);
    if (status == ERROR_SUCCESS)
    {
        /* WDDM has additional chars after "Adapter" */
        static char s_szDeviceDescription[] = "VirtualBox Graphics Adapter";
        wszValue[sizeof(s_szDeviceDescription) - 1] = '\0';
        if (RTUtf16ICmpAscii(wszValue, s_szDeviceDescription) == 0)
            return VINF_SUCCESS;
    }

    return VERR_NOT_FOUND;
}

static HKEY getVideoKey(bool writable)
{
    HKEY hkeyDeviceMap = 0;
    LONG status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\VIDEO", 0, KEY_READ, &hkeyDeviceMap);
    if (status != ERROR_SUCCESS || !hkeyDeviceMap)
    {
        VBoxControlError("Error opening video device map registry key!\n");
        return 0;
    }

    HKEY hkeyVideo = 0;
    ULONG iDevice;
    DWORD dwKeyType;

    /*
     * Scan all '\Device\VideoX' REG_SZ keys to find VBox video driver entry.
     * 'ObjectNumberList' REG_BINARY is an array of 32 bit device indexes (X).
     */

    /* Get the 'ObjectNumberList' */
    ULONG cDevices = 0;
    DWORD adwObjectNumberList[256];
    DWORD cbValue = sizeof(adwObjectNumberList);
    status = RegQueryValueExA(hkeyDeviceMap, "ObjectNumberList", NULL, &dwKeyType, (LPBYTE)&adwObjectNumberList[0], &cbValue);

    if (   status == ERROR_SUCCESS
        && dwKeyType == REG_BINARY)
        cDevices = cbValue / sizeof(DWORD);
    else
    {
       /* The list might not exists. Use 'MaxObjectNumber' REG_DWORD and build a list. */
       DWORD dwMaxObjectNumber = 0;
       cbValue = sizeof(dwMaxObjectNumber);
       status = RegQueryValueExA(hkeyDeviceMap, "MaxObjectNumber", NULL, &dwKeyType, (LPBYTE)&dwMaxObjectNumber, &cbValue);
       if (   status == ERROR_SUCCESS
           && dwKeyType == REG_DWORD)
       {
           /* 'MaxObjectNumber' is inclusive. */
           cDevices = RT_MIN(dwMaxObjectNumber + 1, RT_ELEMENTS(adwObjectNumberList));
           for (iDevice = 0; iDevice < cDevices; iDevice++)
               adwObjectNumberList[iDevice] = iDevice;
       }
    }

    if (cDevices == 0)
    {
        /* Always try '\Device\Video0' as the old code did. Enum can be used in this case in principle. */
        adwObjectNumberList[0] = 0;
        cDevices = 1;
    }

    /* Scan device entries */
    for (iDevice = 0; iDevice < cDevices; iDevice++)
    {
        RTUTF16 wszValueName[64];
        RTUtf16Printf(wszValueName, RT_ELEMENTS(wszValueName), "\\Device\\Video%u", adwObjectNumberList[iDevice]);

        RTUTF16 wszVideoLocation[256];
        cbValue = sizeof(wszVideoLocation);
        status = RegQueryValueExW(hkeyDeviceMap, wszValueName, NULL, &dwKeyType, (LPBYTE)&wszVideoLocation[0], &cbValue);

        /* This value starts with '\REGISTRY\Machine' */
        if (   status == ERROR_SUCCESS
            && dwKeyType == REG_SZ
            && RTUtf16NICmpAscii(wszVideoLocation, RT_STR_TUPLE("\\REGISTRY\\Machine")) == 0)
        {
            status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, &wszVideoLocation[18], 0,
                                   KEY_READ | (writable ? KEY_WRITE : 0), &hkeyVideo);
            if (status == ERROR_SUCCESS)
            {
                int rc = checkVBoxVideoKey(hkeyVideo);
                if (RT_SUCCESS(rc))
                {
                    /* Found, return hkeyVideo to the caller. */
                    break;
                }

                RegCloseKey(hkeyVideo);
                hkeyVideo = 0;
            }
        }
    }

    if (hkeyVideo == 0)
    {
        VBoxControlError("Error opening video registry key!\n");
    }

    RegCloseKey(hkeyDeviceMap);
    return hkeyVideo;
}

static DECLCALLBACK(RTEXITCODE) handleGetVideoAcceleration(int argc, char *argv[])
{
    RT_NOREF2(argc, argv);
    ULONG status;
    HKEY hkeyVideo = getVideoKey(false);

    if (hkeyVideo)
    {
        /* query the actual value */
        DWORD fAcceleration = 1;
        DWORD cbValue = sizeof(fAcceleration);
        DWORD dwKeyType;
        status = RegQueryValueExA(hkeyVideo, "EnableVideoAccel", NULL, &dwKeyType, (LPBYTE)&fAcceleration, &cbValue);
        if (status != ERROR_SUCCESS)
            RTPrintf("Video acceleration: default\n");
        else
            RTPrintf("Video acceleration: %s\n", fAcceleration ? "on" : "off");
        RegCloseKey(hkeyVideo);
    }
    return RTEXITCODE_SUCCESS;
}

static DECLCALLBACK(RTEXITCODE) handleSetVideoAcceleration(int argc, char *argv[])
{
    ULONG status;
    HKEY hkeyVideo;

    /* must have exactly one argument: the new offset */
    if (   (argc != 1)
        || (   RTStrICmp(argv[0], "on")
            && RTStrICmp(argv[0], "off")))
    {
        usage(SET_VIDEO_ACCEL);
        return RTEXITCODE_FAILURE;
    }

    hkeyVideo = getVideoKey(true);

    if (hkeyVideo)
    {
        int fAccel = 0;
        if (RTStrICmp(argv[0], "on") == 0)
            fAccel = 1;
        /* set a new value */
        status = RegSetValueExA(hkeyVideo, "EnableVideoAccel", 0, REG_DWORD, (LPBYTE)&fAccel, sizeof(fAccel));
        if (status != ERROR_SUCCESS)
        {
            VBoxControlError("Error %d writing video acceleration status!\n", status);
        }
        RegCloseKey(hkeyVideo);
    }
    return RTEXITCODE_SUCCESS;
}

static DECLCALLBACK(RTEXITCODE) videoFlagsGet(void)
{
    HKEY hkeyVideo = getVideoKey(false);

    if (hkeyVideo)
    {
        DWORD dwFlags = 0;
        DWORD cbValue = sizeof(dwFlags);
        DWORD dwKeyType;
        ULONG status = RegQueryValueExA(hkeyVideo, "VBoxVideoFlags", NULL, &dwKeyType, (LPBYTE)&dwFlags, &cbValue);
        if (status != ERROR_SUCCESS)
            RTPrintf("Video flags: default\n");
        else
            RTPrintf("Video flags: 0x%08X\n", dwFlags);
        RegCloseKey(hkeyVideo);
        return RTEXITCODE_SUCCESS;
    }

    return RTEXITCODE_FAILURE;
}

static DECLCALLBACK(RTEXITCODE) videoFlagsDelete(void)
{
    HKEY hkeyVideo = getVideoKey(true);

    if (hkeyVideo)
    {
        ULONG status = RegDeleteValueA(hkeyVideo, "VBoxVideoFlags");
        if (status != ERROR_SUCCESS)
            VBoxControlError("Error %d deleting video flags.\n", status);
        RegCloseKey(hkeyVideo);
        return RTEXITCODE_SUCCESS;
    }

    return RTEXITCODE_FAILURE;
}

static DECLCALLBACK(RTEXITCODE) videoFlagsModify(bool fSet, int argc, char *argv[])
{
    if (argc != 1)
    {
        VBoxControlError("Mask required.\n");
        return RTEXITCODE_FAILURE;
    }

    uint32_t u32Mask = 0;
    int rc = RTStrToUInt32Full(argv[0], 16, &u32Mask);
    if (RT_FAILURE(rc))
    {
        VBoxControlError("Invalid video flags mask.\n");
        return RTEXITCODE_FAILURE;
    }

    RTEXITCODE exitCode = RTEXITCODE_SUCCESS;

    HKEY hkeyVideo = getVideoKey(true);
    if (hkeyVideo)
    {
        DWORD dwFlags = 0;
        DWORD cbValue = sizeof(dwFlags);
        DWORD dwKeyType;
        ULONG status = RegQueryValueExA(hkeyVideo, "VBoxVideoFlags", NULL, &dwKeyType, (LPBYTE)&dwFlags, &cbValue);
        if (status != ERROR_SUCCESS)
            dwFlags = 0;

        dwFlags = fSet ? dwFlags | u32Mask : dwFlags & ~u32Mask;

        status = RegSetValueExA(hkeyVideo, "VBoxVideoFlags", 0, REG_DWORD, (LPBYTE)&dwFlags, sizeof(dwFlags));
        if (status != ERROR_SUCCESS)
        {
            VBoxControlError("Error %d writing video flags.\n", status);
            exitCode = RTEXITCODE_FAILURE;
        }

        RegCloseKey(hkeyVideo);
    }
    else
    {
        exitCode = RTEXITCODE_FAILURE;
    }

    return exitCode;
}

static DECLCALLBACK(RTEXITCODE) handleVideoFlags(int argc, char *argv[])
{
    /* Must have a keyword and optional value (32 bit hex string). */
    if (argc != 1 && argc != 2)
    {
        VBoxControlError("Invalid number of arguments.\n");
        usage(VIDEO_FLAGS);
        return RTEXITCODE_FAILURE;
    }

    RTEXITCODE exitCode = RTEXITCODE_SUCCESS;

    if (RTStrICmp(argv[0], "get") == 0)
    {
        exitCode = videoFlagsGet();
    }
    else if (RTStrICmp(argv[0], "delete") == 0)
    {
        exitCode = videoFlagsDelete();
    }
    else if (RTStrICmp(argv[0], "set") == 0)
    {
        exitCode = videoFlagsModify(true, argc - 1, &argv[1]);
    }
    else if (RTStrICmp(argv[0], "clear") == 0)
    {
        exitCode = videoFlagsModify(false, argc - 1, &argv[1]);
    }
    else
    {
        VBoxControlError("Invalid command.\n");
        exitCode = RTEXITCODE_FAILURE;
    }

    if (exitCode != RTEXITCODE_SUCCESS)
    {
        usage(VIDEO_FLAGS);
    }

    return exitCode;
}

#define MAX_CUSTOM_MODES 128

/* the table of custom modes */
struct
{
    DWORD xres;
    DWORD yres;
    DWORD bpp;
} customModes[MAX_CUSTOM_MODES] = {{0}};

void getCustomModes(HKEY hkeyVideo)
{
    ULONG status;
    int curMode = 0;

    /* null out the table */
    RT_ZERO(customModes);

    do
    {
        char valueName[20];
        DWORD xres, yres, bpp = 0;
        DWORD dwType;
        DWORD dwLen = sizeof(DWORD);

        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dWidth", curMode);
        status = RegQueryValueExA(hkeyVideo, valueName, NULL, &dwType, (LPBYTE)&xres, &dwLen);
        if (status != ERROR_SUCCESS)
            break;
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dHeight", curMode);
        status = RegQueryValueExA(hkeyVideo, valueName, NULL, &dwType, (LPBYTE)&yres, &dwLen);
        if (status != ERROR_SUCCESS)
            break;
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dBPP", curMode);
        status = RegQueryValueExA(hkeyVideo, valueName, NULL, &dwType, (LPBYTE)&bpp, &dwLen);
        if (status != ERROR_SUCCESS)
            break;

        /* check if the mode is OK */
        if (   (xres > (1 << 16))
            || (yres > (1 << 16))
            || (   (bpp != 16)
                && (bpp != 24)
                && (bpp != 32)))
            break;

        /* add mode to table */
        customModes[curMode].xres = xres;
        customModes[curMode].yres = yres;
        customModes[curMode].bpp  = bpp;

        ++curMode;

        if (curMode >= MAX_CUSTOM_MODES)
            break;
    } while(1);
}

void writeCustomModes(HKEY hkeyVideo)
{
    ULONG status;
    int tableIndex = 0;
    int modeIndex = 0;

    /* first remove all values */
    for (int i = 0; i < MAX_CUSTOM_MODES; i++)
    {
        char valueName[20];
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dWidth", i);
        RegDeleteValueA(hkeyVideo, valueName);
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dHeight", i);
        RegDeleteValueA(hkeyVideo, valueName);
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dBPP", i);
        RegDeleteValueA(hkeyVideo, valueName);
    }

    do
    {
        if (tableIndex >= MAX_CUSTOM_MODES)
            break;

        /* is the table entry present? */
        if (   (!customModes[tableIndex].xres)
            || (!customModes[tableIndex].yres)
            || (!customModes[tableIndex].bpp))
        {
            tableIndex++;
            continue;
        }

        RTPrintf("writing mode %d (%dx%dx%d)\n", modeIndex, customModes[tableIndex].xres, customModes[tableIndex].yres, customModes[tableIndex].bpp);
        char valueName[20];
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dWidth", modeIndex);
        status = RegSetValueExA(hkeyVideo, valueName, 0, REG_DWORD, (LPBYTE)&customModes[tableIndex].xres,
                                sizeof(customModes[tableIndex].xres));
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dHeight", modeIndex);
        RegSetValueExA(hkeyVideo, valueName, 0, REG_DWORD, (LPBYTE)&customModes[tableIndex].yres,
                       sizeof(customModes[tableIndex].yres));
        RTStrPrintf(valueName, sizeof(valueName), "CustomMode%dBPP", modeIndex);
        RegSetValueExA(hkeyVideo, valueName, 0, REG_DWORD, (LPBYTE)&customModes[tableIndex].bpp,
                       sizeof(customModes[tableIndex].bpp));

        modeIndex++;
        tableIndex++;

    } while(1);

}

static DECLCALLBACK(RTEXITCODE) handleListCustomModes(int argc, char *argv[])
{
    RT_NOREF1(argv);
    if (argc != 0)
    {
        usage(LIST_CUST_MODES);
        return RTEXITCODE_FAILURE;
    }

    HKEY hkeyVideo = getVideoKey(false);

    if (hkeyVideo)
    {
        getCustomModes(hkeyVideo);
        for (int i = 0; i < (sizeof(customModes) / sizeof(customModes[0])); i++)
        {
            if (   !customModes[i].xres
                || !customModes[i].yres
                || !customModes[i].bpp)
                continue;

            RTPrintf("Mode: %d x %d x %d\n",
                             customModes[i].xres, customModes[i].yres, customModes[i].bpp);
        }
        RegCloseKey(hkeyVideo);
    }
    return RTEXITCODE_SUCCESS;
}

static DECLCALLBACK(RTEXITCODE) handleAddCustomMode(int argc, char *argv[])
{
    if (argc != 3)
    {
        usage(ADD_CUST_MODE);
        return RTEXITCODE_FAILURE;
    }

    DWORD xres = RTStrToUInt32(argv[0]);
    DWORD yres = RTStrToUInt32(argv[1]);
    DWORD bpp  = RTStrToUInt32(argv[2]);

    /** @todo better check including xres mod 8 = 0! */
    if (   (xres > (1 << 16))
        || (yres > (1 << 16))
        || (   (bpp != 16)
            && (bpp != 24)
            && (bpp != 32)))
    {
        VBoxControlError("invalid mode specified!\n");
        return RTEXITCODE_FAILURE;
    }

    HKEY hkeyVideo = getVideoKey(true);

    if (hkeyVideo)
    {
        int i;
        int fModeExists = 0;
        getCustomModes(hkeyVideo);
        for (i = 0; i < MAX_CUSTOM_MODES; i++)
        {
            /* mode exists? */
            if (   customModes[i].xres == xres
                && customModes[i].yres == yres
                && customModes[i].bpp  == bpp
               )
            {
                fModeExists = 1;
            }
        }
        if (!fModeExists)
        {
            for (i = 0; i < MAX_CUSTOM_MODES; i++)
            {
                /* item free? */
                if (!customModes[i].xres)
                {
                    customModes[i].xres = xres;
                    customModes[i].yres = yres;
                    customModes[i].bpp  = bpp;
                    break;
                }
            }
            writeCustomModes(hkeyVideo);
        }
        RegCloseKey(hkeyVideo);
    }
    return RTEXITCODE_SUCCESS;
}

static DECLCALLBACK(RTEXITCODE) handleRemoveCustomMode(int argc, char *argv[])
{
    if (argc != 3)
    {
        usage(REMOVE_CUST_MODE);
        return RTEXITCODE_FAILURE;
    }

    DWORD xres = RTStrToUInt32(argv[0]);
    DWORD yres = RTStrToUInt32(argv[1]);
    DWORD bpp  = RTStrToUInt32(argv[2]);

    HKEY hkeyVideo = getVideoKey(true);

    if (hkeyVideo)
    {
        getCustomModes(hkeyVideo);
        for (int i = 0; i < MAX_CUSTOM_MODES; i++)
        {
            /* correct item? */
            if (   (customModes[i].xres == xres)
                && (customModes[i].yres == yres)
                && (customModes[i].bpp  == bpp))
            {
                RTPrintf("found mode at index %d\n", i);
                RT_ZERO(customModes[i]);
                break;
            }
        }
        writeCustomModes(hkeyVideo);
        RegCloseKey(hkeyVideo);
    }

    return RTEXITCODE_SUCCESS;
}

#endif /* RT_OS_WINDOWS */

#ifdef VBOX_WITH_GUEST_PROPS
/**
 * Retrieves a value from the guest property store.
 * This is accessed through the "VBoxGuestPropSvc" HGCM service.
 *
 * @returns Command exit code.
 * @note see the command line API description for parameters
 */
static RTEXITCODE getGuestProperty(int argc, char **argv)
{
    bool fVerbose = false;
    if (   argc == 2
        && (   strcmp(argv[1], "-verbose")  == 0
            || strcmp(argv[1], "--verbose") == 0)
       )
        fVerbose = true;
    else if (argc != 1)
    {
        usage(GUEST_PROP);
        return RTEXITCODE_FAILURE;
    }

    uint32_t u32ClientId = 0;
    int rc = VINF_SUCCESS;

    rc = VbglR3GuestPropConnect(&u32ClientId);
    if (RT_FAILURE(rc))
        VBoxControlError("Failed to connect to the guest property service, error %Rrc\n", rc);

    /*
     * Here we actually retrieve the value from the host.
     */
    const char *pszName = argv[0];
    char *pszValue = NULL;
    uint64_t u64Timestamp = 0;
    char *pszFlags = NULL;
    /* The buffer for storing the data and its initial size.  We leave a bit
     * of space here in case the maximum values are raised. */
    void *pvBuf = NULL;
    uint32_t cbBuf = GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN + 1024;
    if (RT_SUCCESS(rc))
    {
        /* Because there is a race condition between our reading the size of a
         * property and the guest updating it, we loop a few times here and
         * hope.  Actually this should never go wrong, as we are generous
         * enough with buffer space. */
        bool fFinished = false;
        for (unsigned i = 0; i < 10 && !fFinished; ++i)
        {
            void *pvTmpBuf = RTMemRealloc(pvBuf, cbBuf);
            if (NULL == pvTmpBuf)
            {
                rc = VERR_NO_MEMORY;
                VBoxControlError("Out of memory\n");
            }
            else
            {
                pvBuf = pvTmpBuf;
                rc = VbglR3GuestPropRead(u32ClientId, pszName, pvBuf, cbBuf,
                                         &pszValue, &u64Timestamp, &pszFlags,
                                         &cbBuf);
            }
            if (VERR_BUFFER_OVERFLOW == rc)
                /* Leave a bit of extra space to be safe */
                cbBuf += 1024;
            else
                fFinished = true;
        }
        if (VERR_TOO_MUCH_DATA == rc)
            VBoxControlError("Temporarily unable to retrieve the property\n");
        else if (RT_FAILURE(rc) && rc != VERR_NOT_FOUND)
            VBoxControlError("Failed to retrieve the property value, error %Rrc\n", rc);
    }

    /*
     * And display it on the guest console.
     */
    if (VERR_NOT_FOUND == rc)
        RTPrintf("No value set!\n");
    else if (RT_SUCCESS(rc))
    {
        RTPrintf("Value: %s\n", pszValue);
        if (fVerbose)
        {
            RTPrintf("Timestamp: %lld ns\n", u64Timestamp);
            RTPrintf("Flags: %s\n", pszFlags);
        }
    }

    if (u32ClientId != 0)
        VbglR3GuestPropDisconnect(u32ClientId);
    RTMemFree(pvBuf);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Writes a value to the guest property store.
 * This is accessed through the "VBoxGuestPropSvc" HGCM service.
 *
 * @returns Command exit code.
 * @note see the command line API description for parameters
 */
static RTEXITCODE setGuestProperty(int argc, char *argv[])
{
    /*
     * Check the syntax.  We can deduce the correct syntax from the number of
     * arguments.
     */
    bool fUsageOK = true;
    const char *pszName = NULL;
    const char *pszValue = NULL;
    const char *pszFlags = NULL;
    if (2 == argc)
    {
        pszValue = argv[1];
    }
    else if (3 == argc)
        fUsageOK = false;
    else if (4 == argc)
    {
        pszValue = argv[1];
        if (   strcmp(argv[2], "-flags") != 0
            && strcmp(argv[2], "--flags") != 0)
            fUsageOK = false;
        pszFlags = argv[3];
    }
    else if (argc != 1)
        fUsageOK = false;
    if (!fUsageOK)
    {
        usage(GUEST_PROP);
        return RTEXITCODE_FAILURE;
    }
    /* This is always needed. */
    pszName = argv[0];

    /*
     * Do the actual setting.
     */
    uint32_t u32ClientId = 0;
    int rc = VINF_SUCCESS;
    rc = VbglR3GuestPropConnect(&u32ClientId);
    if (RT_FAILURE(rc))
        VBoxControlError("Failed to connect to the guest property service, error %Rrc\n", rc);
    else
    {
        if (pszFlags != NULL)
            rc = VbglR3GuestPropWrite(u32ClientId, pszName, pszValue, pszFlags);
        else
            rc = VbglR3GuestPropWriteValue(u32ClientId, pszName, pszValue);
        if (RT_FAILURE(rc))
            VBoxControlError("Failed to store the property value, error %Rrc\n", rc);
    }

    if (u32ClientId != 0)
        VbglR3GuestPropDisconnect(u32ClientId);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Deletes a guest property from the guest property store.
 * This is accessed through the "VBoxGuestPropSvc" HGCM service.
 *
 * @returns Command exit code.
 * @note see the command line API description for parameters
 */
static RTEXITCODE deleteGuestProperty(int argc, char *argv[])
{
    /*
     * Check the syntax.  We can deduce the correct syntax from the number of
     * arguments.
     */
    bool fUsageOK = true;
    const char *pszName = NULL;
    if (argc < 1)
        fUsageOK = false;
    if (!fUsageOK)
    {
        usage(GUEST_PROP);
        return RTEXITCODE_FAILURE;
    }
    /* This is always needed. */
    pszName = argv[0];

    /*
     * Do the actual setting.
     */
    uint32_t u32ClientId = 0;
    int rc = VbglR3GuestPropConnect(&u32ClientId);
    if (RT_FAILURE(rc))
        VBoxControlError("Failed to connect to the guest property service, error %Rrc\n", rc);
    else
    {
        rc = VbglR3GuestPropDelete(u32ClientId, pszName);
        if (RT_FAILURE(rc))
            VBoxControlError("Failed to delete the property value, error %Rrc\n", rc);
    }

    if (u32ClientId != 0)
        VbglR3GuestPropDisconnect(u32ClientId);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Enumerates the properties in the guest property store.
 * This is accessed through the "VBoxGuestPropSvc" HGCM service.
 *
 * @returns Command exit code.
 * @note see the command line API description for parameters
 */
static RTEXITCODE enumGuestProperty(int argc, char *argv[])
{
    /*
     * Check the syntax.  We can deduce the correct syntax from the number of
     * arguments.
     */
    char const * const *papszPatterns = NULL;
    uint32_t cPatterns = 0;
    if (    argc > 1
        && (   strcmp(argv[0], "-patterns") == 0
            || strcmp(argv[0], "--patterns") == 0))
    {
        papszPatterns = (char const * const *)&argv[1];
        cPatterns = argc - 1;
    }
    else if (argc != 0)
    {
        usage(GUEST_PROP);
        return RTEXITCODE_FAILURE;
    }

    /*
     * Do the actual enumeration.
     */
    uint32_t u32ClientId = 0;
    int rc = VbglR3GuestPropConnect(&u32ClientId);
    if (RT_SUCCESS(rc))
    {
        PVBGLR3GUESTPROPENUM pHandle;
        const char *pszName, *pszValue, *pszFlags;
        uint64_t u64Timestamp;

        rc = VbglR3GuestPropEnum(u32ClientId, papszPatterns, cPatterns, &pHandle,
                                 &pszName, &pszValue, &u64Timestamp, &pszFlags);
        if (RT_SUCCESS(rc))
        {
            while (RT_SUCCESS(rc) && pszName)
            {
                RTPrintf("Name: %s, value: %s, timestamp: %lld, flags: %s\n",
                         pszName, pszValue ? pszValue : "", u64Timestamp, pszFlags);

                rc = VbglR3GuestPropEnumNext(pHandle, &pszName, &pszValue, &u64Timestamp, &pszFlags);
                if (RT_FAILURE(rc))
                    VBoxControlError("Error while enumerating guest properties: %Rrc\n", rc);
            }

            VbglR3GuestPropEnumFree(pHandle);
        }
        else if (VERR_NOT_FOUND == rc)
            RTPrintf("No properties found.\n");
        else
            VBoxControlError("Failed to enumerate the guest properties! Error: %Rrc\n", rc);
        VbglR3GuestPropDisconnect(u32ClientId);
    }
    else
        VBoxControlError("Failed to connect to the guest property service! Error: %Rrc\n", rc);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Waits for notifications of changes to guest properties.
 * This is accessed through the "VBoxGuestPropSvc" HGCM service.
 *
 * @returns Command exit code.
 * @note see the command line API description for parameters
 */
static RTEXITCODE waitGuestProperty(int argc, char **argv)
{
    /*
     * Handle arguments
     */
    const char *pszPatterns = NULL;
    uint64_t u64TimestampIn = 0;
    uint32_t u32Timeout = RT_INDEFINITE_WAIT;
    bool fUsageOK = true;
    if (argc < 1)
        fUsageOK = false;
    pszPatterns = argv[0];
    for (int i = 1; fUsageOK && i < argc; ++i)
    {
        if (   strcmp(argv[i], "-timeout")  == 0
            || strcmp(argv[i], "--timeout") == 0)
        {
            if (   i + 1 >= argc
                || RTStrToUInt32Full(argv[i + 1], 10, &u32Timeout)
                       != VINF_SUCCESS
               )
                fUsageOK = false;
            else
                ++i;
        }
        else if (   strcmp(argv[i], "-timestamp")  == 0
                 || strcmp(argv[i], "--timestamp") == 0)
        {
            if (   i + 1 >= argc
                || RTStrToUInt64Full(argv[i + 1], 10, &u64TimestampIn)
                       != VINF_SUCCESS
               )
                fUsageOK = false;
            else
                ++i;
        }
        else
            fUsageOK = false;
    }
    if (!fUsageOK)
    {
        usage(GUEST_PROP);
        return RTEXITCODE_FAILURE;
    }

    /*
     * Connect to the service
     */
    uint32_t u32ClientId = 0;
    int rc = VbglR3GuestPropConnect(&u32ClientId);
    if (RT_FAILURE(rc))
        VBoxControlError("Failed to connect to the guest property service, error %Rrc\n", rc);

    /*
     * Retrieve the notification from the host
     */
    char *pszName = NULL;
    char *pszValue = NULL;
    uint64_t u64TimestampOut = 0;
    char *pszFlags = NULL;
    bool fWasDeleted = false;
    /* The buffer for storing the data and its initial size.  We leave a bit
     * of space here in case the maximum values are raised. */
    void *pvBuf = NULL;
    uint32_t cbBuf = GUEST_PROP_MAX_NAME_LEN + GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN + _1K;
    /* Because there is a race condition between our reading the size of a
     * property and the guest updating it, we loop a few times here and
     * hope.  Actually this should never go wrong, as we are generous
     * enough with buffer space. */
    for (unsigned iTry = 0; ; iTry++)
    {
        pvBuf = RTMemRealloc(pvBuf, cbBuf);
        if (pvBuf != NULL)
        {
            rc = VbglR3GuestPropWait(u32ClientId, pszPatterns, pvBuf, cbBuf,
                                     u64TimestampIn, u32Timeout,
                                     &pszName, &pszValue, &u64TimestampOut,
                                     &pszFlags, &cbBuf, &fWasDeleted);
            if (rc == VERR_BUFFER_OVERFLOW && iTry < 10)
            {
                cbBuf += _1K; /* Add a bit of extra space to be on the safe side. */
                continue;
            }
            if (rc == VERR_TOO_MUCH_DATA)
                VBoxControlError("Temporarily unable to get a notification\n");
            else if (rc == VERR_INTERRUPTED)
                VBoxControlError("The request timed out or was interrupted\n");
            else if (RT_FAILURE(rc) && rc != VERR_NOT_FOUND)
                VBoxControlError("Failed to get a notification, error %Rrc\n", rc);
        }
        else
        {
            VBoxControlError("Out of memory\n");
            rc = VERR_NO_MEMORY;
        }
        break;
    }

    /*
     * And display it on the guest console.
     */
    if (VERR_NOT_FOUND == rc)
        RTPrintf("No value set!\n");
    else if (rc == VERR_BUFFER_OVERFLOW)
        RTPrintf("Internal error: unable to determine the size of the data!\n");
    else if (RT_SUCCESS(rc))
    {
        if (fWasDeleted)
        {
            RTPrintf("Property %s was deleted\n", pszName);
        }
        else
        {
            RTPrintf("Name: %s\n", pszName);
            RTPrintf("Value: %s\n", pszValue);
            RTPrintf("Timestamp: %lld ns\n", u64TimestampOut);
            RTPrintf("Flags: %s\n", pszFlags);
        }
    }

    if (u32ClientId != 0)
        VbglR3GuestPropDisconnect(u32ClientId);
    RTMemFree(pvBuf);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Access the guest property store through the "VBoxGuestPropSvc" HGCM
 * service.
 *
 * @returns 0 on success, 1 on failure
 * @note see the command line API description for parameters
 */
static DECLCALLBACK(RTEXITCODE) handleGuestProperty(int argc, char *argv[])
{
    if (argc == 0)
    {
        usage(GUEST_PROP);
        return RTEXITCODE_FAILURE;
    }
    if (!strcmp(argv[0], "get"))
        return getGuestProperty(argc - 1, argv + 1);
    if (!strcmp(argv[0], "set"))
        return setGuestProperty(argc - 1, argv + 1);
    if (!strcmp(argv[0], "delete") || !strcmp(argv[0], "unset"))
        return deleteGuestProperty(argc - 1, argv + 1);
    if (!strcmp(argv[0], "enumerate"))
        return enumGuestProperty(argc - 1, argv + 1);
    if (!strcmp(argv[0], "wait"))
        return waitGuestProperty(argc - 1, argv + 1);
    /* unknown cmd */
    usage(GUEST_PROP);
    return RTEXITCODE_FAILURE;
}

#endif
#ifdef VBOX_WITH_SHARED_FOLDERS

/**
 * Lists the Shared Folders provided by the host.
 */
static RTEXITCODE sharedFolder_list(int argc, char **argv)
{
    bool fUsageOK = true;
    bool fOnlyShowAutoMount = false;
    if (argc == 1)
    {
        if (!strcmp(argv[0], "--automount"))
            fOnlyShowAutoMount = true;
        else
            fUsageOK = false;
    }
    else if (argc > 1)
        fUsageOK = false;
    if (!fUsageOK)
    {
        usage(GUEST_SHAREDFOLDERS);
        return RTEXITCODE_SYNTAX;
    }

    uint32_t u32ClientId;
    int rc = VbglR3SharedFolderConnect(&u32ClientId);
    if (RT_FAILURE(rc))
        VBoxControlError("Failed to connect to the shared folder service, error %Rrc\n", rc);
    else
    {
        PVBGLR3SHAREDFOLDERMAPPING paMappings;
        uint32_t cMappings;
        rc = VbglR3SharedFolderGetMappings(u32ClientId, fOnlyShowAutoMount, &paMappings, &cMappings);
        if (RT_SUCCESS(rc))
        {
            if (fOnlyShowAutoMount)
                RTPrintf("Auto-mounted Shared Folder mappings (%u):\n\n", cMappings);
            else
                RTPrintf("Shared Folder mappings (%u):\n\n", cMappings);

            for (uint32_t i = 0; i < cMappings; i++)
            {
                char *pszName;
                char *pszMntPt;
                uint64_t fFlags;
                uint32_t uRootIdVer;
                rc = VbglR3SharedFolderQueryFolderInfo(u32ClientId, paMappings[i].u32Root, 0,
                                                       &pszName, &pszMntPt, &fFlags, &uRootIdVer);
                if (RT_SUCCESS(rc))
                {
                    RTPrintf("%02u - %s [idRoot=%u", i + 1, pszName, paMappings[i].u32Root);
                    if (fFlags & SHFL_MIF_WRITABLE)
                        RTPrintf(" writable");
                    else
                        RTPrintf(" readonly");
                    if (fFlags & SHFL_MIF_AUTO_MOUNT)
                        RTPrintf(" auto-mount");
                    if (fFlags & SHFL_MIF_SYMLINK_CREATION)
                        RTPrintf(" create-symlink");
                    if (fFlags & SHFL_MIF_HOST_ICASE)
                        RTPrintf(" host-icase");
                    if (fFlags & SHFL_MIF_GUEST_ICASE)
                        RTPrintf(" guest-icase");
                    if (*pszMntPt)
                        RTPrintf(" mnt-pt=%s", pszMntPt);
                    RTPrintf("]");
# ifdef RT_OS_OS2
                    /* Show drive letters: */
                    const char *pszOn = " on";
                    for (char chDrive = 'A'; chDrive <= 'Z'; chDrive++)
                    {
                        char szDrive[4] = { chDrive, ':', '\0', '\0' };
                        union
                        {
                            FSQBUFFER2  FsQueryBuf;
                            char        achPadding[512];
                        } uBuf;
                        RT_ZERO(uBuf);
                        ULONG cbBuf = sizeof(uBuf) - 2;
                        APIRET rcOs2 = DosQueryFSAttach(szDrive, 0, FSAIL_QUERYNAME, &uBuf.FsQueryBuf, &cbBuf);
                        if (rcOs2 == NO_ERROR)
                        {
                            const char *pszFsdName = (const char *)&uBuf.FsQueryBuf.szName[uBuf.FsQueryBuf.cbName + 1];
                            if (   uBuf.FsQueryBuf.iType == FSAT_REMOTEDRV
                                && RTStrICmpAscii(pszFsdName, "VBOXSF") == 0)
                            {
                                const char *pszMountedName = (const char *)&pszFsdName[uBuf.FsQueryBuf.cbFSDName + 1];
                                if (RTStrICmp(pszMountedName, pszName) == 0)
                                {
                                    const char *pszTag = pszMountedName + strlen(pszMountedName) + 1; /* safe */
                                    if (*pszTag != '\0')
                                        RTPrintf("%s %s (%s)", pszOn, szDrive, pszTag);
                                    else
                                        RTPrintf("%s %s", pszOn, szDrive);
                                    pszOn = ",";
                                }
                            }
                        }
                    }
# endif
                    RTPrintf("\n");

                    RTStrFree(pszName);
                    RTStrFree(pszMntPt);
                }
                else
                    VBoxControlError("Error while getting the shared folder name for root node = %u, rc = %Rrc\n",
                                     paMappings[i].u32Root, rc);
            }
            if (!cMappings)
            {
                RTPrintf("No Shared Folders available.\n");
                /* Return non-zero status for shell scripts to
                 * detect that there is no mapping available. */
                rc = VERR_NO_DATA;
            }
            VbglR3SharedFolderFreeMappings(paMappings);
        }
        else
            VBoxControlError("Error while getting the shared folder mappings, rc = %Rrc\n", rc);
        VbglR3SharedFolderDisconnect(u32ClientId);
    }
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

# ifdef RT_OS_OS2
/**
 * Attaches a shared folder to a drive letter.
 */
static RTEXITCODE sharedFolder_use(int argc, char **argv)
{
    /*
     * Takes a drive letter and a share name as arguments.
     */
    if (argc != 2)
        return VBoxControlSyntaxError("sharedfolder use: expected a drive letter and a shared folder name\n");

    const char *pszDrive  = argv[0];
    if (!RT_C_IS_ALPHA(pszDrive[0]) || pszDrive[1] != ':' || pszDrive[2] != '\0')
        return VBoxControlSyntaxError("sharedfolder use: not a drive letter: %s\n", pszDrive);

    static const char s_szTag[] = "VBoxControl";
    char        szzNameAndTag[256];
    const char *pszName   = argv[1];
    size_t cchName = strlen(pszName);
    if (cchName < 1)
        return VBoxControlSyntaxError("sharedfolder use: shared folder name cannot be empty!\n");
    if (cchName + 1 + sizeof(s_szTag) >= sizeof(szzNameAndTag))
        return VBoxControlSyntaxError("sharedfolder use: shared folder name is too long! (%s)\n", pszName);

    /*
     * Do the attaching.
     */
    memcpy(szzNameAndTag, pszName, cchName);
    szzNameAndTag[cchName] = '\0';
    memcpy(&szzNameAndTag[cchName + 1], s_szTag, sizeof(s_szTag));

    APIRET rcOs2 = DosFSAttach(pszDrive, "VBOXSF", szzNameAndTag, cchName + 1 + sizeof(s_szTag), FS_ATTACH);
    if (rcOs2 == NO_ERROR)
        return RTEXITCODE_SUCCESS;
    if (rcOs2 == ERROR_INVALID_FSD_NAME)
        return VBoxControlError("Shared folders IFS not installed?\n");
    return VBoxControlError("DosFSAttach/FS_ATTACH failed to attach '%s' to '%s': %u\n", pszName, pszDrive, rcOs2);
}

/**
 * Detaches a shared folder from a drive letter.
 */
static RTEXITCODE sharedFolder_unuse(int argc, char **argv)
{
    /*
     * Only takes a drive letter as argument.
     */
    if (argc != 1)
        return VBoxControlSyntaxError("sharedfolder unuse: expected drive letter\n");
    const char *pszDrive = argv[0];
    if (!RT_C_IS_ALPHA(pszDrive[0]) || pszDrive[1] != ':' || pszDrive[2] != '\0')
        return VBoxControlSyntaxError("sharedfolder unuse: not a drive letter: %s\n", pszDrive);

    /*
     * Do the detaching.
     */
    APIRET rcOs2 = DosFSAttach(pszDrive, "VBOXSF", NULL, 0, FS_DETACH);
    if (rcOs2 == NO_ERROR)
        return RTEXITCODE_SUCCESS;
    return VBoxControlError("DosFSAttach/FS_DETACH failed on '%s': %u\n", pszDrive, rcOs2);
}

# endif /* RT_OS_OS2 */


/**
 * Handles Shared Folders control.
 *
 * @returns 0 on success, 1 on failure
 * @note see the command line API description for parameters
 *      (r=bird: yeah, right. The API description contains nil about params)
 */
static DECLCALLBACK(RTEXITCODE) handleSharedFolder(int argc, char *argv[])
{
    if (argc == 0)
    {
        usage(GUEST_SHAREDFOLDERS);
        return RTEXITCODE_FAILURE;
    }
    if (!strcmp(argv[0], "list"))
        return sharedFolder_list(argc - 1, argv + 1);
# ifdef RT_OS_OS2
    if (!strcmp(argv[0], "use"))
        return sharedFolder_use(argc - 1, argv + 1);
    if (!strcmp(argv[0], "unuse"))
        return sharedFolder_unuse(argc - 1, argv + 1);
# endif

    usage(GUEST_SHAREDFOLDERS);
    return RTEXITCODE_FAILURE;
}

#endif
#if !defined(VBOX_CONTROL_TEST)

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: writecoredump}
 */
static DECLCALLBACK(RTEXITCODE) handleWriteCoreDump(int argc, char *argv[])
{
    RT_NOREF2(argc, argv);
    int rc = VbglR3WriteCoreDump();
    if (RT_SUCCESS(rc))
    {
        RTPrintf("Guest core dump successful.\n");
        return RTEXITCODE_SUCCESS;
    }
    else
    {
        VBoxControlError("Error while taking guest core dump. rc=%Rrc\n", rc);
        return RTEXITCODE_FAILURE;
    }
}

#endif
#ifdef VBOX_WITH_DPC_LATENCY_CHECKER

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: help}
 */
static DECLCALLBACK(RTEXITCODE) handleDpc(int argc, char *argv[])
{
    RT_NOREF(argc,  argv);
    int rc = VERR_NOT_IMPLEMENTED;
# ifndef VBOX_CONTROL_TEST
    for (int i = 0; i < 30; i++)
    {
        VBGLREQHDR Req;
        VBGLREQHDR_INIT(&Req, DPC_LATENCY_CHECKER);
        rc = vbglR3DoIOCtl(VBGL_IOCTL_DPC_LATENCY_CHECKER, &Req, sizeof(Req));
        if (RT_SUCCESS(rc))
            RTPrintf("%d\n", i);
        else
            break;
    }
# endif
    if (RT_FAILURE(rc))
        return VBoxControlError("Error. rc=%Rrc\n", rc);
    RTPrintf("Samples collection completed.\n");
    return RTEXITCODE_SUCCESS;
}
#endif /* VBOX_WITH_DPC_LATENCY_CHECKER */


/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: writelog}
 */
static DECLCALLBACK(RTEXITCODE) handleWriteLog(int argc, char *argv[])
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--no-newline", 'n', RTGETOPT_REQ_NOTHING },
    };
    bool fNoNewline = false;

    RTGETOPTSTATE GetOptState;
    int rc = RTGetOptInit(&GetOptState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions),
                          0 /*iFirst*/, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_SUCCESS(rc))
    {
        RTGETOPTUNION   ValueUnion;
        int             ch;
        while ((ch = RTGetOpt(&GetOptState, &ValueUnion)) != 0)
        {
            switch (ch)
            {
                case VINF_GETOPT_NOT_OPTION:
                {
                    size_t cch = strlen(ValueUnion.psz);
                    if (   fNoNewline
                        || (cch > 0 && ValueUnion.psz[cch - 1] == '\n') )
                        rc = VbglR3WriteLog(ValueUnion.psz, cch);
                    else
                    {
                        char *pszDup = (char *)RTMemDupEx(ValueUnion.psz, cch, 2);
                        if (RT_SUCCESS(rc))
                        {
                            pszDup[cch++] = '\n';
                            pszDup[cch]   = '\0';
                            rc = VbglR3WriteLog(pszDup, cch);
                            RTMemFree(pszDup);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    if (RT_FAILURE(rc))
                        return VBoxControlError("VbglR3WriteLog: %Rrc", rc);
                    break;
                }

                case 'n':
                    fNoNewline = true;
                    break;

                case 'h': return usage(WRITE_LOG);
                case 'V': return printVersion();
                default:
                    return VBoxCtrlGetOptError(ch, &ValueUnion);
            }
        }
    }
    else
        return VBoxControlError("RTGetOptInit: %Rrc", rc);
    return RTEXITCODE_SUCCESS;
}


/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: takesnapshot}
 */
static DECLCALLBACK(RTEXITCODE) handleTakeSnapshot(int argc, char *argv[])
{
    RT_NOREF2(argc, argv); //VbglR3VmTakeSnapshot(argv[0], argv[1]);
    return VBoxControlError("not implemented");
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: savestate}
 */
static DECLCALLBACK(RTEXITCODE) handleSaveState(int argc, char *argv[])
{
    RT_NOREF2(argc, argv); //VbglR3VmSaveState();
    return VBoxControlError("not implemented");
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: suspend|pause}
 */
static DECLCALLBACK(RTEXITCODE) handleSuspend(int argc, char *argv[])
{
    RT_NOREF2(argc, argv); //VbglR3VmSuspend();
    return VBoxControlError("not implemented");
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: poweroff|powerdown}
 */
static DECLCALLBACK(RTEXITCODE) handlePowerOff(int argc, char *argv[])
{
    RT_NOREF2(argc, argv); //VbglR3VmPowerOff();
    return VBoxControlError("not implemented");
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: version}
 */
static DECLCALLBACK(RTEXITCODE) handleVersion(int argc, char *argv[])
{
    RT_NOREF1(argv);
    if (argc)
        return VBoxControlSyntaxError("getversion does not take any arguments");
    return printVersion();
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: help}
 */
static DECLCALLBACK(RTEXITCODE) handleHelp(int argc, char *argv[])
{
    RT_NOREF2(argc, argv); /* ignore arguments for now. */
    usage();
    return RTEXITCODE_SUCCESS;
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: ls}
 */
static DECLCALLBACK(RTEXITCODE) handleLs(int argc, char *argv[])
{
    return RTFsCmdLs(argc + 1, argv - 1);
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: tar}
 */
static DECLCALLBACK(RTEXITCODE) handleTar(int argc, char *argv[])
{
    return RTZipTarCmd(argc + 1, argv - 1);
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: tar}
 */
static DECLCALLBACK(RTEXITCODE) handleGzip(int argc, char *argv[])
{
    return RTZipGzipCmd(argc + 1, argv - 1);
}

/**
 * @callback_method_impl{FNVBOXCTRLCMDHANDLER, Command: unzip}
 */
static DECLCALLBACK(RTEXITCODE) handleUnzip(int argc, char *argv[])
{
    return RTZipUnzipCmd(argc + 1, argv - 1);
}


/** command handler type */
typedef DECLCALLBACKTYPE(RTEXITCODE, FNVBOXCTRLCMDHANDLER,(int argc, char *argv[]));
typedef FNVBOXCTRLCMDHANDLER *PFNVBOXCTRLCMDHANDLER;

/** The table of all registered command handlers. */
struct COMMANDHANDLER
{
    const char             *pszCommand;
    PFNVBOXCTRLCMDHANDLER   pfnHandler;
    bool                    fNeedDevice;
} g_aCommandHandlers[] =
{
#if defined(RT_OS_WINDOWS) && !defined(VBOX_CONTROL_TEST)
    { "getvideoacceleration",   handleGetVideoAcceleration, true  },
    { "setvideoacceleration",   handleSetVideoAcceleration, true  },
    { "videoflags",             handleVideoFlags,           true  },
    { "listcustommodes",        handleListCustomModes,      true  },
    { "addcustommode",          handleAddCustomMode,        true  },
    { "removecustommode",       handleRemoveCustomMode,     true  },
    { "setvideomode",           handleSetVideoMode,         true  },
#endif
#ifdef VBOX_WITH_GUEST_PROPS
    { "guestproperty",          handleGuestProperty,        true  },
#endif
#ifdef VBOX_WITH_SHARED_FOLDERS
    { "sharedfolder",           handleSharedFolder,         true  },
#endif
#if !defined(VBOX_CONTROL_TEST)
    { "writecoredump",          handleWriteCoreDump,        true  },
#endif
#ifdef VBOX_WITH_DPC_LATENCY_CHECKER
    { "dpc",                    handleDpc,                  true  },
#endif
    { "writelog",               handleWriteLog,             true  },
    { "takesnapshot",           handleTakeSnapshot,         true  },
    { "savestate",              handleSaveState,            true  },
    { "suspend",                handleSuspend,              true  },
    { "pause",                  handleSuspend,              true  },
    { "poweroff",               handlePowerOff,             true  },
    { "powerdown",              handlePowerOff,             true  },
    { "getversion",             handleVersion,              false },
    { "version",                handleVersion,              false },
    { "help",                   handleHelp,                 false },
    /* Hany tricks that doesn't cost much space: */
    { "gzip",                   handleGzip,                 false },
    { "ls",                     handleLs,                   false },
    { "tar",                    handleTar,                  false },
    { "unzip",                  handleUnzip,                false },
};

/** Main function */
int main(int argc, char **argv)
{
    /** The application's global return code */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    /** An IPRT return code for local use */
    int rrc = VINF_SUCCESS;
    /** The index of the command line argument we are currently processing */
    int iArg = 1;
    /** Should we show the logo text? */
    bool fShowLogo = true;
    /** Should we print the usage after the logo?  For the -help switch. */
    bool fDoHelp = false;
    /** Will we be executing a command or just printing information? */
    bool fOnlyInfo = false;

    rrc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rrc))
        return RTMsgInitFailure(rrc);

    /*
     * Start by handling command line switches
     */
    /** @todo RTGetOpt conversion of the whole file. */
    bool done = false;      /**< Are we finished with handling switches? */
    while (!done && (iArg < argc))
    {
        if (   !strcmp(argv[iArg], "-V")
            || !strcmp(argv[iArg], "-v")
            || !strcmp(argv[iArg], "--version")
            || !strcmp(argv[iArg], "-version")
           )
        {
            /* Print version number, and do nothing else. */
            printVersion();
            fOnlyInfo = true;
            fShowLogo = false;
            done = true;
        }
        else if (   !strcmp(argv[iArg], "-nologo")
                 || !strcmp(argv[iArg], "--nologo"))
            fShowLogo = false;
        else if (   !strcmp(argv[iArg], "-help")
                 || !strcmp(argv[iArg], "--help"))
        {
            fOnlyInfo = true;
            fDoHelp = true;
            done = true;
        }
        else
            /* We have found an argument which isn't a switch.  Exit to the
             * command processing bit. */
            done = true;
        if (!done)
            ++iArg;
    }

    /*
     * Find the application name, show our logo if the user hasn't suppressed it,
     * and show the usage if the user asked us to
     */
    g_pszProgName = RTPathFilename(argv[0]);
    if (fShowLogo)
        RTPrintf(VBOX_PRODUCT " Guest Additions Command Line Management Interface Version "
                 VBOX_VERSION_STRING "\n"
                 "Copyright (C) 2008-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n");
    if (fDoHelp)
        usage();

    /*
     * Now look for an actual command in the argument list and handle it.
     */
    if (!fOnlyInfo && rcExit == RTEXITCODE_SUCCESS)
    {
        if (argc > iArg)
        {
            /*
             * Try locate the command and execute it, complain if not found.
             */
            unsigned i;
            for (i = 0; i < RT_ELEMENTS(g_aCommandHandlers); i++)
                if (!strcmp(argv[iArg], g_aCommandHandlers[i].pszCommand))
                {
                    if (g_aCommandHandlers[i].fNeedDevice)
                    {
                        rrc = VbglR3Init();
                        if (RT_FAILURE(rrc))
                        {
                            VBoxControlError("Could not contact the host system.  Make sure that you are running this\n"
                                             "application inside a VirtualBox guest system, and that you have sufficient\n"
                                             "user permissions.\n");
                            rcExit = RTEXITCODE_FAILURE;
                        }
                    }
                    if (rcExit == RTEXITCODE_SUCCESS)
                        rcExit = g_aCommandHandlers[i].pfnHandler(argc - iArg - 1, argv + iArg + 1);
                    break;
                }
            if (i >= RT_ELEMENTS(g_aCommandHandlers))
            {
                usage();
                rcExit = RTEXITCODE_SYNTAX;
            }
        }
        else
        {
            /* The user didn't specify a command. */
            usage();
            rcExit = RTEXITCODE_SYNTAX;
        }
    }

    /*
     * And exit, returning the status
     */
    return rcExit;
}

