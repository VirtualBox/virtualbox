/* $Id: svn2git-internal.h 168986 2025-05-26 08:33:00Z alexander.eichner@oracle.com $ */
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOX_INCLUDED_SRC_svn2git_vbox_svn2git_internal_h
#define VBOX_INCLUDED_SRC_svn2git_vbox_svn2git_internal_h
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
DECLHIDDEN(int) s2gGitTransactionFileAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, bool fIsExec, uint64_t cbFile);
DECLHIDDEN(int) s2gGitTransactionFileWriteData(S2GREPOSITORYGIT hGitRepo, const void *pvBuf, size_t cb);
DECLHIDDEN(int) s2gGitTransactionFileRemove(S2GREPOSITORYGIT hGitRepo, const char *pszPath);
DECLHIDDEN(int) s2gGitTransactionSubmoduleAdd(S2GREPOSITORYGIT hGitRepo, const char *pszPath, const char *pszSha1CommitId);


RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_svn2git_vbox_svn2git_internal_h */

