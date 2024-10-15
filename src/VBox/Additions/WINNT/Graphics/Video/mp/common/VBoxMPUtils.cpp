/* $Id: VBoxMPUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Miniport utils
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

#include "VBoxMPUtils.h"

#ifdef VBOX_XPDM_MINIPORT
# include <iprt/nt/ntddk.h>
#endif
#include <VBox/VMMDev.h>
#include <VBox/VBoxGuestLib.h>

#ifdef DEBUG_misha
/* specifies whether the vboxVDbgBreakF should break in the debugger
 * windbg seems to have some issues when there is a lot ( >32) of sw breakpoints defined
 * to simplify things we just insert breaks for the case of intensive debugging WDDM driver*/
int g_bVBoxVDbgBreakF = 0;
int g_bVBoxVDbgBreakFv = 0;
#endif

#pragma alloc_text(PAGE, VBoxQueryWinVersion)
#pragma alloc_text(PAGE, VBoxGetHeightReduction)
#pragma alloc_text(PAGE, VBoxLikesVideoMode)
#pragma alloc_text(PAGE, VBoxQueryDisplayRequest)
#pragma alloc_text(PAGE, VBoxQueryHostWantsAbsolute)
#pragma alloc_text(PAGE, VBoxQueryPointerPos)

/*Returns the windows version we're running on*/
/** @todo r=andy Use RTSystemQueryOSInfo() instead? This also does caching and stuff. */
vboxWinVersion_t VBoxQueryWinVersion(uint32_t *pbuild)
{
    static ULONG            s_pbuild     = 0;
    static vboxWinVersion_t s_WinVersion = WINVERSION_UNKNOWN;

    ULONG major, minor;
    BOOLEAN checkedBuild;

    if (s_WinVersion == WINVERSION_UNKNOWN)
    {
        checkedBuild = PsGetVersion(&major, &minor, &s_pbuild, NULL);
        LOG(("running on version %d.%d, build %d(checked=%d)", major, minor, s_pbuild, (int)checkedBuild));

        if (major > 6)
        {
            /* Everything newer than Windows 8.1, i.e. Windows 10 with major == 10. */
            s_WinVersion = WINVERSION_10;
        }
        else if (major == 6)
        {
            if (minor >= 4)
                s_WinVersion = WINVERSION_10;
            else if (minor == 3)
                s_WinVersion = WINVERSION_81;
            else if (minor == 2)
                s_WinVersion = WINVERSION_8;
            else if (minor == 1)
                s_WinVersion = WINVERSION_7;
            else if (minor == 0)
                s_WinVersion = WINVERSION_VISTA; /* Or Windows Server 2008. */
        }
        else if (major == 5)
            s_WinVersion = (minor>=1) ? WINVERSION_XP: WINVERSION_2K;
        else if (major == 4)
            s_WinVersion = WINVERSION_NT4;
        else
            WARN(("NT4 required!"));
    }

    if (pbuild)
        *pbuild = s_pbuild;

    return s_WinVersion;
}

uint32_t VBoxGetHeightReduction()
{
    uint32_t retHeight = 0;
    int rc;

    LOGF_ENTER();

    VMMDevGetHeightReductionRequest *req = NULL;

    rc = VbglR0GRAlloc((VMMDevRequestHeader**)&req, sizeof(VMMDevGetHeightReductionRequest), VMMDevReq_GetHeightReduction);
    if (RT_FAILURE(rc))
    {
        WARN(("ERROR allocating request, rc = %#xrc", rc));
    }
    else
    {
        rc = VbglR0GRPerform(&req->header);
        if (RT_SUCCESS(rc))
        {
            retHeight = req->heightReduction;
        }
        else
        {
            WARN(("ERROR querying height reduction value from VMMDev. rc = %#xrc", rc));
        }
        VbglR0GRFree(&req->header);
    }

    LOGF_LEAVE();
    return retHeight;
}

bool VBoxLikesVideoMode(uint32_t display, uint32_t width, uint32_t height, uint32_t bpp)
{
    bool bRC = FALSE;

    VMMDevVideoModeSupportedRequest2 *req2 = NULL;

    int rc = VbglR0GRAlloc((VMMDevRequestHeader**)&req2, sizeof(VMMDevVideoModeSupportedRequest2), VMMDevReq_VideoModeSupported2);
    if (RT_FAILURE(rc))
    {
        LOG(("ERROR allocating request, rc = %#xrc", rc));
        /* Most likely the VBoxGuest driver is not loaded.
         * To get at least the video working, report the mode as supported.
         */
        bRC = TRUE;
    }
    else
    {
        req2->display = display;
        req2->width  = width;
        req2->height = height;
        req2->bpp    = bpp;
        rc = VbglR0GRPerform(&req2->header);
        if (RT_SUCCESS(rc))
        {
            bRC = req2->fSupported;
        }
        else
        {
            /* Retry using old interface. */
            AssertCompile(sizeof(VMMDevVideoModeSupportedRequest2) >= sizeof(VMMDevVideoModeSupportedRequest));
            VMMDevVideoModeSupportedRequest *req = (VMMDevVideoModeSupportedRequest *)req2;
            req->header.size        = sizeof(VMMDevVideoModeSupportedRequest);
            req->header.version     = VMMDEV_REQUEST_HEADER_VERSION;
            req->header.requestType = VMMDevReq_VideoModeSupported;
            req->header.rc          = VERR_GENERAL_FAILURE;
            req->header.reserved1   = 0;
            req->width  = width;
            req->height = height;
            req->bpp    = bpp;

            rc = VbglR0GRPerform(&req->header);
            if (RT_SUCCESS(rc))
            {
                bRC = req->fSupported;
            }
            else
            {
                WARN(("ERROR querying video mode supported status from VMMDev. rc = %#xrc", rc));
            }
        }
        VbglR0GRFree(&req2->header);
    }

    LOG(("width: %d, height: %d, bpp: %d -> %s", width, height, bpp, (bRC == 1) ? "OK" : "FALSE"));

    return bRC;
}

bool VBoxQueryDisplayRequest(uint32_t *xres, uint32_t *yres, uint32_t *bpp, uint32_t *pDisplayId)
{
    bool bRC = FALSE;
    VMMDevDisplayChangeRequest2 *req = NULL;

    LOGF_ENTER();

    int rc = VbglR0GRAlloc ((VMMDevRequestHeader **)&req, sizeof (VMMDevDisplayChangeRequest2), VMMDevReq_GetDisplayChangeRequest2);

    if (RT_FAILURE(rc))
    {
        LOG(("ERROR allocating request, rc = %#xrc", rc));
    }
    else
    {
        req->eventAck = 0;

        rc = VbglR0GRPerform (&req->header);

        if (RT_SUCCESS(rc))
        {
            if (xres)
                *xres = req->xres;
            if (yres)
                *yres = req->yres;
            if (bpp)
                *bpp  = req->bpp;
            if (pDisplayId)
                *pDisplayId  = req->display;
            LOG(("returning %d x %d @ %d for %d", req->xres, req->yres, req->bpp, req->display));
            bRC = TRUE;
        }
        else
        {
            WARN(("ERROR querying display request from VMMDev. rc = %#xrc", rc));
        }

        VbglR0GRFree (&req->header);
    }

    LOGF_LEAVE();
    return bRC;
}

static bool VBoxQueryPointerPosInternal(uint16_t *pPosX, uint16_t *pPosY)
{
    bool bRC = FALSE;

    VMMDevReqMouseStatus *req = NULL;

    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&req, sizeof(VMMDevReqMouseStatus), VMMDevReq_GetMouseStatus);

    if (RT_FAILURE(rc))
    {
        LOG(("ERROR allocating request, rc = %#xrc", rc));
    }
    else
    {
        rc = VbglR0GRPerform(&req->header);

        if (RT_SUCCESS(rc))
        {
            if (req->mouseFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE)
            {
                if (pPosX)
                {
                    *pPosX = req->pointerXPos;
                }

                if (pPosY)
                {
                    *pPosY = req->pointerYPos;
                }

                bRC = TRUE;
            }
        }
        else
        {
            LOG(("ERROR querying mouse capabilities from VMMDev. rc = %#xrc", rc));
        }

        VbglR0GRFree(&req->header);
    }

    return bRC;
}

/* Returns if the host wants us to take absolute pointer coordinates. */
bool VBoxQueryHostWantsAbsolute()
{
    return VBoxQueryPointerPosInternal(NULL, NULL);
}

bool VBoxQueryPointerPos(uint16_t *pPosX, uint16_t *pPosY)
{
    if (!pPosX || !pPosY)
    {
        return FALSE;
    }

    return VBoxQueryPointerPosInternal(pPosX, pPosY);
}
