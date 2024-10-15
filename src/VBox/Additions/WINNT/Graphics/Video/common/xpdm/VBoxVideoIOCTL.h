/* $Id: VBoxVideoIOCTL.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Miniport IOCTL related header
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_common_xpdm_VBoxVideoIOCTL_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_common_xpdm_VBoxVideoIOCTL_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/VMMDev.h> /* VBVAMEMORY */
#include <HGSMI.h>

/* ==================== VirtualBox specific VRP's ==================== */

/* Called by the display driver when it is ready to
 * switch to VBVA operation mode.
 * Successful return means that VBVA can be used and
 * output buffer contains VBVAENABLERESULT data.
 * An error means that VBVA can not be used
 * (disabled or not supported by the host).
 */
#define IOCTL_VIDEO_VBVA_ENABLE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x400, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Called to get video port api function pointers */
#define IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x434, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Called to get HGSMI related callbacks */
#define IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x431, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Called to get adapter's HGSMI information */
#define IOCTL_VIDEO_QUERY_HGSMI_INFO \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x430, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Called to enable HGSMI miniport channel */
#define IOCTL_VIDEO_HGSMI_HANDLER_ENABLE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x432, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Called to disable HGSMI miniport channel */
#define IOCTL_VIDEO_HGSMI_HANDLER_DISABLE \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x433, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Called to get framebuffer offset */
#define IOCTL_VIDEO_VHWA_QUERY_INFO \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x435, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Called to get adapter's generic information */
#define IOCTL_VIDEO_QUERY_VBOXVIDEO_INFO \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x436, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ==================== Data structures used by VirtualBox VRPS's ==================== */
typedef void* HVBOXVIDEOHGSMI;

/** Complete host commands addressed to the display */
typedef DECLCALLBACKTYPE(void, FNVBOXVIDEOHGSMICOMPLETION,(HVBOXVIDEOHGSMI hHGSMI,
                                                           struct VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST * pCmd));
typedef FNVBOXVIDEOHGSMICOMPLETION *PFNVBOXVIDEOHGSMICOMPLETION;

/** request the host commands addressed to the display */
typedef DECLCALLBACKTYPE(int, FNVBOXVIDEOHGSMICOMMANDS,(HVBOXVIDEOHGSMI hHGSMI, uint8_t u8Channel, uint32_t iDevice,
                                                        struct VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HOST ** ppCmd));
typedef FNVBOXVIDEOHGSMICOMMANDS *PFNVBOXVIDEOHGSMICOMMANDS;

/** post guest command (offset) to the host */
typedef DECLCALLBACKTYPE(void, FNVBOXVIDEOHGSMIPOSTCOMMAND,(HVBOXVIDEOHGSMI hHGSMI, HGSMIOFFSET offCmd));
typedef FNVBOXVIDEOHGSMIPOSTCOMMAND *PFNVBOXVIDEOHGSMIPOSTCOMMAND;

#pragma pack(1)
/* Data returned by IOCTL_VIDEO_VBVA_ENABLE. */
typedef struct _VBVAENABLERESULT
{
    /* Pointer to VBVAMemory part of VMMDev memory region. */
    VBVAMEMORY *pVbvaMemory;

    /* Called to force the host to process VBVA memory,
     * when there is no more free space in VBVA memory.
     * Normally this never happens.
     *
     * The other purpose is to perform a synchronous command.
     * But the goal is to have no such commands at all.
     */
    DECLR0CALLBACKMEMBER(void, pfnFlush, (void *pvFlush));

    /* Pointer required by the pfnFlush callback. */
    void *pvFlush;

} VBVAENABLERESULT;

/* Data returned by IOCTL_VIDEO_HGSMI_QUERY_PORTPROCS. */
typedef struct _HGSMIQUERYCPORTPROCS
{
    PVOID pContext;
    VBOXVIDEOPORTPROCS VideoPortProcs;
} HGSMIQUERYCPORTPROCS;

/** Data returned by IOCTL_VIDEO_HGSMI_QUERY_CALLBACKS. */
typedef struct _HGSMIQUERYCALLBACKS
{
    HVBOXVIDEOHGSMI hContext;
    PFNVBOXVIDEOHGSMICOMPLETION pfnCompletionHandler;
    PFNVBOXVIDEOHGSMICOMMANDS   pfnRequestCommandsHandler;
} HGSMIQUERYCALLBACKS;

/* Data returned by IOCTL_VIDEO_QUERY_HGSMI_INFO. */
typedef struct _QUERYHGSMIRESULT
{
    /* Device index (0 for primary) */
    ULONG iDevice;

    /* Flags. Currently none are defined and the field must be initialized to 0. */
    ULONG ulFlags;

    /* Describes VRAM chunk for this display device. */
    HGSMIAREA areaDisplay;

    /* Size of the display information area. */
    uint32_t u32DisplayInfoSize;

    /* Minimum size of the VBVA buffer. */
    uint32_t u32MinVBVABufferSize;

    /* IO port to submit guest HGSMI commands. */
    RTIOPORT IOPortGuestCommand;
} QUERYHGSMIRESULT;

/* Data passed to IOCTL_VIDEO_HGSMI_HANDLER_ENABLE. */
typedef struct _HGSMIHANDLERENABLE
{
    uint8_t u8Channel;
} HGSMIHANDLERENABLE;

#ifdef VBOX_WITH_VIDEOHWACCEL
/* Data returned by IOCTL_VIDEO_VHWA_QUERY_INFO. */
typedef struct _VHWAQUERYINFO
{
    ULONG_PTR offVramBase;
} VHWAQUERYINFO;
#endif
#pragma pack()

/* IOCTL_VIDEO_QUERY_INFO */
#define VBOXVIDEO_INFO_LEVEL_REGISTRY_FLAGS 1

#define VBOXVIDEO_REGISTRY_FLAGS_DISABLE_BITMAP_CACHE 0x00000001

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_common_xpdm_VBoxVideoIOCTL_h */
