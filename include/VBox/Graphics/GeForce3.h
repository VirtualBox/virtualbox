/* $Id$ */
/** @file
 * GeForce3 - NVIDIA GeForce3 Ti 500 public interface definitions.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_Graphics_GeForce3_h
#define VBOX_INCLUDED_Graphics_GeForce3_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/** @defgroup grp_geforce3    NVIDIA GeForce3 Ti 500 Emulation
 * @{
 */

/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** GeForce3 vendor and device IDs */
#define GEFORCE3_PCI_VENDOR_ID      0x10DE
#define GEFORCE3_PCI_DEVICE_ID      0x0201

/** GeForce3 command types */
#define GEFORCE3_CMD_NOP            0x00000000  /**< No operation */
#define GEFORCE3_CMD_BITBLT         0x00000001  /**< Bit block transfer */
#define GEFORCE3_CMD_FILL_RECT      0x00000002  /**< Fill rectangle */
#define GEFORCE3_CMD_COPY_RECT      0x00000003  /**< Copy rectangle */
#define GEFORCE3_CMD_SET_MODE       0x00000004  /**< Set display mode */

/** GeForce3 pixel formats */
#define GEFORCE3_FORMAT_8BPP        0x01        /**< 8 bits per pixel */
#define GEFORCE3_FORMAT_16BPP       0x02        /**< 16 bits per pixel */
#define GEFORCE3_FORMAT_24BPP       0x03        /**< 24 bits per pixel */
#define GEFORCE3_FORMAT_32BPP       0x04        /**< 32 bits per pixel */

/** GeForce3 capabilities */
#define GEFORCE3_CAP_2D_ACCEL      RT_BIT(0)    /**< 2D acceleration */
#define GEFORCE3_CAP_3D_ACCEL      RT_BIT(1)    /**< 3D acceleration */
#define GEFORCE3_CAP_HW_CURSOR     RT_BIT(2)    /**< Hardware cursor */
#define GEFORCE3_CAP_MULTI_HEAD    RT_BIT(3)    /**< Multiple displays */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * GeForce3 BitBlt operation parameters.
 */
typedef struct GEFORCE3BITBLT
{
    uint32_t    uSrcX;          /**< Source X coordinate */
    uint32_t    uSrcY;          /**< Source Y coordinate */
    uint32_t    uDstX;          /**< Destination X coordinate */
    uint32_t    uDstY;          /**< Destination Y coordinate */
    uint32_t    uWidth;         /**< Width of operation */
    uint32_t    uHeight;        /**< Height of operation */
    uint32_t    uSrcPitch;      /**< Source pitch */
    uint32_t    uDstPitch;      /**< Destination pitch */
    uint32_t    uSrcAddr;       /**< Source address */
    uint32_t    uDstAddr;       /**< Destination address */
    uint32_t    uFormat;        /**< Pixel format */
} GEFORCE3BITBLT;
/** Pointer to BitBlt parameters. */
typedef GEFORCE3BITBLT *PGEFORCE3BITBLT;

/**
 * GeForce3 fill rectangle operation parameters.
 */
typedef struct GEFORCE3FILLRECT
{
    uint32_t    uX;             /**< X coordinate */
    uint32_t    uY;             /**< Y coordinate */
    uint32_t    uWidth;         /**< Width */
    uint32_t    uHeight;        /**< Height */
    uint32_t    uColor;         /**< Fill color */
    uint32_t    uPitch;         /**< Pitch */
    uint32_t    uAddr;          /**< Destination address */
    uint32_t    uFormat;        /**< Pixel format */
} GEFORCE3FILLRECT;
/** Pointer to fill rectangle parameters. */
typedef GEFORCE3FILLRECT *PGEFORCE3FILLRECT;

/**
 * GeForce3 device information.
 */
typedef struct GEFORCE3INFO
{
    uint32_t    uVendorId;      /**< PCI vendor ID */
    uint32_t    uDeviceId;      /**< PCI device ID */
    uint32_t    uRevision;      /**< Device revision */
    uint32_t    uVramSize;      /**< VRAM size in bytes */
    uint32_t    uCapabilities;  /**< Device capabilities */
    char        szDeviceName[64]; /**< Device name string */
} GEFORCE3INFO;
/** Pointer to device information. */
typedef GEFORCE3INFO *PGEFORCE3INFO;

/** @} */

#endif /* !VBOX_INCLUDED_Graphics_GeForce3_h */