/* $Id: scmdiff.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase / Tool - Source Code Massager Diff Code.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_bldprogs_scmdiff_h
#define VBOX_INCLUDED_SRC_bldprogs_scmdiff_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/stream.h>
#include "scmstream.h"

RT_C_DECLS_BEGIN

/**
 * Diff state.
 */
typedef struct SCMDIFFSTATE
{
    size_t          cDiffs;
    const char     *pszFilename;

    PSCMSTREAM      pLeft;
    PSCMSTREAM      pRight;

    /** Whether to ignore end of line markers when diffing. */
    bool            fIgnoreEol;
    /** Whether to ignore trailing whitespace. */
    bool            fIgnoreTrailingWhite;
    /** Whether to ignore leading whitespace. */
    bool            fIgnoreLeadingWhite;
    /** Whether to print special characters in human readable form or not. */
    bool            fSpecialChars;
    /** The tab size. */
    size_t          cchTab;
    /** Where to push the diff. */
    PRTSTREAM       pDiff;
} SCMDIFFSTATE;
/** Pointer to a diff state. */
typedef SCMDIFFSTATE *PSCMDIFFSTATE;


size_t ScmDiffStreams(const char *pszFilename, PSCMSTREAM pLeft, PSCMSTREAM pRight, bool fIgnoreEol,
                      bool fIgnoreLeadingWhite, bool fIgnoreTrailingWhite, bool fSpecialChars,
                      size_t cchTab, PRTSTREAM pDiff);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_bldprogs_scmdiff_h */

