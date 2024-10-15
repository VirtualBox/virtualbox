/* $Id: display-svga-xf86cvt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Header for display-svga-xf86ctv.cpp.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_display_svga_xf86cvt_h
#define GA_INCLUDED_SRC_x11_VBoxClient_display_svga_xf86cvt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


typedef struct DisplayModeR
{
    int     Clock;
    int     HDisplay;
    int     HSyncStart;
    int     HSyncEnd;
    int     HTotal;
    int     HSkew;
    int     VDisplay;
    int     VSyncStart;
    int     VSyncEnd;
    int     VTotal;
    int     VScan;
    float   HSync;
    float   VRefresh;
} DisplayModeR;

DisplayModeR VBoxClient_xf86CVTMode(int HDisplay, int VDisplay, float VRefresh /* Herz */, bool Reduced, bool Interlaced);


#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_display_svga_xf86cvt_h */

