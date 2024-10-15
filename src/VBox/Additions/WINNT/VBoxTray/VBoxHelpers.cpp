/* $Id: VBoxHelpers.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * helpers - Guest Additions Service helper functions
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

#include <iprt/win/windows.h>

#include <iprt/string.h>
#include <iprt/alloca.h>
#include <iprt/system.h>
#include <iprt/utf16.h>
#include <VBox/Log.h>
#include <VBox/VBoxGuestLib.h>

#include "VBoxHelpers.h"


int hlpReportStatus(VBoxGuestFacilityStatus statusCurrent)
{
    int rc = VbglR3ReportAdditionsStatus(VBoxGuestFacilityType_VBoxTrayClient,
                                         statusCurrent,
                                         0 /* Flags */);
    if (RT_FAILURE(rc))
        Log(("VBoxTray: Could not report VBoxTray status \"%ld\", rc=%Rrc\n", statusCurrent, rc));
    return rc;
}

/**
 * Attempt to force Windows to reload the cursor image by attaching to the
 * thread of the window currently under the mouse, hiding the cursor and
 * showing it again.  This could fail to work in any number of ways (no
 * window under the cursor, the cursor has moved to a different window while
 * we are processing), but we just accept this, as the cursor will be reloaded
 * at some point anyway.
 */
void hlpReloadCursor(void)
{
    POINT mousePos;
    GetCursorPos(&mousePos);

    DWORD hThread = 0;          /* Shut up MSC */
    DWORD hCurrentThread = 0;   /* Ditto */
    HWND hWin = WindowFromPoint(mousePos);
    if (hWin)
    {
        hThread = GetWindowThreadProcessId(hWin, NULL);
        hCurrentThread = GetCurrentThreadId();
        if (hCurrentThread != hThread)
            AttachThreadInput(hCurrentThread, hThread, TRUE);
    }

    ShowCursor(false);
    ShowCursor(true);

    if (hWin && hCurrentThread != hThread)
        AttachThreadInput(hCurrentThread, hThread, FALSE);
}

static unsigned hlpNextAdjacentRectXP(RECTL *paRects, unsigned nRects, unsigned uRect)
{
    unsigned i;
    for (i = 0; i < nRects; i++)
    {
        if (paRects[uRect].right == paRects[i].left)
            return i;
    }
    return ~0U;
}

static unsigned hlpNextAdjacentRectXN(RECTL *paRects, unsigned nRects, unsigned uRect)
{
    unsigned i;
    for (i = 0; i < nRects; i++)
    {
        if (paRects[uRect].left == paRects[i].right)
            return i;
    }
    return ~0U;
}

static unsigned hlpNextAdjacentRectYP(RECTL *paRects, unsigned nRects, unsigned uRect)
{
    unsigned i;
    for (i = 0; i < nRects; i++)
    {
        if (paRects[uRect].bottom == paRects[i].top)
            return i;
    }
    return ~0U;
}

static unsigned hlpNextAdjacentRectYN(RECTL *paRects, unsigned nRects, unsigned uRect)
{
    unsigned i;
    for (i = 0; i < nRects; i++)
    {
        if (paRects[uRect].top == paRects[i].bottom)
            return i;
    }
    return ~0U;
}

void hlpResizeRect(RECTL *paRects, unsigned nRects, unsigned uPrimary,
                   unsigned uResized, int iNewWidth, int iNewHeight,
                   int iNewPosX, int iNewPosY)
{
    Log4Func(("nRects %d, iPrimary %d, iResized %d, NewWidth %d, NewHeight %d\n", nRects, uPrimary, uResized, iNewWidth, iNewHeight));

    RECTL *paNewRects = (RECTL *)alloca (sizeof (RECTL) * nRects);
    memcpy (paNewRects, paRects, sizeof (RECTL) * nRects);
    paNewRects[uResized].right += iNewWidth - (paNewRects[uResized].right - paNewRects[uResized].left);
    paNewRects[uResized].bottom += iNewHeight - (paNewRects[uResized].bottom - paNewRects[uResized].top);
    paNewRects[uResized].right += iNewPosX - paNewRects[uResized].left;
    paNewRects[uResized].bottom += iNewPosY - paNewRects[uResized].top;
    paNewRects[uResized].left = iNewPosX;
    paNewRects[uResized].top = iNewPosY;

    /* Verify all pairs of originally adjacent rectangles for all 4 directions.
     * If the pair has a "good" delta (that is the first rectangle intersects the second)
     * at a direction and the second rectangle is not primary one (which can not be moved),
     * move the second rectangle to make it adjacent to the first one.
     */

    /* X positive. */
    unsigned iRect;
    for (iRect = 0; iRect < nRects; iRect++)
    {
        /* Find the next adjacent original rect in x positive direction. */
        unsigned iNextRect = hlpNextAdjacentRectXP(paRects, nRects, iRect);
        Log4Func(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == uPrimary)
        {
            continue;
        }

        /* Check whether there is an X intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].right - paNewRects[iNextRect].left;

        if (delta != 0)
        {
            Log4Func(("XP intersection right %d left %d, diff %d\n",
                      paNewRects[iRect].right, paNewRects[iNextRect].left,
                      delta));

            paNewRects[iNextRect].left += delta;
            paNewRects[iNextRect].right += delta;
        }
    }

    /* X negative. */
    for (iRect = 0; iRect < nRects; iRect++)
    {
        /* Find the next adjacent original rect in x negative direction. */
        unsigned iNextRect = hlpNextAdjacentRectXN(paRects, nRects, iRect);
        Log4Func(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == uPrimary)
        {
            continue;
        }

        /* Check whether there is an X intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].left - paNewRects[iNextRect].right;

        if (delta != 0)
        {
            Log4Func(("XN intersection left %d right %d, diff %d\n",
                      paNewRects[iRect].left, paNewRects[iNextRect].right,
                      delta));

            paNewRects[iNextRect].left += delta;
            paNewRects[iNextRect].right += delta;
        }
    }

    /* Y positive (in the computer sense, top->down). */
    for (iRect = 0; iRect < nRects; iRect++)
    {
        /* Find the next adjacent original rect in y positive direction. */
        unsigned iNextRect = hlpNextAdjacentRectYP(paRects, nRects, iRect);
        Log4Func(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == uPrimary)
        {
            continue;
        }

        /* Check whether there is an Y intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].bottom - paNewRects[iNextRect].top;

        if (delta != 0)
        {
            Log4Func(("YP intersection bottom %d top %d, diff %d\n",
                      paNewRects[iRect].bottom, paNewRects[iNextRect].top,
                      delta));

            paNewRects[iNextRect].top += delta;
            paNewRects[iNextRect].bottom += delta;
        }
    }

    /* Y negative (in the computer sense, down->top). */
    for (iRect = 0; iRect < nRects; iRect++)
    {
        /* Find the next adjacent original rect in x negative direction. */
        unsigned iNextRect = hlpNextAdjacentRectYN(paRects, nRects, iRect);
        Log4Func(("next %d -> %d\n", iRect, iNextRect));

        if (iNextRect == ~0 || iNextRect == uPrimary)
        {
            continue;
        }

        /* Check whether there is an Y intersection between these adjacent rects in the new rectangles
         * and fix the intersection if delta is "good".
         */
        int delta = paNewRects[iRect].top - paNewRects[iNextRect].bottom;

        if (delta != 0)
        {
            Log4Func(("YN intersection top %d bottom %d, diff %d\n",
                      paNewRects[iRect].top, paNewRects[iNextRect].bottom,
                      delta));

            paNewRects[iNextRect].top += delta;
            paNewRects[iNextRect].bottom += delta;
        }
    }

    /* Primary rectangle must remain at 0,0. */
    int32_t iOffsetX = paNewRects[uPrimary].left;
    int32_t iOffsetY = paNewRects[uPrimary].top;
    for (iRect = 0; iRect < nRects; iRect++)
    {
        paRects[iRect].left   = paNewRects[iRect].left   - iOffsetX;
        paRects[iRect].right  = paNewRects[iRect].right  - iOffsetX;
        paRects[iRect].top    = paNewRects[iRect].top    - iOffsetY;
        paRects[iRect].bottom = paNewRects[iRect].bottom - iOffsetY;
        Log4Func((" [%d]: %d,%d %dx%d -> %d,%d %dx%d%s\n",
                  iRect,
                  paRects[iRect].left, paRects[iRect].top,
                  paRects[iRect].right - paRects[iRect].left,
                  paRects[iRect].bottom - paRects[iRect].top,
                  paNewRects[iRect].left, paNewRects[iRect].top,
                  paNewRects[iRect].right - paNewRects[iRect].left,
                  paNewRects[iRect].bottom - paNewRects[iRect].top,
                  iRect == uPrimary? " <- primary": ""));
    }
    return;
}

int hlpShowBalloonTip(HINSTANCE hInst, HWND hWnd, UINT uID,
                      const char *pszMsg, const char *pszTitle,
                      UINT uTimeout, DWORD dwInfoFlags)
{
    NOTIFYICONDATA niData;
    ZeroMemory(&niData, sizeof(NOTIFYICONDATA));
    niData.cbSize = sizeof(NOTIFYICONDATA);
    niData.uFlags = NIF_INFO; /* Display a balloon notification. */
    niData.hWnd = hWnd;
    niData.uID = uID;
    /* If not timeout set, set it to 5sec. */
    if (uTimeout == 0)
        uTimeout = 5000;
    niData.uTimeout = uTimeout;
    /* If no info flag (info, warning, error) set,
     * set it to info by default. */
    if (dwInfoFlags == 0)
        dwInfoFlags = NIIF_INFO;
    niData.dwInfoFlags = dwInfoFlags;

    /* Do we want to have */

    /* Is the current OS supported (at least WinXP) for displaying
     * our own icon and do we actually *want* to display our own stuff? */
    uint64_t const uNtVersion = RTSystemGetNtVersion();
    if (    uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(5, 0, 0)
        && (dwInfoFlags & NIIF_INFO))
    {
        /* Load (or retrieve handle of) the app's icon. */
        HICON hIcon = LoadIcon(hInst, "IDI_ICON1"); /* see Artwork/win/TemplateR3.rc */
        if (hIcon)
            niData.dwInfoFlags = NIIF_USER; /* Use an own notification icon. */

        if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(5, 1, 0)) /* WinXP. */
        {
            /* Use an own icon instead of the default one. */
            niData.hIcon = hIcon;
        }
        else if (uNtVersion >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0)) /* Vista and up. */
        {
            /* Use an own icon instead of the default one. */
            niData.dwInfoFlags |= NIIF_LARGE_ICON; /* Use a  large icon if available! */
            niData.hIcon        = hIcon;
            niData.hBalloonIcon = hIcon;
        }
    }
    else
    {
        /* This might be a warning, error message or a to old OS. Use the
         * standard icons provided by Windows (if any). */
    }

    strcpy(niData.szInfo, pszMsg ? pszMsg : "-");
    strcpy(niData.szInfoTitle, pszTitle ? pszTitle : "Information");

    if (!Shell_NotifyIcon(NIM_MODIFY, &niData))
    {
        DWORD dwErr = GetLastError();
        return RTErrConvertFromWin32(dwErr);
    }
    return VINF_SUCCESS;
}

/**
 * Shows a message box with a printf() style formatted string.
 *
 * @param   pszTitle            Title of the message box.
 * @param   uStyle              Style of message box to use (see MSDN, MB_ defines).
 *                              When 0 is specified, MB_ICONINFORMATION will be used.
 * @param   pszFmt              Printf-style format string to show in the message box body.
 * @param   ...                 Arguments for format string.
 */
void hlpShowMessageBox(const char *pszTitle, UINT uStyle, const char *pszFmt, ...)
{
    if (!uStyle)
        uStyle = MB_ICONINFORMATION;

    char       *pszMsg;
    va_list     va;
    va_start(va, pszFmt);
    int rc = RTStrAPrintfV(&pszMsg, pszFmt, va);
    va_end(va);
    if (rc >= 0)
    {
        PRTUTF16 pwszTitle;
        rc = RTStrToUtf16(pszTitle, &pwszTitle);
        if (RT_SUCCESS(rc))
        {
            PRTUTF16 pwszMsg;
            rc = RTStrToUtf16(pszMsg, &pwszMsg);
            if (RT_SUCCESS(rc))
            {
                MessageBoxW(GetDesktopWindow(), pwszMsg, pwszTitle, uStyle);
                RTUtf16Free(pwszMsg);
            }
            else
                MessageBoxA(GetDesktopWindow(), pszMsg, pszTitle, uStyle);
            RTUtf16Free(pwszTitle);
        }
    }
    else /* Should never happen! */
        AssertMsgFailed(("Failed to format error text of format string: %s!\n", pszFmt));
    RTStrFree(pszMsg);
}

