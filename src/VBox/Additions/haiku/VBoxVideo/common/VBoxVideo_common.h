/* $Id: VBoxVideo_common.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo, Haiku Guest Additions, common header.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_haiku_VBoxVideo_common_VBoxVideo_common_h
#define GA_INCLUDED_SRC_haiku_VBoxVideo_common_VBoxVideo_common_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <Drivers.h>
#include <Accelerant.h>
#include <PCI.h>

struct SharedInfo
{
    display_mode currentMode;
    area_id framebufferArea;
    void *framebuffer;
};

enum
{
    VBOXVIDEO_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
    VBOXVIDEO_GET_DEVICE_NAME,
    VBOXVIDEO_SET_DISPLAY_MODE
};

static inline uint32 get_color_space_for_depth(uint32 depth)
{
    switch (depth)
    {
        case 1:  return B_GRAY1;
        case 4:  return B_GRAY8;
        /* The app_server is smart enough to translate this to VGA mode */
        case 8:  return B_CMAP8;
        case 15: return B_RGB15;
        case 16: return B_RGB16;
        case 24: return B_RGB24;
        case 32: return B_RGB32;
    }

    return 0;
}

static inline uint32 get_depth_for_color_space(uint32 depth)
{
    switch (depth)
    {
        case B_GRAY1: return 1;
        case B_GRAY8: return 4;
        case B_CMAP8: return 8;
        case B_RGB15: return 15;
        case B_RGB16: return 16;
        case B_RGB24: return 24;
        case B_RGB32: return 32;
    }
    return 0;
}

#endif /* !GA_INCLUDED_SRC_haiku_VBoxVideo_common_VBoxVideo_common_h */

