/* $Id: DBGCScreenAscii.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, ASCII screen with optional coloring support.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGC
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/mem.h>
#include <iprt/string.h>

#include "DBGCInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Debug console ASCII screen.
 */
typedef struct DBGCSCREENINT
{
    /** Width of the screen. */
    uint32_t                cchWidth;
    /** Height of the screen. */
    uint32_t                cchHeight;
    /** Extra amount of characters at the end of each line (usually terminator). */
    uint32_t                cchStride;
    /** Pointer to the char buffer. */
    char                   *pszScreen;
    /** Color information for each pixel. */
    PDBGCSCREENCOLOR        paColors;
} DBGCSCREENINT;
/** Pointer to an ASCII screen. */
typedef DBGCSCREENINT *PDBGCSCREENINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Returns the buffer starting at the given position.
 *
 * @returns Pointer to the ASCII buffer.
 * @param   pThis               The screen.
 * @param   uX                  Horizontal position.
 * @param   uY                  Vertical position.
 */
DECLINLINE(char *) dbgcScreenAsciiGetBufferAtPos(PDBGCSCREENINT pThis, uint32_t uX, uint32_t uY)
{
    AssertReturn(uX < pThis->cchWidth && uY < pThis->cchHeight, NULL);
    return pThis->pszScreen + (pThis->cchWidth + pThis->cchStride) * uY + uX;
}


/**
 * Returns the color buffer starting at the given position.
 *
 * @returns Pointer to the color buffer.
 * @param   pThis               The screen.
 * @param   uX                  Horizontal position.
 * @param   uY                  Vertical position.
 */
DECLINLINE(PDBGCSCREENCOLOR) dbgcScreenAsciiGetColorBufferAtPos(PDBGCSCREENINT pThis, uint32_t uX, uint32_t uY)
{
    AssertReturn(uX < pThis->cchWidth && uY < pThis->cchHeight, NULL);
    return &pThis->paColors[pThis->cchWidth  * uY + uX];
}


/**
 * Converts the given color the correct escape sequence.
 *
 * @returns Pointer to the string containing the escape sequence for the given color.
 * @param   enmColor            The color.
 */
static const char *dbgcScreenAsciiColorToEscapeSeq(DBGCSCREENCOLOR enmColor)
{
    const char *psz = NULL;

    switch (enmColor)
    {
        case DBGCSCREENCOLOR_DEFAULT:
            psz = "\033[0m";
            break;
        case DBGCSCREENCOLOR_BLACK:
            psz = "\033[30m";
            break;
        case DBGCSCREENCOLOR_BLACK_BRIGHT:
            psz = "\033[30;1m";
            break;
        case DBGCSCREENCOLOR_RED:
            psz = "\033[31m";
            break;
        case DBGCSCREENCOLOR_RED_BRIGHT:
            psz = "\033[31;1m";
            break;
        case DBGCSCREENCOLOR_GREEN:
            psz = "\033[32m";
            break;
        case DBGCSCREENCOLOR_GREEN_BRIGHT:
            psz = "\033[32;1m";
            break;
        case DBGCSCREENCOLOR_YELLOW:
            psz = "\033[33m";
            break;
        case DBGCSCREENCOLOR_YELLOW_BRIGHT:
            psz = "\033[33;1m";
            break;
        case DBGCSCREENCOLOR_BLUE:
            psz = "\033[34m";
            break;
        case DBGCSCREENCOLOR_BLUE_BRIGHT:
            psz = "\033[34;1m";
            break;
        case DBGCSCREENCOLOR_MAGENTA:
            psz = "\033[35m";
            break;
        case DBGCSCREENCOLOR_MAGENTA_BRIGHT:
            psz = "\033[35;1m";
            break;
        case DBGCSCREENCOLOR_CYAN:
            psz = "\033[36m";
            break;
        case DBGCSCREENCOLOR_CYAN_BRIGHT:
            psz = "\033[36;1m";
            break;
        case DBGCSCREENCOLOR_WHITE:
            psz = "\033[37m";
            break;
        case DBGCSCREENCOLOR_WHITE_BRIGHT:
            psz = "\033[37;1m";
            break;
        default:
            AssertFailed();
    }

    return psz;
}


/**
 * Creates a new ASCII screen for layouting.
 *
 * @returns VBox status code.
 * @param   phScreen            Where to store the handle to the screen instance on success.
 * @param   cchWidth            Width of the screen in characters.
 * @param   cchHeight           Height of the screen in characters.
 */
DECLHIDDEN(int) dbgcScreenAsciiCreate(PDBGCSCREEN phScreen, uint32_t cchWidth, uint32_t cchHeight)
{
    int rc = VINF_SUCCESS;

    PDBGCSCREENINT pThis = (PDBGCSCREENINT)RTMemAllocZ(sizeof(DBGCSCREENINT));
    if (pThis)
    {
        pThis->cchWidth  = cchWidth;
        pThis->cchHeight = cchHeight;
        pThis->cchStride = 1; /* Zero terminators after every line. */
        pThis->pszScreen = RTStrAlloc((cchWidth + 1) * cchHeight * sizeof(char));
        if (RT_LIKELY(pThis->pszScreen))
        {
            pThis->paColors  = (PDBGCSCREENCOLOR)RTMemAllocZ(cchWidth * cchHeight * sizeof(DBGCSCREENCOLOR));
            if (RT_LIKELY(pThis->paColors))
            {
                memset(pThis->pszScreen, 0, (cchWidth + 1) * cchHeight * sizeof(char));
                /* Initialize the screen with spaces. */
                for (uint32_t i = 0; i < cchHeight; i++)
                    dbgcScreenAsciiDrawLineHorizontal(pThis, 0, cchWidth - 1, i, ' ',
                                                      DBGCSCREENCOLOR_DEFAULT);
                *phScreen = pThis;
            }
            else
                rc = VERR_NO_MEMORY;

            if (RT_FAILURE(rc))
                RTStrFree(pThis->pszScreen);
        }
        else
            rc = VERR_NO_STR_MEMORY;

        if (RT_FAILURE(rc))
            RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Destroys a given ASCII screen.
 *
 * @param   hScreen             The screen handle.
 */
DECLHIDDEN(void) dbgcScreenAsciiDestroy(DBGCSCREEN hScreen)
{
    PDBGCSCREENINT pThis = hScreen;
    AssertPtrReturnVoid(pThis);

    RTStrFree(pThis->pszScreen);
    RTMemFree(pThis->paColors);
    RTMemFree(pThis);
}


/**
 * Blits the entire screen using the given callback callback.
 *
 * @returns VBox status code.
 * @param   hScreen             The screen to blit.
 * @param   pfnBlit             Blitting callback.
 * @param   pvUser              Opaque user data to pass to the dumper callback.
 * @param   fAddColors          Flag whether to use the color info inserting
 *                              appropriate escape sequences.
 */
DECLHIDDEN(int) dbgcScreenAsciiBlit(DBGCSCREEN hScreen, PFNDGCSCREENBLIT pfnBlit, void *pvUser, bool fAddColors)
{
    int rc = VINF_SUCCESS;
    PDBGCSCREENINT pThis = hScreen;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    if (!fAddColors)
    {
        for (uint32_t iY = 0; iY < pThis->cchHeight && RT_SUCCESS(rc); iY++)
        {
            /* Play safe and restore line endings. */
            char *psz = dbgcScreenAsciiGetBufferAtPos(pThis, 0, iY);
            psz[pThis->cchWidth] = '\0';
            rc = pfnBlit(psz, pvUser);
            if (RT_SUCCESS(rc))
                rc = pfnBlit("\n", pvUser);
        }
    }
    else
    {
        for (uint32_t iY = 0; iY < pThis->cchHeight && RT_SUCCESS(rc); iY++)
        {
            /* Play safe and restore line endings. */
            char *psz = dbgcScreenAsciiGetBufferAtPos(pThis, 0, iY);
            PDBGCSCREENCOLOR pColor = dbgcScreenAsciiGetColorBufferAtPos(pThis, 0, iY);
            psz[pThis->cchWidth] = '\0';

            /*
             * Blit only stuff with the same color at once so to be able to inject the
             * correct color escape sequences.
             */
            uint32_t uStartX = 0;
            while (   uStartX < pThis->cchWidth
                   && RT_SUCCESS(rc))
            {
                uint32_t cchWrite = 0;
                DBGCSCREENCOLOR enmColorStart = *pColor;
                while (   uStartX + cchWrite < pThis->cchWidth
                       && enmColorStart == *pColor)
                {
                    pColor++;
                    cchWrite++;
                }

                const char *pszEsc = dbgcScreenAsciiColorToEscapeSeq(enmColorStart);
                rc = pfnBlit(pszEsc, pvUser);
                if (RT_SUCCESS(rc))
                {
                    char chTmp = psz[cchWrite];
                    psz[cchWrite] = '\0';
                    rc = pfnBlit(psz, pvUser);
                    psz[cchWrite] = chTmp;
                    uStartX += cchWrite;
                    psz += cchWrite;
                }
            }
            rc = pfnBlit("\n", pvUser);
        }

        /* Restore to default values at the end. */
        if (RT_SUCCESS(rc))
        {
            const char *pszEsc = dbgcScreenAsciiColorToEscapeSeq(DBGCSCREENCOLOR_DEFAULT);
            rc = pfnBlit(pszEsc, pvUser);
        }
    }

    return rc;
}


/**
 * Draws a single character to the screen at the given coordinates.
 *
 * @returns VBox status code.
 * @param   hScreen             The screen handle.
 * @param   uX                  X coordinate.
 * @param   uY                  Y coordinate.
 * @param   ch                  Character to draw.
 * @param   enmColor            The color to use.
 */
DECLHIDDEN(int) dbgcScreenAsciiDrawCharacter(DBGCSCREEN hScreen, uint32_t uX, uint32_t uY, char ch,
                                             DBGCSCREENCOLOR enmColor)
{
    PDBGCSCREENINT pThis = hScreen;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    char *psz = dbgcScreenAsciiGetBufferAtPos(pThis, uX, uY);
    PDBGCSCREENCOLOR pColor = dbgcScreenAsciiGetColorBufferAtPos(pThis, uX, uY);
    AssertPtrReturn(psz, VERR_INVALID_STATE);
    AssertPtrReturn(pColor, VERR_INVALID_STATE);
    AssertReturn(*psz != '\0', VERR_INVALID_STATE);

    *psz = ch;
    *pColor = enmColor;
    return VINF_SUCCESS;
}


/**
 * Draws a vertical line at the given coordinates.
 *
 * @returns VBox status code.
 * @param   hScreen             The screen handle.
 * @param   uX                  X position to draw.
 * @param   uStartY             Y position to start drawing.
 * @param   uEndY               Y position to draw to (inclusive).
 * @param   ch                  The character to use for drawing.
 * @param   enmColor            The color to use.
 */
DECLHIDDEN(int) dbgcScreenAsciiDrawLineVertical(DBGCSCREEN hScreen, uint32_t uX, uint32_t uStartY,
                                                uint32_t uEndY, char ch, DBGCSCREENCOLOR enmColor)
{
    PDBGCSCREENINT pThis = hScreen;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    while (uStartY <= uEndY)
    {
        char *psz = dbgcScreenAsciiGetBufferAtPos(pThis, uX, uStartY);
        PDBGCSCREENCOLOR pColor = dbgcScreenAsciiGetColorBufferAtPos(pThis, uX, uStartY);
        AssertPtrReturn(psz, VERR_INVALID_STATE);
        AssertPtrReturn(pColor, VERR_INVALID_STATE);
        *psz = ch;
        *pColor = enmColor;
        uStartY++;
    }

    return VINF_SUCCESS;
}


/**
 * Draws a horizontal line at the given coordinates.
 *
 * @returns VBox status code..
 * @param   hScreen             The screen handle.
 * @param   uStartX             X position to start drawing.
 * @param   uEndX               X position to draw the line to (inclusive).
 * @param   uY                  Y position.
 * @param   ch                  The character to use for drawing.
 * @param   enmColor            The color to use.
 */
DECLHIDDEN(int) dbgcScreenAsciiDrawLineHorizontal(DBGCSCREEN hScreen, uint32_t uStartX, uint32_t uEndX,
                                                  uint32_t uY, char ch, DBGCSCREENCOLOR enmColor)
{
    PDBGCSCREENINT pThis = hScreen;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    char *psz = dbgcScreenAsciiGetBufferAtPos(pThis, uStartX, uY);
    PDBGCSCREENCOLOR pColor = dbgcScreenAsciiGetColorBufferAtPos(pThis, uStartX, uY);
    AssertPtrReturn(psz, VERR_INVALID_STATE);
    AssertPtrReturn(pColor, VERR_INVALID_STATE);

    memset(psz, ch, uEndX - uStartX + 1);
    for (unsigned i = 0; i < uEndX - uStartX + 1; i++)
        pColor[i] = enmColor;

    return VINF_SUCCESS;
}


/**
 * Draws a given string to the screen.
 *
 * @returns VBox status code..
 * @param   hScreen             The screen handle.
 * @param   uX                  X position to start drawing.
 * @param   uY                  Y position.
 * @param   pszText             The string to draw.
 * @param   enmColor            The color to use.
 */
DECLHIDDEN(int) dbgcScreenAsciiDrawString(DBGCSCREEN hScreen, uint32_t uX, uint32_t uY, const char *pszText,
                                          DBGCSCREENCOLOR enmColor)
{
    PDBGCSCREENINT pThis = hScreen;
    size_t cchText = strlen(pszText);
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(uX + cchText <= pThis->cchWidth, VERR_OUT_OF_RANGE);
    AssertReturn(uY < pThis->cchHeight, VERR_OUT_OF_RANGE);

    char *psz = dbgcScreenAsciiGetBufferAtPos(pThis, uX, uY);
    PDBGCSCREENCOLOR pColor = dbgcScreenAsciiGetColorBufferAtPos(pThis, uX, uY);
    AssertPtrReturn(psz, VERR_INVALID_STATE);
    AssertPtrReturn(pColor, VERR_INVALID_STATE);

    memcpy(psz, pszText, cchText);

    for (unsigned i = 0; i < cchText; i++)
        pColor[i] = enmColor;

    return VINF_SUCCESS;
}
