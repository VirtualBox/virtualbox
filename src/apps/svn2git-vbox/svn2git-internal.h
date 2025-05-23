/* $Id: svn2git-internal.h 168978 2025-05-23 15:27:51Z alexander.eichner@oracle.com $ */
/** @file
 * IPRT - Internal svn2git header.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_INTERNAL_svn2git_internal_h
#define IPRT_INCLUDED_INTERNAL_svn2git_internal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN


/* git.cpp */
typedef struct S2GREPOSITORYGITINT *S2GREPOSITORYGIT;
typedef S2GREPOSITORYGIT *PS2GREPOSITORYGIT;

DECLHIDDEN(int) s2gGitRepositoryCreate(PS2GREPOSITORYGIT phGitRepo, const char *pszGitRepoPath, const char *pszDefaultBranch);
DECLHIDDEN(int) s2gGitRepositoryClose(S2GREPOSITORYGIT hGitRepo);

DECLHIDDEN(int) s2gGitTransactionStart(S2GREPOSITORYGIT hGitRepo);
DECLHIDDEN(int) s2gGitTransactionCommit(S2GREPOSITORYGIT hGitRepo, const char *pszAuthor, const char *pszAuthorEmail,
                                       const char *pszLog, int64_t cEpochSecs);
DECLHIDDEN(int) s2gGitTransactionFileAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, uint64_t cbFile);
DECLHIDDEN(int) s2gGitTransactionFileWriteData(S2GREPOSITORYGIT hGitRepo, const void *pvBuf, size_t cb);
DECLHIDDEN(int) s2gGitTransactionFileRemove(S2GREPOSITORYGIT hGitRepo, const char *pszPath);
DECLHIDDEN(int) s2gGitTransactionSubmoduleAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, const char *pszSha1CommitId);


RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_svn2git_internal_h */

