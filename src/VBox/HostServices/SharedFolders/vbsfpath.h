/* $Id: vbsfpath.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Folders Service - Guest/host path convertion and verification.
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

#ifndef VBOX_INCLUDED_SRC_SharedFolders_vbsfpath_h
#define VBOX_INCLUDED_SRC_SharedFolders_vbsfpath_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "shfl.h"
#include <VBox/shflsvc.h>

#define VBSF_O_PATH_WILDCARD                UINT32_C(0x00000001)
#define VBSF_O_PATH_PRESERVE_LAST_COMPONENT UINT32_C(0x00000002)
#define VBSF_O_PATH_CHECK_ROOT_ESCAPE       UINT32_C(0x00000004)
#define VBSF_O_PATH_CHECK_SYMLINK_POLICY    UINT32_C(0x00000008)

#define VBSF_F_PATH_HAS_WILDCARD_IN_PREFIX UINT32_C(0x00000001) /* A component before the last one contains a wildcard. */
#define VBSF_F_PATH_HAS_WILDCARD_IN_LAST   UINT32_C(0x00000002) /* The last component contains a wildcard. */

/**
 *
 * @param pClient                Shared folder client.
 * @param hRoot                  Root handle.
 * @param pGuestString           Guest want to access the path.
 * @param cbGuestString          Size of pGuestString memory buffer.
 * @param ppszHostPath           Returned full host path: root prefix + guest path.
 * @param pcbHostPathRoot        Length of the root prefix in bytes. Optional, can be NULL.
 * @param fu32Options            Options.
 * @param pfu32PathFlags         VBSF_F_PATH_* flags. Optional, can be NULL.
 */
int vbsfPathGuestToHost(SHFLCLIENTDATA *pClient, SHFLROOT hRoot,
                        PCSHFLSTRING pGuestString, uint32_t cbGuestString,
                        char **ppszHostPath, uint32_t *pcbHostPathRoot,
                        uint32_t fu32Options, uint32_t *pfu32PathFlags);

/** Free the host path returned by vbsfPathGuestToHost.
 *
 * @param pszHostPath Host path string.
 */
void vbsfFreeHostPath(char *pszHostPath);

/**
 * Build the absolute path by combining an absolute pszRoot and a relative pszPath.
 * The resulting path does not contain '.' and '..' components.
 * Similar to RTPathAbsEx but with support for Windows extended-length paths ("\\?\" prefix).
 * Uses RTPathAbsEx for regular paths and on non-Windows hosts.
 *
 * @param pszRoot The absolute prefix. It is copied to the pszAbsPath without any processing.
 *                If NULL then the pszPath must be converted to the absolute path.
 * @param pszPath The relative path to be appended to pszRoot. Already has correct delimiters (RTPATH_SLASH).
 * @param pszAbsPath Where to store the resulting absolute path.
 * @param cbAbsPath Size of pszAbsBuffer in bytes.
 */
int vbsfPathAbs(const char *pszRoot, const char *pszPath, char *pszAbsPath, size_t cbAbsPath);

#endif /* !VBOX_INCLUDED_SRC_SharedFolders_vbsfpath_h */
