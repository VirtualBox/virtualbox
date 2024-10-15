/* $Id: bs3-cmn-paging.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Internal Paging Structures, Variables and Functions.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef BS3KIT_INCLUDED_bs3_cmn_paging_h
#define BS3KIT_INCLUDED_bs3_cmn_paging_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "bs3kit.h"

RT_C_DECLS_BEGIN

/** Root directory for page protected mode.
 * UINT32_MAX if not initialized. */
extern uint32_t g_PhysPagingRootPP;
/** Root directory pointer table for PAE mode.
 * UINT32_MAX if not initialized. */
extern uint32_t g_PhysPagingRootPAE;
/** Root table (level 4) for long mode.
 * UINT32_MAX if not initialized. */
extern uint32_t g_PhysPagingRootLM;

#undef bs3PagingGetLegacyPte
BS3_CMN_PROTO_STUB(X86PTE BS3_FAR *, bs3PagingGetLegacyPte,(RTCCUINTXREG cr3, uint32_t uFlat, bool fUseInvlPg, int *prc));
#undef bs3PagingGetPaePte
BS3_CMN_PROTO_STUB(X86PTEPAE BS3_FAR *, bs3PagingGetPaePte,(RTCCUINTXREG cr3, uint8_t bMode, uint64_t uFlat,
                                                            bool fUseInvlPg, int *prc));

RT_C_DECLS_END

#include "bs3kit-mangling-code.h"

#endif /* !BS3KIT_INCLUDED_bs3_cmn_paging_h */

