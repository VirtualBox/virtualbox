/* $Id: Helper.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxFB - Helper routines.
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

#include "VBoxFB.h"
#include "Helper.h"

/**
 * Globals
 */
videoMode g_videoModes[MAX_VIDEOMODES] = {{0}};
uint32_t g_numVideoModes = 0;

/**
 * callback handler for populating the supported video modes
 *
 * @returns callback success indicator
 * @param   width        width in pixels of the current video mode
 * @param   height       height in pixels of the current video mode
 * @param   bpp          bits per pixel of the current video mode
 * @param   callbackdata user data pointer
 */
DFBEnumerationResult enumVideoModesHandler(int width, int height, int bpp, void *callbackdata)
{
    RT_NOREF(callbackdata);
    if (g_numVideoModes >= MAX_VIDEOMODES)
    {
        return DFENUM_CANCEL;
    }
    // don't take palette based modes
    if (bpp >= 16)
    {
        // don't take modes we already have (I have seen many cases where
        // DirectFB returns the same modes several times)
        int32_t existingMode = getBestVideoMode(width, height, bpp);
        if (   existingMode == -1
            || g_videoModes[existingMode].width  != (uint32_t)width
            || g_videoModes[existingMode].height != (uint32_t)height
            || g_videoModes[existingMode].bpp    != (uint32_t)bpp)
        {
            g_videoModes[g_numVideoModes].width  = (uint32_t)width;
            g_videoModes[g_numVideoModes].height = (uint32_t)height;
            g_videoModes[g_numVideoModes].bpp    = (uint32_t)bpp;
            g_numVideoModes++;
        }
    }
    return DFENUM_OK;
}

/**
 * Returns the best fitting video mode for the given characteristics.
 *
 * @returns index of the best video mode, -1 if no suitable mode found
 * @param   width  requested width
 * @param   height requested height
 * @param   bpp    requested bit depth
 */
int32_t getBestVideoMode(uint32_t width, uint32_t height, uint32_t bpp)
{
    int32_t bestMode = -1;

    for (uint32_t i = 0; i < g_numVideoModes; i++)
    {
        // is this mode compatible?
        if (g_videoModes[i].width >= width && g_videoModes[i].height >= height && g_videoModes[i].bpp >= bpp)
        {
            // first suitable mode?
            if (bestMode == -1)
                bestMode = i;
            else
            {
                // is it better than the one we got before?
                if (   g_videoModes[i].width  < g_videoModes[bestMode].width
                    || g_videoModes[i].height < g_videoModes[bestMode].height
                    || g_videoModes[i].bpp    < g_videoModes[bestMode].bpp)
                {
                    bestMode = i;
                }
            }
        }
    }
    return bestMode;
}
