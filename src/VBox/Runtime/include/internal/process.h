/* $Id: process.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal RTProc header.
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

#ifndef IPRT_INCLUDED_INTERNAL_process_h
#define IPRT_INCLUDED_INTERNAL_process_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/process.h>
#include <iprt/param.h>

RT_C_DECLS_BEGIN

extern DECL_HIDDEN_DATA(RTPROCESS)      g_ProcessSelf;
extern DECL_HIDDEN_DATA(RTPROCPRIORITY) g_enmProcessPriority;
extern DECL_HIDDEN_DATA(char)           g_szrtProcExePath[RTPATH_MAX];
extern DECL_HIDDEN_DATA(size_t)         g_cchrtProcExePath;
extern DECL_HIDDEN_DATA(size_t)         g_cchrtProcExeDir;
extern DECL_HIDDEN_DATA(size_t)         g_offrtProcName;

/**
 * Validates and sets the process priority.
 *
 * This will check that all rtThreadNativeSetPriority() will success for all the
 * thread types when applied to the current thread.
 *
 * @returns IPRT status code.
 * @param   enmPriority     The priority to validate and set.
 *
 * @remark  Located in sched.
 */
DECLHIDDEN(int) rtProcNativeSetPriority(RTPROCPRIORITY enmPriority);

/**
 * Determines the full path to the executable image.
 *
 * This is called by rtR3Init.
 *
 * @returns IPRT status code.
 *
 * @param   pszPath     Pointer to the g_szrtProcExePath buffer.
 * @param   cchPath     The size of the buffer.
 */
DECLHIDDEN(int) rtProcInitExePath(char *pszPath, size_t cchPath);

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_process_h */

