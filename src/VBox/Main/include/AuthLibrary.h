/* $Id: AuthLibrary.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - external authentication library interface.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_AuthLibrary_h
#define MAIN_INCLUDED_AuthLibrary_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/VBoxAuth.h>
#include <iprt/types.h>

typedef struct AUTHLIBRARYCONTEXT
{
    RTLDRMOD hAuthLibrary;
    PAUTHENTRY pfnAuthEntry;
    PAUTHENTRY2 pfnAuthEntry2;
    PAUTHENTRY3 pfnAuthEntry3;
} AUTHLIBRARYCONTEXT;

int AuthLibLoad(AUTHLIBRARYCONTEXT *pAuthLibCtx, const char *pszLibrary);
void AuthLibUnload(AUTHLIBRARYCONTEXT *pAuthLibCtx);

AuthResult AuthLibAuthenticate(const AUTHLIBRARYCONTEXT *pAuthLibCtx,
                               PCRTUUID pUuid, AuthGuestJudgement guestJudgement,
                               const char *pszUser, const char *pszPassword, const char *pszDomain,
                               uint32_t u32ClientId);
void AuthLibDisconnect(const AUTHLIBRARYCONTEXT *pAuthLibCtx, PCRTUUID pUuid, uint32_t u32ClientId);

#endif /* !MAIN_INCLUDED_AuthLibrary_h */
