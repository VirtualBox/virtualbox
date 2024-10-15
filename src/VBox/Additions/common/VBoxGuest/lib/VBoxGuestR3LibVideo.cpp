/* $Id: VBoxGuestR3LibVideo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Video.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxGuestR3LibInternal.h"

#include <VBox/log.h>
#include <VBox/HostServices/GuestPropertySvc.h>  /* For Save and RetrieveVideoMode */
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#ifdef VBOX_VBGLR3_XFREE86
/* Rather than try to resolve all the header file conflicts, I will just
   prototype what we need here. */
extern "C" void* xf86memcpy(void*,const void*,xf86size_t);
# undef memcpy
# define memcpy xf86memcpy
extern "C" void* xf86memset(const void*,int,xf86size_t);
# undef memset
# define memset xf86memset
#endif /* VBOX_VBGLR3_XFREE86 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VIDEO_PROP_PREFIX "/VirtualBox/GuestAdd/Vbgl/Video/"


/**
 * Enable or disable video acceleration.
 *
 * @returns VBox status code.
 *
 * @param   fEnable       Pass zero to disable, any other value to enable.
 */
VBGLR3DECL(int) VbglR3VideoAccelEnable(bool fEnable)
{
    VMMDevVideoAccelEnable Req;
    vmmdevInitRequest(&Req.header, VMMDevReq_VideoAccelEnable);
    Req.u32Enable = fEnable;
    Req.cbRingBuffer = VMMDEV_VBVA_RING_BUFFER_SIZE;
    Req.fu32Status = 0;
    return vbglR3GRPerform(&Req.header);
}


/**
 * Flush the video buffer.
 *
 * @returns VBox status code.
 */
VBGLR3DECL(int) VbglR3VideoAccelFlush(void)
{
    VMMDevVideoAccelFlush Req;
    vmmdevInitRequest(&Req.header, VMMDevReq_VideoAccelFlush);
    return vbglR3GRPerform(&Req.header);
}


/**
 * Send mouse pointer shape information to the host.
 *
 * @returns VBox status code.
 *
 * @param   fFlags      Mouse pointer flags.
 * @param   xHot        X coordinate of hot spot.
 * @param   yHot        Y coordinate of hot spot.
 * @param   cx          Pointer width.
 * @param   cy          Pointer height.
 * @param   pvImg       Pointer to the image data (can be NULL).
 * @param   cbImg       Size of the image data pointed to by pvImg.
 */
VBGLR3DECL(int) VbglR3SetPointerShape(uint32_t fFlags, uint32_t xHot, uint32_t yHot, uint32_t cx, uint32_t cy,
                                      const void *pvImg, size_t cbImg)
{
    VMMDevReqMousePointer *pReq;
    size_t cbReq = vmmdevGetMousePointerReqSize(cx, cy);
    AssertReturn(   !pvImg
                 || cbReq == RT_UOFFSETOF(VMMDevReqMousePointer, pointerData) + cbImg,
                 VERR_INVALID_PARAMETER);
    int rc = vbglR3GRAlloc((VMMDevRequestHeader **)&pReq, cbReq, VMMDevReq_SetPointerShape);
    if (RT_SUCCESS(rc))
    {
        pReq->fFlags = fFlags;
        pReq->xHot = xHot;
        pReq->yHot = yHot;
        pReq->width = cx;
        pReq->height = cy;
        if (pvImg)
            memcpy(pReq->pointerData, pvImg, cbImg);

        rc = vbglR3GRPerform(&pReq->header);
        if (RT_SUCCESS(rc))
            rc = pReq->header.rc;
        vbglR3GRFree(&pReq->header);
    }
    return rc;
}


/**
 * Send mouse pointer shape information to the host.
 * This version of the function accepts a request for clients that
 * already allocate and manipulate the request structure directly.
 *
 * @returns VBox status code.
 *
 * @param   pReq        Pointer to the VMMDevReqMousePointer structure.
 */
VBGLR3DECL(int) VbglR3SetPointerShapeReq(VMMDevReqMousePointer *pReq)
{
    int rc = vbglR3GRPerform(&pReq->header);
    if (RT_SUCCESS(rc))
        rc = pReq->header.rc;
    return rc;
}


/**
 * Query the last display change request sent from the host to the guest.
 *
 * @returns iprt status value
 * @param   pcx         Where to store the horizontal pixel resolution
 * @param   pcy         Where to store the vertical pixel resolution
 *                      requested (a value of zero means do not change).
 * @param   pcBits      Where to store the bits per pixel requested (a value
 *                      of zero means do not change).
 * @param   piDisplay   Where to store the display number the request was for
 *                      - 0 for the primary display, 1 for the first
 *                      secondary display, etc.
 * @param   fAck        whether or not to acknowledge the newest request sent by
 *                      the host.  If this is set, the function will return the
 *                      most recent host request, otherwise it will return the
 *                      last request to be acknowledged.
 *
 */
static int getDisplayChangeRequest2(uint32_t *pcx, uint32_t *pcy,
                                    uint32_t *pcBits, uint32_t *piDisplay,
                                    bool fAck)
{
    VMMDevDisplayChangeRequest2 Req;

    AssertPtrReturn(pcx, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcy, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcBits, VERR_INVALID_PARAMETER);
    AssertPtrReturn(piDisplay, VERR_INVALID_PARAMETER);
    RT_ZERO(Req);
    vmmdevInitRequest(&Req.header, VMMDevReq_GetDisplayChangeRequest2);
    if (fAck)
        Req.eventAck = VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST;
    int rc = vbglR3GRPerform(&Req.header);
    if (RT_SUCCESS(rc))
        rc = Req.header.rc;
    if (RT_SUCCESS(rc))
    {
        *pcx = Req.xres;
        *pcy = Req.yres;
        *pcBits = Req.bpp;
        *piDisplay = Req.display;
    }
    return rc;
}


/**
 * Query the last display change request sent from the host to the guest.
 *
 * @returns iprt status value
 * @param   pcx         Where to store the horizontal pixel resolution
 *                      requested (a value of zero means do not change).
 * @param   pcy         Where to store the vertical pixel resolution
 *                      requested (a value of zero means do not change).
 * @param   pcBits      Where to store the bits per pixel requested (a value
 *                      of zero means do not change).
 * @param   piDisplay   Where to store the display number the request was for
 *                      - 0 for the primary display, 1 for the first
 *                      secondary display, etc.
 * @param   fAck        whether or not to acknowledge the newest request sent by
 *                      the host.  If this is set, the function will return the
 *                      most recent host request, otherwise it will return the
 *                      last request to be acknowledged.
 *
 * @param   pdx         New horizontal position of the secondary monitor.
 *                      Optional.
 * @param   pdy         New vertical position of the secondary monitor.
 *                      Optional.
 * @param   pfEnabled   Secondary monitor is enabled or not. Optional.
 * @param   pfChangeOrigin  Whether the mode hint retrieved included
 *                      information about origin/display offset inside the
 *                      frame-buffer. Optional.
 *
 */
VBGLR3DECL(int) VbglR3GetDisplayChangeRequest(uint32_t *pcx, uint32_t *pcy,
                                              uint32_t *pcBits,
                                              uint32_t *piDisplay,
                                              uint32_t *pdx, uint32_t *pdy,
                                              bool *pfEnabled,
                                              bool *pfChangeOrigin,
                                              bool fAck)
{
    VMMDevDisplayChangeRequestEx Req;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pcx, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcy, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcBits, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pdx, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pdy, VERR_INVALID_PARAMETER);
    AssertPtrReturn(piDisplay, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfEnabled, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfChangeOrigin, VERR_INVALID_PARAMETER);

    RT_ZERO(Req);
    rc = vmmdevInitRequest(&Req.header, VMMDevReq_GetDisplayChangeRequestEx);
    AssertRCReturn(rc, rc);
    if (fAck)
        Req.eventAck = VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST;
    rc = vbglR3GRPerform(&Req.header);
    if (RT_SUCCESS(rc))
        rc = Req.header.rc;
    if (RT_SUCCESS(rc))
    {
        *pcx = Req.xres;
        *pcy = Req.yres;
        *pcBits = Req.bpp;
        *piDisplay = Req.display;
        if (pdx)
            *pdx = Req.cxOrigin;
        if (pdy)
            *pdy = Req.cyOrigin;
        if (pfEnabled)
            *pfEnabled = Req.fEnabled;
        if (pfChangeOrigin)
            *pfChangeOrigin = Req.fChangeOrigin;
        return VINF_SUCCESS;
    }

    /* NEEDS TESTING: test below with current Additions on VBox 4.1 or older. */
    /** @todo Can we find some standard grep-able string for "NEEDS TESTING"? */
    if (rc == VERR_NOT_IMPLEMENTED)  /* Fall back to the old API. */
    {
        if (pfEnabled)
            *pfEnabled = true;
        if (pfChangeOrigin)
            *pfChangeOrigin = false;
        return getDisplayChangeRequest2(pcx, pcy, pcBits, piDisplay, fAck);
    }
    return rc;
}


/**
 * Query the last display change request sent from the host to the guest.
 *
 * @returns iprt status value
 * @param   cDisplaysIn   How many elements in the paDisplays array.
 * @param   pcDisplaysOut How many elements were returned.
 * @param   paDisplays    Display information.
 * @param   fAck          Whether or not to acknowledge the newest request sent by
 *                        the host.  If this is set, the function will return the
 *                        most recent host request, otherwise it will return the
 *                        last request to be acknowledged.
 */
VBGLR3DECL(int) VbglR3GetDisplayChangeRequestMulti(uint32_t cDisplaysIn,
                                                   uint32_t *pcDisplaysOut,
                                                   VMMDevDisplayDef *paDisplays,
                                                   bool fAck)
{
    VMMDevDisplayChangeRequestMulti *pReq;
    size_t cbDisplays;
    size_t cbAlloc;
    int rc = VINF_SUCCESS;

    AssertReturn(cDisplaysIn > 0 && cDisplaysIn <= 64 /* VBOX_VIDEO_MAX_SCREENS */, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcDisplaysOut, VERR_INVALID_PARAMETER);
    AssertPtrReturn(paDisplays, VERR_INVALID_PARAMETER);

    cbDisplays = cDisplaysIn * sizeof(VMMDevDisplayDef);
    cbAlloc = RT_UOFFSETOF(VMMDevDisplayChangeRequestMulti, aDisplays) + cbDisplays;
    pReq = (VMMDevDisplayChangeRequestMulti *)RTMemTmpAlloc(cbAlloc);
    AssertPtrReturn(pReq, VERR_NO_MEMORY);

    memset(pReq, 0, cbAlloc);
    rc = vmmdevInitRequest(&pReq->header, VMMDevReq_GetDisplayChangeRequestMulti);
    AssertRCReturnStmt(rc, RTMemTmpFree(pReq), rc);

    pReq->header.size += (uint32_t)cbDisplays;
    pReq->cDisplays = cDisplaysIn;
    if (fAck)
        pReq->eventAck = VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST;

    rc = vbglR3GRPerform(&pReq->header);
    AssertRCReturnStmt(rc, RTMemTmpFree(pReq), rc);

    rc = pReq->header.rc;
    if (RT_SUCCESS(rc))
    {
        memcpy(paDisplays, pReq->aDisplays, pReq->cDisplays * sizeof(VMMDevDisplayDef));
        *pcDisplaysOut = pReq->cDisplays;
    }

    RTMemTmpFree(pReq);
    return rc;
}


/**
 * Query the host as to whether it likes a specific video mode.
 *
 * @returns the result of the query
 * @param   cx     the width of the mode being queried
 * @param   cy     the height of the mode being queried
 * @param   cBits  the bpp of the mode being queried
 */
VBGLR3DECL(bool) VbglR3HostLikesVideoMode(uint32_t cx, uint32_t cy, uint32_t cBits)
{
    bool fRc = true;  /* If for some reason we can't contact the host then
                       * we like everything. */
    int rc;
    VMMDevVideoModeSupportedRequest req;

    vmmdevInitRequest(&req.header, VMMDevReq_VideoModeSupported);
    req.width      = cx;
    req.height     = cy;
    req.bpp        = cBits;
    req.fSupported = true;
    rc = vbglR3GRPerform(&req.header);
    if (RT_SUCCESS(rc) && RT_SUCCESS(req.header.rc))
        fRc = req.fSupported;
    return fRc;
}

/**
 * Get the highest screen number for which there is a saved video mode or "0"
 * if there are no saved modes.
 *
 * @returns iprt status value
 * @returns VERR_NOT_SUPPORTED if the guest property service is not available.
 * @param   pcScreen   where to store the virtual screen number
 */
VBGLR3DECL(int) VbglR3VideoModeGetHighestSavedScreen(unsigned *pcScreen)
{
#if defined(VBOX_WITH_GUEST_PROPS)
    int rc;
    HGCMCLIENTID idClient = 0;
    PVBGLR3GUESTPROPENUM pHandle = NULL;
    const char *pszName = NULL;
    unsigned cHighestScreen = 0;

    /* Validate input. */
    AssertPtrReturn(pcScreen, VERR_INVALID_POINTER);

    /* Query the data. */
    rc = VbglR3GuestPropConnect(&idClient);
    if (RT_SUCCESS(rc))
    {
        const char *pszPattern = VIDEO_PROP_PREFIX"*";
        rc = VbglR3GuestPropEnum(idClient, &pszPattern, 1, &pHandle, &pszName, NULL, NULL, NULL);
        int rc2 = VbglR3GuestPropDisconnect(idClient);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

    /* Process the data. */
    while (RT_SUCCESS(rc) && pszName != NULL)
    {
        uint32_t cScreen;

        rc = RTStrToUInt32Full(pszName + sizeof(VIDEO_PROP_PREFIX) - 1, 10, &cScreen);
        if (RT_SUCCESS(rc))  /* There may be similar properties with text. */
            cHighestScreen = RT_MAX(cHighestScreen, cScreen);
        rc = VbglR3GuestPropEnumNext(pHandle, &pszName, NULL, NULL, NULL);
    }

    VbglR3GuestPropEnumFree(pHandle);

    /* Return result. */
    if (RT_SUCCESS(rc))
        *pcScreen = cHighestScreen;
    return rc;
#else /* !VBOX_WITH_GUEST_PROPS */
    RT_NOREF(pcScreen);
    return VERR_NOT_SUPPORTED;
#endif /* !VBOX_WITH_GUEST_PROPS */
}

/**
 * Save video mode parameters to the guest property store.
 *
 * @returns iprt status value
 * @param   idScreen  The virtual screen number.
 * @param   cx        mode width
 * @param   cy        mode height
 * @param   cBits     bits per pixel for the mode
 * @param   x         virtual screen X offset
 * @param   y         virtual screen Y offset
 * @param   fEnabled  is this virtual screen enabled?
 */
VBGLR3DECL(int) VbglR3SaveVideoMode(unsigned idScreen, unsigned cx, unsigned cy, unsigned cBits,
                                    unsigned x, unsigned y, bool fEnabled)
{
#ifdef VBOX_WITH_GUEST_PROPS
    unsigned cHighestScreen = 0;
    int rc = VbglR3VideoModeGetHighestSavedScreen(&cHighestScreen);
    if (RT_SUCCESS(rc))
    {
        HGCMCLIENTID idClient = 0;
        rc = VbglR3GuestPropConnect(&idClient);
        if (RT_SUCCESS(rc))
        {
            int rc2;
            char szModeName[GUEST_PROP_MAX_NAME_LEN];
            char szModeParms[GUEST_PROP_MAX_VALUE_LEN];
            RTStrPrintf(szModeName, sizeof(szModeName), VIDEO_PROP_PREFIX "%u", idScreen);
            RTStrPrintf(szModeParms, sizeof(szModeParms), "%ux%ux%u,%ux%u,%u", cx, cy, cBits, x, y, (unsigned) fEnabled);

            rc = VbglR3GuestPropWriteValue(idClient, szModeName, szModeParms);
            /* Write out the mode using the legacy name too, in case the user
             * re-installs older Additions. */
            if (idScreen == 0)
            {
                RTStrPrintf(szModeParms, sizeof(szModeParms), "%ux%ux%u", cx, cy, cBits);
                VbglR3GuestPropWriteValue(idClient, VIDEO_PROP_PREFIX "SavedMode", szModeParms);
            }

            rc2 = VbglR3GuestPropDisconnect(idClient);
            if (rc != VINF_PERMISSION_DENIED)
            {
                if (RT_SUCCESS(rc))
                    rc = rc2;
                if (RT_SUCCESS(rc))
                {
                    /* Sanity check 1.  We do not try to make allowance for someone else
                     * changing saved settings at the same time as us. */
                    bool     fEnabled2 = false;
                    unsigned cx2       = 0;
                    unsigned cy2       = 0;
                    unsigned cBits2    = 0;
                    unsigned x2        = 0;
                    unsigned y2        = 0;
                    rc = VbglR3RetrieveVideoMode(idScreen, &cx2, &cy2, &cBits2, &x2, &y2, &fEnabled2);
                    if (   RT_SUCCESS(rc)
                        && (cx != cx2 || cy != cy2 || cBits != cBits2 || x != x2 || y != y2 || fEnabled != fEnabled2))
                        rc = VERR_WRITE_ERROR;
                    /* Sanity check 2.  Same comment. */
                    else if (RT_SUCCESS(rc))
                    {
                        unsigned cHighestScreen2 = 0;
                        rc = VbglR3VideoModeGetHighestSavedScreen(&cHighestScreen2);
                        if (RT_SUCCESS(rc))
                            if (cHighestScreen2 != RT_MAX(cHighestScreen, idScreen))
                                rc = VERR_INTERNAL_ERROR;
                    }
                }
            }
        }
    }
    return rc;
#else /* !VBOX_WITH_GUEST_PROPS */
    RT_NOREF(idScreen, cx, cy, cBits, x, y, fEnabled);
    return VERR_NOT_SUPPORTED;
#endif /* !VBOX_WITH_GUEST_PROPS */
}


/**
 * Retrieve video mode parameters from the guest property store.
 *
 * @returns iprt status value
 * @param   idScreen   The virtual screen number.
 * @param   pcx        where to store the mode width
 * @param   pcy        where to store the mode height
 * @param   pcBits     where to store the bits per pixel for the mode
 * @param   px         where to store the virtual screen X offset
 * @param   py         where to store the virtual screen Y offset
 * @param   pfEnabled  where to store whether this virtual screen is enabled
 */
VBGLR3DECL(int) VbglR3RetrieveVideoMode(unsigned idScreen,
                                        unsigned *pcx, unsigned *pcy,
                                        unsigned *pcBits,
                                        unsigned *px, unsigned *py,
                                        bool *pfEnabled)
{
#ifdef VBOX_WITH_GUEST_PROPS
    /*
     * First we retrieve the video mode which is saved as a string in the
     * guest property store.
     */
    HGCMCLIENTID idClient = 0;
    int rc = VbglR3GuestPropConnect(&idClient);
    if (RT_SUCCESS(rc))
    {
        int rc2;
        /* The buffer for VbglR3GuestPropReadValue.  If this is too small then
         * something is wrong with the data stored in the property. */
        char szModeParms[1024];
        char szModeName[GUEST_PROP_MAX_NAME_LEN]; /** @todo add a VbglR3GuestPropReadValueF/FV that does the RTStrPrintf for you. */
        RTStrPrintf(szModeName, sizeof(szModeName), VIDEO_PROP_PREFIX "%u", idScreen);
        rc = VbglR3GuestPropReadValue(idClient, szModeName, szModeParms, sizeof(szModeParms), NULL);
        /* Try legacy single screen name. */
        if (rc == VERR_NOT_FOUND && idScreen == 0)
            rc = VbglR3GuestPropReadValue(idClient,
                                          VIDEO_PROP_PREFIX"SavedMode",
                                          szModeParms, sizeof(szModeParms),
                                          NULL);
        rc2 = VbglR3GuestPropDisconnect(idClient);
        if (RT_SUCCESS(rc))
            rc = rc2;

        /*
         * Now we convert the string returned to numeric values.
         */
        if (RT_SUCCESS(rc))
        {
            /* Mandatory chunk: 640x480x32 */
            char       *pszNext;
            uint32_t    cx = 0;
            rc = VERR_PARSE_ERROR;
            rc2 = RTStrToUInt32Ex(szModeParms, &pszNext, 10, &cx);
            if (rc2 == VWRN_TRAILING_CHARS && *pszNext == 'x')
            {
                uint32_t cy = 0;
                rc2 = RTStrToUInt32Ex(pszNext + 1, &pszNext, 10, &cy);
                if (rc2 == VWRN_TRAILING_CHARS && *pszNext == 'x')
                {
                    uint8_t cBits = 0;
                    rc2 = RTStrToUInt8Ex(pszNext + 1, &pszNext, 10, &cBits);
                    if (rc2 == VINF_SUCCESS || rc2 == VWRN_TRAILING_CHARS)
                    {
                        /* Optional chunk: ,32x64,1  (we fail if this is partially there) */
                        uint32_t x        = 0;
                        uint32_t y        = 0;
                        uint8_t  fEnabled = 1;
                        if (rc2 == VINF_SUCCESS)
                            rc = VINF_SUCCESS;
                        else if (*pszNext == ',')
                        {
                            rc2 = RTStrToUInt32Ex(pszNext + 1, &pszNext, 10, &x);
                            if (rc2 == VWRN_TRAILING_CHARS && *pszNext == 'x')
                            {
                                rc2 = RTStrToUInt32Ex(pszNext + 1, &pszNext, 10, &y);
                                if (rc2 == VWRN_TRAILING_CHARS && *pszNext == ',')
                                {
                                    rc2 = RTStrToUInt8Ex(pszNext + 1, &pszNext, 10, &fEnabled);
                                    if (rc2 == VINF_SUCCESS)
                                        rc = VINF_SUCCESS;
                                }
                            }
                        }

                        /*
                         * Set result if successful.
                         */
                        if (rc == VINF_SUCCESS)
                        {
                            if (pcx)
                                *pcx = cx;
                            if (pcy)
                                *pcy = cy;
                            if (pcBits)
                                *pcBits = cBits;
                            if (px)
                                *px = x;
                            if (py)
                                *py = y;
                            if (pfEnabled)
                                *pfEnabled = RT_BOOL(fEnabled);
                        }
                    }
                }
            }
        }
    }

    return rc;
#else /* !VBOX_WITH_GUEST_PROPS */
    RT_NOREF(idScreen, pcx, pcy, pcBits, px, py, pfEnabled);
    return VERR_NOT_SUPPORTED;
#endif /* !VBOX_WITH_GUEST_PROPS */
}
