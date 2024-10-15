/* $Id: shflhandle.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Folders Host Service - Handles helper functions header.
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

#ifndef VBOX_INCLUDED_SRC_SharedFolders_shflhandle_h
#define VBOX_INCLUDED_SRC_SharedFolders_shflhandle_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "shfl.h"
#include <VBox/shflsvc.h>
#include <iprt/dir.h>

#define SHFL_HF_TYPE_MASK       (0x000000FF)
#define SHFL_HF_TYPE_DIR        (0x00000001)
#define SHFL_HF_TYPE_FILE       (0x00000002)
#define SHFL_HF_TYPE_VOLUME     (0x00000004)
#define SHFL_HF_TYPE_DONTUSE    (0x00000080)

#define SHFL_HF_VALID           (0x80000000)

#define SHFLHANDLE_MAX          (4096)

typedef struct _SHFLHANDLEHDR
{
    uint32_t u32Flags;
} SHFLHANDLEHDR;

#define ShflHandleType(__Handle) BIT_FLAG(((SHFLHANDLEHDR *)(__Handle))->u32Flags, SHFL_HF_TYPE_MASK)

typedef struct _SHFLFILEHANDLE
{
    SHFLHANDLEHDR Header;
    SHFLROOT root; /* Where the handle has been opened. */
    union
    {
        struct
        {
            RTFILE        Handle;
            uint64_t      fOpenFlags;       /**< RTFILE_O_XXX. */
        } file;
        struct
        {
            RTDIR         Handle;
            RTDIR         SearchHandle;
            PRTDIRENTRYEX pLastValidEntry;  /**< last found file in a directory search */
        } dir;
    };
} SHFLFILEHANDLE;


SHFLHANDLE      vbsfAllocDirHandle(PSHFLCLIENTDATA pClient);
SHFLHANDLE      vbsfAllocFileHandle(PSHFLCLIENTDATA pClient);
void            vbsfFreeFileHandle (PSHFLCLIENTDATA pClient, SHFLHANDLE hHandle);


int         vbsfInitHandleTable();
int         vbsfFreeHandleTable();
SHFLHANDLE  vbsfAllocHandle(PSHFLCLIENTDATA pClient, uint32_t uType,
                            uintptr_t pvUserData);
SHFLFILEHANDLE *vbsfQueryFileHandle(PSHFLCLIENTDATA pClient,
                                    SHFLHANDLE handle);
SHFLFILEHANDLE *vbsfQueryDirHandle(PSHFLCLIENTDATA pClient, SHFLHANDLE handle);
uint32_t        vbsfQueryHandleType(PSHFLCLIENTDATA pClient,
                                    SHFLHANDLE handle);

#endif /* !VBOX_INCLUDED_SRC_SharedFolders_shflhandle_h */
