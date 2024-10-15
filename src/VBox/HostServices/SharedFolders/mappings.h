/* $Id: mappings.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared folders service - Mappings header.
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

#ifndef VBOX_INCLUDED_SRC_SharedFolders_mappings_h
#define VBOX_INCLUDED_SRC_SharedFolders_mappings_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "shfl.h"
#include <VBox/shflsvc.h>

typedef struct
{
    char       *pszFolderName;          /**< Directory at the host to share with the guest. */
    PSHFLSTRING pMapName;               /**< Share name for the guest. */
    uint32_t    cMappings;              /**< Number of mappings. */
    bool        fValid;                 /**< Mapping entry is used/valid. */
    bool        fHostCaseSensitive;     /**< Host file name space is case-sensitive. */
    bool        fGuestCaseSensitive;    /**< Guest file name space is case-sensitive. */
    bool        fWritable;              /**< Folder is writable for the guest. */
    PSHFLSTRING pAutoMountPoint;        /**< Where the guest should try auto-mount the folder. */
    bool        fAutoMount;             /**< Folder will be auto-mounted by the guest. */
    bool        fSymlinksCreate;        /**< Guest is able to create symlinks. */
    bool        fMissing;               /**< Mapping not invalid but host path does not exist.
                                             Any guest operation on such a folder fails! */
    bool        fPlaceholder;           /**< Mapping does not exist in the VM settings but the guest
                                             still has. fMissing is always true for this mapping. */
    bool        fLoadedRootId;          /**< Set if vbsfMappingLoaded has found this mapping already. */
    SymlinkPolicy_T enmSymlinkPolicy;   /**< Symbolic link creation policy within the guest. */
} MAPPING;
/** Pointer to a MAPPING structure. */
typedef MAPPING *PMAPPING;

void vbsfMappingInit(void);

bool vbsfMappingQuery(uint32_t iMapping, PMAPPING *pMapping);

int vbsfMappingsAdd(const char *pszFolderName, PSHFLSTRING pMapName, bool fWritable,
                    bool fAutoMount, PSHFLSTRING pAutoMountPoint, bool fCreateSymlinks, bool fMissing, bool fPlaceholder,
                    SymlinkPolicy_T enmSymlinkPolicy);
int vbsfMappingsRemove(PSHFLSTRING pMapName);

int vbsfMappingsQuery(PSHFLCLIENTDATA pClient, bool fOnlyAutoMounts, PSHFLMAPPING pMappings, uint32_t *pcMappings);
int vbsfMappingsQueryName(PSHFLCLIENTDATA pClient, SHFLROOT root, SHFLSTRING *pString);
int vbsfMappingsQueryWritable(PSHFLCLIENTDATA pClient, SHFLROOT root, bool *fWritable);
int vbsfMappingsQueryAutoMount(PSHFLCLIENTDATA pClient, SHFLROOT root, bool *fAutoMount);
int vbsfMappingsQuerySymlinksCreate(PSHFLCLIENTDATA pClient, SHFLROOT root, bool *fSymlinksCreate);
int vbsfMappingsQuerySymlinkPolicy(PSHFLCLIENTDATA pClient, SHFLROOT root, SymlinkPolicy_T *enmSymlinkPolicy);
int vbsfMappingsQueryInfo(PSHFLCLIENTDATA pClient, SHFLROOT root, PSHFLSTRING pNameBuf, PSHFLSTRING pMntPtBuf,
                          uint64_t *pfFlags, uint32_t *puVersion);

int vbsfMapFolder(PSHFLCLIENTDATA pClient, PSHFLSTRING pszMapName, RTUTF16 delimiter,
                  bool fCaseSensitive, SHFLROOT *pRoot);
int vbsfUnmapFolder(PSHFLCLIENTDATA pClient, SHFLROOT root);

int vbsfMappingsWaitForChanges(PSHFLCLIENTDATA pClient, VBOXHGCMCALLHANDLE hCall, PVBOXHGCMSVCPARM pParm, bool fRestored);
int vbsfMappingsCancelChangesWaits(PSHFLCLIENTDATA pClient);

const char* vbsfMappingsQueryHostRoot(SHFLROOT root);
int vbsfMappingsQueryHostRootEx(SHFLROOT hRoot, const char **ppszRoot, uint32_t *pcbRootLen);
bool vbsfIsGuestMappingCaseSensitive(SHFLROOT root);
bool vbsfIsHostMappingCaseSensitive(SHFLROOT root);

void vbsfMappingLoadingStart(void);
int  vbsfMappingLoaded(MAPPING const *pLoadedMapping, SHFLROOT root);
void vbsfMappingLoadingDone(void);
PMAPPING vbsfMappingGetByRoot(SHFLROOT root);

#endif /* !VBOX_INCLUDED_SRC_SharedFolders_mappings_h */

