/* $Id: vbsf.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Shared Folders header.
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

#ifndef VBOX_INCLUDED_SRC_SharedFolders_vbsf_h
#define VBOX_INCLUDED_SRC_SharedFolders_vbsf_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "shfl.h"
#include <VBox/shflsvc.h>

int vbsfCreate(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pPath, uint32_t cbPath, SHFLCREATEPARMS *pParms);

int vbsfClose(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle);

int vbsfRead(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint64_t offset, uint32_t *pcbBuffer, uint8_t *pBuffer);
int vbsfReadPages(SHFLCLIENTDATA *pClient, SHFLROOT idRoot, SHFLHANDLE hFile, uint64_t offFile,
                  uint32_t *pcbBuffer, PVBOXHGCMSVCPARMPAGES pPages);
int vbsfWrite(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint64_t *poffFile, uint32_t *pcbBuffer, uint8_t *pBuffer);
int vbsfWritePages(SHFLCLIENTDATA *pClient, SHFLROOT idRoot, SHFLHANDLE hFile, uint64_t *poffFile,
                   uint32_t *pcbBuffer, PVBOXHGCMSVCPARMPAGES pPages);
int vbsfCopyFilePart(SHFLCLIENTDATA *pClient, SHFLROOT idRootSrc, SHFLHANDLE hFileSrc, uint64_t offSrc,
                     SHFLROOT idRootDst, SHFLHANDLE hFileDst, uint64_t offDst, uint64_t *pcbToCopy, uint32_t fFlags);

int vbsfLock(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint64_t offset, uint64_t length, uint32_t flags);
int vbsfUnlock(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint64_t offset, uint64_t length, uint32_t flags);
int vbsfRemove(SHFLCLIENTDATA *pClient, SHFLROOT root, PCSHFLSTRING pPath, uint32_t cbPath, uint32_t flags, SHFLHANDLE hToClose);
int vbsfRename(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pSrc, SHFLSTRING *pDest, uint32_t flags);
int vbsfCopyFile(SHFLCLIENTDATA *pClient, SHFLROOT idRootSrc, PCSHFLSTRING pStrPathSrc,
                 SHFLROOT idRootDst, PCSHFLSTRING pStrPathDst, uint32_t fFlags);
int vbsfDirList(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, SHFLSTRING *pPath, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer, uint32_t *pIndex, uint32_t *pcFiles);
int vbsfFileInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer);
int vbsfSetFileSize(SHFLCLIENTDATA *pClient, SHFLROOT idRoot, SHFLHANDLE hHandle, uint64_t cbNewSize);
int vbsfQueryFSInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer);
int vbsfSetFSInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer);
int vbsfFlush(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle);
int vbsfDisconnect(SHFLCLIENTDATA *pClient);
int vbsfQueryFileInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer);
int vbsfReadLink(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pPath, uint32_t cbPath, uint8_t *pBuffer, uint32_t cbBuffer);
int vbsfSymlink(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pNewPath, SHFLSTRING *pOldPath, SHFLFSOBJINFO *pInfo);

#endif /* !VBOX_INCLUDED_SRC_SharedFolders_vbsf_h */

